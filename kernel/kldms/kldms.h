/*
 * Copyright (c) 2010,2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010,2015-2016 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#ifndef __KLDMS_H
#define __KLDMS_H

#include <linux/kref.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/time.h>
#include "ldms_core.h"
#include "kldms_req.h"

typedef struct kldms_value_s *kldms_value_t;
typedef struct kldms_schema_s *kldms_schema_t;
typedef struct kldms_set *kldms_set_t;

#define LDMS_SET_ID_DATA    0x1000000

struct kldms_request {
	struct list_head			list;
	union {
		struct kldms_req_hdr		hdr;
		struct kldms_req_hello		hello;
		struct kldms_req_publish_set	publish;
		struct kldms_req_unpublish_set	unpub;
		struct kldms_req_update_set	update;
	};
};

struct kldms_device {
	struct kref		ref;
	struct completion	comp;
	struct device		*dev;
	int			devnum;
	struct cdev		cdev;

	spinlock_t		req_list_lock;
	struct list_head	req_list;

	spinlock_t		file_list_lock;
	struct list_head	file_list;
};

struct kldms_file {
	struct kref		ref;
	spinlock_t		lock;
	struct mutex		mutex;
	struct kldms_device	*device;
	wait_queue_head_t	poll_wait;
	int			is_closed;
	struct fasync_struct	*async_queue;
	struct list_head	list;
};

struct kldms_mdef_s {
	char *name;
	enum ldms_value_type type;
	char units[8];
	uint32_t flags;	/* DATA/MDATA flag */
	uint32_t count; /* Number of elements in the array if this is of an array type */
	size_t meta_sz;
	size_t data_sz;
	struct list_head entry;
};

typedef struct kldms_mdef_s *kldms_mdef_t;

struct kldms_set {
	int			ks_id;
	size_t			ks_size;
	int			ks_page_order;
	struct page		*ks_page;
	struct ldms_set_hdr	*ks_meta;
	struct ldms_data_hdr	*ks_data;
	struct list_head	ks_list;
	char			ks_name[LDMS_SET_NAME_MAX];
};

struct kldms_schema_s {
	char *name;
	int metric_count;
	size_t meta_sz;
	size_t data_sz;
	struct list_head metric_list;
	struct list_head entry;
};

struct kldms_metric {
	struct kldms_set *set;
	struct ldms_value_desc *desc;
	union ldms_value *value;
	struct list_head list;
};

typedef struct kldms_metric kldms_metric_t;

#define LDMS_GN_INCREMENT(_gn) do { \
	(_gn) = __cpu_to_le64(__le64_to_cpu((_gn)) + 1); \
} while (0)

/*
 * exported KLDMS API
 */
extern int kldms_schema_metric_add(kldms_schema_t s, const char *name, enum ldms_value_type type,
				   const char *units);
extern int kldms_schema_meta_add(kldms_schema_t s, const char *name, enum ldms_value_type type,
				 const char *units);
extern int kldms_schema_metric_array_add(kldms_schema_t s, const char *name, enum ldms_value_type type,
					 const char *units, uint32_t count);
extern int kldms_schema_meta_array_add(kldms_schema_t s, const char *name, enum ldms_value_type type,
				       const char *units, uint32_t count);
void kldms_metric_array_set_val(kldms_set_t set, int metric_idx, int array_idx, ldms_mval_t src);
extern void kldms_metric_set(kldms_set_t s, int i, ldms_mval_t v);
extern kldms_schema_t kldms_schema_new(const char * schema_name);
extern kldms_schema_t kldms_schema_find(const char *schema_name);
extern kldms_set_t kldms_set_new(const char *name, kldms_schema_t schema);
extern void kldms_schema_delete(kldms_schema_t schema);
extern void kldms_set_delete(kldms_set_t schema);
extern void kldms_transaction_begin(kldms_set_t s);
extern void kldms_transaction_end(kldms_set_t s);
extern void kldms_set_publish(struct kldms_set *set);

#endif
