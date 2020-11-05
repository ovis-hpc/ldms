#ifndef parse_stat_h_seen
#define parse_stat_h_seen

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>


struct proc_pid_io {
	unsigned long long rchar;
	unsigned long long wchar;
	unsigned long long syscr;
	unsigned long long syscw;
	unsigned long long read_bytes;
	unsigned long long write_bytes;
	unsigned long long cancelled_write_bytes;
};

struct proc_pid_statm {
	unsigned long long size;
	unsigned long long resident;
	unsigned long long share;
	unsigned long long text;
	unsigned long long lib;
	unsigned long long data;
	unsigned long long dt;
};

#define COMM_SZ NAME_MAX+1
struct proc_pid_stat {
	int pid;
	char comm[COMM_SZ];
	char state;
	int ppid;
	int pgrp;
	int session; 
	int tty_nr;
	int tpgid;
	unsigned int flags; 
	unsigned long minflt;
	unsigned long cminflt;
	unsigned long majflt;
	unsigned long cmajflt;
	unsigned long utime;
	unsigned long stime;
	long cutime;
	long cstime;
	long priority;
	long nice;
	long num_threads;
	long itrealvalue;
	unsigned long long starttime;
	unsigned long vsize;
	long rss;
	unsigned long rsslim;
	unsigned long startcode;
	unsigned long endcode;
	unsigned long startstack;
	unsigned long kstkesp;
	unsigned long kstkeip;
	unsigned long signal;
	unsigned long blocked;
	unsigned long sigignore;
	unsigned long sigcatch;
	unsigned long wchan;
	unsigned long nswap;
	unsigned long cnswap;
	int exit_signal;
	int processor;
	unsigned rt_priority;
	unsigned policy;
	unsigned long long delayacct_blkio_ticks;
};

struct proc_pid_fd {
	uint32_t fd_count; /*< number of open file descriptors */
	uint32_t fd_max; /*< highest file number */
	uint32_t fd_socket; /* targets starting with socket: */
	uint32_t fd_dev; /* targets starting with /dev: */
	uint32_t fd_anon_inode; /* targets starting with anon_inode: */
	uint32_t fd_pipe; /* targets starting with pipe: */
	uint32_t fd_path; /* targets starting with . or / but not /dev. */
};

/* \brief parse /proc/$pid/io and fill provided struct.
 * \return 0 on success, errno from fopen, ENODATA from
 * failed fgets, ENOKEY or ENAMETOOLONG from failed parse.
 */
int parse_proc_pid_io(struct proc_pid_io *s, const char *pid);

/* \brief parse /proc/$pid/stat and fill provided struct.
 * \return 0 on success, errno from fopen, ENODATA from
 * failed fgets, ENOKEY or ENAMETOOLONG from failed parse.
 */
int parse_proc_pid_stat(struct proc_pid_stat *s, const char *pid);

/* \brief parse /proc/$pid/stat and fill provided struct.
 * \return 0 on success, errno from fopen, ENODATA from
 * failed fgets, ENOKEY or ENAMETOOLONG from failed parse.
 */
int parse_proc_pid_statm(struct proc_pid_statm *s, const char *pid);

/* \brief parse /proc/$pid/fd and fill provided struct.
 * \param details: if false, update only fd_count, otherwise classify the 
 * file descriptors by link target name heuristics.
 * \return 0 on success, errno from readdir/opendir.
 * On a nonzero return, the content of s is undefined.
 */
int parse_proc_pid_fd(struct proc_pid_fd *s, const char *pid, bool details);

#endif /* parse_stat_h_seen */
