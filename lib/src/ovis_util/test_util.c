#include "util.h"
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv)
{

	char * s1 = "narate";
	char * s2 = "nichamon";
	char * s3 = "tom";
	char buf[256] = { 0};
	char shortbuf[14];
	char * r;
	const char * t;
	const char * t2;
	t = "narate/nichamon/tom";
	t2 = "naratenichamontom";
	int errcnt = 0;

	/* success cases */
	r = ovis_join(NULL,s1,s2,s3,NULL);
	if (!r || strcmp(t,r)) {
		printf("error 1: ovis_join(NULL,s1,s2,s3,NULL)\n");
		errcnt++;
	}
	free(r);

	r = ovis_join("",s1,s2,s3,NULL);
	if (!r || strcmp(t2,r)) {
		printf("error 2: ovis_join(\"\",s1,s2,s3,NULL)\n");
		errcnt++;
	}
	free(r);

	int rc = ovis_join_buf(buf, sizeof(buf), NULL, s1,s2,s3, NULL);
	if (rc || strcmp(t,buf)) {
		printf("error 3: ovis_join_buf(b,s,NULL,s1,s2,s3,NULL)\n");
		printf("error 3: %s\n",buf);
		errcnt++;
	}

	/* odd cases */

	r = ovis_join(NULL,NULL); // empty list
	if (r)  {
		printf("error 4: ovis_join(NULL,NULL) returned something.\n");
		errcnt++;
	}

	rc = ovis_join_buf(NULL,0,NULL); // empty list
	if (!rc)  {
		printf("error 5: ovis_join_buf(NULL,0,NULL) returned something.\n");
		errcnt++;
	}

	shortbuf[0] = '\0'; // insufficient buf
	rc = ovis_join_buf(shortbuf, sizeof(shortbuf), NULL,s1,s2,s3,NULL);
	if (rc == 0 || strcmp(t,shortbuf) == 0) {
		printf("error 6: ovis_join_buf(sb,ss,NULL,s1,s2,s3,NULL) %d %s\n",
			rc, strerror(rc));
		errcnt++;
	}

	printf("expect crash soon\n");
	r = ovis_join(NULL,s1,s2,s3); // unterminated list
	if (r) {
		printf("error 7: ovis_join(NULL,s1,s2,s3) returned something\n");
		printf("error 7: %s\n", r);
		errcnt++;
	}
	free(r);

	rc = ovis_join_buf(NULL,0,s1,s2,s3); // unterminated list
	if (!rc)  {
		printf("error 8: ovis_join_buf(NULL,0,s1,s2,s3) returned something.\n");
		errcnt++;
	}

	if (errcnt)
		return 1;
	return 0;
}
