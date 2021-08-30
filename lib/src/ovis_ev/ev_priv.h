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

typedef struct ev__s {
	ev_worker_t e_src;
	ev_worker_t e_dst;
	ev_type_t e_type;
	uint32_t e_refcount;
	int e_posted;
	ev_status_t e_status;
	struct timespec e_to;
	struct rbn e_to_rbn;
	TAILQ_ENTRY(ev__s) e_entry;
	struct ev_s e_ev;
} *ev__t;

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
	struct timespec w_sem_wait;
	sem_t w_sem;
	struct rbn w_rbn;
	pthread_mutex_t w_lock;
	ev_actor_t *w_dispatch;
	size_t w_dispatch_len;
	/* An ordered tree of events with timeouts */
	struct rbt w_event_tree;
	/* A list of events without timeouts */
	TAILQ_HEAD(w_event_list, ev__s) w_event_list;
	int w_ev_list_len;
};

#define EV(_e_) container_of(_e_, struct ev__s, e_ev);
#endif

