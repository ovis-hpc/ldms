/**
 * Copyright (c) 2015,2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015,2017 Open Grid Computing, Inc. All rights reserved.
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
#include "label-set.h"
#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *lang_to_string[] =
{
	"error",
	"least",
	"python",
	"url",
	"r",
	"c",
	"amqp",
	"file",
	"last",
	NULL
};

int main(int argc, char **argv)
{
	int i;
	enum id_lang il;
	struct ovis_label_set *aols[il_last];

	/* any len */
	aols[0] = NULL;
	for (il = il_least; il < il_last; il++) {
		aols[il] = ovis_label_set_create(il,0);
	}
	for (il = il_least; il < il_last; il++) {
		printf("\n%s:\n", lang_to_string[il]);
		struct ovis_label_set * ols = aols[il];
		struct ovis_name id;
		for (i = 0; i < argc; i++) {
			printf("%s\n",argv[i]);
			id = ovis_label_set_insert(ols,
				ovis_name_from_string(argv[i]));
			printf("\t%s\n",id.name);
		}
		ovis_label_set_destroy(ols);
		aols[il] = NULL;
	}

	/* limited len */
	for (il = il_least; il < il_last; il++) {
		aols[il] = ovis_label_set_create(il,31);
	}
	for (il = il_least; il < il_last; il++) {
		printf("\n%s31:\n", lang_to_string[il]);
		struct ovis_label_set * ols = aols[il];
		struct ovis_name id;
		for (i = 0; i < argc; i++) {
			id = ovis_label_set_insert(ols,
				ovis_name_from_string(argv[i]));
			printf("\t%s\n",id.name);
		}
		ovis_label_set_destroy(ols);
		aols[il] = NULL;
	}

	/* string lifecycle */
	for (il = il_least; il < il_last; il++) {
		aols[il] = ovis_label_set_create(il,0);
	}
	for (il = il_least; il < il_last; il++) {
		printf("\n%s:\n", lang_to_string[il]);
		struct ovis_label_set * ols = aols[il];
		struct ovis_name id;
		for (i = 0; i < argc; i++) {
			printf("%s\n",argv[i]);
			char *st = strdup(argv[i]);
			id = ovis_label_set_own(ols,
				ovis_name_from_string(st));
			printf("\t%s\n",id.name);
		}
		ovis_label_set_destroy(ols);
		aols[il] = NULL;
	}

	return 0;
}
