/*
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
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
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <string.h>
#include <assert.h>
#include "bpt.h"

void print_node(obj_idx_t idx, int ent, bpt_node_t n, int indent)
{
	bpt_t t = idx->priv;
	int i;

	if (!n) {
		printf("<nil>\n");
		return;
	}

	/* Print this node */
	if (n->is_leaf && n->parent)
		indent += 4;
	printf("%p - %*s%s[%d] | %p : ", (void *)(unsigned long)n->parent,
	       indent, "",
	       (n->is_leaf?"LEAF":"NODE"),
	       ent, n);
	for (i = 0; i < n->count; i++) {
		obj_key_t key = ods_obj_ref_to_ptr(t->ods, n->entries[i].key);
		printf("%s:%p, ",
		       (key ? obj_key_to_str(idx, key) : "-"),
		       (void *)(unsigned long)n->entries[i].ref);
	}
	printf("\n");
	fflush(stdout);
	if (n->is_leaf)
		return;
	/* Now print all it's children */
	for (i = 0; i < n->count; i++) {
		bpt_node_t node = ods_obj_ref_to_ptr(t->ods, n->entries[i].ref);
		print_node(idx, i, node, indent + 2);
	}
}

void print_tree(obj_idx_t idx)
{
	bpt_t t = idx->priv;
	print_node(idx, 0, t->root, 0);
}

void iter_tree(obj_idx_t idx)
{
	obj_key_t key;
	obj_ref_t ref;
	obj_iter_t i = obj_iter_new(idx);
	int rc;
	for (rc = obj_iter_begin(i); !rc; rc = obj_iter_next(i)) {
		key = obj_iter_key(i);
		ref = obj_iter_ref(i);
		printf("%s, ", obj_key_to_str(idx, key));
		ref = obj_idx_find(idx, key);
		assert(ref);
	}
	printf("\n");
	obj_iter_delete(i);
}

void usage(int argc, char *argv[])
{
	printf("usage: %s -p <path> -k <key_str> [-o <order>]\n"
	       "       -k <key_str>    The key type string.\n"
	       "       -p <path>       The path to the index files.\n"
	       "       -o <order>      The order of the B+ tree (default is 5).\n",
	       argv[0]);
	exit(1);
}

const char *nlstrip(char *s)
{
	static char ss[80];
	strcpy(ss, s);
	strtok(ss, " \t\n");
}

#define FMT "k:p:o:"
int main(int argc, char *argv[])
{
	obj_idx_t idx;
	int order = 5;
	char *idx_path = NULL;
	char *key_str = NULL;
	char buf[2048];
	char *s, *k;
	obj_iter_t iter;
	obj_key_t key = obj_key_new(1024);
	obj_ref_t ref;
	int rc;
	uint64_t inode;

	while ((rc = getopt(argc, argv, FMT)) > 0) {
		switch (rc) {
		case 'k':
			key_str = strdup(optarg);
			break;
		case 'p':
			idx_path = strdup(optarg);
			break;
		case 'o':
			order = atoi(optarg);
			break;
		default:
			usage(argc, argv);
		}
	}
	if (!idx_path || !key_str)
		usage(argc, argv);

	rc = obj_idx_create(idx_path, 0660, "BPTREE", key_str, order);
	if (rc) {
		printf("The index '%s' could not be created due to error %d.\n",
		       idx_path, rc);
		return rc;
	}
	idx = obj_idx_open(idx_path);
	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		obj_key_from_str(idx, key, nlstrip(s));
		inode = strtoul(s, NULL, 0);
		if (!inode) {
			printf("Ignoring key that results in <nil> object reference.\n");
			continue;
		}
		obj_idx_insert(idx, key, inode);
	}
	print_tree(idx);

	/* Delete the min in the tree until the tree is empty */
	iter = obj_iter_new(idx);
	for (rc = obj_iter_begin(iter); !rc; rc = obj_iter_begin(iter)) {
		obj_key_t k = obj_iter_key(iter);
		printf("delete %s\n", obj_key_to_str(idx, k));
		ref = obj_idx_delete(idx, k);
		if (!ref) {
			printf("FAILURE: The key '%s' is in the iterator, but could not be deleted.\n",
			       key->value);
		}
	}
	print_tree(idx);

	/* Build another tree */
	fseek(stdin, 0, SEEK_SET);
	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		obj_key_from_str(idx, key, nlstrip(s));
		inode = strtoul(s, NULL, 0);
		if (!inode) {
			printf("Ignoring key that results in <nil> object reference.\n");
			continue;
		}
		(void)obj_idx_insert(idx, key, inode);
	}
	print_tree(idx);

	/* Delete the max in the tree until the tree is empty */
	for (rc = obj_iter_end(iter); !rc; rc = obj_iter_end(iter)) {
		obj_key_t k = obj_iter_key(iter);
		printf("delete %s\n", obj_key_to_str(idx, k));
		ref = obj_idx_delete(idx, k);
		if (!ref) {
			printf("FAILURE: The key '%s' is in the iterator, but could not be deleted.\n",
			       key->value);
		}
	}
	print_tree(idx);

	/* Build another tree */
	fseek(stdin, 0, SEEK_SET);
	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		obj_key_from_str(idx, key, nlstrip(s));
		inode = strtoul(s, NULL, 0);
		if (!inode) {
			printf("Ignoring key that results in <nil> object reference.\n");
			continue;
		}
		obj_idx_insert(idx, key, inode);
	}
	print_tree(idx);

	/* Delete an interior key until the tree is empty */
	bpt_t t = idx->priv;
	while (t->root) {
		int cnt = t->root->count >> 1;
		obj_ref_t key_ref = t->root->entries[cnt].key;
		obj_key_t k = ods_obj_ref_to_ptr(t->ods, key_ref);
		printf("delete %s\n", obj_key_to_str(idx, k));
		ref = obj_idx_delete(idx, k);
		if (!ref) {
			printf("FAILURE: The key '%s' is in the iterator, but could not be deleted.\n",
			       key->value);
		}
	}
	print_tree(idx);

	static char *keys[] = {
		"1", "3", "5", "7", "9", "11", "13", "15", "17", "19", "21"
	};
	for (rc = 0; rc < sizeof(keys) / sizeof(keys[0]); rc++) {
		obj_key_from_str(idx, key, keys[rc]);
		inode = strtoul(keys[rc], NULL, 0);
		obj_idx_insert(idx, key, inode);
	}
	print_tree(idx);
	for (rc = 0; rc < sizeof(keys) / sizeof(keys[0]); rc++) {
		int k = strtoul(keys[rc], NULL, 0) + 1;
		char ks[32];
		sprintf(ks, "%d", k);
		obj_key_from_str(idx, key, ks);
		printf("LUB of %s is %p.\n", ks, obj_idx_find_lub(idx, key));
		printf("GLB of %s is %p.\n", ks, obj_idx_find_glb(idx, key));
	}
	return 0;
}
