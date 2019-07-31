#ifndef _TADA_H_
#define _TADA_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>

typedef enum test_result {
	TEST_PASSED = 0,
	TEST_FAILED,
	TEST_SKIPPED
} test_result_t;

typedef struct test_assertion_s {
	test_result_t result;
	const char *description;
	struct test_s *test;
} *test_assertion_t;

typedef struct test_s {
	const char *suite_name;
	const char *test_name;
	const char *test_type;
	const char *test_user;
	const char *commit_id;
	const char *test_desc;
	char test_id[65];
	struct sockaddr_in sin;
	int udp_fd;
	struct test_assertion_s test_asserts[];
} *test_t;

#define TADA_TRUE	(1 == 1)
#define TADA_FALSE	(1 == 0)
#define TADAD_HOST	"tadad-host"
#define TADAD_PORT	9862

#define TEST_BEGIN(_suite_name, _test_name, _test_type, \
		   _test_user, _commit_id, _test_desc, c_name) \
	struct test_s c_name = {			    \
		.suite_name = _suite_name,		    \
		.test_name = _test_name,		    \
		.test_type = _test_type,		    \
		.test_user = _test_user,		    \
		.test_desc = _test_desc,		    \
		.commit_id = _commit_id,		    \
		.test_asserts = {			    \


#define TEST_ASSERTION(c_test_name, assert_no, _desc)			\
	[assert_no] = {							\
		.result = TEST_SKIPPED,					\
		.description = _desc,					\
		.test = &c_test_name,					\
	},

#define TEST_END(_c_name) {0}}}

extern void tada_start(test_t test);
extern void tada_finish(test_t test);
extern int tada_assert(test_t test, int assert_no, int cond, const char *cond_str);
#define TEST_START(c_name) tada_start(&c_name)
#define TEST_FINISH(c_name) tada_finish(&c_name)
#define TEST_ASSERT(_t_, _no_, _cond_) tada_assert(&_t_, _no_, _cond_, #_cond_)

#endif
