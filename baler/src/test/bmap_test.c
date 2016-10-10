/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Sandia Corporation. All rights reserved.
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

#include "baler/bcommon.h"
#include "baler/btypes.h"
#include "baler/butils.h"
#include "baler/bmapper.h"

uint32_t test_insert(struct bmap *bmap, char *cstr)
{
	char buff[4096];
	struct bstr *str = (void*)buff;
	int id;

	bstr_set_cstr(str, cstr, 0);
	id = bmap_insert(bmap, str);
	if (id < BMAP_ID_BEGIN) {
		berr("bmap_insert error, code: %d", id);
		exit(-1);
	}
	return id;
}

void test_get_id(struct bmap *bmap, char *cstr, int cmp_id)
{
	char buff[4096];
	struct bstr *str = (void*) buff;
	str->blen = strlen(cstr);
	memcpy(str->cstr, cstr, str->blen);
	uint32_t id = bmap_get_id(bmap, str);
	if (id != cmp_id) {
		berr("Expecting %u, but got %u from key: %s", cmp_id, id, cstr);
		exit(-1);
	}

	binfo("id: %u, key: %s: OK", id, cstr);
}

void test_get_bstr(struct bmap *bmap, int id, char *cmp_cstr)
{
	const struct bstr *str;
	int len = strlen(cmp_cstr);
	str = bmap_get_bstr(bmap, id);
	if (!str) {
		berr("Cannot get bstr of ID: %u", id);
		exit(-1);
	}
	if (str->blen != len || strncmp(cmp_cstr, str->cstr, len) != 0) {
		berr("cmp_cstr (%s) != (%.*s) bstr", cmp_cstr, len, str->cstr);
		exit(-1);
	}
	binfo("str: %s, id: %u, OK", cmp_cstr, id);
}

/**
 * bmapper test program.
 */
int main(int argc, char **argv)
{
	char *path = "/tmp/bmap_test";
	struct bmap *bmap = bmap_open(path);
	if (!bmap) {
		berr("Cannot open bmap: %s, error: %m", path);
		exit(-1);
	}
	uint32_t id1,id2,id3;
	id1 = test_insert(bmap, "Hello");
	id2 = test_insert(bmap, "World");
	id3 = test_insert(bmap, ":)");

	/* close && reopen */
	bmap_close_free(bmap);
	bmap = bmap_open(path);

	if (!bmap) {
		berr("Cannot open bmap: %s", path);
		exit(-1);
	}

	test_get_id(bmap, "Hello", id1);
	test_get_id(bmap, "World", id2);
	test_get_id(bmap, ":)", id3);

	test_get_bstr(bmap, id1, "Hello");
	test_get_bstr(bmap, id2, "World");
	test_get_bstr(bmap, id3, ":)");
	bmap_close_free(bmap);

	/* clean up */
	char tmp[1024];
	sprintf(tmp, "rm -rf %s", path);
	return system(tmp);
}
