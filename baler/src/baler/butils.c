/* -*- c-basic-offset: 8 -*-
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
/**
 * \file butils.c
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 20, 2013
 *
 * \brief Implementation of functions (and some global variables) defined in
 * butils.h
 */
#include "butils.h"
#include <linux/limits.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>

FILE *blog_file;
pthread_mutex_t __blog_mutex = PTHREAD_MUTEX_INITIALIZER;

void __attribute__ ((constructor)) butils_init();
void butils_init()
{
	static int visited = 0;
	if (visited)
		return;
	visited = 1;
	blog_file = stdout;
}

void blog_set_file(FILE *f)
{
	blog_file = f;
	/* redirect stdout and stderr to this file too */
	dup2(fileno(f), 1);
	dup2(fileno(f), 2);
}

int blog_open_file(const char *path)
{
	FILE *f = fopen(path, "a");
	if (!f)
		return errno;
	blog_set_file(f);
	return 0;
}

int blog_close_file()
{
	return fclose(blog_file);
}

void __blog(const char *fmt, ...)
{
	pthread_mutex_lock(&__blog_mutex);
	va_list ap;
	char date[32];
	time_t t = time(NULL);
	ctime_r(&t, date);
	date[24] = 0;
	fprintf(blog_file, "%s ", date);
	va_start(ap, fmt);
	vfprintf(blog_file, fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&__blog_mutex);
	fflush(blog_file);
}

int blog_flush()
{
	return fflush(blog_file);
}

int blog_rotate(const char *path)
{
	int rc = 0;
	FILE *new_log = fopen(path, "a");
	if (!new_log)
		return errno;

	pthread_mutex_lock(&__blog_mutex);
	dup2(fileno(new_log), 1);
	dup2(fileno(new_log), 2);

	blog_flush();
	blog_close_file();

	blog_file = new_log;
	pthread_mutex_unlock(&__blog_mutex);
	return rc;
}

int bfile_exists(const char *path)
{
	struct stat st;
	int rc = stat(path, &st);
	return !rc;
}

int bis_dir(const char *path)
{
	struct stat st;
	int rc = stat(path, &st);
	if (rc == -1)
		return 0;
	return S_ISDIR(st.st_mode);
}

int bmkdir_p(const char *path, __mode_t mode)
{
	static char str[PATH_MAX];
	static char *_str;
	strcpy(str, path);
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
		if (!bfile_exists(str)) {
			if (mkdir(str, mode) == -1)
				return -1;
		}
		if (!bis_dir(str)) {
			errno = ENOENT;
			return -1;
		}
		*_str = '/';
		_str++;
	}
	return mkdir(str, 0755);
}

struct bdstr* bdstr_new(size_t len)
{
	if (!len)
		len = 4096;
	struct bdstr *s = malloc(sizeof(*s));
	if (!s)
		return NULL;
	s->str = malloc(len);
	if (!s->str) {
		free(s);
		return NULL;
	}
	s->alloc_len = len;
	s->str_len = 0;
	s->str[0] = '\0';
	return s;
}

int bdstr_expand(struct bdstr *bs, size_t new_size)
{
	char *new_str = realloc(bs->str, new_size);
	if (!new_str)
		return errno;
	bs->alloc_len = new_size;
	bs->str = new_str;
	return 0;
}

int bdstr_append(struct bdstr *bs, const char *str)
{
	int len = strlen(str);
	int rc;
	if (bs->str_len + len + 1 > bs->alloc_len) {
		rc = bdstr_expand(bs, bs->alloc_len + 65536);
		if (rc)
			return rc;
	}
	strcat(bs->str + bs->str_len, str);
	bs->str_len += len;
	return 0;
}

