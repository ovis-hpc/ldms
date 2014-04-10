/*
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

/*
 * Author: Tom Tucker tom at ogc dot us
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
#include "sos.h"
#include <coll/idx.h>

#include "mds.h"

#define DIRTY_THRESHOLD		(1024 * 1024 * 1)

static int records;
static int flush_count;

char tmp_path[PATH_MAX];
int max_q_depth;

struct metric_store_s;
typedef void (*io_work_fn)(struct metric_store_s *);
struct metric_store_s {
	sos_t sos;
	char *path;
	enum {
		MDS_STATE_INIT=0,
		MDS_STATE_OPEN,
		MDS_STATE_CLOSED,
		MDS_STATE_ERROR
	} state;
	size_t dirty_count;
	pthread_mutex_t lock;
	TAILQ_ENTRY(metric_store_s) lru_entry;
	LIST_ENTRY(metric_store_s) work_entry;
	io_work_fn work_fn;
	int work_pending;
};

/*
 * LRU list for mds.
 */
TAILQ_HEAD(lru_list, metric_store_s) lru_list;
pthread_mutex_t lru_list_lock;
int open_count;

pthread_t io_thread;
pthread_mutex_t io_mutex;
pthread_cond_t io_cv;
LIST_HEAD(io_work_q, metric_store_s) io_work_q;
static int io_work_q_depth;

int queue_work(struct metric_store_s *m, io_work_fn fn)
{
	int wake_up = 0;
	int queue = 1;
	pthread_mutex_lock(&m->lock);
	if (m->work_pending)
		queue = 0;
	else {
		m->work_pending = 1;
		m->work_fn = fn;
	}
	pthread_mutex_unlock(&m->lock);

	if (!queue)
		return io_work_q_depth;

	pthread_mutex_lock(&io_mutex);
	io_work_q_depth++;
	// if (LIST_EMPTY(&io_work_q))
		wake_up = 1;
	LIST_INSERT_HEAD(&io_work_q, m, work_entry);
	if (io_work_q_depth > max_q_depth)
		max_q_depth = io_work_q_depth;
	if (wake_up)
		pthread_cond_signal(&io_cv);
	pthread_mutex_unlock(&io_mutex);
	return io_work_q_depth;
}

void flush_metric_store(struct metric_store_s *m)
{
	flush_count++;
	sos_commit(m->sos, ODS_COMMIT_ASYNC);
	m->dirty_count = 0;
}

void close_metric_store(struct metric_store_s *m)
{
	if (!m->sos)
		return;
	sos_close(m->sos, ODS_COMMIT_ASYNC);
	m->sos = NULL;
	m->state = MDS_STATE_CLOSED;
	m->dirty_count = 0;
	pthread_mutex_lock(&lru_list_lock);
	open_count -= 1;
	pthread_mutex_unlock(&lru_list_lock);
}

static int io_exit;

void *io_proc(void *arg)
{
	struct timeval tv0;
	struct timeval tv1;
	struct timeval tvres;
	struct timeval tvsum = { 0, 0 };
	struct metric_store_s *m;
	do {
		pthread_mutex_lock(&io_mutex);
		gettimeofday(&tv0, NULL);
		while (!LIST_EMPTY(&io_work_q)) {
			m = LIST_FIRST(&io_work_q);
			LIST_REMOVE(m, work_entry);
			io_work_q_depth--;
			pthread_mutex_unlock(&io_mutex);

			pthread_mutex_lock(&m->lock);
			m->work_fn(m);
			m->work_pending = 0;
			pthread_mutex_unlock(&m->lock);

			pthread_mutex_lock(&io_mutex);
		}
		gettimeofday(&tv1, NULL);
		timersub(&tv1, &tv0, &tvres);
		timeradd(&tvsum, &tvres, &tvsum);
		if (!io_exit)
			pthread_cond_wait(&io_cv, &io_mutex);
		pthread_mutex_unlock(&io_mutex);
	} while (!io_exit || !LIST_EMPTY(&io_work_q));
	printf("io_seconds %ld %ld\n", tvsum.tv_sec, tvsum.tv_usec);
	return NULL;
}

int mds_term()
{
	struct metric_store_s *m;

	pthread_mutex_lock(&lru_list_lock);
	while (!TAILQ_EMPTY(&lru_list)) {
		m = TAILQ_FIRST(&lru_list);
		TAILQ_REMOVE(&lru_list, m, lru_entry);
		pthread_mutex_unlock(&lru_list_lock);
		sos_close(m->sos, ODS_COMMIT_ASYNC);
		pthread_mutex_lock(&lru_list_lock);
	}
	io_exit = 1;
	if (LIST_EMPTY(&io_work_q))
		pthread_cond_signal(&io_cv);

	pthread_mutex_unlock(&io_mutex);
	pthread_join(io_thread, NULL);
	return 0;
}

int mds_init()
{
	if (pthread_mutex_init(&io_mutex, 0))
		return 1;
	if (pthread_cond_init(&io_cv, NULL))
		return 1;
	if (pthread_create(&io_thread, NULL, io_proc, NULL))
		return 1;
	if (pthread_mutex_init(&lru_list_lock, 0))
		return 1;
	TAILQ_INIT(&lru_list);

	return 0;
}

idx_t ct_idx;
idx_t c_idx;
struct timeval tv0, tv1, tvres, tvsum;

typedef struct mds_tuple_s {
	uint32_t tv_usec;
	uint32_t tv_sec;
	uint32_t comp_id;
	uint64_t value;
} *mds_tuple_t;

int add_obj(sos_t sos, mds_tuple_t t)
{
	sos_obj_t obj = sos_obj_new(sos);
	if (!obj) {
		errno = ENOMEM;
		return -1;
	}
	sos_obj_attr_set(sos, 0, obj, &t->tv_sec);
	sos_obj_attr_set(sos, 1, obj, &t->tv_usec);
	sos_obj_attr_set(sos, 2, obj, &t->comp_id);
	sos_obj_attr_set(sos, 3, obj, &t->value);
	if (sos_obj_add(sos, obj))
		return -1;
	return 0;
}

int tuple_add(struct metric_store_s *ms, mds_tuple_t t)
{
	int rc;
	int flush = 0;
	records++;
	double bytespersec = 0.0;
	if (!(records % 1000000)) {
		gettimeofday(&tv1, NULL);
		if (tv0.tv_sec != 0) {
			double dur;
			timersub(&tv1, &tv0, &tvres);
			dur = (double)tvres.tv_sec +
				((double)tvres.tv_usec / 1000000.0);
			bytespersec = (1000000.0 * 128.0) / dur;
		}
		gettimeofday(&tv0, NULL);
		printf("records %d flush %d open %d Mbytes/sec %g\n",
		       records, flush_count, open_count, bytespersec / 1000000.0);
	}
	pthread_mutex_lock(&ms->lock);
	switch (ms->state) {
	case MDS_STATE_OPEN:
		ms->dirty_count += sizeof *t;
		rc = add_obj(ms->sos, t);
		break;

	case MDS_STATE_INIT:
		errno = EINVAL;
		rc = -1;
		break;

	case MDS_STATE_CLOSED:
		ms->sos = sos_open(ms->path, O_CREAT | O_RDWR, 0660,
				   &ovis_metric_class);
		if (ms->sos) {
			ms->state = MDS_STATE_OPEN;
			rc = add_obj(ms->sos, t);
		} else {
			ms->state = MDS_STATE_ERROR;
			errno = EIO;
			rc = -1;
		}
		break;

	case MDS_STATE_ERROR:
		errno = EIO;
		rc = -1;
		break;
	default:
		errno = EINVAL;
		rc = -1;
	}
	if (ms->dirty_count >= DIRTY_THRESHOLD)
		flush = 1;
	pthread_mutex_unlock(&ms->lock);
	if (!rc) {
		pthread_mutex_lock(&lru_list_lock);
		/* Move this set to the tail of the LRU queue */
		TAILQ_REMOVE(&lru_list, ms, lru_entry);
		TAILQ_INSERT_TAIL(&lru_list, ms, lru_entry);
		pthread_mutex_unlock(&lru_list_lock);

		if (flush)
			queue_work(ms, flush_metric_store);
	}
	if (rc)
		printf("tuple_add failure %d\n", rc);
	return rc;

}

void close_lru()
{
	struct metric_store_s *mx;
	/*
	 * close the
	 */
	pthread_mutex_lock(&lru_list_lock);
	do {
		if (TAILQ_EMPTY(&lru_list)) {
			mx = NULL;
			break;
		}
		mx = TAILQ_FIRST(&lru_list);
		TAILQ_REMOVE(&lru_list, mx, lru_entry);
		if (!mx->sos)
			printf("WARNING: Removed metric store "
			       "with null MDS from LRU list.\n");
	} while (!mx->sos);
	pthread_mutex_unlock(&lru_list_lock);

	if (mx)
		close_metric_store(mx);
	else
		printf("WARNING: Could not find an MDS to close.\n");
}

int main(int argc, char *argv[])
{
	char *s;
	static char pfx[32];
	static char buf[128];
	static char c_key[32];
	static char comp_type[32];
	static char metric_name[32];
	struct metric_store_s *m;

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
	mds_term();
	printf("records %d\n", records);
	return 0;
 err:
	return 1;
}
