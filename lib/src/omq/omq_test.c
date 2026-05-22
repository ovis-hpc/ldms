#include <stdio.h>
#include <pthread.h>
#include "omq.h"

int store(omq_t q, omq_msg_t msg)
{
	omq_msg_free(msg);
	return 0;
}

int main(int argc, char *argv[])
{
	int cnt;
	omq_t q = omq_new("test", NULL, NULL, store);
	omq_msg_t msg;

	for (cnt = 0; cnt < 1024 * 1024; cnt++) {
		msg = omq_msg_alloc(q, 128, NULL);
		omq_msg_publish(msg);
	}
	return 0;
}
