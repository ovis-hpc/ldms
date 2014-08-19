/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-14 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-14 Sandia Corporation. All rights reserved.
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
#include <sys/errno.h>
#include <string.h>
#include <malloc.h>
#include "util.h"

char *av_name(struct attr_value_list *av_list, int idx)
{
	if (idx < av_list->count)
		return av_list->list[idx].name;
	return NULL;
}

char *av_value(struct attr_value_list *av_list, char *name)
{
	int i;
	for (i = 0; i < av_list->count; i++)
		if (0 == strcmp(name, av_list->list[i].name))
			return av_list->list[i].value;
	return NULL;
}

char *av_value_at_idx(struct attr_value_list *av_list, int i)
{
	if (i < av_list->count)
		return av_list->list[i].value;
	return NULL;
}

int tokenize(char *cmd,
	     struct attr_value_list *kw_list,
	     struct attr_value_list *av_list)
{
	char *token, *next_token, *ptr;
	struct attr_value *av;
	int next_av, next_kw;
	next_av = next_kw = 0;
	for (token = strtok_r(cmd, " \t\n", &ptr); token;) {
		char *value = strstr(token, "=");
		next_token = strtok_r(NULL, " \t\n", &ptr);
		if (value) {
			if (next_av >= av_list->size)
				goto err;
			av = &(av_list->list[next_av++]);
			av->name = token;
			*value = '\0';
			value++;
			av->value = value;
		} else {
			if (next_kw >= kw_list->size)
				goto err;
			kw_list->list[next_kw].name = token;
			kw_list->list[next_kw].value = NULL;
			next_kw++;
		}
		token = next_token;
	}
	kw_list->count = next_kw;
	av_list->count = next_av;
	return 0;
err:
	return ENOMEM;
}

struct attr_value_list *av_new(size_t size)
{
	size_t bytes = sizeof(struct attr_value_list) +
		       (sizeof(struct attr_value) * size);
	struct attr_value_list *avl = malloc(bytes);

	if (avl) {
		memset(avl, 0, bytes);
		avl->size = size;
	}
	return avl;
}

size_t ovis_get_mem_size(const char *s)
{
    char unit;
    char tmp[256];
    sprintf(tmp, "%s%s", s, "B");
    size_t size;
    sscanf(tmp, "%lu %c", &size, &unit);
    switch (unit) {
    case 'b':
    case 'B':
            return size;
    case 'k':
    case 'K':
            return size * 1024L;
    case 'm':
    case 'M':
            return size * 1024L * 1024L;
    case 'g':
    case 'G':
            return size * 1024L * 1024L * 1024L;
    case 't':
    case 'T':
            return size * 1024L * 1024L * 1024L;
    default:
            return 0;
    }
}

pid_t ovis_execute(const char *command)
{
	char *argv[] = {"/bin/sh", "-c", (char*)command, NULL};
	pid_t pid = fork();
	int i, flags;
	if (pid)
		return pid;
	/* close all parent's file descriptor */
	for (i = getdtablesize() - 1; i > -1; i--) {
		close(i); /* blindly closes everything before exec */
	}
	execv("/bin/sh", argv);
}
