#ifndef __EV_PRIV_H_
#define __EV_PRIV_H_

#include <sys/queue.h>
#include <semaphore.h>
#include <inttypes.h>
#include <coll/rbt.h>
#include "ev.h"

struct ev_type_s {
	char *t_name;
	uint64_t t_id;
	struct rbn t_rbn;
	size_t t_size;
};

typedef struct ev_ref_s {
	ev_t r_ev;
	ev_worker_t r_src;
	ev_worker_t r_dst;
	struct timespec r_to;
	struct rbn r_to_rbn;
} *ev_ref_t;

enum evw_state_e {
	EV_WORKER_STOPPED,
	EV_WORKER_RUNNING,
	EV_WORKER_FLUSHING
};

struct ev_worker_s {
	char *w_name;
	ev_actor_t w_actor;
	pthread_t w_thread;
	enum evw_state_e w_state;
	sem_t w_sem;
	struct rbn w_rbn;
	pthread_mutex_t w_event_tree_lock;
	ev_actor_t *w_dispatch;
	size_t w_dispatch_len;
	struct rbt w_event_tree;
};

#endif

