static int64_t uptime()
{
	struct sysinfo s_info;
	int error = sysinfo(&s_info);
	if(error != 0)
	{
		return -1;
	}
	return (int64_t)s_info.uptime;
}

/*
 * Copyright (C) 2014-2018 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Written by Colin Ian King <colin.king@canonical.com>
 *
 * Some of this code originally derived from eventstat and powerstat
 * also by the same author.
 *
 */

/*
 *  get_proc_self_stat_field()
 *      find nth field of /proc/$PID/stat data. This works around
 *      the problem that the comm field can contain spaces and
 *      multiple ) so sscanf on this field won't work.  The returned
 *      pointer is the start of the Nth field and it is up to the
 *      caller to determine the end of the field
 */
static const char *get_proc_self_stat_field(const char *buf, const int num)
{
	const char *ptr = buf, *comm_end;
	int n;

	if (num < 1 || !buf || !*buf)
		return NULL;
	if (num == 1)
		return buf;
	if (num == 2)
		return strstr(buf, "(");

	comm_end = NULL;
	for (ptr = buf; *ptr; ptr++) {
		if (*ptr == ')')
			comm_end = ptr;
	}
	if (!comm_end)
		return NULL;
	comm_end++;
	n = num - 2;

	ptr = comm_end;
	while (*ptr) {
		while (*ptr && *ptr == ' ')
			ptr++;
		n--;
		if (n <= 0)
			break;
		while (*ptr && *ptr != ' ')
			ptr++;
	}

	return ptr;
}

static int get_timeval_from_tick(uint64_t starttime, struct timeval * const tv)
{
	/* from proc_info_get_timeval convert tick since boot to clock */
	double uptime_secs = (double)uptime(), secs = 0;
	long jiffies;
	struct timeval now = {.tv_sec = 0, .tv_usec = 0};

	errno = 0;
	jiffies = sysconf(_SC_CLK_TCK);
	if (errno)
		return 1;
	secs = uptime_secs - ((double)starttime / (double)jiffies);
	if (secs < 0.0)
		return 1;

	if (gettimeofday(&now, NULL) < 0)
		return 1;

	secs = ( (double)now.tv_sec + ((double)now.tv_usec / 1000000.0) ) - secs;
	tv->tv_sec = secs;
	tv->tv_usec = (suseconds_t)secs % 1000000;
	return 0;
}

void proc_exe_buf(const pid_t pid, char *buffer, size_t buflen)
{
	ssize_t ret;
	char path[32];
	snprintf(path, sizeof(path), "/proc/%d/exe", pid);
	ret = readlink(path, buffer, buflen - 1);
	if (ret < 0)
		sprintf(buffer,"(nullexe)");
	else
		buffer[ret] = '\0';
}


#define GOT_TGID                (0x01)
#define GOT_PPID                (0x02)
#define GOT_ALL                 (GOT_TGID | GOT_PPID)


/*
 *  get_parent_pid()
 *      get parent pid and set is_thread to true if process
 *      not forked but a newly created thread
 */
static pid_t get_parent_pid(const pid_t pid, bool * const is_thread)
{
	FILE *fp;
	pid_t tgid = 0, ppid = 0;
	unsigned int got = 0;
	char path[PATH_MAX];
	char buffer[4096];

	*is_thread = false;
	(void)snprintf(path, sizeof(path), "/proc/%u/status", pid);
	fp = fopen(path, "r");
	if (!fp)
		return 0;

	while (((got & GOT_ALL) != GOT_ALL) &&
		(fgets(buffer, sizeof(buffer), fp) != NULL)) {
		if (!strncmp(buffer, "Tgid:", 5)) {
			if (sscanf(buffer + 5, "%u", &tgid) == 1) {
				got |= GOT_TGID;
			} else {
				tgid = 0;
			}
		}
		if (!strncmp(buffer, "PPid:", 5)) {
			if (sscanf(buffer + 5, "%u", &ppid) == 1)
				got |= GOT_PPID;
			else
				ppid = 0;
		}
	}
	(void)fclose(fp);

	if ((got & GOT_ALL) == GOT_ALL) {
		/*  TGID and PID are not the same if it is a thread */
		if (tgid != pid) {
			/* In this case, the parent is the TGID */
			ppid = tgid;
			*is_thread = true;
		}
	} else {
		ppid = 0;
	}

	return ppid;
}

