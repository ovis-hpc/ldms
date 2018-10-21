/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#include "kvd_log.h"

FILE *kvd_log_file;
pthread_mutex_t __kvd_log_mutex = PTHREAD_MUTEX_INITIALIZER;
int __kvd_log_level;

void __attribute__ ((constructor)) butils_init();
void butils_init()
{
	static int visited = 0;
	if (visited)
		return;
	visited = 1;
	kvd_log_file = stderr;
}

void kvd_log_set_level(int level)
{
	__kvd_log_level = level;
}

const char *__level_lbl[] = {
	[KVD_LOG_LV_DEBUG] = "DEBUG",
	[KVD_LOG_LV_INFO] = "INFO",
	[KVD_LOG_LV_WARN] = "WARN",
	[KVD_LOG_LV_ERR] = "ERROR",
	[KVD_LOG_LV_QUIET] = "QUIET",
};

int kvd_log_set_level_str(const char *level)
{
	int i, rc;
	int n, len;
	/* Check if level is pure number */
	n = 0;
	sscanf(level, "%d%n", &i, &n);
	len = strlen(level);
	if (n == len) {
		kvd_log_set_level(i);
		return 0;
	}
	for (i = 0; i < KVD_LOG_LV_LAST; i++) {
		rc = strncmp(level, __level_lbl[i], len);
		if (rc == 0) {
			kvd_log_set_level(i);
			return 0;
		}
	}
	return EINVAL;
}

int kvd_log_get_level()
{
	return __kvd_log_level;
}

void kvd_log_set_file(FILE *f)
{
	kvd_log_file = f;
	/* redirect stdout and stderr to this file too */
	dup2(fileno(f), 1);
	dup2(fileno(f), 2);
}

int kvd_log_open(const char *path)
{
	FILE *f = fopen(path, "a");
	if (!f)
		return errno;
	kvd_log_set_file(f);
	return 0;
}

int kvd_log_close()
{
	return fclose(kvd_log_file);
}

void __kvd_log(const char *fmt, ...)
{
	pthread_mutex_lock(&__kvd_log_mutex);
	va_list ap;
	char date[32];
	time_t t = time(NULL);
	ctime_r(&t, date);
	date[24] = 0;
	fprintf(kvd_log_file, "%s ", date);
	va_start(ap, fmt);
	vfprintf(kvd_log_file, fmt, ap);
	va_end(ap);
	fflush(kvd_log_file);
	pthread_mutex_unlock(&__kvd_log_mutex);
}

int kvd_log_flush()
{
	return fflush(kvd_log_file);
}
