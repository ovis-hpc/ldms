/*
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
#include <assert.h>

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
static int ods_remap(ods_t ods);

static inline size_t ods_page_count(ods_t ods, size_t sz)
{
	return ((sz + (ODS_PAGE_SIZE - 1)) & ODS_PAGE_MASK) >> ODS_PAGE_SHIFT;
}

static inline uint64_t ods_obj_ptr_to_page(ods_t ods, void *p)
{
	return ods_obj_ptr_to_ref(ods, p) >> ODS_PAGE_SHIFT;
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

void *ods_obj_ref_to_ptr(ods_t ods, uint64_t off)
{
	if (!off)
		return NULL;
	/* Check if an extend has occurred */
	if (ods->obj->gen != ods->obj_gen || ods->pg_table->gen != ods->pg_gen)
		(void)ods_remap(ods);
	/* GUARD */
	assert(off < ods->obj_sz);
	return (void *)((uint64_t)off + (uint64_t)ods->obj);
}

uint64_t ods_obj_ptr_to_ref(ods_t ods, void *p)
{
	return (uint64_t)p - (uint64_t)ods->obj;
}

static int extend_file(int fd, size_t sz)
{
	int rc = lseek(fd, sz - 1, SEEK_SET);
	if (rc < 0)
		return rc;
	rc = 0;
	rc = write(fd, &rc, 1);
	if (rc < 0)
		return -1;
	return 0;
}

static int init_pgtbl(ods_t ods)
{
	static struct ods_pgt_s pgt;
	size_t min_sz;
	int rc;
	unsigned char pge;
	int count;

	count = ODS_OBJ_MIN_SZ >> ODS_PAGE_SHIFT;
	min_sz = count + sizeof(struct ods_pgt_s);
	extend_file(ods->pg_fd, min_sz);

	memset(&pgt, 0, sizeof pgt);
	memcpy(pgt.signature, ODS_PGT_SIGNATURE, sizeof ODS_PGT_SIGNATURE);
	pgt.gen = 1;
	pgt.count = count;

	rc = lseek(ods->pg_fd, 0, SEEK_SET);
	if (rc < 0)
		return -1;
	rc = write(ods->pg_fd, &pgt, sizeof pgt);
	if (rc != sizeof pgt)
		return -1;

	/* Page 0 is OBJ file header */
	pge = ODS_F_ALLOCATED;
	rc = write(ods->pg_fd, &pge, 1);
	if (rc != 1)
		return -1;
	pge = 0;
	while (count--) {
		rc = write(ods->pg_fd, &pge, 1);
		if (rc != 1)
			return -1;
	}
	ods->pg_sz = min_sz;
	return 0;
}

static int init_obj(ods_t ods)
{
	static struct obj_hdr {
		struct ods_obj_s obj;
		unsigned char pad[ODS_PAGE_SIZE - sizeof(struct ods_obj_s)];
		struct ods_pg_s pg;
	} hdr;
	int rc;

	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.obj.signature, ODS_OBJ_SIGNATURE, sizeof ODS_OBJ_SIGNATURE);
	memcpy(&hdr.obj.version, ODS_OBJ_VERSION, sizeof hdr.obj.version);
	hdr.obj.gen = 1;

	/* Page 0 is the header page */
	hdr.obj.pg_free = 1 << ODS_PAGE_SHIFT;

	memset(hdr.obj.blk_free, 0, sizeof hdr.obj.blk_free);

	hdr.pg.next = 0;
	hdr.pg.count = (ODS_OBJ_MIN_SZ >> ODS_PAGE_SHIFT) - 1;

	rc = lseek(ods->obj_fd, 0, SEEK_SET);
	if (rc < 0)
		return -1;
	rc = write(ods->obj_fd, &hdr, sizeof hdr);
	if (rc != sizeof(hdr))
		return -1;

	if (extend_file(ods->obj_fd, ODS_OBJ_MIN_SZ))
		return -1;
	ods->obj_sz = ODS_OBJ_MIN_SZ;
	return 0;
}

int ods_remap(ods_t ods)
{
	int rc;
	void *pg_map, *obj_map;
	struct stat sb;

	rc = fstat(ods->pg_fd, &sb);
	pg_map = mmap(NULL, sb.st_size,
		      PROT_READ | PROT_WRITE,
		      MAP_FILE | MAP_SHARED,
		      ods->pg_fd, 0);
	if (!pg_map) {
		rc = ENOMEM;
		goto out;
	}
	if (ods->pg_table)
		munmap(ods->pg_table, ods->pg_sz);
	ods->pg_sz = sb.st_size;
	ods->pg_table = pg_map;
	ods->pg_gen = ods->pg_table->gen;

	rc = fstat(ods->obj_fd, &sb);
	obj_map = mmap(0, sb.st_size,
		       PROT_READ | PROT_WRITE,
		       MAP_FILE | MAP_SHARED,
		       ods->obj_fd, 0);
	if (!obj_map) {
		rc = ENOMEM;
		goto out;
	}
	if (ods->obj)
		munmap(ods->obj, ods->obj_sz);
	ods->obj_sz = sb.st_size;
	ods->obj = obj_map;
	ods->obj_gen = ods->obj->gen;

	ods_commit(ods, ODS_COMMIT_SYNC);

	return 0;
 out:
	return rc;
}

int ods_extend(ods_t ods, size_t sz)
{
	struct ods_pg_s *pg;
	size_t n_pages;
	size_t n_sz;
	size_t new_pg_off;
	int rc;

	/*
	 * Extend the page table first, that way if we fail extending
	 * the object file, we can simply adjust the page_count back
	 * down and leave the object store in a consistent state
	 */
	new_pg_off = ods->obj_sz;
	n_sz = ods->obj_sz + sz;
	n_pages = n_sz >> ODS_PAGE_SHIFT;
	if (n_pages > (ods->pg_sz - sizeof(struct ods_pgt_s))) {
		rc = extend_file(ods->pg_fd, n_pages + sizeof(struct ods_pgt_s));
		if (rc)
			goto out;
		ods->pg_table->gen++;
	}

	/* Now extend the obj file */
	rc = extend_file(ods->obj_fd, n_sz);
	if (rc)
		goto out;
	ods->obj->gen++;

	ods_remap(ods);
	pg = ods_obj_ref_to_ptr(ods, new_pg_off);
	pg->count = n_pages - ods->pg_table->count;
	pg->next = ods->obj->pg_free; /* prepend the page free list */
	ods->obj->pg_free = new_pg_off; /* new page free head */

	ods->pg_table->count = n_pages;
	ods->pg_gen = ods->pg_table->gen;
	ods->obj_gen = ods->obj->gen;
	rc = 0;
 out:
	return rc;
}

static int ods_create(ods_t ods)
{
	int rc;

	rc = init_obj(ods);
	if (rc)
		goto out;

	rc = init_pgtbl(ods);
	if (rc)
		goto out;

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
	if (fd < 0)
		goto err;
	ods->obj_fd = fd;

	/* Open the page table file */
	sprintf(tmp_path, "%s%s", path, ODS_PGTBL_SUFFIX);
	fd = open(tmp_path, o_flag, o_mode);
	if (fd < 0)
		goto err;
	ods->pg_fd = fd;

	ods->path = strdup(path);
	if (!ods->path)
		goto err;

	rc = fstat(ods->obj_fd, &sb);
	if (rc)
		goto err;

	if (sb.st_size < ODS_OBJ_MIN_SZ) {
		rc = ods_create(ods);
		if (rc)
			goto err;
	}
	if (ods_remap(ods))
		goto err;

	return ods;

 err:
	if (ods->path)
		free(ods->path);
	if (ods->pg_fd)
		close(ods->pg_fd);
	if (ods->obj_fd)
		close(ods->obj_fd);
	free(ods);
	return NULL;
}

size_t ods_set_user_data(ods_t ods, void *data, size_t sz)
{
	void *p = ods->obj + 1;
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
	for (pg = ods_obj_ref_to_ptr(ods, ods->obj->pg_free);
	     pg;
	     p_pg = pg, pg = ods_obj_ref_to_ptr(ods, pg->next)) {
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
	off = ods_obj_ptr_to_ref(ods, p);
	for (count = ODS_PAGE_SIZE / sz; count; count--, off += sz) {
		blk = ods_obj_ref_to_ptr(ods, off);
		blk->next = ods->obj->blk_free[bkt];
		ods->obj->blk_free[bkt] = off;
	}
}

static void *alloc_bkt(ods_t ods, int bkt)
{
	ods_blk_t blk = ods_obj_ref_to_ptr(ods, ods->obj->blk_free[bkt]);
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

size_t ods_obj_size(ods_t ods, void *obj)
{
	uint64_t page = ods_obj_ptr_to_page(ods, obj);
	unsigned char flags = ods->pg_table->pages[page];
	uint64_t count;

	if (flags & ODS_F_IDX_VALID)
		return ods_bkt_to_size(ods, (flags & ODS_M_IDX));

	/*
	 * If the first page has the prev bit set, obj is in the
	 * middle of an allocation
	 */
	if (flags & ODS_F_PREV)
		return -1;

	count = 1;
	while (flags & ODS_F_NEXT) {
		page++;
		count++;
		flags = ods->pg_table->pages[page];
	}
	return count << ODS_PAGE_SHIFT;
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
	ods->obj->blk_free[bkt] = ods_obj_ptr_to_ref(ods, blk);
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
	ods->obj->pg_free = ods_obj_ptr_to_ref(ods, pg);
}

void ods_commit(ods_t ods, int flags)
{
	int mflag = (flags ? MS_SYNC : MS_ASYNC);
	msync(ods->pg_table, ods->pg_sz, mflag);
	msync(ods->obj, ods->obj_sz, mflag);
}
void ods_close(ods_t ods, int flags)
{
	if (!ods)
		return;
	ods_commit(ods, flags);
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

char *bits(ods_t ods, unsigned char mask)
{
	static char mask_str[80];
	mask_str[0] = '\0';
	if (mask & ODS_F_IDX_VALID) {
		int bkt = mask & ODS_M_IDX;
		sprintf(mask_str, "IDX[%zu] ", ods_bkt_to_size(ods, bkt));
	}
	if (mask & ODS_F_PREV)
		strcat(mask_str, "PREV ");
	if (mask & ODS_F_NEXT)
		strcat(mask_str, "NEXT ");
	return mask_str;
}

int blk_is_free(ods_t ods, int bkt, void *ptr)
{
	ods_blk_t blk;
	if (!ods->obj->blk_free[bkt])
		return 0;
	for (blk = ods_obj_ref_to_ptr(ods, ods->obj->blk_free[bkt]);
	     blk; blk = ods_obj_ref_to_ptr(ods, blk->next)) {
		if (blk == ptr)
			return 1;
	}
	return 0;
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
	uint64_t count = 0;
	for(i = 0; i < ods->pg_table->count; i++) {
		uint64_t start;
		if (!(ods->pg_table->pages[i] & ODS_F_ALLOCATED))
			continue;
		start = i;
		while (ods->pg_table->pages[i] & ODS_F_NEXT) i++;
		if (start == i)
			fprintf(fp, "%ld %s\n", start, bits(ods, ods->pg_table->pages[i]));
		else
			fprintf(fp, "%ld..%ld\n", start, i);
		count += (i - start + 1);
	}
	fprintf(fp, "Total Allocated Pages: %ld\n", count);
	fprintf(fp, "--------------------------- Allocated Blocks ----------------------------\n");
	for(i = 0; i < ods->pg_table->count; i++) {
		if (!(ods->pg_table->pages[i] & ODS_F_ALLOCATED))
			continue;
		if (ods->pg_table->pages[i] & ODS_F_IDX_VALID) {
			int bkt = ods->pg_table->pages[i] & ODS_M_IDX;
			size_t sz = ods_bkt_to_size(ods, bkt);
			char *blk = (char *)ods_obj_page_to_ptr(ods, i);
			char *next = (char *)ods_obj_page_to_ptr(ods, i+1);
			printf("======== Size %zu ========\n", sz);
			for (count = 0; blk < next; blk += sz, count++) {
				if (blk_is_free(ods, bkt, blk))
					continue;
				fprintf(fp, "%p\n", blk);
			}
			printf("Count %ld\n", count);
		}
	}
	fprintf(fp, "------------------------------ Free Pages ------------------------------\n");
	count = 0;
	for (pg = ods_obj_ref_to_ptr(ods, ods->obj->pg_free);
	     pg;
	     pg = ods_obj_ref_to_ptr(ods, pg->next)) {
		fprintf(fp, "%-32s : 0x%016lx\n", "Page Offset",
			ods_obj_ptr_to_ref(ods, pg));
		fprintf(fp, "%-32s : %zu\n", "Page Count",
			pg->count);
		count += pg->count;
	}
	fprintf(fp, "Total Free Pages: %ld\n", count);
	fprintf(fp, "------------------------------ Free Blocks -----------------------------\n");
	int bkt;
	ods_blk_t blk;
	for (bkt = 0; bkt < (ODS_PAGE_SHIFT - ODS_GRAIN_SHIFT); bkt++) {
		if (!ods->obj->blk_free[bkt])
			continue;
		count = 0;
		fprintf(fp, "%-32s : %zu\n", "Block Size",
			ods_bkt_to_size(ods, bkt));
		for (blk = ods_obj_ref_to_ptr(ods, ods->obj->blk_free[bkt]);
		     blk;
		     blk = ods_obj_ref_to_ptr(ods, blk->next)) {

			fprintf(fp, "    %-32s : 0x%016lx\n", "Block Offset",
				ods_obj_ptr_to_ref(ods, blk));
			count++;
		}
		fprintf(fp, "Total Free %zu blocks: %ld\n",
					ods_bkt_to_size(ods,bkt), count);
	}
	fprintf(fp, "==============================- ODS End =================================\n");
}

void ods_iter(ods_t ods, ods_iter_fn_t iter_fn, void *arg)
{
	ods_pg_t pg;
	uint64_t i, start, end;
	int bkt;
	char *blk;
	char *next;
	size_t sz;
	for(i = 1; i < ods->pg_table->count; i++) {
		if (!(ods->pg_table->pages[i] & ODS_F_ALLOCATED))
			continue;
		if (ods->pg_table->pages[i] & ODS_F_IDX_VALID) {
			bkt = ods->pg_table->pages[i] & ODS_M_IDX;
			sz = ods_bkt_to_size(ods, bkt);
			blk = (char *)ods_obj_page_to_ptr(ods, i);
			next = (char *)ods_obj_page_to_ptr(ods, i+1);
			for (; blk < next; blk += sz) {
				if (blk_is_free(ods, bkt, blk))
					continue;
				iter_fn(ods, blk, sz, arg);
			}
		} else {
			for (start = end = i;
			     (end < ods->pg_table->count) &&
				     (0 != (ods->pg_table->pages[end] & ODS_F_NEXT));
			     end++);
			pg = ods_obj_page_to_ptr(ods, start);
			sz = (end - start + 1) << ODS_PAGE_SHIFT;
			iter_fn(ods, pg, sz, arg);
			i = end;
		}
	}
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

