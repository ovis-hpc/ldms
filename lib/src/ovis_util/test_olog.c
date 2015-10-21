#include "olog.h"
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{

	ovis_log_level_set(OL_CRITICAL);
	olog(OL_INFO, "this is a bogus log level\n");
	ovis_log_level_set(OL_DEBUG);
	olog(23, "this is a bogus log level\n");
	olog(OL_CRITICAL, "olog called before init\n");
	oldebug("test debug\n");
	olinfo("test info\n");
	olwarn("test warning\n");
	olerr("test error\n");
	olcrit("test critical\n");
	oluser("test user\n");

	int rc = ovis_log_init(argv[0], "/root/olog_test.log","debug");
	if (!rc) {
		printf("FAIL: /root init test succeeded. are you root?\n");
	} else {
		printf("PASS: /root init failed as expected.\n");
	}

	ovis_log_final();
	rc = ovis_log_init(argv[0], "./ologtest.log","debug");
	if (!rc) {
		printf("PASS: local init test succeeded.\n");
		printf("See ./ologtest.log\n");
	} else {
		printf("FAIL: local init failed. %d\n",rc);
		printf("Run in a directory with write permission\n");
		exit(1);
	}
	
	ovis_loglevels_t lev;
	for (lev = OL_NONE; lev < OL_ENDLEVEL; lev++) {
		olog(lev, "test level %d %s\n",(int)lev, ol_to_string(lev));
	}
	oldebug("test debug\n");
	olinfo("test info\n");
	olwarn("test warning\n");
	olerr("test error\n");
	olcrit("test critical\n");
	oluser("test user\n");

	const char *names[] = {
	"debug","info","warn","error","critical","user","always","quiet" };
	size_t i = 0;
	for (i = 0; i < sizeof(names)/sizeof(char *); i++) {
		ovis_loglevels_t lev = ol_to_level(names[i]);
		printf("PASS: got level %d(%s) from %s\n",(int)lev,
			ol_to_string(lev), names[i]);
	}

	for (lev = OL_NONE; lev < OL_ENDLEVEL; lev++) {
		int sl = ol_to_syslog(lev);
		printf("PASS: Got syslog %d from level %s\n",
			sl,ol_to_string(lev));
	}
	int eno = 0;
#define TOPENO 256 /* hp uses high values with gaps sometimes. */
	for (eno = 0; eno < TOPENO; eno++) {
		printf("%d: %s\n", eno, ovis_rcname(eno));
	}

	ovis_log_final();
	oluser("FAIL: We should never see this\n");

	return 0;
}
