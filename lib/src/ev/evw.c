#define _GNU_SOURCE
#include <linux/param.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>
#include <coll/rbt.h>
#include "ev.h"
#include "ev_priv.h"

static pthread_mutex_t worker_lock = PTHREAD_MUTEX_INITIALIZER;

static int type_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}
static struct rbt worker_tree = RBT_INITIALIZER(type_cmp);

int ev_time_cmp(struct timespec *tsa, const struct timespec *tsb)
{
	if (tsa->tv_sec < tsb->tv_sec)
		return -1;
	if (tsa->tv_sec > tsb->tv_sec)
		return 1;
	if (tsa->tv_nsec < tsb->tv_nsec)
		return -1;
	if (tsa->tv_nsec < tsb->tv_nsec)
		return 1;
	return 0;

}

/*
 * Process all of the events in the worker's event tree that have a
 * timeout before or at the current time.
 *
 * Return the 1st event that has a timeout > now
 */
static ev_ref_t process_worker_events(ev_worker_t w)
{
	ev_ref_t r;
	struct rbn *rbn;
	struct timespec now;
	ev_actor_t actor;
	int rc = clock_gettime(CLOCK_REALTIME, &now);

 next:
	r = NULL;
	rbn = rbt_min(&w->w_event_tree);
	if (!rbn)
		goto out;

	r = container_of(rbn, struct ev_ref_s, r_to_rbn);
	if (ev_time_cmp(&r->r_to, &now) > 0)
		goto out;

	rbt_del(&w->w_event_tree, &r->r_to_rbn);
	r->r_ev->e_posted = 0;
	pthread_mutex_unlock(&w->w_event_tree_lock);
	actor = NULL;
	if (r->r_ev->e_type->t_id < w->w_dispatch_len)
		actor = w->w_dispatch[r->r_ev->e_type->t_id];
	if (!actor)
		actor = w->w_actor;
	actor(r->r_src, r->r_dst, r->r_ev);
	ev_put(r->r_ev);
	free(r);
	pthread_mutex_lock(&w->w_event_tree_lock);
	goto next;

 out:
	return r;
}

void ev_sched_to(struct timespec *to, time_t secs, int nsecs)
{
	int rc = clock_gettime(CLOCK_REALTIME, to);
	to->tv_sec += secs;
	to->tv_nsec += nsecs;
}

static void *worker_proc(void *arg)
{
	int res;
	ev_ref_t r;
	ev_worker_t w = arg;
	struct timespec wait;
	w->w_state = EV_WORKER_RUNNING;
	ev_sched_to(&wait, 0, 0);
	res = sem_timedwait(&w->w_sem, &wait);
	while (1) {
		pthread_mutex_lock(&w->w_event_tree_lock);
		r = process_worker_events(w);
		if (r) {
			wait = r->r_to;
		} else {
			ev_sched_to(&wait, 10, 0);
		}
		pthread_mutex_unlock(&w->w_event_tree_lock);
		res = sem_timedwait(&w->w_sem, &wait);
	}
	return NULL;
}

void ev_flush(ev_worker_t w, ev_actor_t actor)
{
	ev_ref_t r;
	struct rbn *rbn;

	pthread_mutex_lock(&w->w_event_tree_lock);
	w->w_state = EV_WORKER_FLUSHING;
	while (NULL != (rbn = rbt_min(&w->w_event_tree))) {
		r = container_of(rbn, struct ev_ref_s, r_to_rbn);
		rbt_del(&w->w_event_tree, &r->r_to_rbn);
		actor(r->r_src, r->r_dst, r->r_ev);
		ev_put(r->r_ev);
		free(r);
	}
	w->w_state = EV_WORKER_STOPPED;
	pthread_mutex_unlock(&w->w_event_tree_lock);
}

ev_worker_t ev_worker_new(const char *name, ev_actor_t actor_fn)
{
	int err = ENOMEM;
	ev_worker_t w = calloc(1, sizeof(*w));
	struct rbn *rbn;

	w = calloc(1, sizeof(*w));
	if (!w)
		goto err_0;
	w->w_name = strdup(name);
	if (!w->w_name)
		goto err_1;
	w->w_actor = actor_fn;
	err = sem_init(&w->w_sem, 0, 0);
	if (err)
		goto err_2;

	w->w_state = EV_WORKER_STOPPED;
	pthread_mutex_init(&w->w_event_tree_lock, NULL);
	rbt_init(&w->w_event_tree, (int (*)(void *, const void*))ev_time_cmp);

	pthread_mutex_lock(&worker_lock);
	err = EEXIST;
	rbn = rbt_find(&worker_tree, name);
	if (rbn)
		goto err_2;
	rbn_init(&w->w_rbn, w->w_name);
	rbt_ins(&worker_tree, &w->w_rbn);
	pthread_mutex_unlock(&worker_lock);

	err = pthread_create(&w->w_thread, NULL, worker_proc, w);
	if (err)
		goto err_2;
	size_t namelen = strlen(w->w_name);
	size_t nameoff = 0;
	if (namelen > 15)
		/* Use the last 16 chars of the worker name */
		nameoff = namelen - 15;
	pthread_setname_np(w->w_thread, &w->w_name[nameoff]);
	errno = 0;
	return w;
 err_2:
	pthread_mutex_unlock(&worker_lock);
 err_1:
	free(w->w_name);
 err_0:
	free(w);
	errno = err;
	return NULL;
}

ev_worker_t ev_worker_get(const char *name)
{
	ev_worker_t w = NULL;
	struct rbn *rbn;

	pthread_mutex_lock(&worker_lock);
	rbn = rbt_find(&worker_tree, name);
	if (rbn) {
		w = container_of(rbn, struct ev_worker_s, w_rbn);
	} else {
		errno = ENOENT;
	}
	pthread_mutex_unlock(&worker_lock);
	return w;
}

const char *ev_worker_name(ev_worker_t w)
{
	return w->w_name;
}

/**
 * \brief Dispatch an event type to an actor
 *
 * Specify a particular actor for an event type. Events without an
 * actor are routed to the default actor specified when the worker was
 * created.
 *
 * \param w Worker handle
 * \param t The event tyep
 * \param fn The actor function
 *
 * \retval 0 success
 */
#define EV_DISPATCH_TBL_SIZE 100
int ev_dispatch(ev_worker_t w, ev_type_t t, ev_actor_t fn)
{
	size_t size;
	if (!w->w_dispatch) {
		if (t->t_id < EV_DISPATCH_TBL_SIZE)
			size = EV_DISPATCH_TBL_SIZE;
		else
			size = t->t_id << 1;
		w->w_dispatch = calloc(size, sizeof(ev_actor_t));
		if (!w->w_dispatch)
			return ENOMEM;
		w->w_dispatch_len = size;
	} else {
		if (t->t_id >= w->w_dispatch_len) {
			int i, new_len;
			new_len = t->t_id << 1;
			w->w_dispatch = realloc(w->w_dispatch, new_len * sizeof(ev_actor_t));
			if (!w->w_dispatch)
				return ENOMEM;
			for (i = w->w_dispatch_len; i < new_len; i++)
				w->w_dispatch[i] = NULL;
			w->w_dispatch_len = new_len;
		}
	}
	w->w_dispatch[t->t_id] = fn;
	return 0;
}
