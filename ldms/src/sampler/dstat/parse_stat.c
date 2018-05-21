#include "parse_stat.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/time.h>

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
	strncpy(s->comm, commstart , COMM_SZ);
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

struct proc_pid_stat s;
struct proc_pid_io si;
struct proc_pid_statm sm;

int main() {
	struct timeval t0, t1;
	gettimeofday(&t0, NULL);
	int rc = parse_proc_pid_io(&si, "self");
	int rc2 = parse_proc_pid_stat(&s, "self");
	int rc3 = parse_proc_pid_statm(&sm, "self");
	gettimeofday(&t1,NULL);
	uint64_t us1, us0, du;
	us1 = 1000000* t1.tv_sec + t1.tv_usec;
	us0 = 1000000* t0.tv_sec + t0.tv_usec;
	du = us1 - us0;
	printf("%u.06%u\n%u.%06u\nsample usec %" PRIu64 "\n",
		t1.tv_sec, t1.tv_usec,
		t0.tv_sec, t0.tv_usec, du);

	printf("io test\n");
	if (rc != 0) {
		printf("fail: %d %s\n", rc, strerror(rc));
		return rc;
	}
	dump_proc_pid_io(&si);

	printf("stat test\n");
	if (rc2 != 0) {
		printf("fail: %d %s\n", rc2, strerror(rc));
		return rc2;
	}
	dump_proc_pid_stat(&s);

	printf("statm test\n");
	if (rc3 != 0) {
		printf("fail: %d %s\n", rc3, strerror(rc));
		return rc3;
	}
	dump_proc_pid_statm(&sm);

	return 0;
}
#endif
