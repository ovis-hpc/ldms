/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <pthread.h>
#include <errno.h>
#include "ldms.h"
#include "ldmsd.h"

#define BYTES_PER_RECORD 64

/**
 * Calculation of dirty threshold.
 * \param mem_total The total memory size (in bytes).
 * \param dirty_ratio The threshold of dirty page ratio before flushing (as in
 * 	/proc/sys/vm/dirty_ratio)
 * \return Dirty threshold based on given \c mem_total.
 */
size_t calculate_total_dirty_threshold(size_t mem_total, size_t dirty_ratio)
{
	return mem_total * dirty_ratio / 100 / BYTES_PER_RECORD;
}

/**
 * \brief Dirty Threshold (per flush thread).
 *
 * The value of dirty_threshold is set in ldmsd.c
 */
size_t dirty_threshold = 0;

static int records;
static int flush_count;

char tmp_path[PATH_MAX];
int max_q_depth;

/*
 * LRU list for mds.
 */
TAILQ_HEAD(lru_list, store_instance) lru_list;
pthread_mutex_t lru_list_lock;
int open_count;

pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;

struct flush_thread {
	pthread_t thread; /**< the thread */
	pthread_cond_t cv; /**< conditional variable */
	pthread_mutex_t mutex; /**< cv + thread mutex */
	int store_count; /**< number of store assigned to this flush_thread */
	int dirty_count; /**< dirty count */
	pthread_mutex_t dmutex; /**< mutex for dirty count */
	LIST_HEAD(store_list, store_instance) store_list; /**< List of stores */

	struct timeval tvsum; /**< Time spent in flushing for this thread */
};

static int flush_N; /**< Number of flush threads. */
struct flush_thread *flush_thread; /**< An array of ::flush_thread's */
pthread_mutex_t flush_smutex = PTHREAD_MUTEX_INITIALIZER; /**< Mutex for
							   flush_store_count. */

void *flush_proc(void *arg);

int flush_thread_init(struct flush_thread *ft)
{
	int rc = 0;
	if ((rc = pthread_mutex_init(&ft->mutex, 0)))
		goto err;
	if ((rc = pthread_cond_init(&ft->cv, NULL)))
		goto err;
	if ((rc = pthread_mutex_init(&ft->dmutex, 0)))
		goto err;
	if ((rc = pthread_create(&ft->thread, NULL, flush_proc, ft)))
		goto err;
err:
	return rc;
}

int ldmsd_store_init(int __flush_N)
{
	flush_N = __flush_N;
	int i;
	int rc = 0;
	flush_thread = calloc(flush_N, sizeof(flush_thread[0]));
	if (!flush_thread) {
		rc = errno;
		goto err0;
	}

	for (i=0; i<flush_N; i++) {
		if ((rc = flush_thread_init(&flush_thread[i])))
			goto err1;
	}

	if ((rc = pthread_mutex_init(&lru_list_lock, 0)))
		goto err1;

	TAILQ_INIT(&lru_list);

	return 0;

err1:
	for (i=0; i<flush_N; i++) {
		if (!flush_thread[i].thread)
			pthread_cancel(flush_thread[i].thread);
		pthread_mutex_destroy(&flush_thread[i].mutex);
		pthread_mutex_destroy(&flush_thread[i].dmutex);
		pthread_cond_destroy(&flush_thread[i].cv);
	}
	free(flush_thread);
err0:
	return rc;
}

int flush_check(struct flush_thread *ft)
{
	pthread_mutex_lock(&ft->dmutex);
	if (ft->dirty_count > dirty_threshold) {
		ft->dirty_count = 0;
		pthread_cond_signal(&ft->cv);
	}
	pthread_mutex_unlock(&ft->dmutex);
}

void flush_store_instance(struct store_instance *si)
{
	flush_count++;
	ldmsd_store_flush(si->store_engine, si->store_handle);
	si->dirty_count = 0;
}

/* XXX FIXME: check close vs destroy */
void ldmsd_close_store_instance(struct store_instance *si)
{
	ldmsd_store_close(si->store_engine, si->store_handle);
	si->store_handle = NULL;
	si->state = STORE_STATE_CLOSED;
	si->dirty_count = 0;
	pthread_mutex_lock(&lru_list_lock);
	open_count -= 1;
	pthread_mutex_unlock(&lru_list_lock);
}

static int io_exit;

/**
 * Procedure for a ::flush_thread.
 *
 * \param arg The pointer to ::flush_thread structure.
 * \return always NULL.
 */
void *flush_proc(void *arg)
{
	struct flush_thread *ft = arg;
	struct timeval tv0;
	struct timeval tv1;
	struct timeval tvres;
	struct store_instance *si;
	pthread_mutex_lock(&ft->mutex);
	do {
		pthread_cond_wait(&ft->cv, &ft->mutex);
		gettimeofday(&tv0, NULL);
		LIST_FOREACH(si, &ft->store_list, flush_entry) {
			flush_store_instance(si);
		}
		gettimeofday(&tv1, NULL);
		timersub(&tv1, &tv0, &tvres);
		timeradd(&ft->tvsum, &tvres, &ft->tvsum);
	} while (!io_exit);
	return NULL;
}

/**
 * Print ::flush_thread information to the log.
 */
void process_info_flush_thread(void)
{
	int i;
	ldms_log("Flush Thread Info:\n");
	ldms_log("%-16s %-16s %-16s\n", "----------------", "----------------",
					"----------------");
	ldms_log("%-16s %-16s %-16s\n", "Thread", "Store Count",
					"Flush Time (sec.)");
	ldms_log("%-16s %-16s %-16s\n", "----------------", "----------------",
					"----------------");
	for (i = 0; i < flush_N; i++) {
		ldms_log("%-16p %-16d % 10d.%06d\n", flush_thread[i].thread,
				flush_thread[i].store_count,
				flush_thread[i].tvsum.tv_sec,
				flush_thread[i].tvsum.tv_usec);
	}
}

int assign_flush_thread(struct store_instance *si)
{
	int i;
	int fidx;
	int min;
	int rc = 0;
	rc = pthread_mutex_lock(&flush_smutex);
	if (rc) {
		ldms_log("assign_flush_thread::"
				"pthread_mutex_lock(&flush_smutex)"
				" error %d: %s\n", rc, strerror(rc));
		return rc;
	}
	fidx = 0;
	min = flush_thread[0].store_count;
	for (i = 1; i < flush_N; i++) {
		if (flush_thread[i].store_count < min) {
			min = flush_thread[i].store_count;
			fidx = i;
		}
	}
	flush_thread[fidx].store_count++;
	struct flush_thread *ft = &flush_thread[fidx];
	si->ft = ft;
	rc = pthread_mutex_lock(&ft->mutex);
	if (rc) {
		ldms_log("assign_flush_thread::"
				"pthread_mutex_lock(&ft->mutex)"
				" error %d: %s\n", rc, strerror(rc));
		return rc;
	}
	LIST_INSERT_HEAD(&ft->store_list, si, flush_entry);
	rc = pthread_mutex_unlock(&ft->mutex);
	if (rc) {
		ldms_log("assign_flush_thread::"
				"pthread_mutex_unlock(&ft->mutex)"
				" error %d: %s\n", rc, strerror(rc));
		return rc;
	}
	rc = pthread_mutex_unlock(&flush_smutex);
	if (rc) {
		ldms_log("assign_flush_thread::"
				"pthread_mutex_lock(&flush_smutex)"
				" error %d: %s\n", rc, strerror(rc));
		return rc;
	}
	return 0;
}

struct timeval tv0, tv1, tvres, tvsum;

int ldmsd_store_data_add(struct ldmsd_store_policy *lsp,
			ldms_set_t set, struct ldms_mvec *mvec)
{
	int rc;
	struct store_instance *si = lsp->si;
	int flush = 0;
	records++;
	double bytespersec = 0.0;


	pthread_mutex_lock(&si->lock);
	switch (si->state) {
	case STORE_STATE_INIT:
		errno = EINVAL;
		rc = -1;
		break;

	case STORE_STATE_CLOSED:
		si->store_handle = ldmsd_store_new(si->store_engine,
					  lsp->comp_type, lsp->container,
					  &lsp->metric_list, si);
		if (!si->store_handle) {
			si->state = STORE_STATE_ERROR;
			errno = EIO;
			rc = -1;
			break;
		}

		si->state = STORE_STATE_OPEN;

		rc = assign_flush_thread(si);
		if (rc)
			break;
		/* If no error, treat it as STORE_STATE_OPEN case */
		/* Intentionally NO break here */
	case STORE_STATE_OPEN:
		rc = si->store_engine->store(si->store_handle, set, mvec);
		/* If error, don't do dirty counting and flush checking */
		if (rc)
			break;
		pthread_mutex_lock(&si->ft->dmutex);
		si->ft->dirty_count += mvec->count;
		pthread_mutex_unlock(&si->ft->dmutex);
		flush_check(si->ft);
		break;

	case STORE_STATE_ERROR:
		errno = EIO;
		rc = -1;
		break;

	default:
		errno = EINVAL;
		rc = -1;
	}
	pthread_mutex_unlock(&si->lock);
	if (!rc) {
		pthread_mutex_lock(&lru_list_lock);
		/* Move this set to the tail of the LRU queue */
		TAILQ_REMOVE(&lru_list, si, lru_entry);
		TAILQ_INSERT_TAIL(&lru_list, si, lru_entry);
		pthread_mutex_unlock(&lru_list_lock);
	}
	return rc;
}

void close_lru()
{
	struct store_instance *si;
	/*
	 * close the least recently used metric store
	 */
	pthread_mutex_lock(&lru_list_lock);
	do {
		if (TAILQ_EMPTY(&lru_list)) {
			si = NULL;
			break;
		}
		si = TAILQ_FIRST(&lru_list);
		TAILQ_REMOVE(&lru_list, si, lru_entry);
		if (!si->store_handle)
			printf("WARNING: Removed store "
			       "with null store_handle from LRU list.\n");
	} while (!si->store_handle);
	pthread_mutex_unlock(&lru_list_lock);

	if (si)
		ldmsd_close_store_instance(si);
	else
		printf("WARNING: Could not find a store_instance to close.\n");
}

struct store_instance *
new_store_instance(struct ldmsd_store *store, struct ldmsd_store_policy *sp)
{
	int retry_count = 10;
	struct store_instance *s_inst;
	s_inst = calloc(1, sizeof *s_inst);
	if (!s_inst)
		goto out;
	pthread_mutex_init(&s_inst->lock, 0);
retry:
	s_inst->store_engine = store;
	s_inst->store_handle = ldmsd_store_new(store, sp->comp_type,
					sp->container, &sp->metric_list,
					s_inst);
	if (s_inst->store_handle) {
		s_inst->state = STORE_STATE_OPEN;
		int rc = assign_flush_thread(s_inst);
		if (rc) {
			ldmsd_store_close(store, s_inst->store_handle);
			goto fail;
		}
	}
	else {
		ldms_log("Could not create new store_handle. "
			 "Closing LRU and retrying.\n");
		/*
		 * Close the LRU mds to recoup its
		 * handles for our use.
		 */
		close_lru();
		if (retry_count--)
			goto retry;
		else
			goto fail;
	}
	pthread_mutex_lock(&lru_list_lock);
	open_count += 1;
	TAILQ_INSERT_TAIL(&lru_list, s_inst, lru_entry);
	pthread_mutex_unlock(&lru_list_lock);
out:
	return s_inst;
fail:
	free(s_inst);
	return NULL;
}

struct store_instance *
ldmsd_store_instance_get(struct ldmsd_store *store,
			struct ldmsd_store_policy *sp)
{
	ldmsd_store_handle_t sh;
	struct store_instance *s_inst;
	pthread_mutex_lock(&cfg_lock);
	sh = ldmsd_store_get(store, sp->container);
	if (!sh)
		s_inst = new_store_instance(store, sp);
	else
		s_inst = ldmsd_store_get_context(store, sh);
	pthread_mutex_unlock(&cfg_lock);
	return s_inst;
}
#if 0
#include <coll/idx.h>
idx_t ct_idx;
idx_t c_idx;
int main(int argc, char *argv[])
{
	char *s;
	static char pfx[32];
	static char buf[128];
	static char c_key[32];
	static char comp_type[32];
	static char metric_name[32];
	struct metric_store *m;

	if (argc < 2) {
		printf("usage: ./mds_load <dir>\n");
		exit(1);
	}
	mds_init();
	strcpy(pfx, argv[1]);
	ct_idx = idx_create();
	c_idx = idx_create();

	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		struct mds_tuple_s tuple;
		sscanf(buf, "%[^,],%[^,],%d,%ld,%d,%d",
		       comp_type, metric_name,
		       &tuple.comp_id,
		       &tuple.value,
		       &tuple.tv_usec,
		       &tuple.tv_sec);

		/* Add a component type directory if one does not
		 * already exist
		 */
		if (!idx_find(ct_idx, &comp_type, 2)) {
			sprintf(tmp_path, "%s/%s", pfx, comp_type);
			mkdir(tmp_path, 0777);
			idx_add(ct_idx, &comp_type, 2, (void *)1UL);
		}
		sprintf(c_key, "%s:%s", comp_type, metric_name);
		m = idx_find(c_idx, c_key, strlen(c_key));
		if (!m) {
			/*
			 * Open a new MDS for this component-type and
			 * metric combination
			 */
			m = calloc(1, sizeof *m);
			pthread_mutex_init(&m->lock, 0);
			sprintf(tmp_path, "%s/%s/%s", pfx, comp_type, metric_name);
			m->path = strdup(tmp_path);
		retry:
			m->sos = sos_open(m->path, O_CREAT | O_RDWR, 0660,
					  &ovis_metric_class);
			if (m->sos) {
				m->state = MDS_STATE_OPEN;
				idx_add(c_idx, c_key, strlen(c_key), m);
			} else {
				if (errno != EMFILE)
					exit(1);

				/*
				 * Close the LRU mds to recoup its
				 * handles for our use
				 */
				close_lru();
				goto retry;
			}
			pthread_mutex_lock(&lru_list_lock);
			open_count +=1;
			TAILQ_INSERT_TAIL(&lru_list, m, lru_entry);
			pthread_mutex_unlock(&lru_list_lock);
		}
		if (tuple_add(m, &tuple)) {
			perror("tuple_add");
			goto err;
		}
	}
	return 0;
 err:
	return 1;
}
#endif
