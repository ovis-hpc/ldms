#include "parse_stat.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/time.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include "ovis_util/util.h"

#define PIDFMAX 32
#define BUFMAX 512

int parse_proc_pid_io(struct proc_pid_io *s, const char *pid) {
	if (!s)
		return EINVAL;
	int rc;
	errno = 0;
	char fname[PIDFMAX];
	if (strlen(pid) >= PIDFMAX)
		return EINVAL;
	sprintf(fname,"/proc/%s/io", pid);
	FILE *f = fopen(fname, "r");
	if (!f)
		return errno;
	rc = fscanf(f, "rchar: %llu wchar: %llu syscr: %llu syscw: %llu  read_bytes: %llu write_bytes: %llu cancelled_write_bytes: %llu",
		&s->rchar,
		&s->wchar,
		&s->syscr,
		&s->syscw,
		&s->read_bytes,
		&s->write_bytes,
		&s->cancelled_write_bytes);
	fclose(f);
	if (rc != 7) {
#ifdef MAIN
		printf("io only %d\n", rc);
#endif
		return ENOKEY;
	}
	return 0;
}

/* parse /proc/self/statm and fill provides struct.
 * \return 0 on success, errno from fopen, ENODATA from
 * failed fgets, ENOKEY or ENAMETOOLONG from failed parse.
 */
int parse_proc_pid_statm(struct proc_pid_statm *s, const char *pid) {
	if (!s)
		return EINVAL;
	int rc;
	char buf[BUFMAX];
	errno = 0;
	char fname[PIDFMAX];
	if (strlen(pid) >= PIDFMAX)
		return EINVAL;
	sprintf(fname,"/proc/%s/statm", pid);
	FILE *f = fopen(fname, "r");
	if (!f)
		return errno;
	char *dat = fgets(buf, BUFMAX, f);
	fclose(f);
	if (!dat)
		return ENODATA;
#ifdef MAIN
	printf("%s\n", dat);
#endif
	rc = sscanf(dat,
		"%llu "
		"%llu "
		"%llu "
		"%llu "
		"%llu "
		"%llu "
		"%llu",
		&s->size,
		&s->resident,
		&s->share,
		&s->text,
		&s->lib,
		&s->data,
		&s->dt);
	if (rc != 7) {
#ifdef MAIN
		printf("statm only %d\n", rc);
#endif
		return ENOKEY;
	}
	return 0;
}

#undef BUFMAX
#define BUFMAX 2048
/* parse /proc/$pid/stat and fill provides struct.
 * \return 0 on success, errno from fopen, ENODATA from
 * failed fgets, ENOKEY or ENAMETOOLONG from failed parse.
 */
int parse_proc_pid_stat(struct proc_pid_stat *s, const char *pid) {
	if (!s)
		return EINVAL;
	int rc;
	char buf[BUFMAX];
	errno = 0;
	char fname[PIDFMAX];
	if (strlen(pid) >= PIDFMAX)
		return EINVAL;
	sprintf(fname,"/proc/%s/stat", pid);
	FILE *f = fopen(fname, "r");
	if (!f)
		return errno;
	char *dat = fgets(buf, BUFMAX, f);
	fclose(f);
	if (!dat)
		return ENODATA;
#ifdef MAIN
	printf("%s\n", dat);
#endif
	/* do a bit of work because file names may contain space and paren. */
	char *commstart = strchr(dat, '(');
	char *commend = strrchr(dat, ')');
	if (!commstart || !commend)
		return ENOKEY;
	*commstart = '\0';
	*commend = '\0';
	commend++;
	commstart++;
	if ((commend - commstart) >= COMM_SZ)
		return ENAMETOOLONG;
	memccpy(s->comm, commstart , 0, COMM_SZ - 1);
	s->comm[COMM_SZ-1] = 0;
	rc = sscanf(dat, "%d", &s->pid);
	if (rc != 1) {
		return ENOKEY;
	}
	rc = sscanf(commend,
		" %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu "
		"%ld %ld %ld %ld %ld %ld "
		"%llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu "
		"%lu %lu %lu %lu %lu %d %d "
		"%u %u %llu",
		&s->state,
		&s->ppid,
		&s->pgrp,
		&s->session,
		&s->tty_nr,
		&s->tpgid,
		&s->flags,
		&s->minflt,
		&s->cminflt,
		&s->majflt,
		&s->cmajflt,
		&s->utime,
		&s->stime,
		&s->cutime,
		&s->cstime,
		&s->priority,
		&s->nice,
		&s->num_threads,
		&s->itrealvalue,
		&s->starttime,
		&s->vsize,
		&s->rss,
		&s->rsslim,
		&s->startcode,
		&s->endcode,
		&s->startstack,
		&s->kstkesp,
		&s->kstkeip,
		&s->signal,
		&s->blocked,
		&s->sigignore,
		&s->sigcatch,
		&s->wchan,
		&s->nswap,
		&s->cnswap,
		&s->exit_signal,
		&s->processor,
		&s->rt_priority,
		&s->policy,
		&s->delayacct_blkio_ticks);
	if (rc != 40) {
#ifdef MAIN
		printf("stat only %d\n", rc);
#endif
		return ENOKEY;
	}
	return 0;
}

#undef BUFMAX
#define BUFMAX 32
int parse_proc_pid_fd(struct proc_pid_fd *s, const char *pid, bool details)
{
	if (!s)
		return EINVAL;
	int rc = 0;
	int blen;
	char buf[BUFMAX];
	errno = 0;
	char dname[2*PIDFMAX];
	if (strlen(pid) >= PIDFMAX)
		return EINVAL;
	sprintf(dname,"/proc/%s/fd", pid);
	DIR * dirp;

	dirp = opendir(dname);
	if (!dirp)
		return errno;
	s->fd_count = 0;
	s->fd_max = 0;
	s->fd_socket = 0;
	s->fd_dev = 0;
	s->fd_anon_inode = 0;
	s->fd_pipe = 0;
	s->fd_path = 0;
	struct dirent *result;
	long n;
	char *endptr;
	result = readdir(dirp);
	while (result != NULL) {
		n = strtol(result->d_name, &endptr, 10);
		if (endptr && (endptr[0] != '\0')) {
			/* This is not a file descriptor, e.g., '.' and '..' */
			goto next;
		}
		s->fd_count++;
		if (details) {
			if (n > s->fd_max) {
				s->fd_max = n;
			}
			buf[0] = '\0';
			snprintf(dname, 2*PIDFMAX, "/proc/%s/fd/%ld", pid, n);
			blen = readlink(dname, buf, BUFMAX);
			switch (buf[0]) {
			case '.':
				s->fd_path++;
				break;
			case '/':
				if (strncmp(buf, "/dev/", 5) == 0) {
					s->fd_dev++;
				} else {
					s->fd_path++;
				}
				break;
			case 'a':
				if (blen > 11 && strncmp(buf, "anon_inode:", 11) == 0) {
					s->fd_anon_inode++;
				}
				break;
			case 'p':
				if (blen > 5 && strncmp(buf, "pipe:", 5) == 0) {
					s->fd_pipe++;
				}
				break;
			case 's':
				if (blen > 7 && strncmp(buf, "socket:", 7) == 0) {
					s->fd_socket++;
				}
				break;
			}
		}
	next:
		result = readdir(dirp);
	}
	closedir(dirp);
	return rc;

}

#ifdef MAIN

void dump_proc_pid_io(struct proc_pid_io *s)
{
	if (!s)
		return;
	printf( "%llu %llu %llu %llu %llu %llu %llu\n",
		s->rchar,
		s->wchar,
		s->syscr,
		s->syscw,
		s->read_bytes,
		s->write_bytes,
		s->cancelled_write_bytes);
}

void dump_proc_pid_statm(struct proc_pid_statm *s)
{
	if (!s)
		return;
	printf( "%llu %llu %llu %llu %llu %llu %llu\n",
		s->size,
		s->resident,
		s->share,
		s->text,
		s->lib,
		s->data,
		s->dt);
}

void dump_proc_pid_stat(struct proc_pid_stat *s)
{
	if (!s)
		return;
	printf( "%d (%s) %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu "
		"%ld %ld %ld %ld %ld %ld "
		"%llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu "
		"%lu %lu %lu %lu %lu %d %d "
		"%u %u %llu\n",
		s->pid,
		s->comm,
		s->state,
		s->ppid,
		s->pgrp,
		s->session,
		s->tty_nr,
		s->tpgid,
		s->flags,
		s->minflt,
		s->cminflt,
		s->majflt,
		s->cmajflt,
		s->utime,
		s->stime,
		s->cutime,
		s->cstime,
		s->priority,
		s->nice,
		s->num_threads,
		s->itrealvalue,
		s->starttime,
		s->vsize,
		s->rss,
		s->rsslim,
		s->startcode,
		s->endcode,
		s->startstack,
		s->kstkesp,
		s->kstkeip,
		s->signal,
		s->blocked,
		s->sigignore,
		s->sigcatch,
		s->wchan,
		s->nswap,
		s->cnswap,
		s->exit_signal,
		s->processor,
		s->rt_priority,
		s->policy,
		s->delayacct_blkio_ticks);

}

void dump_proc_pid_fd(struct proc_pid_fd *s)
{
	if (!s)
		return;
	printf( "count=%u max=%u socket=%u dev=%u anon=%u pipe=%u path=%u\n",
		s->fd_count,
		s->fd_max,
		s->fd_socket,
		s->fd_dev,
		s->fd_anon_inode,
		s->fd_pipe,
		s->fd_path);
}

struct proc_pid_stat s;
struct proc_pid_io si;
struct proc_pid_statm sm;
struct proc_pid_fd sf;
struct proc_pid_fd sfd, rfd;

int main() {
	struct timeval t0, t1;
	gettimeofday(&t0, NULL);
	int rc = parse_proc_pid_io(&si, "self");
	int rc2 = parse_proc_pid_stat(&s, "self");
	int rc3 = parse_proc_pid_statm(&sm, "self");
	int rc4 = parse_proc_pid_fd(&sf, "self", false);
	int rc5 = parse_proc_pid_fd(&sfd, "self", true);
	int rc6 = parse_proc_pid_fd(&rfd, "1", true);
	gettimeofday(&t1,NULL);
	uint64_t us1, us0, du;
	us1 = 1000000* t1.tv_sec + t1.tv_usec;
	us0 = 1000000* t0.tv_sec + t0.tv_usec;
	du = us1 - us0;
	printf("%lu.06%lu\n%lu.%06lu\nsample usec %" PRIu64 "\n",
		t1.tv_sec, t1.tv_usec,
		t0.tv_sec, t0.tv_usec, du);

	printf("io test\n");
	if (rc != 0) {
		printf("fail: %d %s\n", rc, STRERROR(rc));
		return rc;
	}
	dump_proc_pid_io(&si);

	printf("stat test\n");
	if (rc2 != 0) {
		printf("fail: %d %s\n", rc2, STRERROR(rc2));
		return rc2;
	}
	dump_proc_pid_stat(&s);

	printf("statm test\n");
	if (rc3 != 0) {
		printf("fail: %d %s\n", rc3, STRERROR(rc3));
		return rc3;
	}
	dump_proc_pid_statm(&sm);

	printf("fd test\n");
	if (rc4 != 0) {
		printf("fail: %d %s\n", rc4, STRERROR(rc4));
		return rc4;
	}
	dump_proc_pid_fd(&sf);

	printf("fd test details\n");
	if (rc5 != 0) {
		printf("fail: %d %s\n", rc5, STRERROR(rc5));
		return rc5;
	}
	dump_proc_pid_fd(&sfd);

	printf("fd root test details\n");
	if (rc6 != 0) {
		printf("xfail: %d %s\n", rc6, STRERROR(rc6));
	} else
		dump_proc_pid_fd(&rfd);

	return 0;
}
#endif
