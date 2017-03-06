#include "notification.h"
#include "olog.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
/* to run this test,
 * mkfifo /tmp/ovis_notification_test.fifo.txt
 * cat /tmp/ovis_notification_test.fifo.txt 
 * in one window and run the test in the other.
 */


int main()
{
	int rc = 0;
	FILE * etmp = stderr;
	rc = ovis_log_init(__FILE__, "ovis_notification_test.log",
			ol_to_string(OL_DEBUG));
	if (rc) {
		fprintf(etmp, "Failed to open log file: %s.\n", strerror(rc));
		goto err;
	}
	oldebug( "Opened log file.\n");

	printf("opening /tmp/ovis_notification_test.file.txt\n");
	struct ovis_notification *onpfile = ovis_notification_open(
		"/tmp/ovis_notification_test.file.txt",
		3,
		5,
		4,
		(ovis_notification_log_fn)olog,
		0644,
		false);
	if (!onpfile) {
		rc = errno;
		printf("ovis_notification_open file failed: %s\n", strerror(rc));
		goto err;
	}

	printf("opening /tmp/ovis_notification_test.fifo.txt\n");
	struct ovis_notification *onpfifo = ovis_notification_open(
		"/tmp/ovis_notification_test.fifo.txt",
		3,
		5,
		2,
		(ovis_notification_log_fn)olog,
		0644,
		true);
	if (!onpfifo) {
		rc = errno;
		printf("ovis_notification_open fifo failed: %s\n",strerror(rc));
		goto err;
	}

#define ADDTEST(q,msg) \
	rc = ovis_notification_add(q, msg); \
	if (rc) { \
		printf("ovis_notification_add(%s, %s) failed: %s\n", \
			#q, msg, strerror(rc)); \
		goto out; \
	}
	ADDTEST(onpfifo, "OPEN vmstat /tmp/vmstat");
	ADDTEST(onpfifo, "CLOSE vmstat /tmp/vmstat");
	ADDTEST(onpfifo, "APPEND vmstat /tmp/vmstat.1");
	ADDTEST(onpfile, "OPEN meminfo /tmp/meminfo");
	ADDTEST(onpfile, "CLOSE meminfo /tmp/meminfo");
	ADDTEST(onpfile, "APPEND meminfo /tmp/meminfo.1");

	sleep(2);

	ovis_notification_close(onpfifo);

	printf("##############################");
	printf("try using retired reference\n");
	rc = ovis_notification_add(onpfifo, "FAIL expected");
	if (!rc)
		printf("ovis_notification_add on closed queue should fail but did not\n");

	ovis_notification_close(onpfile);

 out:
 err:
	ovis_log_final();
	return rc;
}

