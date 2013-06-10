#ifndef __TEST_H__
#define  __TEST_H__

#include <stdio.h>
#include <stdarg.h>

static inline void TEST_ASSERT(int cond, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (cond)
		printf("[0m[38;5;34mPASS[0m -- ");
	else
		printf("[48;5;232;38;5;9mFAIL[0m -- ");
	vprintf(fmt, ap);
	va_end(ap);
}

#endif
