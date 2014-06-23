/*
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
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

/**
 * \file blob.c
 * \author Narate Taerat (narate at ogc dot us)
 * \brief Key comparison of two sos blob objects.
 */

#include <string.h>
#include "obj_idx.h"
#include "obj_idx_priv.h"

typedef struct sos_blob_obj_s {
	uint64_t len; /**< Length of the blob. */
	unsigned char data[0]; /**< blob data */
} *sos_blob_obj_t;

#define SOS_BLOB_SIZE(blob) (sizeof(*blob) + blob->len)

static const char *get_type(void)
{
	return "BLOB";
}

static const char *get_doc(void)
{
	return  "OBJ_KEY_BLOB: The key is a BLOB. The memcmp function is used\n"
		"              to compare the two keys. If the lengths of the two keys\n"
		"              is not equal, but they are lexically equal, the function\n"
		"              returns the difference in length between the two keys.\n";
}

static int blob_comparator(obj_key_t a, obj_key_t b)
{
	sos_blob_obj_t ba = (void*)a;
	sos_blob_obj_t bb = (void*)b;
	int res;
	int cmp_len = ba->len;
	if (cmp_len > bb->len)
		cmp_len = bb->len;
	res = memcmp(ba->data, bb->data, cmp_len);
	if (res == 0)
		return ba->len - bb->len;
	return res;
}

static const char *to_str(obj_key_t key)
{
	return ((sos_blob_obj_t)key->value)->data;
}

static int from_str(obj_key_t key, const char *str)
{
	sos_blob_obj_t blob = key->value;
        strcpy(blob->data, str);
	blob->len = strlen(str)+1;
	key->len = SOS_BLOB_SIZE(blob);
	return 0;
}

static struct obj_idx_comparator key_comparator = {
	get_type,
	get_doc,
	to_str,
	from_str,
	blob_comparator
};

struct obj_idx_comparator *get(void)
{
	return &key_comparator;
}
