/*
 * Copyright (c) 2015-16 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-16 Sandia Corporation. All rights reserved.
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
#include "rmaninfo.h"
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct resource_info_priv {
	struct resource_info ri;
	rim_updatef update;
	pthread_mutex_t mut;
	int ref;
};
#define RIP(x) struct resource_info_priv *x = (struct resource_info_priv *)ri

static const int RIM_CAP_DEFAULT=16;

/* we're not going to overoptimize this, because add/get are one time
per client. */
struct resource_info_manager_s {
	int size;
	int cap;
	struct resource_info_priv **all;
	pthread_mutex_t mut;
};

// convert opaque to manager
#define MAN(x) struct resource_info_manager_s *x = (struct resource_info_manager_s *)rim

static
int growall(struct resource_info_manager_s *m) {
	if (!m) {
		return EINVAL;
	}
	if (m->size < m->cap) {
		return 0;
	}
	int newcap = 2*m->cap;
	struct resource_info_priv **allnew = 
		realloc(m->all, sizeof(struct resource_info_priv *) * newcap);
	if (!allnew) {
		return ENOMEM;
	}
	int i;
	m->cap = newcap;
	m->all = allnew;
	for (i=m->size; i < newcap; i++) {
		allnew[i] = NULL;
	}
	return 0;
}

#if 0
// static
// int compare_ri(void *tree, void *n)
// {
	// struct resource_info *rit = (struct resource_info *) tree;
	// struct resource_info *rin = (struct resource_info *) n;
	// if (!tree || !n || !rit->name || !rin->name) {
		// errno = EINVAL;
		// return -1;
	// }
	// return strcmp(rit->name,rin->name);
// }
#endif

resource_info_manager
create_resource_info_manager()
{
	errno = 0;
	resource_info_manager rim = NULL;
	struct resource_info_manager_s *priv = malloc(sizeof(*priv));
	if (!priv) {
		errno = ENOMEM;
		return NULL;
	}
	pthread_mutex_init(&(priv->mut),NULL);
	priv->cap = RIM_CAP_DEFAULT;
	priv->size = 0;
	priv->all = calloc(sizeof(struct resource_info_priv *) , priv->cap);
	if (!priv->all) {
		errno = ENOMEM;
		free(priv);
		priv = NULL;
	}
	rim = (resource_info_manager) priv;
	return rim;
}

int register_resource_info(resource_info_manager rim, const char *resource_name, const char * type, struct attr_value_list *config_args, rim_updatef update, void *updateData)
{
	MAN(m);
	int ires = 0;
	if (!m || !resource_name || !update ) {
		return EINVAL;
	}
	if ( growall(m) ) {
		return ENOMEM;
	}
	struct resource_info_priv *rip = calloc(1,sizeof(*rip));
	if (!rip) {
		return ENOMEM;
	}
	struct resource_info *ri = &(rip->ri);
	ri->name = strdup(resource_name);
	if (!ri->name)
		goto err;
	ri->type = strdup(type);
	if (!ri->type)
		goto err;
	ri->generation = 0;
	rip->update = update;
	ri->data = updateData;
	rip->ref = 1;
	struct timespec ts;
	(void)clock_gettime(CLOCK_REALTIME, &ts);
	ires = update(ri,rim_init,config_args);
	if (ires)
		goto err;
	ires = update(ri, rim_update, &ts);
	if (ires) {
		update(ri, rim_final, &ts);
		goto err;
	}
	m->all[m->size] = rip;
	m->size++;
	return 0;
err:
	free(ri->name);
	free(ri->type);
	free(rip);
	return ires;
}


/* releases manager memory. outstanding resource_info 
 references will still be valid */
void clear_resource_info_manager(resource_info_manager rim)
{
	MAN(m);
	if (!m)
		return;
	pthread_mutex_lock(&(m->mut));
	struct resource_info_manager_s *priv = rim;
	int i;
	for (i=0; i < m->size; i++) {
		release_resource_info(&(m->all[i]->ri));
		m->all[i] = NULL;
	}
	free(m->all);
	pthread_mutex_unlock(&(m->mut));
	pthread_mutex_destroy(&(m->mut));
	free(priv);
}

struct resource_info *get_resource_info(resource_info_manager rim, const char *resource_name)
{
	MAN(m);
	pthread_mutex_lock(&(m->mut));
	int i;
	struct resource_info *res = NULL;
	struct resource_info_priv *rip = NULL;
	for (i = 0; i < m->size; i++) {
		if (strcmp(m->all[i]->ri.name,resource_name)==0) {
			printf("got %s\n",resource_name);
			pthread_mutex_lock(&(m->all[i]->mut));
			rip = m->all[i];
			rip->ref++;
			res = &(rip->ri);
			pthread_mutex_unlock(&(rip->mut));
			break;
		}
	}
	pthread_mutex_unlock(&(m->mut));
	return res;
}

/* update ri->v (if needed). update calls will be serialized. */
int update_resource_info(struct resource_info *ri)
{
	struct timespec ts;
	(void)clock_gettime(CLOCK_REALTIME, &ts);
	RIP(rip);
	return rip->update(ri, rim_update, &ts);
}

/* releaase the counted ri */
void release_resource_info(struct resource_info *ri)
{
	RIP(rip);
	pthread_mutex_lock(&(rip->mut));
	rip->ref--;
	if (rip->ref == 0) {
		struct timespec ts;
		(void)clock_gettime(CLOCK_REALTIME, &ts);
		rip->update(ri, rim_final, ri->data );
		rip->update = NULL;
		free(ri->name);
		free(ri->type);
		ri->generation = 0;
		ri->v.i64=0;
		pthread_mutex_unlock(&(rip->mut));
		pthread_mutex_destroy(&(rip->mut));
		free(rip);
	} else {
		pthread_mutex_unlock(&(rip->mut));
	}
}


