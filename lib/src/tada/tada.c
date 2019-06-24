
#include <netdb.h>
#include <assert.h>
#include <string.h>
#include "tada.h"

/*
 * { "msg-type" : "test-start",
 *   "test-suite" : <suite-name>,
 *   "test-type" : <test-type>,
 *   "test-name" : <test-name>,
 *   "timestamp" : <timestamp>
 * }
 */
void tada_start(test_t test)
{
	size_t cnt;
	char msg_buf[1024];
	char *tada_addr = getenv("TADA_ADDR");
	char *tada_host;
	short tada_port;
	struct hostent *h;

	if (!tada_addr) {
		tada_host = TADAD_HOST;
		tada_port = htons(TADAD_PORT);
	} else {
		char *s = strdup(tada_addr);
		tada_host = strtok(s, ":");
		tada_port = htons(atoi(strtok(NULL, ":")));
	}
	h = gethostbyname(tada_host);
	assert(h);
	assert (h->h_addrtype == AF_INET);

	test->sin.sin_addr.s_addr = *(unsigned int *)(h->h_addr_list[0]);
	test->sin.sin_family = h->h_addrtype;
	test->sin.sin_port = tada_port;

	cnt = snprintf(msg_buf, sizeof(msg_buf),
		       "{ \"msg-type\" : \"test-start\","
		       "\"test-suite\" : \"%s\","
		       "\"test-type\" : \"%s\","
		       "\"test-name\" : \"%s\","
		       "\"timestamp\" : %d"
		       "}",
		       test->suite_name,
		       test->test_type,
		       test->test_name,
		       time(NULL)
		       );
	assert(cnt < sizeof(msg_buf));
	test->udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
	assert(test->udp_fd >= 0);
	cnt = sendto(test->udp_fd, msg_buf, cnt, 0, (struct sockaddr *)&test->sin, sizeof(test->sin));
}

void tada_finish(test_t test)
{
	size_t cnt;
	char msg_buf[1024];
	struct hostent *h;
	int assert_no;

	/*
	 * Go through the list of all test assertions and send status
	 * for un-tested assertions.
	 */
	for (assert_no = 0; test->test_asserts[assert_no].test; assert_no++) {
		if (test->test_asserts[assert_no].result != TEST_SKIPPED)
			continue;
		cnt = snprintf(msg_buf, sizeof(msg_buf),
			       "{ \"msg-type\" : \"assert-status\","
			       "\"test-suite\" : \"%s\","
			       "\"test-type\" : \"%s\","
			       "\"test-name\" : \"%s\","
			       "\"assert-no\" : %d,"
			       "\"assert-desc\" : \"%s\","
			       "\"assert-cond\" : \"none\","
			       "\"test-status\" : \"skipped\""
			       "}",
			       test->suite_name,
			       test->test_type,
			       test->test_name,
			       assert_no,
			       test->test_asserts[assert_no].description
			       );
		assert(cnt < sizeof(msg_buf));
		cnt = sendto(test->udp_fd, msg_buf, cnt,
			     0, (struct sockaddr *)&test->sin, sizeof(test->sin));
	}

	cnt = snprintf(msg_buf, sizeof(msg_buf),
		       "{ \"msg-type\" : \"test-finish\","
		       "\"test-suite\" : \"%s\","
		       "\"test-type\" : \"%s\","
		       "\"test-name\" : \"%s\","
		       "\"timestamp\" : %d"
		       "}",
		       test->suite_name,
		       test->test_type,
		       test->test_name,
		       time(NULL)
		       );
	assert(cnt < sizeof(msg_buf));
	cnt = sendto(test->udp_fd, msg_buf, cnt, 0, (struct sockaddr *)&test->sin, sizeof(test->sin));
	close(test->udp_fd);
	test->udp_fd = -1;
}

int tada_assert(test_t test, int assert_no, int cond, const char *cond_str)
{
	size_t cnt;
	char msg_buf[1024];
	char esc_str[512];
	struct hostent *h;
	const char *s;
	char *e;

	/* Escape any embedded '"' in the cond_str */
	for (s = cond_str, e = esc_str; *s != '\0'; s++) {
		if (*s == '"') {
			*e++ = '\\';
			*e++ = '"';
		} else {
			*e++ = *s;
		}
	}
	*e = '\0';

	assert(test->test_asserts[assert_no].test);
	if (cond)
		test->test_asserts[assert_no].result = TEST_PASSED;
	else
		test->test_asserts[assert_no].result = TEST_FAILED;
	cnt = snprintf(msg_buf, sizeof(msg_buf),
		       "{ \"msg-type\" : \"assert-status\","
		       "\"test-suite\" : \"%s\","
		       "\"test-type\" : \"%s\","
		       "\"test-name\" : \"%s\","
		       "\"assert-no\" : %d,"
		       "\"assert-desc\" : \"%s\","
		       "\"assert-cond\" : \"%s\","
		       "\"test-status\" : \"%s\""
		       "}",
		       test->suite_name,
		       test->test_type,
		       test->test_name,
		       assert_no,
		       test->test_asserts[assert_no].description,
		       esc_str,
		       test->test_asserts[assert_no].result == TEST_PASSED ? "passed" : "failed"
		       );
	assert(cnt < sizeof(msg_buf));
	cnt = sendto(test->udp_fd, msg_buf, cnt,
		     0, (struct sockaddr *)&test->sin, sizeof(test->sin));
	return cond;
}
