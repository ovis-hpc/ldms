/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2017 Sandia Corporation. All rights reserved.
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
#include <linux/limits.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <regex.h>
#include <assert.h>
#include <errno.h>
#include "util.h"

static const char *get_env_var(const char *src, size_t start_off, size_t end_off)
{
	static char name[100];
	size_t name_len;

	/* Strip $, {, and } from the name */
	if (src[start_off] == '$')
		start_off += 1;
	if (src[start_off] == '{')
		start_off += 1;
	if (src[end_off-1] == '}')
		end_off -= 1;

	name_len = end_off - start_off;
	assert(name_len < sizeof(name));

	strncpy(name,  &src[start_off], name_len);
	name[name_len] = '\0';
	return name;
}

char *str_repl_env_vars(const char *str)
{
	if (!str)
		return NULL;
	const char *name;
	const char *value;
	const char *values[100];
	char *res, *res_ptr;
	size_t res_len;
	regex_t regex;
	char *expr = "\\$\\{[[:alnum:]_]+\\}";
	int rc, i;
	regmatch_t match[100];
	size_t name_len, value_len, name_total, value_total;

	name_total = value_total = 0;

	rc = regcomp(&regex, expr, REG_EXTENDED);
	assert(rc == 0);
	int pos;
	rc = 0;
	for (i = pos = 0; !rc; pos = match[i].rm_eo, i++) {
		match[i].rm_so = -1;
		rc = regexec(&regex, &str[pos], 1, &match[i], 0);
		if (rc)
			break;
		res_len = match[i].rm_eo - match[i].rm_so;
		name_total += res_len;
		name = get_env_var(&str[pos], match[i].rm_so, match[i].rm_eo);
		value = getenv(name);
		if (value) {
			value_total += strlen(value);
			values[i] = value;
		} else {
			values[i] = "";
		}
		match[i].rm_eo += pos;
		match[i].rm_so += pos;
	}
	regfree(&regex);
	if (!i)
		return strdup(str);

	/* Allocate the result string */
	res_len = strlen(str) - name_total + value_total + 1;
	res = malloc(res_len);
	if (!res) {
		errno = ENOMEM;
		return NULL;
	}

	size_t so = 0;
	size_t eo = 0;

	res_ptr = res;
	res[0] = '\0';
	for (i = 0; i < sizeof(match) / sizeof(match[0]); i++) {
		if (match[i].rm_so < 0)
			break;
		size_t len = match[i].rm_so - so;
		memcpy(res_ptr, &str[so], len);
		res_ptr[len] = '\0';
		res_ptr = &res_ptr[len];
		so = match[i].rm_eo;
		strcpy(res_ptr, values[i]);
		res_ptr = &res[strlen(res)];
	}
	/* Copy the remainder of str into the result */
	strcpy(res_ptr, &str[so]);
	assert(res_len == (strlen(res) + 1));
 out:
	return res;
}

char *av_name(struct attr_value_list *av_list, int idx)
{
	if (!av_list)
		return NULL;
	if (idx < av_list->count)
		return av_list->list[idx].name;
	return NULL;
}

char *av_value(struct attr_value_list *av_list, const char *name)
{
	if (!av_list)
		return NULL;
	int i;
	for (i = 0; i < av_list->count; i++) {
		if (strcmp(name, av_list->list[i].name))
			continue;
		char *str = str_repl_env_vars(av_list->list[i].value);
		if (str) {
			string_ref_t ref = malloc(sizeof(*ref));
			if (!ref) {
				free(str);
				return NULL;
			}
			ref->str = str;
			LIST_INSERT_HEAD(&av_list->strings, ref, entry);
			return str;
		} else
			break;
	}
	return NULL;
}

char *av_value_at_idx(struct attr_value_list *av_list, int i)
{
	if (i < av_list->count) {
		char *str = str_repl_env_vars(av_list->list[i].value);
		if (str) {
			string_ref_t ref = malloc(sizeof(*ref));
			if (!ref) {
				free(str);
				return NULL;
			}
			ref->str = str;
			LIST_INSERT_HEAD(&av_list->strings, ref, entry);
			return str;
		}
	}
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

void av_free(struct attr_value_list *avl)
{
	if (!avl)
		return;
	string_ref_t ref;
	while (!LIST_EMPTY(&avl->strings)) {
		ref = LIST_FIRST(&avl->strings);
		free(ref->str);
		LIST_REMOVE(ref, entry);
		free(ref);
	}
	free(avl);
}

struct attr_value_list *av_new(size_t size)
{
	size_t bytes = sizeof(struct attr_value_list) +
		       (sizeof(struct attr_value) * size);
	struct attr_value_list *avl = malloc(bytes);

	if (avl) {
		memset(avl, 0, bytes);
		avl->size = size;
		LIST_INIT(&avl->strings);
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
	int i;
	if (pid)
		return pid;
	/* close all parent's file descriptor */
	for (i = getdtablesize() - 1; i > -1; i--) {
		close(i); /* blindly closes everything before exec */
	}
	execv("/bin/sh", argv);
	return pid;
}

#define DSTRING_USE_SHORT
#include "dstring.h"
char *ovis_join(char *pathsep, ...)
{
	va_list ap;
	char *result = NULL;
	char *tmp = NULL;
	const char *n = NULL;
	char *sep = "/";
	if (pathsep)
		sep = pathsep;

	dsinit(ds);
	va_start(ap, pathsep);
	n = va_arg(ap, const char *);
	if (n)
		tmp = dscat(ds, n);
	else
		errno = EINVAL;

	while ( tmp && (n = va_arg(ap, const char *)) != NULL) {
		dscat(ds, sep);
		tmp = dscat(ds, n);
	}

	va_end(ap);

	result = dsdone(ds);
	if (!tmp) {
		free(result);
		return NULL;
	}
	return result;
}

int ovis_join_buf(char *buf, size_t buflen, char *pathsep, ...)
{
	int rc = 0;
	va_list ap;
	const char *n = NULL;
	char *sep = "/";
	if (pathsep)
		sep = pathsep;
	size_t sepsize = strlen(sep);
	size_t len = 0;
	size_t chunk;

	if (!buf)
		return EINVAL;

	va_start(ap, pathsep);
	n = va_arg(ap, const char *);
	if (!n) {
		rc = EINVAL;
		goto out;
	}

	chunk = strlen(n);
	if ( (len + chunk) < buflen) {
		printf("chunk %zu %s\n", chunk, n);
		strncat(buf + len, n, chunk);
		len += chunk;
	} else {
		rc = E2BIG;
	}

	while ( 0 == rc && (n = va_arg(ap, const char *)) != NULL) {
		if ((len + sepsize) < buflen) {
			strncat(buf + len, sep, sepsize);
			len += sepsize;
		} else {
			rc = E2BIG;
			break;
		}
		chunk = strlen(n);
		if ((len + chunk) < buflen) {
			strncat(buf + len, n, chunk);
			len += chunk;
		} else {
			rc = E2BIG;
			break;
		}
	}
 out:
	va_end(ap);
	buf[len] = '\0';

	return rc;
}

int f_file_exists(const char *path)
{
	struct stat st;
	int rc = stat(path, &st);
	return !rc;
}

int f_is_dir(const char *path)
{
	struct stat st;
	int rc = stat(path, &st);
	if (rc == -1)
		return 0;
	rc = S_ISDIR(st.st_mode);
	if (!rc) {
		errno = ENOTDIR;
	}
	return rc;
}

int f_mkdir_p(const char *path, __mode_t mode)
{
	char *str = strdup(path);
	char *_str;
	int rc = 0;
	if (!str)
		return ENOMEM;
	_str = str;
	int len = strlen(str);
	if (str[len-1] == '/') {
		len--;
		str[len] = 0;
	}
	if (_str[0] == '/')
		_str++; /* skip the leading '/' */
	while ((_str = strstr(_str, "/"))) {
		*_str = 0;
		if (!f_file_exists(str)) {
			if (mkdir(str, mode) == -1) {
				rc = errno;
				goto cleanup;
			}
		}
		if (!f_is_dir(str)) {
			errno = ENOTDIR;
			rc = ENOTDIR;
			goto cleanup;
		}
		*_str = '/';
		_str++;
	}
	rc = mkdir(str, 0755);
	if (rc)
		rc = errno;
cleanup:
	free(str);
	return rc;
}

FILE *fopen_perm(const char *path, const char *f_mode, int o_mode)
{
	int fd;
	int flags = O_RDWR|O_CREAT;

	if (*f_mode == 'w')
		flags |= O_TRUNC;
	fd = open(path, flags, o_mode);
	if (fd < 0)
		return NULL;
	return fdopen(fd, f_mode);
}
