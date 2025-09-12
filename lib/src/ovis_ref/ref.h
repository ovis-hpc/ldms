#ifndef _REF_H_
#define _REF_H_
#include <sys/queue.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

/*
 * This header implements a fast reference counter.
 *
 * ref_put() and ref_get() are generally thread safe, however the reference
 * counter is NOT not race-to-zero safe. Users of this reference counter MUST
 * implement their own race-to-zero protection strategy.
 *
 * Leaving race-to-zero protection to the caller permits this reference counter
 * to be as low over head as possible in general operation.
 *
 * The race-to-zero problem is avoided by ensuring that the a ref_get() is
 * not possible when the final ref_put() is issued.
 *
 * Some general approaches to implement race-to-zero safety are:
 *
 * 1) Only call ref_get() when the caller knows a reference is still held
 *
 *    For instance, a holder of a reference may call ref_get() on behalf
 *    of another component and logically "pass" that reference's ownership
 *    onto the other component. This hand off must be well documented.
 *
 *    Alternatively, a function might declare that its callers must already
 *    hold a reference on a counter on which it plans to call ref_get()
 *    itself. This contract must be well documented.
 *
 * 2) Implement a locking scheme under which the counter is tested to
 *    see if acquiring a reference is currently permitted. Then and only
 *    then may we ref_get() while still under the same lock.
 *
 *    For instance, in psuedo code the process looks like:
 *
 *       mutex_lock(lock)
 *       if (reference_is_acquirable(...)) {
 *          ref_get(ref)
 *       }
 *       mutex_unlock(lock)
 *
 *    In the LDMS code, a common approach is to store the object holding
 *    the reference counter in a collection (list, tree, etc.) that is
 *    protected by a mutex. The entity that placed the object in the collection
 *    is contractually bound to:
 *       A) hold a reference at the time it inserts the object into
 *          the collection
 *       B) retain that the reference until it removes the object
 *          from the collection.
 *    By doing so, any non-reference holder is safe to call get_ref() on
 *    an object that it finds in the collection _while holding the lock_
 *    on the collection.
 *
 *    For example, in psuedo-code:
 *
 *      X's code (already holds a reference on "obj"):
 *
 *        pthread_mutex_lock(foo_tree_lock);
 *        tree_insert(foo_tree, obj);
 *        pthread_mutex_unlock(foo_tree_lock);
 *
 *      Y's code:
 *
 *        pthread_mutex_lock(foo_tree_lock);
 *        # our aquirability test is to check for the object's presence
 *        # in the tree
 *        obj = tree_lookup(foo_tree, <thing we are searching for>);
 *        if (obj != NULL) {
 *            ref_get(obj->ref);
 *        }
 *        pthread_mutex_unlock(foo_tree_lock);
 *
 *      X's code to drop its reference:
 *
 *        pthread_mutex_lock(foo_tree_lock);
 *        tree_remove(foo_tree, obj);
 *        ref_put(obj->ref);
 *        pthread_mutex_lock(foo_tree_lock);
 *
 * It is essential to document for each reference counter the contract used
 * to ensure race-to-zero safety.
 */

#ifdef _REF_TRACK_
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
typedef struct ref_inst_s {
	const char *get_func;
	const char *put_func;
	int get_line;
	int put_line;
	const char *name;
	int ref_count;
	LIST_ENTRY(ref_inst_s) entry;
} *ref_inst_t;
#endif

typedef void (*ref_free_fn_t)(void *arg);
typedef struct ref_s {
	int ref_count;		/* for all ref instances */
	ref_free_fn_t free_fn;
	void *free_arg;
#ifdef _REF_TRACK_
	pthread_mutex_t lock;
	LIST_HEAD(, ref_inst_s) head;
#endif
} *ref_t;

static inline int _ref_put(ref_t r, const char *name, const char *func, int line)
{
	int count;
#ifdef _REF_TRACK_
	ref_inst_t inst;
	assert(r->ref_count);
	pthread_mutex_lock(&r->lock);
	LIST_FOREACH(inst, &r->head, entry) {
		if (0 == strcmp(inst->name, name)) {
			if (0 == inst->ref_count) {
				fprintf(stderr,
					"name %s func %s line %d put "
					"of zero reference\n",
					name, func, line);
				assert(0);
			}
			inst->put_func = func;
			inst->put_line = line;
			__sync_sub_and_fetch(&inst->ref_count, 1);
			count = __sync_sub_and_fetch(&r->ref_count, 1);
			goto out;
		}
	}
	fprintf(stderr,
		"name %s ref_count %d func %s line %d put but not taken\n",
		name, r->ref_count, func, line);
	pthread_mutex_unlock(&r->lock);
	assert(0);
 out:
	if (!count)
		r->free_fn(r->free_arg);
	else
		pthread_mutex_unlock(&r->lock);
#else
	count = __sync_sub_and_fetch(&r->ref_count, 1);
	if (!count)
		r->free_fn(r->free_arg);
#endif
	return count;
}
#define ref_put(_r_, _n_) _ref_put((_r_), (_n_), __func__, __LINE__)

static inline void _ref_get(ref_t r, const char *name, const char *func, int line)
{
#ifdef _REF_TRACK_
	ref_inst_t inst;
	pthread_mutex_lock(&r->lock);
	if (0 == r->ref_count) {
		fprintf(stderr, "name %s func %s line %d use after free\n",
			name, func, line);
		assert(0);
	}
	LIST_FOREACH(inst, &r->head, entry) {
		if (0 == strcmp(inst->name, name)) {
			__sync_fetch_and_add(&inst->ref_count, 1);
			__sync_fetch_and_add(&r->ref_count, 1);
			inst->get_func = func;
			inst->get_line = line;
			goto out;
		}
	}

	/* No reference with this name exists yet */
	inst = calloc(1, sizeof *inst); assert(inst);
	inst->get_func = func;
	inst->get_line = line;
	inst->name = name;
	inst->ref_count = 1;
	__sync_fetch_and_add(&r->ref_count, 1);
	LIST_INSERT_HEAD(&r->head, inst, entry);
 out:
	pthread_mutex_unlock(&r->lock);
#else
	__sync_fetch_and_add(&r->ref_count, 1);
#endif
}
#define ref_get(_r_, _n_) _ref_get((_r_), (_n_), __func__, __LINE__)

static inline void _ref_init(ref_t r, const char *name,
			     ref_free_fn_t fn, void *arg,
			     const char *func, int line)
{
#ifdef _REF_TRACK_
	ref_inst_t inst;
	pthread_mutex_init(&r->lock, NULL);
	LIST_INIT(&r->head);
	inst = calloc(1, sizeof *inst); assert(inst);
	inst->get_func = func;
	inst->get_line = line;
	inst->name = name;
	inst->ref_count = 1;
	LIST_INSERT_HEAD(&r->head, inst, entry);
#endif
	r->free_fn = fn;
	r->free_arg = arg;
	r->ref_count = 1;
}
#define ref_init(_r_, _n_, _f_, _a_) _ref_init((_r_), (_n_), (_f_), (_a_), __func__, __LINE__)

/*
 * NOTE: This function is for debuggging. `__attribute__((unused))` will
 * suppress the `-Werror=unused-function` for this function.
 */
__attribute__((unused))
static void ref_dump_no_lock(ref_t r, const char *name, FILE *f)
{
#ifdef _REF_TRACK_
	ref_inst_t inst;
	fprintf(f, "... %s: ref %p free_fn %p free_arg %p ...\n",
		name, r, r->free_fn, r->free_arg);
	fprintf(f,
		"%-24s %-8s %-32s %-32s\n", "Name", "Count",
		"Get Line:Func", "Put Line:Func");
	fprintf(stderr,
		"------------------------ -------- --------------------------------- "
		"---------------------------------\n");
	LIST_FOREACH(inst, &r->head, entry) {
		fprintf(f,
			"%-24s %8d %6d:%-23s %6d:%-23s\n",
			inst->name, inst->ref_count,
			inst->get_line, inst->get_func,
			inst->put_line, inst->put_func);
	}
	fprintf(f, "%24s %8d\n", "Total", r->ref_count);
#endif
}

/*
 * NOTE: This function is for debuggging. `__attribute__((unused))` will
 * suppress the `-Werror=unused-function` for this function.
 */
__attribute__((unused))
static void ref_dump(ref_t r, const char *name, FILE *f)
{
#ifdef _REF_TRACK_
	pthread_mutex_lock(&r->lock);
	ref_dump_no_lock(r, name, f);
	pthread_mutex_unlock(&r->lock);
#endif
}

__attribute__((unused))
static void ref_assert_count_ge(ref_t r, const char *name, int count)
{
#ifdef _REF_TRACK_
	ref_inst_t inst;
	LIST_FOREACH(inst, &r->head, entry) {
		if (0 == strcmp(inst->name, name)) {
			assert(inst->ref_count >= count);
			return;
		}
	}
	assert("Reference not present\n");
#endif
}

#endif /* _REF_H_ */

