/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Author: Tom Tucker tom at ogc dot us
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <errno.h>

#include "ods.h"
#include "ods_priv.h"

#define ODS_OBJ_SUFFIX		".OBJ"
#define ODS_PGTBL_SUFFIX	".PG"
#define ODS_PANIC(__msg) \
{ \
	fprintf(stderr, "Fatal Error: %s[%d]\n  " #__msg, \
		__FUNCTION__, __LINE__); \
	exit(1); \
}

#ifndef PERROR
#define PERROR() \
        fprintf(stderr, "%s:%d %s: err(%d): %s\n", __FILE__, __LINE__, __FUNCTION__, errno, sys_errlist[errno]);
#endif

static inline size_t ods_page_count(ods_t ods, size_t sz)
{
	return ((sz + (ODS_PAGE_SIZE - 1)) & ODS_PAGE_MASK) >> ODS_PAGE_SHIFT;
}

static inline uint64_t ods_obj_ptr_to_page(ods_t ods, void *p)
{
	return ods_obj_ptr_to_offset(ods, p) >> ODS_PAGE_SHIFT;
}

static inline int ods_bkt(ods_t ods, size_t sz)
{
	size_t bkt_sz;
	int bkt = 0;
	for (bkt_sz = ODS_GRAIN_SIZE; sz > bkt_sz; bkt_sz <<= 1, bkt++);
	return bkt;
}

static inline size_t ods_bkt_to_size(ods_t ods, int bkt)
{
	return ODS_GRAIN_SIZE << bkt;
}

void *ods_obj_offset_to_ptr(ods_t ods, uint64_t off)
{
	if (!off)
		return NULL;
	return (void *)((uint64_t)off + (uint64_t)ods->obj);
}

uint64_t ods_obj_ptr_to_offset(ods_t ods, void *p)
{
	return (uint64_t)p - (uint64_t)ods->obj;
}

static int extend_file(int fd, size_t sz)
{
	int rc = lseek(fd, sz - 1, SEEK_SET);
	rc = write(fd, &rc, 1);
	if (rc < 0)
		return -1;
	return 0;
}

static int init_pgtbl(ods_t ods)
{
	struct ods_pgt_s *pgt = ods->pg_table;
	size_t min_sz;

	memcpy(pgt->signature, ODS_PGT_SIGNATURE, sizeof pgt->signature);
	pgt->count = (ods->obj_sz >> ODS_PAGE_SHIFT);
	memset(pgt->pages, 0, pgt->count);

	min_sz = pgt->count + sizeof(struct ods_pgt_s);
	if (min_sz > ods->pg_sz)
		extend_file(ods->pg_fd, min_sz);

	/* Page 0 is OBJ file header */
	pgt->pages[0] = ODS_F_ALLOCATED;

	return 0;
}

static int init_obj(ods_t ods)
{
	struct ods_obj_s *obj = ods->obj;
	struct ods_pg_s *pg;

	memcpy(obj->signature, ODS_OBJ_SIGNATURE, sizeof obj->signature);

	/* Page 0 is the header page */
	ods->obj->pg_free = 1 << ODS_PAGE_SHIFT;

	memset(ods->obj->blk_free, 0, sizeof ods->obj->blk_free);

	pg = ods_obj_page_to_ptr(ods, 1);
	pg->next = 0;
	pg->count = (ods->obj_sz >> ODS_PAGE_SHIFT) - 1;

	return 0;
}

int ods_extend(ods_t ods, size_t sz)
{
	struct ods_pg_s *pg, *n_pg = NULL;
	size_t n_pages;
	size_t n_sz;
	int rc;

	/*
	 * Extend the page table first, that way if we fail extending
	 * the object file, we can simply adjust the page_count back
	 * down and leave the object store in a consistent state
	 */
	n_sz = ods->obj_sz + sz;
	n_pages = n_sz >> ODS_PAGE_SHIFT;
	if (n_pages > (ods->pg_sz - sizeof(struct ods_pgt_s))) {
		void *pg_map;
		rc = extend_file(ods->pg_fd, n_pages + sizeof(struct ods_pgt_s));
		if (rc)
			goto out;
		pg_map = mmap(NULL, n_pages + sizeof(struct ods_pgt_s),
			      PROT_READ | PROT_WRITE,
			      MAP_FILE | MAP_SHARED,
			      ods->pg_fd, 0);
		if (!pg_map) {
			rc = ENOMEM;
			goto out;
		}
		munmap(ods->pg_table, ods->pg_sz);
		ods->pg_sz = n_pages + sizeof(struct ods_pgt_s);
		ods->pg_table = pg_map;
	}
	memset(&ods->pg_table->pages[ods->pg_table->count], 0,
	       n_pages - ods->pg_table->count);

	/* Now extend the obj file */
	rc = extend_file(ods->obj_fd, n_sz);
	if (rc)
		goto out;

	void *obj_map = mmap(0, n_sz,
			     PROT_READ | PROT_WRITE,
			     MAP_FILE | MAP_SHARED,
			     ods->obj_fd, 0);
	if (!obj_map) {
		rc = ENOMEM;
		goto out;
	}
	munmap(ods->obj, ods->obj_sz);
	ods->obj = obj_map;
	if (ods->obj->pg_free == 0)
		ods->obj->pg_free = ods->obj_sz;
	else {
		/* walk to end and append new block to end */
		for (pg = ods_obj_offset_to_ptr(ods, ods->obj->pg_free);
		     pg;
		     n_pg = pg, pg = ods_obj_offset_to_ptr(ods, pg->next));
		n_pg->next = ods->obj_sz;
	}
	pg = ods_obj_offset_to_ptr(ods, ods->obj_sz);
	pg->count = n_pages - ods->pg_table->count;
	pg->next = 0;
	ods->obj_sz = n_sz;
	ods->pg_table->count = n_pages;
	rc = 0;
 out:
	return rc;
}

ods_t ods_open(const char *path, int o_flag, ...)
{
	char tmp_path[PATH_MAX];
	va_list argp;
	int o_mode;
	struct stat sb;
	ods_t ods;
	int fd;
	int rc;

	ods = calloc(1, sizeof *ods);
	if (!ods)
		return NULL;

	if (o_flag & O_CREAT) {
		va_start(argp, o_flag);
		o_mode = va_arg(argp, int);
	} else
		o_mode = 0;

	/* Open the obj file */
	sprintf(tmp_path, "%s%s", path, ODS_OBJ_SUFFIX);
	fd = open(tmp_path, o_flag, o_mode);
	if (fd < 0) {
		PERROR();
		goto err;
	}

	ods->obj_fd = fd;
	rc = fstat(ods->obj_fd, &sb);
	if (rc) {
		PERROR();
		goto err;
	}

	if (sb.st_size < ODS_OBJ_MIN_SZ) {
		ods->obj_sz = ODS_OBJ_MIN_SZ;
		if (extend_file(ods->obj_fd, ods->obj_sz))
			goto err;
	} else
		ods->obj_sz = sb.st_size;

	ods->obj = mmap(NULL, ods->obj_sz, PROT_READ | PROT_WRITE,
			MAP_FILE | MAP_SHARED,
			ods->obj_fd, 0);
	if (!ods->obj) {
		PERROR();
		goto err;
	}

	if (ods->obj == -1) {
		PERROR();
		goto err;
	}

	if (memcmp(ods->obj->signature, ODS_OBJ_SIGNATURE,
		   sizeof(ods->obj->signature)))
		if (init_obj(ods))
			goto err;

	/* Open the page table file */
	sprintf(tmp_path, "%s%s", path, ODS_PGTBL_SUFFIX);
	fd = open(tmp_path, o_flag, o_mode);
	if (fd < 0) {
		PERROR();
		goto err;
	}

	ods->pg_fd = fd;
	rc = fstat(ods->pg_fd, &sb);
	if (rc) {
		PERROR();
		goto err;
	}

	if (sb.st_size < ODS_PGTBL_MIN_SZ) {
		ods->pg_sz = ODS_PGTBL_MIN_SZ;
		if (extend_file(ods->pg_fd, ods->pg_sz))
			goto err;
	} else
		ods->pg_sz = sb.st_size;

	ods->pg_table = mmap(NULL, ods->pg_sz, PROT_READ | PROT_WRITE,
			    MAP_FILE | MAP_SHARED,
			    ods->pg_fd, 0);
	if (!ods->pg_table || ods->pg_table == -1) {
		PERROR();
		goto err;
	}

	if (memcmp(ods->pg_table->signature, ODS_PGT_SIGNATURE,
		   sizeof(ods->pg_table->signature)))
		if (init_pgtbl(ods))
			goto err;

	ods->path = strdup(path);
	if (!ods->path)
		goto err;

	return ods;
 err:
	if (ods->path)
		free(ods->path);

	if (ods->pg_table)
		munmap(ods->pg_table, ods->pg_sz);
	if (ods->pg_fd)
		close(ods->pg_fd);

	if (ods->obj)
		munmap(ods->obj, ods->obj_sz);
	if (ods->obj_fd)
		close(ods->obj_fd);

	free(ods);
	return NULL;
}

size_t ods_set_user_data(ods_t ods, void *data, size_t sz)
{
	void *p = ods + 1;
	size_t w_sz = sz <= ODS_UDATA_SIZE ? sz : ODS_UDATA_SIZE;
	memcpy(p, data, w_sz);
	return sz;
}

void *ods_get_user_data(ods_t ods, size_t *sz)
{
	*sz = ODS_UDATA_SIZE;
	return ods->obj + 1;
}

static void update_page_table(ods_t ods, ods_pg_t pg, size_t count, int bkt)
{
	unsigned char flags;
	uint64_t page;
	/*
	 * Update page table so that all pages in pg are now
	 * allocated.
	 */
	flags = ODS_F_ALLOCATED;
	if (bkt >= 0)
		flags |= bkt | ODS_F_IDX_VALID;;
	if (count > 1)
		flags |= ODS_F_NEXT;
	for (page = ods_obj_ptr_to_page(ods, pg);
	     count; count--,page++) {
		if (count == 1)
			flags &= ~ODS_F_NEXT;
		ods->pg_table->pages[page] = flags;
		flags |= ODS_F_PREV;
	}
}

static void *alloc_pages(ods_t ods, size_t sz, int bkt)
{
	int pg_needed = ods_page_count(ods, sz);
	ods_pg_t pg, p_pg, n_pg;
	uint64_t pg_off;
	uint64_t page;

	p_pg = NULL;
	for (pg = ods_obj_offset_to_ptr(ods, ods->obj->pg_free);
	     pg;
	     p_pg = pg, pg = ods_obj_offset_to_ptr(ods, pg->next)) {
		if (pg_needed <= pg->count)
			break;
	}
	if (!pg)
		goto out;

	if (pg->count > pg_needed) {
		page = ods_obj_ptr_to_page(ods, pg);
		page += pg_needed;

		/* Fix-up the new page */
		n_pg = ods_obj_page_to_ptr(ods, page);
		n_pg->count = pg->count - pg_needed;
		n_pg->next = pg->next;

		pg_off = page << ODS_PAGE_SHIFT;
	} else {
		/* pg gets removed altogether */
		pg_off = pg->next;
	}
	update_page_table(ods, pg, pg_needed, bkt);

	/* p_pg is either NULL or pointing to a previous list member */
	if (p_pg)
		p_pg->next = pg_off;
	else
		ods->obj->pg_free = pg_off;

 out:
	return pg;
}

static void replenish_bkt(ods_t ods, int bkt)
{
	size_t count;
	size_t sz;
	ods_blk_t blk;
	uint64_t off;
	void *p = alloc_pages(ods, 1, bkt);
	if (!p)
		/* Errors are caught in alloc_bkt */
		return;

	sz = ods_bkt_to_size(ods, bkt);
	off = ods_obj_ptr_to_offset(ods, p);
	for (count = ODS_PAGE_SIZE / sz; count; count--, off += sz) {
		blk = ods_obj_offset_to_ptr(ods, off);
		blk->next = ods->obj->blk_free[bkt];
		ods->obj->blk_free[bkt] = off;
	}
}

static void *alloc_bkt(ods_t ods, int bkt)
{
	ods_blk_t blk = ods_obj_offset_to_ptr(ods, ods->obj->blk_free[bkt]);
	if (!blk)
		goto out;
	ods->obj->blk_free[bkt] = blk->next;
 out:
	return blk;
}

static void *alloc_blk(ods_t ods, size_t sz)
{
	int bkt = ods_bkt(ods, sz);
	if (!ods->obj->blk_free[bkt])
		replenish_bkt(ods, bkt);
	return alloc_bkt(ods, bkt);
}

void *ods_alloc(ods_t ods, size_t sz)
{
	if (sz < ODS_PAGE_SIZE)
		return alloc_blk(ods, sz);
	return alloc_pages(ods, sz, -1);
}

uint64_t ods_get_alloc_size(ods_t ods, uint64_t size)
{
	if (size < ODS_PAGE_SIZE)
		return ods_bkt_to_size(ods, ods_bkt(ods, size));
	/* allocated size is in pages. */
	return ods_page_count(ods, size) * ODS_PAGE_SIZE;
}

static void free_blk(ods_t ods, void *ptr)
{
	uint64_t page;
	int bkt;
	unsigned char flags;
	ods_blk_t blk;

	page = ods_obj_ptr_to_page(ods, ptr);
	flags = ods->pg_table->pages[page];
	bkt = flags & ODS_M_IDX;
	if (0 == (flags & ODS_F_ALLOCATED) ||
	    0 == (flags & ODS_F_IDX_VALID) ||
	    bkt < 0 ||
	    bkt > (ODS_PAGE_SHIFT - ODS_GRAIN_SHIFT)) {
		ODS_PANIC("Pointer specified to free is invalid.\n");
		return;
	}

	blk = ptr;
	blk->next = ods->obj->blk_free[bkt];
	ods->obj->blk_free[bkt] = ods_obj_ptr_to_offset(ods, blk);
}

static void free_pages(ods_t ods, void *ptr)
{
	ods_pg_t pg;
	uint64_t count;
	uint64_t page;
	unsigned char flags;

	page = ods_obj_ptr_to_page(ods, ptr);
	flags = ods->pg_table->pages[page];

	/*
	 * If the first page has the prev bit set, ptr is in the
	 * middle of a previous allocation
	 */
	if (flags & ODS_F_PREV) {
		ODS_PANIC("Freeing in middle of page allocation\n");
		return;
	}
	count = 1;
	for (page = ods_obj_ptr_to_page(ods, ptr); page;
	     page++, count++) {
		flags = ods->pg_table->pages[page];
		ods->pg_table->pages[page] = 0; /* free */

		if (0 == (flags & ODS_F_NEXT))
			break;
	}
	pg = ptr;
	pg->count = count;
	pg->next = ods->obj->pg_free;
	ods->obj->pg_free = ods_obj_ptr_to_offset(ods, pg);
}

void ods_flush(ods_t ods)
{
	msync(ods->pg_table, ods->pg_sz, MS_ASYNC);
	msync(ods->obj, ods->obj_sz, MS_ASYNC);
}
void ods_close(ods_t ods)
{
	ods_flush(ods);
	munmap(ods->pg_table, ods->pg_sz);
	munmap(ods->obj, ods->obj_sz);
	close(ods->pg_fd);
	close(ods->obj_fd);
	free(ods->path);
	free(ods);
}

void ods_free(ods_t ods, void *ptr)
{
	uint64_t page;

	if (!ptr)
		return;

	/* Get the page this ptr is in */
	page = ods_obj_ptr_to_page(ods, ptr);
	if (ods->pg_table->pages[page] & ODS_F_IDX_VALID)
		free_blk(ods, ptr);
	else
		free_pages(ods, ptr);
}

void ods_dump(ods_t ods, FILE *fp)
{
	fprintf(fp, "------------------------------- ODS Dump --------------------------------\n");
	fprintf(fp, "%-32s : \"%s\"\n", "Path", ods->path);
	fprintf(fp, "%-32s : %d\n", "Object File Fd", ods->obj_fd);
	fprintf(fp, "%-32s : %zu\n", "Object File Size", ods->obj_sz);
	fprintf(fp, "%-32s : %d\n", "Page File Fd", ods->pg_fd);
	fprintf(fp, "%-32s : %zu\n", "Page File Size", ods->pg_sz);

	ods_pg_t pg;

	fprintf(fp, "--------------------------- Allocated Pages ----------------------------\n");
	uint64_t i;
	for(i = 0; i < ods->pg_table->count; i++) {
		uint64_t start;
		if (!(ods->pg_table->pages[i] & ODS_F_ALLOCATED))
			continue;
		start = i;
		while (ods->pg_table->pages[i] & ODS_F_NEXT) i++;
		if (start == i)
			fprintf(fp, "%ld\n", start);
		else
			fprintf(fp, "%ld..%ld\n", start, i);
	}
	fprintf(fp, "------------------------------ Free Pages ------------------------------\n");
	for (pg = ods_obj_offset_to_ptr(ods, ods->obj->pg_free);
	     pg;
	     pg = ods_obj_offset_to_ptr(ods, pg->next)) {
		fprintf(fp, "%-32s : 0x%016lx\n", "Page Offset",
			ods_obj_ptr_to_offset(ods, pg));
		fprintf(fp, "%-32s : %zu\n", "Page Count",
			pg->count);
	}
	fprintf(fp, "------------------------------ Free Blocks -----------------------------\n");
	int bkt;
	ods_blk_t blk;
	for (bkt = 0; bkt < (ODS_PAGE_SHIFT - ODS_GRAIN_SHIFT); bkt++) {
		if (!ods->obj->blk_free[bkt])
			continue;
		fprintf(fp, "%-32s : %zu\n", "Block Size",
			ods_bkt_to_size(ods, bkt));
		for (blk = ods_obj_offset_to_ptr(ods, ods->obj->blk_free[bkt]);
		     blk;
		     blk = ods_obj_offset_to_ptr(ods, blk->next)) {

			fprintf(fp, "    %-32s : 0x%016lx\n", "Block Offset",
				ods_obj_ptr_to_offset(ods, blk));
		}
	}
	fprintf(fp, "==============================- ODS End =================================\n");
}

#ifdef ODS_MAIN
int main(int argc, char *argv[])
{
	ods_t ods;
	void **p;
	int count = 1024;
	int i, big, small;

	p = calloc(count, sizeof(uint64_t));
	srandom(time(NULL));
	ods = ods_open("ods_test/ods_data", O_RDWR | O_CREAT, 0666);
	big = small = 0;

	ods_dump(ods, stdout);

	for (i = 0; i < count; i++) {
		size_t sz = random() % (3 * 4096);
		printf("%-12s %zu\n", (sz > 2048? "big" : "small"), sz);
		if (sz > 2048)
			big++;
		else
			small++;
		p[i] = ods_alloc(ods, sz);
		if (!p[i]) {
			printf("Extending object store\n");
			ods_dump(ods, stdout);
			ods_extend(ods, 128*1024);
			ods_dump(ods, stdout);
		}
	}

	ods_dump(ods, stdout);

	for (i = 0; i < count; i++)
		ods_free(ods, p[i]);

	ods_close(ods);

	return 0;
}

#endif

