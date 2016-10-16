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
#include "dstring.h"
#include "stdio.h"
#include "stdlib.h"
#include "limits.h"
/* macros for convenience in typical use of one dynamic string in a function */
/* paste as needed in application code. */
/** declare and init a dstring named ds */
#define dsinit \
	dstring_t ds; \
	dstr_init(&ds)

/** declare and init an oversized dstring named ds with initial capacity cap.*/
#define dsinit2(cap) \
	dstring_t ds; \
	dstr_init2(&ds,cap)

/** append a dstring with  null terminated string char_star_x.*/
#define dscat(char_star_x) \
	dstrcat(&ds, char_star_x, DSTRING_ALL)

/** create real string (char *) from ds and reset ds, freeing any internal memory allocated. returns a char* the caller must free later. */
#define dsdone \
	dstr_extract(&ds)


/* simple test until we get unit testing in place. */

void check_int()
{
	int j = INT_MAX;
        int64_t k = INT64_MAX;
	dsinit;
	char *c = dstr_set_int(&ds,j);
	printf("fmt intmax= %s\n",c);

        c = dstr_set_int(&ds,k);
        printf("fmt int8max= %s\n",c);

}

char *createfoo (int lim) 
{
	int i;
	dsinit;
	for (i=0; i < lim; i++) {
		dscat("1000 years luck");
	}
	return dsdone;
}

int main(int argc, char **argv) {
	char * x, *y;
	check_int();
	x = createfoo(10);
	printf("%s\n",x);
	free(x);
	y = createfoo(10000);
	free(y);
	dsinit2(100000);
	int i,lim=10000;
	for (i = 0; i < lim; i++) {
		dscat("1000 years luck");
	}
	char *res = dsdone;
	free(res);
	return 0;
}

