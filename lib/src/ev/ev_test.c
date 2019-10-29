#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "ev.h"
#include "ev_priv.h"

ev_type_t to_type;

struct data_s {
	int v;
};

pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;

static int timer_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t e)
{
	struct timespec to;
	ev_sched_to(&to, 0, 0);

	pthread_mutex_lock(&io_lock);
	printf("%s: type=%s, id=%d\n", __func__, ev_type_name(ev_type(e)), ev_type_id(ev_type(e)));
	printf("    status  : %s\n", status ? "FLUSH" : "OK");
	printf("    src     : %s\n", src->w_name);
	printf("    dst     : %s\n", dst->w_name);
	printf("    tv_sec  : %ld\n", to.tv_sec);
	printf("    tv_nsec : %ld\n", to.tv_nsec);
	printf("    v       : %d\n", EV_DATA(e, struct data_s)->v);
	pthread_mutex_unlock(&io_lock);

	EV_DATA(e, struct data_s)->v += 1;

	to.tv_sec += 5;
	to.tv_nsec = 0;
	ev_post(src, dst, e, &to);

	return 0;
}

int main(int argc, char *argv[])
{
	struct timespec to;
	ev_worker_t timer, a, b;
	ev_type_t timeout, request, response;
	ev_t to_ev, req_ev, resp_ev;

	timer = ev_worker_new("TIMER", timer_actor);
	a = ev_worker_new("A", timer_actor);
	b = ev_worker_new("B", timer_actor);

	timeout = ev_type_new("timeout", sizeof(struct data_s));
	request = ev_type_new("request", sizeof(struct data_s));
	response = ev_type_new("response", sizeof(struct data_s));

	to_ev = ev_new(timeout);
	EV_DATA(to_ev, struct data_s)->v = 10000;

	req_ev = ev_new(request);
	EV_DATA(req_ev, struct data_s)->v = 0;

	resp_ev = ev_new(response);
	EV_DATA(resp_ev, struct data_s)->v = 1000;

	ev_sched_to(&to, 0, 0);
	ev_post(a, b, req_ev, &to);
	ev_sched_to(&to, 1, 0);
	ev_post(b, a, resp_ev, &to);
	ev_sched_to(&to, 2, 0);
	ev_post(timer, timer, to_ev, &to);

	sleep(30);
	ev_flush(timer);
	ev_flush(a);
	ev_flush(b);
	sleep(5);
	return 0;
}
