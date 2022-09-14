#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <coll/rbt.h>
#include "ev.h"
#include "ev_priv.h"

static uint32_t next_type_id = 1;

static int type_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}

static pthread_mutex_t type_lock = PTHREAD_MUTEX_INITIALIZER;
static struct rbt type_tree = RBT_INITIALIZER(type_cmp);

ev_type_t ev_type_new(const char *name, size_t size)
{
	ev_type_t evt;
	struct rbn *rbn;

	evt = calloc(1, sizeof(*evt));
	if (!evt)
		goto err_0;
	evt->t_name = strdup(name);
	if (!evt->t_name)
		goto err_1;
	evt->t_size = size;
	evt->t_id = __sync_fetch_and_add(&next_type_id, 1);

	pthread_mutex_lock(&type_lock);
	rbn = rbt_find(&type_tree, name);
	if (rbn)
		goto err_2;
	rbn_init(&evt->t_rbn, evt->t_name);
	rbt_ins(&type_tree, &evt->t_rbn);
	pthread_mutex_unlock(&type_lock);
	return evt;

 err_2:
	pthread_mutex_unlock(&type_lock);
 err_1:
	free(evt->t_name);
 err_0:
	free(evt);
	return NULL;
}

ev_type_t ev_type_get(const char *name)
{
	ev_type_t evt = NULL;
	struct rbn *rbn;

	pthread_mutex_lock(&type_lock);
	rbn = rbt_find(&type_tree, name);
	if (rbn) {
		evt = container_of(rbn, struct ev_type_s, t_rbn);
	} else {
		errno = ENOENT;
	}
	pthread_mutex_unlock(&type_lock);
	return evt;
}

ev_type_t ev_type(ev_t ev)
{
	ev__t e = EV(ev);
	return e->e_type;
}

uint32_t ev_type_id(ev_type_t t)
{
	return t->t_id;
}

const char *ev_type_name(ev_type_t t)
{
	return t->t_name;
}

ev_t ev_new(ev_type_t evt)
{
	ev__t e = malloc(sizeof(*e) + evt->t_size);
	if (!e)
		return NULL;

	e->e_refcount = 1;
	e->e_type = evt;
	e->e_posted = 0;

	return (ev_t)&e->e_ev;
}

void ev_put(ev_t ev)
{
	ev__t e = EV(ev);
	assert(e->e_refcount);
	if (0 == __sync_sub_and_fetch(&e->e_refcount, 1)) {
		free(e);
	}
}

ev_t ev_get(ev_t ev)
{
	ev__t e = EV(ev);
	assert(__sync_fetch_and_add(&e->e_refcount, 1) > 0);
	return ev;
}

int ev_posted(ev_t ev)
{
	ev__t e = EV(ev);
	return e->e_posted;
}

int ev_canceled(ev_t ev)
{
	ev__t e = EV(ev);
	return (e->e_status == EV_FLUSH);
}

int ev_post(ev_worker_t src, ev_worker_t dst, ev_t ev, struct timespec *to)
{
	ev__t e = EV(ev);
	int rc;

	/* If multiple threads attempt to post the same event all but
	 * one will receive EBUSY. If the event is already posted all
	 * will receive EBUSY */
	if (__sync_val_compare_and_swap(&e->e_posted, 0, 1))
		return EBUSY;

	e->e_status = EV_OK;

	if (to) {
		e->e_to = *to;
	} else {
		e->e_to.tv_sec = 0;
		e->e_to.tv_nsec = 0;
	}

	e->e_src = src;
	e->e_dst = dst;
	rbn_init(&e->e_to_rbn, &e->e_to);

	pthread_mutex_lock(&dst->w_lock);
	if (dst->w_state == EV_WORKER_FLUSHING)
		goto err;
	ev_get(&e->e_ev);
	if (to) {
		rbt_ins(&dst->w_event_tree, &e->e_to_rbn);
	} else {
		__sync_fetch_and_add(&dst->w_ev_list_len, 1);
		TAILQ_INSERT_TAIL(&dst->w_event_list, e, e_entry);

	}
	rc = (ev_time_cmp(&e->e_to, &dst->w_sem_wait) <= 0);
	pthread_mutex_unlock(&dst->w_lock);
	if (rc)
		sem_post(&dst->w_sem);

	return 0;
 err:
	e->e_posted = 0;
	pthread_mutex_unlock(&dst->w_lock);
	return EBUSY;
}

int ev_cancel(ev_t ev)
{
	ev__t e = EV(ev);
	int rc = EINVAL;
	struct timespec now;

	if (!e->e_posted && ev_canceled(ev))
		return 0;
	pthread_mutex_lock(&e->e_dst->w_lock);
	/*
	 * Set the status to EV_CANCEL so the actor will see that
	 * status
	 */
	e->e_status = EV_FLUSH;

	/*
	 * If the event is on the list or is soon to expire, do
	 * nothing. This avoids racing with the worker.
	 */
	rc = 0;
	(void)clock_gettime(CLOCK_REALTIME, &now);
	if (ev_time_diff(&e->e_to, &now) < 1.0) {
		rc = EBUSY;
		goto out;
	}

	rbt_del(&e->e_dst->w_event_tree, &e->e_to_rbn);
	__sync_fetch_and_add(&e->e_dst->w_ev_list_len, 1);
	TAILQ_INSERT_TAIL(&e->e_dst->w_event_list, e, e_entry);
 out:
	pthread_mutex_unlock(&e->e_dst->w_lock);
	if (!rc)
		sem_post(&e->e_dst->w_sem);
	return rc;
}

static void __attribute__ ((constructor)) ev_init(void)
{
}

static void __attribute__ ((destructor)) ev_term(void)
{
}
