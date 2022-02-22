#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

void __test_str_repl_cmd(const char *s)
{
	char *out;
	printf("  s_in: %s\n", s);
	out = str_repl_cmd(s);
	printf("  s_out: %s\n", out);
	free(out);
}

void test_str_repl_cmd()
{
	printf("--- test_str_repl_cmd ---\n");
	__test_str_repl_cmd("verb opt=$(hostname)");
	__test_str_repl_cmd("verb opt=$(hostname) opt2=$(hostname)");
	__test_str_repl_cmd("verb opt=$(echo $PPID) opt2=$(hostname)");
	__test_str_repl_cmd("verb opt=$(hostname)/bal haha");
	__test_str_repl_cmd("verb opt=$(sleep 3) opt2=$(hostname)");
}

void test_av_tilde()
{
	struct attr_value_list *av_list, *kw_list;
	av_list = av_new(7);
	kw_list = av_new(7);
	char *s = strdup("a=b c=~{a} d=~{c}q f=g~{a} a1=~{a2} a2=~{a1} k=~{m}");
	if (!s) {
		printf("test_av_tilde: strdup fail\n");
		return;
	}
	int rc = tokenize(s, kw_list, av_list);
	if (rc) {
		printf("test_av_tilde: failed tokenize\n");
		return;
	}
	if (strcmp(av_value(av_list, "a"), "b")) {
		printf("test_av_tilde: list[a] != 'b'\n");
	}
	if (strcmp(av_value(av_list, "c"), "b")) {
		printf("test_av_tilde: list[c] != 'b'\n");
	}
	if (strcmp(av_value(av_list, "d"), "bq")) {
		printf("test_av_tilde: list[d] != 'bq'\n");
	}
	if (strcmp(av_value(av_list, "f"), "gb")) {
		printf("test_av_tilde: list[f] != 'gb'\n");
	}
	char *empty = av_value(av_list, "k");
	if (empty) {
		if (strcmp(empty, ""))
			printf("test_av_tilde: list[k] != ''\n");
	} else {
			printf("test_av_tilde: list[k] returned NULL\n");
	}
	char *badslt = av_value(av_list, "a1");
	if (badslt) {
		printf("test_av_tilde: recursion ->'%s'\n", badslt);
		if (strcmp(badslt, "")) {
			printf("test_av_tilde: list[a1] != ''\n");
		}
	} else {
		printf("test_av_tilde: recursion found: errno:%s\n",strerror(errno));
	}
	free(s);
	av_free(av_list);
	av_free(kw_list);
}

void test_av()
{
	struct attr_value_list *av_list, *kw_list, *cp;
	av_list = av_new(4);
	kw_list = av_new(4);
	char *s = strdup("a=b c=d e f=ga");
	if (!s) {
		printf("strdup fail\n");
		return;
	}
	int rc = tokenize(s, kw_list, av_list);
	if (rc) {
		printf("failed tokenize\n");
		return;
	}
	cp = av_copy(av_list);
	if (!cp) {
		printf("av copy failed\n");
		return;
	}
	char *p1 = av_to_string(cp, 0);
	char *p2 = av_to_string(av_list, 0);
	if (strcmp(p1,p2)) {
		printf("copy: %s\norig: %s\nnot the same.", p1, p2);
	} else {
		printf("copy succeeded\n");
	}
	free(s);
	free(p1);
	free(p2);
	av_free(cp);
	av_free(av_list);
	av_free(kw_list);
}

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

	test_str_repl_cmd();

	test_av();

	test_av_tilde();

	/* odd cases */

	r = ovis_join(NULL,NULL); // empty list
	if (r)  {
		printf("error 4: ovis_join(NULL,NULL) returned something.\n");
		errcnt++;
	}

	rc = ovis_join_buf(NULL,0,NULL,NULL); // empty list
	if (!rc)  {
		printf("error 5: ovis_join_buf(NULL,0,NULL) returned something.\n");
		errcnt++;
	}

	shortbuf[0] = '\0'; // insufficient buf
	rc = ovis_join_buf(shortbuf, sizeof(shortbuf), NULL,s1,s2,s3,NULL);
	if (rc == 0 || strcmp(t,shortbuf) == 0) {
		printf("error 6: ovis_join_buf(sb,ss,NULL,s1,s2,s3,NULL) %d %s\n",
			rc, STRERROR(rc));
		errcnt++;
	}
	snprintf(shortbuf,12,"0123456789"); // insufficient buf
	rc = ovis_join_buf(shortbuf, sizeof(shortbuf), NULL,s1,s2,s3,NULL);
	if (rc == 0 || strcmp(t,shortbuf) == 0) {
		printf("error 7: ovis_join_buf(sb,ss,NULL,s1,s2,s3,NULL) %d %s\n",
			rc, STRERROR(rc));
		errcnt++;
	}
	/* do not test for unterminated list. compiler warning from attribute sentinel
 	* warns the user about those. */

	if (errcnt)
		return 1;
	return 0;
}
