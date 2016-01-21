/* 
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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

#include "spool.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <stdio.h>
#include <pthread.h>

#define DEBUG_SLAVE 0
#define DEBUG_MASTER 0

static int sheller_pipefd[2];
static FILE *sp = NULL;
static enum shell_state {
	ss_unstarted,
	ss_starting,
	ss_started,
	ss_dead
} init_once = ss_unstarted;

static pthread_mutex_t sheller_lock = PTHREAD_MUTEX_INITIALIZER;
#define SHELLER_ARGMAX 1024
#define SHELLER_ARGLEN_MAX 131072
static const char *arg0 = "sheller_ARGV0\n";
static const char *argend = "sheller_ARGVEND\n";

static void clear_argv(int argc, char **argv, int usefree)
{
	int i;
	if (!argv)
		return;
	for (i=0;i < argc; i++) {
		if (usefree)
			free(argv[i]);
		argv[i] = NULL;
	}
}

#if DEBUG_SLAVE
FILE *commlog = NULL;
#endif
FILE *shlog = NULL;

static int dispatch(int argc, char **argv) {
	pid_t pid;
	int rc = 0;
	if (!(pid = fork())) {
		if (!fork()) {
#if DEBUG_SLAVE
			int i = 0;
			for ( ; i < argc; i++) {
				fprintf(commlog,"%s ", argv[i]);
			}
			fprintf(commlog,"\n");
			fflush(commlog);
#endif
			rc = execv(argv[0],argv);
			if (rc) {
				rc = errno;
#if DEBUG_SLAVE
				fprintf(commlog,"%s\n",strerror(rc));
#endif
			}
			exit(rc);
		} else {
			/* the first spawned process exits without 
			trying to use slave atexit handlers. */
			_exit(0);
		}
	}
	if (pid > 0) {
		/* this is the slave process */
		/* wait for the first spawned process to exit
			which it will immediately */
		waitpid(pid,NULL,0);
	}
	if (pid < 0 ) {
		rc = errno;
	}
	return rc;
}

static int shell_child() {
	close(sheller_pipefd[1]); 
#if 0
	if ( freopen("/dev/null", "r", stdin) );
	if ( freopen("/dev/null", "w", stdout) );
	if ( freopen("/dev/null", "w", stderr) );
#endif
#if DEBUG_SLAVE
	commlog = fopen("/tmp/commlog","w");
#endif
	
	FILE *csp = fdopen(sheller_pipefd[0],"r");
	char *argv[SHELLER_ARGMAX+1];
	char buf[SHELLER_ARGLEN_MAX];
	char *s;
	int rc = 0;
	int argc = 0;
	clear_argv(argc,argv,0);
	while (1) {
		s = fgets(buf, sizeof(buf), csp);
		if (!s)
			break;
		if (strcmp(s, arg0) == 0) {
			clear_argv(argc, argv, 1);
			argc = 0;
			continue;
		}
		if (strcmp(s, argend) == 0) {
			argv[argc] = NULL;
			(void)dispatch(argc, argv);
			clear_argv(argc, argv, 1);
			argc = 0;
			continue;
		}
		int sz = strlen(s);
		while (isspace(s[sz-1]) && sz > 0) {
			s[sz-1] = '\0';
			sz--;
		}
		if (!sz)
			continue;
		argv[argc] = strdup(s);
		if (!argv[argc]) {
			/* bail: out of memory */
			clear_argv(argc, argv, 1);
			rc = ENOMEM;
			break;
		}
		argc++;
		if (argc >= SHELLER_ARGMAX) {
			/* drop input that is too big.
			Remainder yet unread will be flushed at next arg0. */
			clear_argv(argc, argv, 1);
			argc = 0;
		}
	}
	fclose(csp);
#if DEBUG_SLAVE
	fclose(commlog);
#endif
	return rc;
}

/** Fork a slave which listens for commands from us but will
 * not share any output related state.
 * The slave takes these commands and double-forks independent processes
 * that share no state with the calling daemon.
 *
 * This should be called before any output files are opened
 * or multithreading is started, thus avoiding all potential for
 * competing buffer flushes.
 * This can only be called once, since presumably if the child 
 * dies later the restart will be after IO/threads are running.
 * \return 0 ok, or errno.
 */
int sheller_init() {

	if (init_once != ss_unstarted) {
		return EALREADY;
	}
	init_once = ss_starting;
	pid_t child_pid;
	int rc = 0;

	fflush(NULL);
	if (pipe(sheller_pipefd) != 0) {
		rc = errno;
		init_once = ss_dead;
		return rc;
	} 
	fcntl(sheller_pipefd[1], F_SETFD, FD_CLOEXEC);

	child_pid = fork(); 
	if (child_pid == 0) {
		_exit(shell_child());
	} else {
		close(sheller_pipefd[0]);
		sp = fdopen(sheller_pipefd[1],"a");
		if (!sp) {
			int rc = errno;
			close(sheller_pipefd[1]);
			init_once = ss_dead;
			return rc;
		}
	}
	init_once = ss_started;
	return 0;
}

/** Stop the sheller. No other sheller calls will work after this. */
int sheller_final() {
	if (init_once != ss_started)
		return ENODEV;
	pthread_mutex_lock(&sheller_lock);
	init_once = ss_dead;
	int rc = fclose(sp);
	if (rc != 0)
		rc = errno;
	pthread_mutex_unlock(&sheller_lock);
	return rc;
}

/* Dispatch an argument list to the child spawner for use with execl.
 * Strings in argv may contain embedded spaces.
 * Strings in argv should be no longer than 131071 bytes.
 * \param argc length of argv which must be < 1024.
 * \param argv array with name of executable and its args.
 */
int sheller_call(int argc, const char **argv) {
	if (init_once < ss_started) {
		return ENODEV;
	}

	pthread_mutex_lock(&sheller_lock);
	if (init_once > ss_started) {
		pthread_mutex_unlock(&sheller_lock);
		return ECHILD;
	}

	int i = 0;
	if (fprintf(sp, "%s", arg0) < 0) 
		goto err;

	for ( ; i < argc; i++) {
		if ( fprintf(sp, "%s\n", argv[i]) < 0) {
			goto err;
		}
	}

	if (fprintf(sp, "%s", argend) < 0) 
		goto err;
	if (fflush(sp) != 0) {
		goto err;
	}
	
	pthread_mutex_unlock(&sheller_lock);
	return 0;
 err:
	init_once = ss_dead;
	fclose(sp);
	pthread_mutex_unlock(&sheller_lock);
	return ECHILD;
}

static const char badarg[] = "bad arguments";
static const char badspooler[] = "spool program not usable";
static const char baddata[] = "data filename not usable";
static const char baddir[] = "directory given not correct";
static const char badfork[] = "spooling failed";

int ovis_file_spool(const char *spoolexec, const char *outfile,
	const char *spooldir, const char **errmsg)
{
	if (!errmsg || (!spoolexec && !spooldir) || !outfile)  {
		return 0;
	}
	if (!spoolexec || !outfile || !spooldir || 
		strlen(spoolexec) <2 || strlen(outfile) < 2 ||
		strlen(spooldir) < 2) {
		*errmsg = badarg;
		return EINVAL;
	}
	struct stat ebuf;   
	struct stat fbuf;   
	struct stat dbuf;   
	int rc;
	int err=0;
	if ( (rc = stat(spoolexec, &ebuf)) != 0) {
		rc = errno;
		*errmsg = badspooler;
		err++;
	}
	if ( (rc = stat(outfile, &fbuf)) != 0) {
		rc = errno;
		*errmsg = badarg;
		err++;
	}
	if ( (rc=stat(spooldir, &dbuf)) != 0 || !S_ISDIR(dbuf.st_mode)) {
		rc = errno;
		*errmsg = baddir;
		err++;
	}
	if (err)
		return rc;

	sheller_init();
	const char *argv[] = { spoolexec, outfile, spooldir, NULL};
	int sherr = sheller_call((int)(sizeof(argv)/sizeof(argv[0])) -1, argv);
	if (sherr != 0) {
		*errmsg = badfork;
		return ECANCELED;
	}
	return 0;
}

