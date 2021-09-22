#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <coll/rbt.h>
#include "ev.h"
#include "ev_priv.h"

#ifdef _EV_TRACK_
static int ev_cmp(void *a, const void *b)
{
	return (uint64_t)a - (uint64_t)b;
}
static pthread_mutex_t ev_tree_lock = PTHREAD_MUTEX_INITIALIZER;
static struct rbt ev_tree = RBT_INITIALIZER(ev_cmp);

static void __print_leading(FILE *f, int leading)
{
	fprintf(f, "%*s", leading, "");
}
static void __ev_dump_detail(struct ev__s *e, FILE *f, int leading)
{
	__print_leading(f, leading);
	fprintf(f, "ID: %" PRIu64 "\n", e->id);
	__print_leading(f, leading);
	fprintf(f, "type: %s\n", e->e_type->t_name);
	__print_leading(f, leading);
	fprintf(f, "new: %s():%d\n", e->new_func, e->new_line);
	__print_leading(f, leading);
	fprintf(f, "del: ");
	if (e->del_func)
		fprintf(f, "%s():%d\n", e->del_func, e->del_line);
	else
		fprintf(f, "N/A\n");
	__print_leading(f, leading);
	fprintf(f, "status: %s\n", (e->e_status == EV_OK)?"OK":"FLUSH");
	__print_leading(f, leading);
	fprintf(f, "src -> dst: ");
	if (e->e_dst) {
		fprintf(f, "%s [%s():%d] -> %s -- %s\n",
				(e->e_src)?(e->e_src->w_name):"NA",
				e->post_func, e->post_line,
				e->e_dst->w_name,
				(e->e_posted)?"NOT DELIVERED":"DELIVERED");
	} else {
		fprintf(f, "not posted\n");
	}
}

static void __ev_dump_single(struct ev__s *e, FILE *f, int leading)
{
	struct rbn *rbn;
	__print_leading(f, leading);
	fprintf(f, "%" PRIu64 " \"%s\" [%s():%d] -- %s ",
					e->id, e->e_type->t_name,
					e->new_func, e->new_line,
					(e->e_status == EV_OK)?"OK":"FLUSH");

	rbn = rbt_find(&ev_tree, (void *)e->id);
	if (!rbn)
		fprintf(f, " --- not in ev_tree.");

	if (e->e_dst) {
		fprintf(f, "-- %s [%s():%d] -> %s -- %s ",
				(e->e_src)?(e->e_src->w_name):"NA",
				e->post_func, e->post_line,
				e->e_dst->w_name,
				(e->e_posted)?"NOT DELIVERED":"DELIVERED");
	} else {
		fprintf(f, "-- not posted ");
	}
}

static void __ev_dump(struct ev__s *e, FILE *f, int is_print_parent, int leading, int detail)
{
	if (detail)
		__ev_dump_detail(e, f, leading);
	else
		__ev_dump_single(e, f, leading);

	if (is_print_parent) {
		fprintf(f, "-- parent: ");
		if (e->parent_id) {
			fprintf(f, "%" PRIu64 " ", e->parent_id);
		} else {
			fprintf(f, "NONE ");
		}
	}
	fprintf(f, "\n");
}
#endif /* _EV_TRACK_ */

__attribute__((unused))
static void ev_dump(ev_t ev, uint64_t id, FILE *f)
{
#ifdef _EV_TRACK_
	struct rbn *rbn;
	struct ev__s *e;
	if (!ev) {
		rbn = rbt_find(&ev_tree, (void *)id);
		if (!rbn) {
			fprintf(f, "ev %" PRIu64 " not found.\n", id);
			return;
		}
		e = container_of(rbn, struct ev__s, rbn);
	} else {
		e = EV(ev);
	}
	__ev_dump(e, f, 1, 1, 1);
#else /* _EV_TRACK_ */
	fprintf(stderr, "Not supported. Please compile with -D_EV_TRACK_");
#endif /* _EV_TRACK_ */
	fflush(f);
}

__attribute__((unused))
static void __ev_tree_dump(FILE *f)
{
#ifdef _EV_TRACK_
	struct ev__s *e;
	struct rbn *rbn;
	char *s;

	fprintf(f, "%5s %16s %3s %20s %7s %32s %s\n",
			"ID", "type", "ref",
			"new_func", "newline",
			"src->dst", "status");
	pthread_mutex_lock(&ev_tree_lock);
	rbn = rbt_min(&ev_tree);
	while (rbn) {
		e = container_of(rbn, struct ev__s, rbn);
		fprintf(f, "%5" PRIu64 " %16s %3d %20s %7d ",
					e->id, e->e_type->t_name,
					e->e_refcount,
					e->new_func, e->new_line);
		(void) asprintf(&s, "(%s -> %s)",
				(e->e_src)?(e->e_src->w_name):"",
				(e->e_dst)?(e->e_dst->w_name):"");
		fprintf(f, "%32s ", s);
		free(s);
		fprintf(f, "%s", (e->e_status == EV_OK)?"OK":"FLUSH");
		fprintf(f, "\n");
		rbn = rbn_succ(rbn);
	}
	pthread_mutex_unlock(&ev_tree_lock);
#else /* _EV_TRACK_ */
	fprintf(stderr, "Not supported. Please compile with -D_EV_TRACK_\n");
#endif /* _EV_TRACK_ */
	fflush(f);
}


void ev_tree_dump()
{
	__ev_tree_dump(stdout);
}

void free_ev_tree_dump()
{
	__ev_tree_dump(stdout);
}

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

ev_t _ev_new(ev_type_t evt, const char *func, int line)
{
	ev__t e = calloc(1, sizeof(*e) + evt->t_size);
	if (!e)
		return NULL;

	e->e_refcount = 1;
	e->e_type = evt;
	e->e_posted = 0;

#ifdef _EV_TRACK_
	static uint64_t id = 1;
	e->id = id++;
	e->new_func = func;
	e->new_line = line;
	rbn_init(&e->rbn, (void *)e->id);
	pthread_mutex_init(&e->lock, NULL);
	pthread_mutex_lock(&ev_tree_lock);
	rbt_ins(&ev_tree, &e->rbn);
	pthread_mutex_unlock(&ev_tree_lock);
	e->e_src = e->e_dst = NULL;
	e->e_status = EV_OK;

	extern ev_worker_t worker_find_by_thr(pthread_t thread);
	pthread_t thr = pthread_self();
	struct ev_worker_s *src = worker_find_by_thr(thr);
	if (!src) {
		e->parent_id = 0;
	} else {
		if (src->cur_ev) {
			e->parent_id = src->cur_ev->id;
		}
	}

#endif /* _EV_TRACK_ */
	return (ev_t)&e->e_ev;
}

void _ev_put(ev_t ev, const char *func, int line)
{
	ev__t e = EV(ev);
	assert(e->e_refcount);
	if (0 == __sync_sub_and_fetch(&e->e_refcount, 1)) {
#ifdef _EV_TRACK_
		e->del_func = func;
		e->del_line = line;
		pthread_mutex_lock(&ev_tree_lock);
		rbt_del(&ev_tree, &e->rbn);
		pthread_mutex_unlock(&ev_tree_lock);
#endif /* _EV_TRACK_ */
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

#ifdef _EV_TRACK_
__attribute__((unused))
static const char *ev_post_func(ev_t ev)
{
	ev__t e = EV(ev);
	return e->post_func;
}

__attribute__((unused))
static int ev_post_line(ev_t ev)
{
	ev__t e = EV(ev);
	return e->post_line;
}

#endif /* _EV_TRACK_ */

int _ev_post(ev_worker_t src, ev_worker_t dst, ev_t ev, struct timespec *to,
					const char *func, int line)
{
	ev__t e = EV(ev);
	int rc;
#ifdef _EV_TRACK_
	e->post_func = func;
	e->post_line = line;
#endif /* _EV_TRACK_ */

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

#ifdef _EV_TRACK_
inline struct ev__s *ev__s_get(ev_t e) {
	return EV(e);
}
#endif /* _EV_TRACK_ */

static void __attribute__ ((constructor)) ev_init(void)
{
}

static void __attribute__ ((destructor)) ev_term(void)
{
}
