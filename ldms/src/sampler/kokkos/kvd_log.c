
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

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

