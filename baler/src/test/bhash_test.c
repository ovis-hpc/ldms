/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
#include <unistd.h>
#include <stdlib.h>

#include "baler/bhash.h"

int main(int argc, char **argv)
{
	int i;
	int rc;
	struct bhash_entry *ent;
	const char *keys[] = {
		"key number one",
		"key number two",
		"key number three",
		"key number four",
		"key number five",
	};

	struct bhash *h = bhash_new(4099, 7, NULL);
	if (!h) {
		perror("bhash_new()");
		exit(-1);
	}

	for (i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		printf("setting key: %s, value: %d\n", keys[i], i);
		ent = bhash_entry_set(h, keys[i], strlen(keys[i])+1, i);
		if (!ent) {
			printf("bhash_entry_set() error(%d): %s\n", rc, strerror(rc));
			exit(-1);
		}
	}

	printf("-----------\n");

	struct bhash_iter *iter = bhash_iter_new(h);
	if (!iter) {
		perror("bhash_iter_new()");
		exit(-1);
	}

	rc = bhash_iter_begin(iter);
	if (rc) {
		printf("bhash_iter_begin() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	while ((ent = bhash_iter_entry(iter))) {
		printf("%s: %lu\n", ent->key, ent->value);
		bhash_iter_next(iter);
	}

	printf("-----------\n");

	for (i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		ent = bhash_entry_get(h, keys[i], strlen(keys[i])+1);
		if (!ent) {
			printf("Error: %s not found!\n", keys[i]);
			exit(-1);
		}
		printf("%s:%d --> %s:%lu\n", keys[i], i, ent->key, ent->value);
	}

	printf("-----------\n");

	printf("Deleting %s\n", keys[0]);
	rc = bhash_entry_del(h, keys[0], strlen(keys[0])+1);
	if (rc) {
		printf("bhash_entry_del() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	ent = bhash_entry_get(h, keys[4], strlen(keys[4])+1);
	printf("Deleting %s\n", keys[4]);
	rc = bhash_entry_remove(h, ent);
	if (rc) {
		printf("bhash_entry_remove() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	rc = bhash_iter_begin(iter);
	if (rc) {
		printf("bhash_iter_begin() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	while ((ent = bhash_iter_entry(iter))) {
		printf("%s: %lu\n", ent->key, ent->value);
		bhash_iter_next(iter);
	}

	printf("-----------\n");

	bhash_free(h);

	return 0;
}
