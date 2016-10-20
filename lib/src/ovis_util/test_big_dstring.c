/*
 * Copyright (c) 2014-16 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014-16 Sandia Corporation. All rights reserved.
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
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "assert.h"
#include "limits.h"
#include "big_dstring.h"
BIG_DSTRING_TYPE(65536);

/* simple test until we get unit testing in place. */

struct x {
	char *x1;
	char *x2;
};

struct x createfoo(int lim)
{
	struct x x;
	int i;
	big_dstring_t bs;
	// expect 10000 sheep rescued prints
	bdstr_init(&bs);
	for (i = 0; i < lim; i++) {
		bdstrcat(&bs, "1000 sheep rescued", DSTRING_ALL);
	}
	x.x1 = bdstr_extract(&bs);
	bdstr_set(&bs, "QQ");
	for (i = 0; i < lim; i++) {
		bdstrcat(&bs, "1000 sheep rescued", DSTRING_ALL);
	}
	x.x2 = bdstr_extract(&bs);
	return x;
}

void check_int()
{
	int j = INT_MAX;
	int64_t k = INT64_MAX;
	big_dstring_t bs;
	bdstr_init(&bs);
	char *c = bdstr_set_int(&bs,j);
	printf("fmt intmax= %s\n",c);

	c = bdstr_set_int(&bs,k);
	printf("fmt int8max= %s\n",c);
}

void dostuff() {
	check_int();
	struct x x, y;
	x = createfoo(10);
	printf("%s\n", x.x1);
	printf("%s\n", x.x2);
	free(x.x1);
	free(x.x2);

	y = createfoo(10000);
	printf("%lu,%lu\n", strlen(y.x1), strlen(y.x2));
	free(y.x1);
	free(y.x2);
}

int main(int argc, char **argv)
{
	dostuff();
	return 0;
}

