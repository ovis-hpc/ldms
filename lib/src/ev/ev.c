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
	ev_type_t evt = calloc(1, sizeof(*evt));
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
	return ev->e_type;
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
	ev_t ev = malloc(sizeof(*ev) + evt->t_size);
	if (!ev)
		goto out;
	ev->e_refcount = 1;
	ev->e_type = evt;
	ev->e_posted = 0;
 out:
	return ev;
}

void ev_put(ev_t ev)
{
	assert(ev->e_refcount);
	if (0 == __sync_sub_and_fetch(&ev->e_refcount, 1)) {
		free(ev);
	}
}

ev_t ev_get(ev_t ev)
{
	assert(__sync_fetch_and_add(&ev->e_refcount, 1) > 0);
	return ev;
}

int ev_posted(ev_t ev)
{
	return ev->e_posted;
}

void ev_post(ev_worker_t src, ev_worker_t dst, ev_t ev, struct timespec *to)
{
	int rc;
	struct timespec now;
	ev_ref_t r;

	if (ev->e_posted)
		return;

	r = malloc(sizeof *r);
	if (to) {
		r->r_to = *to;
	} else {
		r->r_to.tv_sec = 0;
		r->r_to.tv_nsec = 0;
	}
	r->r_ev = ev_get(ev);
	r->r_src = src;
	r->r_dst = dst;
	rbn_init(&r->r_to_rbn, &r->r_to);
	pthread_mutex_lock(&dst->w_event_tree_lock);
	rbt_ins(&dst->w_event_tree, &r->r_to_rbn);
	r->r_ev->e_posted = 1;
	pthread_mutex_unlock(&dst->w_event_tree_lock);

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (ev_time_cmp(&r->r_to, &now) <= 0)
		sem_post(&dst->w_sem);
}

static void __attribute__ ((constructor)) ev_init(void)
{
}

static void __attribute__ ((destructor)) ev_term(void)
{
}
