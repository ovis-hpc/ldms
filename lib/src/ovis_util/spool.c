
#include "spool.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

void ovis_file_spool(const char *spoolexec, const char *outfile,
	const char *spooldir, ovis_log_fn_t log)
{
	if (!log || (!spoolexec && !spooldir) || !outfile)  {
		return;
	}
	if (!spoolexec || !outfile || !spooldir || 
		strlen(spoolexec) <2 || strlen(outfile) < 2 ||
		strlen(spooldir) < 2) {
		log(OL_ERROR,"ldms_store_spool called with bogus input\n");
		log(OL_ERROR,"input: %s, %s, %s\n",spoolexec,outfile,
			spooldir);
		return;
	}
	struct stat ebuf;   
	struct stat fbuf;   
	struct stat dbuf;   
	int rc;
	int err=0;
	if ( (rc=stat(spoolexec, &ebuf)) != 0) {
		log(OL_ERROR,"ldms_store_spool: exec %s not usable:%s\n",
			spoolexec, strerror(rc));
		err++;
	}
	if ( (rc=stat(outfile, &fbuf)) != 0) {
		log(OL_ERROR,"ldms_store_spool: data %s no good:%s\n",
			outfile, strerror(rc));
		err++;
	}
	if ( (rc=stat(spooldir, &dbuf)) != 0 || !S_ISDIR(dbuf.st_mode)) {
		log(OL_ERROR,"ldms_store_spool: spool %s no good:%s\n",
			spooldir, strerror(rc));
		err++;
	}
	if (err)
		return;

	pid_t pid;
	if (!(pid = fork())) {
		if (!fork()) {
			execl(spoolexec,spoolexec,outfile, spooldir, NULL);
			exit(0);
		} else {
			/* the first child process exits */
			exit(0);
		}
	} 
	if (pid > 0) {
		log(OL_DEBUG, "%s %s %s\n", spoolexec, outfile, spooldir);
		/* this is the original process */  
		/* wait for the first child to exit which it will immediately */
		waitpid(pid,NULL,0);
	}
	if (pid < 0 ) {
		log(OL_ERROR,"ldms_store_spool: fork failed\n");
	}

}
