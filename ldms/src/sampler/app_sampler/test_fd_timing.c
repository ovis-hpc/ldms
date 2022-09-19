
/**
 * timing test for relative costs of /proc/pid/fd/ scanning operations.
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>


#define PROCPID_SZ 128
#define BUFMAX 4096
struct fey {
	ino_t          d_ino;       /* inode number */
	unsigned short d_reclen;    /* length of this record */
};

int procdir(const char *path, int what)
{

	struct fey fe;
        char dname[PROCPID_SZ];
        int blen;
        char buf[BUFMAX];
        DIR *dir;
        struct dirent enttmp;
        struct dirent *dent;
        dir = opendir(path);
        if (!dir) {
                return 1;
        }
        errno = 0;
        int rc;
        char *endptr;
	struct stat statb;
	long n;

	while ( (rc = readdir_r(dir, &enttmp, &dent)), (rc == 0 && dent != NULL)) {
		n = strtol(enttmp.d_name, &endptr, 10);
		if (endptr && (endptr[0] != '\0')) {
			/* This is not a file descriptor, e.g., '.' and '..' */
			continue;
		}
		snprintf(dname, PROCPID_SZ, "%s/%s", path, enttmp.d_name);
		if (enttmp.d_type != DT_LNK) {
			continue;
		}
		fe.d_ino = enttmp.d_ino;
		fe.d_reclen = enttmp.d_reclen;
		/* what==1: skip readlink scan
		 * what==2: readlink scan
		 * what==3: readlink+stat scan
		 * what==4: lstat scan fd
		 */
		switch (what) {
		case 1:
		//	blen = readlink(dname, buf, BUFMAX);
			break;
		case 2:
			blen = readlink(dname, buf, BUFMAX);
			buf[blen] = '\0';
			break;
		case 3:
			blen = readlink(dname, buf, BUFMAX);
			buf[blen] = '\0';
			if (buf[0] == '/') {
				memset(&statb, 0, sizeof(statb));
				if (stat(buf, &statb)) {
					printf("cannot stat %s: %d %s\n", buf, errno, strerror(errno));
				}
			}
			break;
		case 4:
			lstat(dname, &statb);
			break;
		case 5:
			blen = readlink(dname, buf, BUFMAX);
			buf[blen] = '\0';
			if (buf[0] == '/') {
				lstat(dname, &statb);
				printf("ino %ld, reclen %d, lsz %ld, lino %ld, %s  %s \n", fe.d_ino, fe.d_reclen, statb.st_size, statb.st_ino, dname,  buf);
			}
			break;
		}
	}
	closedir(dir);
	return 0;
}

int main()
{
	struct timeval tv1;
	struct timeval tv2;
	struct timeval tdiff;
	int what;
	glob_t g;
	glob("/proc/[0-9]*/fd", GLOB_ONLYDIR, NULL, &g);
	char *msg[] = {NULL, "scan", "readlink", "readlink-st", "lst"};
	int c;
	for (what = 1; what < 5; what++) {
		gettimeofday(&tv1, NULL);
		int k;
		for (k = 0; k < 100; k++) {
			for (c = 0; c < g.gl_pathc; c++) {
				procdir(g.gl_pathv[c], what);
			}
		}
		gettimeofday(&tv2, NULL);
		timersub(&tv2, &tv1, &tdiff);
		printf("difftime: %ld.%06ld %s\n", tdiff.tv_sec, tdiff.tv_usec, msg[what]);
	}
	globfree(&g);
	return 0;
}
