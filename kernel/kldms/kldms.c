/*
 * Copyright (c) 2010,2017 Open Grid Computing, Inc. All rights reserved.
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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/sysctl.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/time.h>
#include <asm/uaccess.h>
#include <asm/page.h>

#include "kldms.h"

static struct class 		*kldms_class;
static struct kldms_device	*kldms_dev;
static spinlock_t		set_list_lock;
static struct list_head		set_list;
static int			kldms_set_no;
static spinlock_t		schema_list_lock;
static struct list_head		schema_list;

static kldms_set_t kldms_set_alloc(const char *name, kldms_schema_t schema);

static char *type_names[] = {
	[LDMS_V_NONE] = "none",
	[LDMS_V_U8] = "u8",
	[LDMS_V_S8] = "s8",
	[LDMS_V_U16] = "u16",
	[LDMS_V_S16] = "s16",
	[LDMS_V_U32] = "u32",
	[LDMS_V_S32] = "s32",
	[LDMS_V_U64] = "u64",
	[LDMS_V_S64] = "s64",
	[LDMS_V_F32] = "f32",
	[LDMS_V_D64] = "d64",
	[LDMS_V_U8_ARRAY] = "u8_ARRAY",
	[LDMS_V_S8_ARRAY] = "s8_ARRAY",
	[LDMS_V_U16_ARRAY] = "u16_ARRAY",
	[LDMS_V_S16_ARRAY] = "s16_ARRAY",
	[LDMS_V_U32_ARRAY] = "u32_ARRAY",
	[LDMS_V_S32_ARRAY] = "s32_ARRAY",
	[LDMS_V_U64_ARRAY] = "u64_ARRAY",
	[LDMS_V_S64_ARRAY] = "s64_ARRAY",
	[LDMS_V_F32_ARRAY] = "f32_ARRAY",
	[LDMS_V_D64_ARRAY] = "d64_ARRAY",
};

static struct kldms_set *find_set(const char *set_name)
{
	struct kldms_set *set;

	list_for_each_entry(set, &set_list, ks_list) {
		if (0 == strcmp(set->ks_name, set_name))
			return set;
	}
	return NULL;
}

static struct kldms_set *find_set_by_no(int no)
{
	struct kldms_set *set;

	list_for_each_entry(set, &set_list, ks_list) {
		if (no == set->ks_id)
			return set;
	}
	return NULL;
}

struct kldms_set *kldms_set_new(const char *set_name, kldms_schema_t schema)
{
	struct kldms_set *set, *oset;

	spin_lock(&set_list_lock);

	/* see if set already exists */
	oset = find_set(set_name);
	if (oset) {
		spin_unlock(&set_list_lock);
		return NULL;
	}

	spin_unlock(&set_list_lock);
	set = kldms_set_alloc(set_name, schema);
	if (!set)
		return NULL;

	strncpy(set->ks_name, set_name, sizeof(set->ks_name) - 1);

	set->ks_id = kldms_set_no++;
	INIT_LIST_HEAD(&set->ks_list);
	spin_lock(&set_list_lock);
	list_add(&set->ks_list, &set_list);
	spin_unlock(&set_list_lock);

	return set;
}
EXPORT_SYMBOL(kldms_set_new);

void kldms_set_delete(struct kldms_set *set)
{
	spin_lock(&set_list_lock);
	list_del(&set->ks_list);
	spin_unlock(&set_list_lock);

	if (set->ks_page)
		free_pages((unsigned long)set->ks_meta, set->ks_page_order);

	kfree(set);
}
EXPORT_SYMBOL(kldms_set_delete);

void kldms_transaction_begin(kldms_set_t s)
{
	struct ldms_data_hdr *dh = s->ks_data;
	struct timespec ts;
	dh->trans.flags = LDMS_TRANSACTION_BEGIN;
	getnstimeofday(&ts);
	dh->trans.ts.sec = __cpu_to_le32(ts.tv_sec);
	dh->trans.ts.usec = __cpu_to_le32(ts.tv_nsec / 1000);
}
EXPORT_SYMBOL(kldms_transaction_begin);

static void __send_update(kldms_set_t set)
{
        struct kldms_file *file, *n;
        struct kldms_request *kreq;

        if (!kldms_dev)
                return;

	pr_debug("Sending update for set %p\n", set);
        if (list_empty(&kldms_dev->file_list)) {
		pr_debug("No open files on %p\n", kldms_dev);
                return;
	}

        kreq = kmalloc(sizeof(*kreq), GFP_KERNEL);
        if (!kreq)
                return;

        kreq->update.hdr.req_id = KLDMS_REQ_UPDATE_SET;
        kreq->update.set_id = set->ks_id;
        INIT_LIST_HEAD(&kreq->list);
        spin_lock(&kldms_dev->req_list_lock);
        list_add_tail(&kreq->list, &kldms_dev->req_list);
        spin_unlock(&kldms_dev->req_list_lock);
        list_for_each_entry_safe(file, n, &kldms_dev->file_list, list) {
                wake_up_interruptible(&file->poll_wait);
                kill_fasync(&file->async_queue, SIGIO, POLL_IN);
        }
}

void kldms_transaction_end(kldms_set_t s)
{
	struct ldms_data_hdr *dh = s->ks_data;
	struct timespec ts;
	getnstimeofday(&ts);
	dh->trans.dur.sec = ts.tv_sec - __le32_to_cpu(dh->trans.ts.sec);
	dh->trans.dur.usec = (ts.tv_nsec / 1000) - __le32_to_cpu(dh->trans.ts.usec);
	if (((int32_t)dh->trans.dur.usec) < 0) {
		dh->trans.dur.sec -= 1;
		dh->trans.dur.usec += 1000000;
	}
	dh->trans.dur.sec = __cpu_to_le32(dh->trans.dur.sec);
	dh->trans.dur.usec = __cpu_to_le32(dh->trans.dur.usec);
	dh->trans.ts.sec = __cpu_to_le32(ts.tv_sec);
	dh->trans.ts.usec = __cpu_to_le32(ts.tv_nsec / 1000);
	dh->trans.flags = LDMS_TRANSACTION_END;
	__send_update(s);
}
EXPORT_SYMBOL(kldms_transaction_end);

kldms_schema_t kldms_schema_find(const char *schema_name)
{
	struct kldms_schema_s *schema;

	spin_lock(&schema_list_lock);
	list_for_each_entry(schema, &schema_list, entry) {
		if (0 == strcmp(schema->name, schema_name)) {
			spin_unlock(&schema_list_lock);
			return schema;
		}
	}
	spin_unlock(&schema_list_lock);
	return NULL;
}
EXPORT_SYMBOL(kldms_schema_find);

static int value_size[] = {
	[LDMS_V_NONE] = 0,
	[LDMS_V_U8] = sizeof(uint8_t),
	[LDMS_V_S8] = sizeof(uint8_t),
	[LDMS_V_U16] = sizeof(uint16_t),
	[LDMS_V_S16] = sizeof(uint16_t),
	[LDMS_V_U32] = sizeof(uint32_t),
	[LDMS_V_S32] = sizeof(uint32_t),
	[LDMS_V_U64] = sizeof(uint64_t),
	[LDMS_V_S64] = sizeof(uint64_t),
	[LDMS_V_F32] = sizeof(float),
	[LDMS_V_D64] = sizeof(double),
	/* size of array type in this table is the size of an element */
	[LDMS_V_U8_ARRAY] = sizeof(uint8_t),
	[LDMS_V_S8_ARRAY] = sizeof(uint8_t),
	[LDMS_V_U16_ARRAY] = sizeof(uint16_t),
	[LDMS_V_S16_ARRAY] = sizeof(uint16_t),
	[LDMS_V_U32_ARRAY] = sizeof(uint32_t),
	[LDMS_V_S32_ARRAY] = sizeof(uint32_t),
	[LDMS_V_U64_ARRAY] = sizeof(uint64_t),
	[LDMS_V_S64_ARRAY] = sizeof(uint64_t),
	[LDMS_V_F32_ARRAY] = sizeof(float),
	[LDMS_V_D64_ARRAY] = sizeof(double),
};

size_t __ldms_value_size_get(enum ldms_value_type t, uint32_t count)
{
	size_t value_sz = 0;
	value_sz = value_size[t] * count;
	/* Values are aligned on 8b boundary */
	return roundup(value_sz, 8);
}

void __ldms_metric_size_get(const char *name, enum ldms_value_type t,
                uint32_t count, size_t *meta_sz, size_t *data_sz)
{
	/* Descriptors are aligned on eight byte boundaries */
	*meta_sz = roundup(sizeof(struct ldms_value_desc) + strlen(name) + 1, 8);

	*data_sz = __ldms_value_size_get(t, count);
}

static kldms_set_t kldms_set_alloc(const char *instance_name,
				   kldms_schema_t schema)
{
	struct ldms_data_hdr *data;
	struct ldms_set_hdr *meta;
	struct ldms_value_desc *vd;
	size_t meta_sz;
	uint64_t value_off;
	struct kldms_mdef_s *md;
	int metric_idx;
	ldms_name_t lname;
	size_t vd_size;
	struct kldms_set *set;
	size_t sz;
	int i;

	if (!instance_name || !schema) {
		return NULL;
	}

	set = kmalloc(sizeof(struct kldms_set), GFP_KERNEL|__GFP_ZERO);
	if (!set) {
		return NULL;
	}

	meta_sz = schema->meta_sz /* header + metric dict */
		+ strlen(schema->name) + 2 /* schema name + '\0' + len */
		+ strlen(instance_name) + 2; /* instance name + '\0' + len */
	meta_sz = roundup(meta_sz, 8);

	sz = meta_sz + schema->data_sz;
	if (sz < PAGE_SIZE)
		sz = PAGE_SIZE;
	set->ks_size = sz;

	/* Since the min size is one page, the 'sz' is guaranteed to be a
	 * multiple of PAGE_SIZE after roundup_power_of_two. */
	set->ks_page_order = ilog2(sz / PAGE_SIZE);
	if (((1 << set->ks_page_order) * PAGE_SIZE) < sz)
		set->ks_page_order++;

	set->ks_page = alloc_pages(GFP_KERNEL | GFP_DMA, set->ks_page_order);
	if (!set->ks_page) {
		kfree(set);
		return NULL;
	}

	meta = page_address(set->ks_page);

	LDMS_VERSION_SET(meta->version);
	meta->card = __cpu_to_le32(schema->metric_count);
	meta->meta_sz = __cpu_to_le32(meta_sz);

	data = (struct ldms_data_hdr *)((unsigned char*)meta + meta_sz);
	meta->data_sz = __cpu_to_le32(schema->data_sz);
	data->size = __cpu_to_le64(schema->data_sz);

	/* Initialize the metric set header */
	meta->meta_gn = __cpu_to_le64(1);
	meta->flags = LDMS_SETH_F_LCLBYTEORDER;

	meta->array_card = 1;
	meta->uid = meta->gid = 0;
	meta->perm = 0777;

	/*
	 * Set the instance name.
	 * NB: Must be set first because get_schema_name uses the
	 * instance name len field.
	 */
	lname = get_instance_name(meta);
	lname->len = strlen(instance_name) + 1;
	strcpy(lname->name, instance_name);

	/* Set the schema name. */
	lname = get_schema_name(meta);
	lname->len = strlen(schema->name) + 1;
	strcpy(lname->name, schema->name);

	data->gn = data->meta_gn = meta->meta_gn;
	data->curr_idx = 0;

	/* Add the metrics from the schema */
	vd = get_first_metric_desc(meta);
	value_off = roundup(sizeof(*data), 8);
	metric_idx = 0;
	vd_size = 0;
	list_for_each_entry(md, &schema->metric_list, entry) {
		/* Add descriptor to dictionary */
		meta->dict[metric_idx] = __cpu_to_le32(ldms_off_(meta, vd));

		/* Build the descriptor */
		vd->vd_type = md->type;
		vd->vd_flags = md->flags;
		vd->vd_array_count = __cpu_to_le32(md->count);
		// memcpy(vd->vd_units, md->units, sizeof(vd->vd_units));
		vd->vd_name_len = strlen(md->name) + 1;
		strncpy(vd->vd_name, md->name, vd->vd_name_len);
		if (md->flags & LDMS_MDESC_F_DATA) {
			vd->vd_data_offset = __cpu_to_le32(value_off);
			value_off += __ldms_value_size_get(md->type, md->count);
		} else
			vd->vd_data_offset = 0; /* set after all metrics defined */

		/* Advance to next descriptor */
		metric_idx++;
		vd = (struct ldms_value_desc *)((char *)vd + md->meta_sz);
		vd_size += md->meta_sz;
	}
	/*
	 * Now that the end of all vd is known, assign the data offsets for the
	 * meta-attributes from the meta data area
	 */
	value_off = (uint64_t)((char *)vd - (char *)meta);
	value_off = roundup(value_off, 8);
	for (i = 0; i < schema->metric_count; i++) {
		vd = ldms_ptr_(struct ldms_value_desc, meta, __le32_to_cpu(meta->dict[i]));
		if (vd->vd_flags & LDMS_MDESC_F_DATA)
			continue;
		vd->vd_data_offset = value_off;
		value_off += __ldms_value_size_get(vd->vd_type,
						   __le32_to_cpu(vd->vd_array_count));
	}

	set->ks_meta = meta;
	set->ks_data = data;

	return set;
}

kldms_schema_t kldms_schema_new(const char *schema_name)
{
	kldms_schema_t s = kmalloc(sizeof *s, GFP_KERNEL|__GFP_ZERO);

	if (s) {
		s->name = kstrdup(schema_name, GFP_KERNEL);
		s->meta_sz = sizeof(struct ldms_set_hdr);
		s->data_sz = sizeof(struct ldms_data_hdr);
		INIT_LIST_HEAD(&s->metric_list);
		INIT_LIST_HEAD(&s->entry);
		spin_lock(&schema_list_lock);
		list_add(&s->entry, &schema_list);
		spin_unlock(&schema_list_lock);
	}
	return s;
}
EXPORT_SYMBOL(kldms_schema_new);

void kldms_schema_delete(kldms_schema_t schema)
{
	kldms_mdef_t m;

	if (!schema)
		return;

	while (!list_empty(&schema->metric_list)) {
		m = list_first_entry(&schema->metric_list, struct kldms_mdef_s,
				     entry);
		list_del(&m->entry);
		kfree(m->name);
		kfree(m);
	}
	list_del(&schema->entry);
	kfree(schema->name);
	kfree(schema);
}
EXPORT_SYMBOL(kldms_schema_delete);

#define ROUNDUP(_v,_s) ((_v + (_s - 1)) & ~(_s - 1))

union ldms_value ldms_value_zero = {
	.v_u64 = 0
};

int __schema_metric_add(kldms_schema_t s, const char *name, int flags,
			enum ldms_value_type type, const char *units,
			uint32_t array_count)
{
	kldms_mdef_t m;

	if (!s || !name)
		return -EINVAL;

	/* check if the name is a duplicate */
	list_for_each_entry(m, &s->metric_list, entry) {
		if (!strcmp(m->name, name))
			return -EEXIST;
	}
	m = kmalloc(sizeof *m, GFP_KERNEL | __GFP_ZERO);
	if (!m)
		return -ENOMEM;

	m->name = kstrdup(name, GFP_KERNEL);
	m->type = type;
	m->flags = flags;
	m->count = array_count;
	if (units)
		strncpy(m->units, units, sizeof(m->units));
	else
		memset(m->units, '\0', sizeof(m->units));
	__ldms_metric_size_get(name, type, m->count, &m->meta_sz, &m->data_sz);
	list_add_tail(&m->entry, &s->metric_list);
	s->metric_count++;
	s->meta_sz += m->meta_sz + sizeof(uint32_t) /* + dict entry */;
	if (flags & LDMS_MDESC_F_DATA)
		s->data_sz += m->data_sz;
	else
		s->meta_sz += m->data_sz;
	return s->metric_count - 1;
}

int kldms_schema_metric_add(kldms_schema_t s, const char *name,
			    enum ldms_value_type type, const char *units)
{
	if (type > LDMS_V_D64)
		return -EINVAL;
	return __schema_metric_add(s, name, LDMS_MDESC_F_DATA, type, units, 1);
}
EXPORT_SYMBOL(kldms_schema_metric_add);

int kldms_schema_meta_add(kldms_schema_t s, const char *name,
			  enum ldms_value_type type, const char *units)
{
	if (type > LDMS_V_D64)
		return -EINVAL;
	return __schema_metric_add(s, name, LDMS_MDESC_F_META, type, units, 1);
}
EXPORT_SYMBOL(kldms_schema_meta_add);

static int __type_is_array(enum ldms_value_type t)
{
	return !(t < LDMS_V_CHAR_ARRAY || LDMS_V_D64_ARRAY < t);
}

int kldms_schema_metric_array_add(kldms_schema_t s, const char *name,
				  enum ldms_value_type type, const char *units,
				  uint32_t count)
{
	if (!__type_is_array(type))
		return -EINVAL;
	return __schema_metric_add(s, name, LDMS_MDESC_F_DATA, type, units, count);
}
EXPORT_SYMBOL(kldms_schema_metric_array_add);

int kldms_schema_meta_array_add(kldms_schema_t s, const char *name,
				enum ldms_value_type type, const char *units,
				uint32_t count)
{
	if (!__type_is_array(type))
		return -EINVAL;
	return __schema_metric_add(s, name, LDMS_MDESC_F_META, type, units, count);
}
EXPORT_SYMBOL(kldms_schema_meta_array_add);

static void __metric_array_set(kldms_set_t s, ldms_mdesc_t desc, ldms_mval_t dst,
			       int i, ldms_mval_t src)
{
	if (i < 0 || i >= __le32_to_cpu(desc->vd_array_count)) {
		pr_err("Attempt to set idx %d in array of %d elements.\n",
		       i, __le32_to_cpu(desc->vd_array_count));
		return;
	}

	switch (desc->vd_type) {
	case LDMS_V_CHAR_ARRAY:
	case LDMS_V_U8_ARRAY:
	case LDMS_V_S8_ARRAY:
		dst->a_u8[i] = src->v_u8;
		break;
	case LDMS_V_U16_ARRAY:
	case LDMS_V_S16_ARRAY:
		dst->a_u16[i] = __cpu_to_le16(src->v_u16);
		break;
	case LDMS_V_U32_ARRAY:
	case LDMS_V_S32_ARRAY:
		dst->a_u32[i] = __cpu_to_le32(src->v_u32);
		break;
	case LDMS_V_U64_ARRAY:
	case LDMS_V_S64_ARRAY:
		dst->a_u64[i] = __cpu_to_le64(src->v_u64);
		break;
	case LDMS_V_F32_ARRAY:
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		dst->a_f[i] = src->v_f;
#else
		*(uint32_t *)&dst->a_f[i] = __cpu_to_le32(*(uint32_t*)&src->v_f);
#endif
		break;
	case LDMS_V_D64_ARRAY:
#if LDMS_SETH_F_LCLBYTEORDER == LDMS_SETH_F_LE
		dst->a_d[i] = src->v_d;
#else
		*(uint64_t*)&dst->a_d[i] = __cpu_to_le64(*(uint64_t*)&src->v_d);
#endif
		break;
	default:
		pr_err("Unsupported metric type %d\n", desc->vd_type);
		return;
	}
}

void __ldms_gn_inc(struct kldms_set *set, ldms_mdesc_t desc)
{
	if (desc->vd_flags & LDMS_MDESC_F_DATA) {
		LDMS_GN_INCREMENT(set->ks_data->gn);
	} else {
		LDMS_GN_INCREMENT(set->ks_meta->meta_gn);
		set->ks_data->meta_gn = set->ks_meta->meta_gn;
	}
}

void kldms_metric_array_set_val(kldms_set_t set, int metric_idx, int array_idx, ldms_mval_t src)
{
	ldms_mdesc_t desc;
	ldms_mval_t dst;

	desc = ldms_ptr_(struct ldms_value_desc, set->ks_meta,
			__le32_to_cpu(set->ks_meta->dict[metric_idx]));
	dst = ldms_ptr_(union ldms_value, set->ks_data,
			__le32_to_cpu(desc->vd_data_offset));

	__metric_array_set(set, desc, dst, array_idx, src);
	__ldms_gn_inc(set, desc);
}
EXPORT_SYMBOL(kldms_metric_array_set_val);

void kldms_metric_set(kldms_set_t s, int i, ldms_mval_t v)
{
	ldms_mdesc_t desc = ldms_ptr_(struct ldms_value_desc, s->ks_meta,
			__le32_to_cpu(s->ks_meta->dict[i]));
	ldms_mval_t mv = ldms_ptr_(union ldms_value, s->ks_data,
			__le32_to_cpu(desc->vd_data_offset));

	switch (desc->vd_type) {
	case LDMS_V_CHAR:
	case LDMS_V_U8:
	case LDMS_V_S8:
		mv->v_u8 = v->v_u8;
		break;
	case LDMS_V_U16:
	case LDMS_V_S16:
		mv->v_u16 = __cpu_to_le16(v->v_u16);
		break;
	case LDMS_V_U32:
	case LDMS_V_S32:
		mv->v_u32 = __cpu_to_le32(v->v_u32);
		break;
	case LDMS_V_U64:
	case LDMS_V_S64:
		mv->v_u64 = __cpu_to_le64(v->v_u64);
		break;
	case LDMS_V_F32:
		*(uint32_t*)&mv->v_f = __cpu_to_le32(*(uint32_t*)&v->v_f);
		break;
	case LDMS_V_D64:
		*(uint64_t*)&mv->v_d = __cpu_to_le32(*(uint64_t*)&v->v_d);
		break;
	default:
		pr_err("unexpected metric type %d.\n", desc->vd_type);
		return;
	}
	__ldms_gn_inc(s, desc);
}
EXPORT_SYMBOL(kldms_metric_set);

static char create_schema[LDMS_SET_NAME_MAX+64];
static int handle_create_schema(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp,
				loff_t *ppos)
{
	int n;
	kldms_schema_t schema;
	char schema_name[LDMS_SET_NAME_MAX];

	if (!write)
		return -EINVAL;

	if (!kldms_dev)
		return -ENODEV;

	if (*lenp > sizeof(create_schema))
		return -EFAULT;

	if (copy_from_user(create_schema, buffer, *lenp))
		return -EFAULT;

	n = sscanf(create_schema, "%127s", schema_name);
	if (n != 1)
		return -EINVAL;

	schema_name[LDMS_SET_NAME_MAX-1] = '\0';

	schema = kldms_schema_new(schema_name);
	if (schema == NULL) {
		pr_info("handle_create_schema: create failed\n");
		return -EINVAL;
	}

	return 0;
}

static char create_req[(2*LDMS_SET_NAME_MAX)+64];
static int handle_create_set(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp,
			     loff_t *ppos)
{
	int n;
	struct kldms_set *set;
	kldms_schema_t schema;
	char set_name[LDMS_SET_NAME_MAX];
	char schema_name[LDMS_SET_NAME_MAX];

	if (!write)
		return -EINVAL;

	if (!kldms_dev)
		return -ENODEV;

	if (*lenp > sizeof(create_req))
		return -EFAULT;

	if (copy_from_user(create_req, buffer, *lenp))
		return -EFAULT;

	n = sscanf(create_req, "%127s %127s", schema_name, set_name);
	if (n != 2)
		return -EINVAL;

	set_name[LDMS_SET_NAME_MAX-1] = '\0';
	schema_name[LDMS_SET_NAME_MAX-1] = '\0';

	schema = kldms_schema_find(schema_name);
	if (schema == NULL) {
		pr_info("handle_create_set: schema %s not found\n", schema_name);
		return -ENOENT;
	}

	set = kldms_set_new(set_name, schema);
	if (set == NULL) {
		pr_info("handle_create_set: set %s not found\n", set_name);
		return -EINVAL;
	}

	return 0;
}

static int handle_create_metric(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp,
				loff_t *ppos)
{
	enum ldms_value_type n;
	char metric_name[LDMS_SET_NAME_MAX];
	char schema_name[LDMS_SET_NAME_MAX];
	char metric_type[16];
	kldms_schema_t s;

	if (!write)
		return -EINVAL;

	if (!kldms_dev)
		return -ENODEV;

	if (*lenp > sizeof(create_req))
		return -EFAULT;

	if (copy_from_user(create_req, buffer, *lenp))
		return -EFAULT;

	n = sscanf(create_req, "%127s %127s %15s", schema_name, metric_name, metric_type);
	if (n != 3)
		return -EINVAL;

	s = kldms_schema_find(schema_name);
	if (s == NULL)
		return -ENOENT;

	metric_name[LDMS_SET_NAME_MAX-1] = '\0';

	/* look up metric type */
	metric_type[15] = '\0';
	for (n = 0; n < sizeof(type_names) / sizeof(type_names[0]); n++) {
		if (0 == strcmp(metric_type, type_names[n]))
			goto found_type;
	}
	return -EINVAL;

 found_type:

	if (!kldms_schema_metric_add(s, metric_name, n, ""))
		return -EINVAL;
	return 0;
}

static char set_list_req[1024];
static int handle_set_list(struct ctl_table *table, int write,
			   void __user *buffer, size_t *lenp,
			   loff_t *ppos)
{
	struct kldms_set *set, *tmp;
	size_t max_len = *lenp;
	size_t skip_len = *ppos;

	*lenp = 0;
	list_for_each_entry_safe(set, tmp, &set_list, ks_list) {
		size_t len;
		char *sz;
		sprintf(set_list_req, "%d %u %s\n", set->ks_id,
			set->ks_meta->data_sz + set->ks_meta->meta_sz,
			set->ks_name);
		len = strlen(set_list_req);
		sz = set_list_req;
		if (skip_len) {
			if (skip_len > len) {
				skip_len -= len;
				continue;
			} else {
				len -= skip_len;
				sz += skip_len;
			}
		}
		if (len > max_len)
			len = max_len;
		max_len -= len;
		*lenp = *lenp + len;
		*ppos = *ppos + len;
		if (len && copy_to_user(buffer, sz, len))
			return -EFAULT;
		buffer += len;
	}

	return 0;
}

static char metric_list_req[1024];
static int handle_metric_list(struct ctl_table *table, int write,
			      void __user *buffer, size_t *lenp,
			      loff_t *ppos)
{
	struct kldms_schema_s *schema, *tmp;
	size_t max_len = *lenp;
	size_t skip_len = *ppos;
	kldms_mdef_t m;

	*lenp = 0;
	list_for_each_entry_safe(schema, tmp, &schema_list, entry) {
		size_t len;
		char *sz;

		sprintf(metric_list_req, "%d %d %s\n", schema->metric_count, (int)schema->data_sz, schema->name);
		len = strlen(metric_list_req);
		sz = metric_list_req;
		if (skip_len) {
			if (skip_len > len) {
				skip_len -= len;
				goto skip;
			} else {
				len -= skip_len;
				sz += skip_len;
				skip_len = 0;
			}
		}
		if (len > max_len)
			len = max_len;
		max_len -= len;
		*lenp = *lenp + len;
		*ppos = *ppos + len;
		if (len && copy_to_user(buffer, sz, len))
			return -EFAULT;
		buffer += len;
	skip:
;

		list_for_each_entry(m, &schema->metric_list, entry) {
			sprintf(metric_list_req, "    %4s %s\n", type_names[m->type], m->name);
			len = strlen(metric_list_req);
			sz = metric_list_req;
			if (skip_len) {
				if (skip_len > len) {
					skip_len -= len;
					continue;
				} else {
					len -= skip_len;
					sz += skip_len;
					skip_len = 0;
				}
			}
			if (len > max_len)
				len = max_len;
			max_len -= len;
			*lenp = *lenp + len;
			*ppos = *ppos + len;
			if (len && copy_to_user(buffer, sz, len))
				return -EFAULT;
			buffer += len;
		}
	}

	return 0;
}

void kldms_set_publish(struct kldms_set *set)
{
	struct kldms_file *file, *n;
	struct kldms_request *kreq;

	if (!kldms_dev)
		return;

	kreq = kmalloc(sizeof(*kreq), GFP_KERNEL);
	if (!kreq)
		return;

	kreq->publish.hdr.req_id = KLDMS_REQ_PUBLISH_SET;
	kreq->publish.set_id = set->ks_id;
	kreq->publish.data_len = set->ks_meta->data_sz;
	INIT_LIST_HEAD(&kreq->list);
	spin_lock(&kldms_dev->req_list_lock);
	list_add_tail(&kreq->list, &kldms_dev->req_list);
	spin_unlock(&kldms_dev->req_list_lock);
	list_for_each_entry_safe(file, n, &kldms_dev->file_list, list) {
		wake_up_interruptible(&file->poll_wait);
		kill_fasync(&file->async_queue, SIGIO, POLL_IN);
	}
}
EXPORT_SYMBOL(kldms_set_publish);

static int handle_publish(struct ctl_table *table, int write,
			  void __user *buffer, size_t *lenp,
			  loff_t *ppos)
{
	int n;
	struct kldms_set *set;
	char set_number[16];
	int set_no;

	if (!write)
		return -EINVAL;

	if (!kldms_dev)
		return -ENODEV;

	if (*lenp > sizeof(set_number))
		return -EFAULT;

	if (copy_from_user(set_number, buffer, *lenp))
		return -EFAULT;

	n = sscanf(set_number, "%d", &set_no);
	if (n != 1)
		return -EINVAL;

	/* look up metric set */
	spin_lock(&set_list_lock);
	set = find_set_by_no(set_no);
	spin_unlock(&set_list_lock);
	if (!set)
		return -ENOENT;

	/* Publish set to user-mode server */
	kldms_set_publish(set);
	return 0;
}

static struct ctl_table_header *kldms_table_header;
static struct ctl_table kldms_parm_table[] = {
	{
		.procname	= "create_set",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_create_set,
	},
	{
		.procname	= "create_schema",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_create_schema,
	},
	{
		.procname	= "create_metric",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_create_metric,
	},
	{
		.procname	= "set_list",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_set_list,
	},
	{
		.procname	= "metric_list",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_metric_list,
	},
	{
		.procname	= "publish",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_publish,
	},
	{ },
};

static struct ctl_table kldms_root_table[] = {
	{
		.procname	= "kldms",
		.mode		= 0555,
		.child		= kldms_parm_table
	},
	{ },
};

enum {
	KLDMS_MAJOR = 201,
	KLDMS_BASE_MINOR = 192,
	KLDMS_MAX_DEVICES = 32
};

#define KLDMS_BASE_DEV	MKDEV(KLDMS_MAJOR, KLDMS_BASE_MINOR)

static struct class *kldms_class;

static void kldms_release_dev(struct kref *ref)
{
	struct kldms_device *kdev =
		container_of(ref, struct kldms_device, ref);
	complete(&kdev->comp);
}

static void kldms_release_file(struct kref *ref)
{
	struct kldms_file *file =
		container_of(ref, struct kldms_file, ref);
	struct kldms_device *kdev = file->device;

	module_put(kdev->cdev.owner);
	kref_put(&kdev->ref, kldms_release_dev);
	kfree(file);
}

static ssize_t kldms_file_read(struct file *filp, char __user *buf,
			       size_t count, loff_t *pos)
{
	struct kldms_file *file = filp->private_data;
	struct kldms_request *req;
	struct kldms_device *kdev;
	int reqsz;
	int ret = 0;

	if (!file)
		return -ENOENT;

	kdev = file->device;
	if (!kdev)
		return -ENODEV;

	spin_lock(&kdev->req_list_lock);
	while (list_empty(&kdev->req_list)) {
		spin_unlock(&kdev->req_list_lock);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		pr_info("Waiting %p\n", file);
		if (wait_event_interruptible(file->poll_wait,
					     !list_empty(&kdev->req_list)))
			return -ERESTARTSYS;

		spin_lock(&kdev->req_list_lock);
	}

	req = list_entry(kdev->req_list.next, struct kldms_request, list);
	pr_info("Handling req %p\n", req);

	switch (req->hdr.req_id) {
	case KLDMS_REQ_HELLO:
		reqsz = sizeof(req->hello);
		break;
	case KLDMS_REQ_PUBLISH_SET:
		reqsz = sizeof(req->publish);
		break;
	case KLDMS_REQ_UNPUBLISH_SET:
		reqsz = sizeof(req->unpub);
		break;
	case KLDMS_REQ_UPDATE_SET:
		reqsz = sizeof(req->update);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	if (reqsz > count) {
		ret   = -EINVAL;
		goto out;
	}
	list_del(kdev->req_list.next);
	spin_unlock_irq(&kdev->req_list_lock);

	if (copy_to_user(buf, &req->hdr, reqsz))
		ret = -EFAULT;
	else
		ret = reqsz;

 out:
	kfree(req);
	return ret;
}

static unsigned int kldms_file_poll(struct file *filp,
				    struct poll_table_struct *wait)
{
	unsigned int pollflags = 0;
	struct kldms_file *file = filp->private_data;
	struct kldms_device *kdev = file->device;

	poll_wait(filp, &file->poll_wait, wait);

	spin_lock_irq(&kdev->req_list_lock);
	if (!list_empty(&kdev->req_list))
		pollflags = POLLIN | POLLRDNORM;
	spin_unlock_irq(&kdev->req_list_lock);

	return pollflags;
}

static int kldms_file_fasync(int fd, struct file *filp, int on)
{
	struct kldms_file *file = filp->private_data;

	return fasync_helper(fd, filp, on, &file->async_queue);
}

static int kldms_file_close(struct inode *inode, struct file *filp)
{
	struct kldms_file *file = filp->private_data;
	struct kldms_device *kdev;

	kdev = file->device;

	/* Take the file off the device list */
	spin_lock(&kdev->file_list_lock);
	list_del(&file->list);
	spin_unlock(&kdev->file_list_lock);

	kref_put(&file->ref, kldms_release_file);
	return 0;
}

static ssize_t kldms_file_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *pos)
{
#if 0
	struct kldms_file *file = filp->private_data;
#endif
	return 0;
}

static int kldms_file_open(struct inode *inode, struct file *filp)
{
	struct kldms_device *kdev;
	struct kldms_file *file;
	int ret;

	kdev = container_of(inode->i_cdev, struct kldms_device, cdev);
	if (!kdev)
		return -ENXIO;

	spin_lock(&kdev->file_list_lock);
	if (!list_empty(&kdev->file_list)) {
		/* One file for now */
		ret = -EBUSY;
		goto err0;
	}

	kref_get(&kdev->ref);

	if (!try_module_get(kdev->cdev.owner)) {
		ret = -ENODEV;
		goto err1;
	}

	file = kmalloc(sizeof *file, GFP_KERNEL);
	if (!file) {
		ret = -ENOMEM;
		goto err2;
	}

	file->device = kdev;
	kref_init(&file->ref);
	mutex_init(&file->mutex);
	spin_lock_init(&file->lock);
	init_waitqueue_head(&file->poll_wait);
	file->is_closed = 0;
	file->async_queue = NULL;
	filp->private_data = file;

	INIT_LIST_HEAD(&file->list);
	list_add_tail(&file->list, &kdev->file_list);
	spin_unlock(&kdev->file_list_lock);

	return 0;

 err2:
	module_put(THIS_MODULE);
 err1:
	kref_put(&kdev->ref, kldms_release_dev);
 err0:
	spin_unlock(&kdev->file_list_lock);
	return ret;
}

/*
 * This service provides a mechanism for the application to allocate
 * memory that is shared between the kernel driver and the
 * application. The usage is that the kernel sends a message to the
 * application (through the read/write interface). The application
 * responds by calling mmap to cause the device to allocate, pin, and
 * map the memory that will be used to contain the metric set.
 */
static int kldms_file_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int len = vma->vm_end - vma->vm_start;
	int set_no;
	struct kldms_set *set;
	int rc;

	pr_info("%s:%d len %d\n", __func__, __LINE__, len);
	pr_info("%s:%d vm_pgoff %0lx\n", __func__, __LINE__, vma->vm_pgoff);
	pr_info("%s:%d vm_start %0lx\n", __func__, __LINE__, vma->vm_start);
	pr_info("%s:%d vm_end %0lx\n", __func__, __LINE__, vma->vm_end);

	/* Look up the set */
	set_no = vma->vm_pgoff & ~(LDMS_SET_ID_DATA >> PAGE_SHIFT);
	set_no >>= PAGE_SHIFT;
	spin_lock(&set_list_lock);
	set = find_set_by_no(set_no);
	spin_unlock(&set_list_lock);
	if (!set) {
		rc = -ENOENT;
		goto out;
	}

	pr_info("%s:%d page %p pfn %p\n", __func__, __LINE__,
	       set->ks_page, (void *)(unsigned long)page_to_pfn(set->ks_page));
	pr_info("%s:%d ks_page %p pfn %p size %d\n", __func__, __LINE__,
	       set->ks_page, (void *)(unsigned long)page_to_pfn(set->ks_page), (int)set->ks_size);
	rc = remap_pfn_range(vma, vma->vm_start, page_to_pfn(set->ks_page),
			     len, vma->vm_page_prot);

 out:
	pr_info("%s:%d rc %d\n", __func__, __LINE__, rc);
	return rc;
}

static const struct file_operations kldms_file_fops = {
	.owner	 = THIS_MODULE,
	.write	 = kldms_file_write,
	.open	 = kldms_file_open,
	.release = kldms_file_close,
	.read	 = kldms_file_read,
	.poll    = kldms_file_poll,
	.fasync  = kldms_file_fasync,
	.mmap    = kldms_file_mmap,
};

static ssize_t show_dev_abi_version(struct device *device,
				    struct device_attribute *attr, char *buf)
{
	struct kldms_device *dev = dev_get_drvdata(device);

	if (!dev)
		return -ENODEV;

	return sprintf(buf, "kldms\n");
}

static DEVICE_ATTR(abi_version, S_IRUGO, show_dev_abi_version, NULL);
#define KLDMS_ABI_VERSION "2"
static CLASS_ATTR_STRING(abi_version, S_IRUGO, KLDMS_ABI_VERSION);

static struct kldms_device *create_kldms_device(void)
{
	struct kldms_device *kdev;
	kdev = kzalloc(sizeof *kldms_dev, GFP_KERNEL);
	if (!kdev)
		return ERR_PTR(-ENOMEM);

	kref_init(&kdev->ref);
	init_completion(&kdev->comp);

	spin_lock_init(&kdev->req_list_lock);
	INIT_LIST_HEAD(&kdev->req_list);

	spin_lock_init(&kdev->file_list_lock);
	INIT_LIST_HEAD(&kdev->file_list);

	cdev_init(&kdev->cdev, NULL);
	kdev->cdev.owner = THIS_MODULE;
	kdev->cdev.ops = &kldms_file_fops;
	kdev->devnum = 0;
	kobject_set_name(&kdev->cdev.kobj, "kldms%d", kdev->devnum);
	if (cdev_add(&kdev->cdev, KLDMS_BASE_DEV, 1))
		goto err_cdev;

	kdev->dev = device_create(kldms_class, NULL,
				  kdev->cdev.dev, kdev,
				  "kldms%d", kdev->devnum);
	if (IS_ERR(kdev->dev))
		goto err_cdev;

	if (device_create_file(kdev->dev, &dev_attr_abi_version))
		goto err_class;

	return kdev;

err_class:
	pr_info("Error creating class file.\n");
	device_destroy(kldms_class, kdev->cdev.dev);

err_cdev:
	pr_info("Error creating cdev.\n");
	cdev_del(&kdev->cdev);

	kref_put(&kdev->ref, kldms_release_dev);
	wait_for_completion(&kdev->comp);
	kfree(kdev);
	return ERR_PTR(-ENOMEM);
}

static void delete_kldms_device(struct kldms_device *kdev)
{
	dev_set_drvdata(kdev->dev, NULL);
	device_destroy(kldms_class, kdev->cdev.dev);
	cdev_del(&kdev->cdev);

	kref_put(&kdev->ref, kldms_release_dev);
	wait_for_completion(&kdev->comp);
	kfree(kdev);
}

void kldms_cleanup(void)
{
	pr_info("KLDMS Module Removed\n");
	if (kldms_table_header) {
		unregister_sysctl_table(kldms_table_header);
		kldms_table_header = NULL;
	}
	delete_kldms_device(kldms_dev);
	unregister_chrdev_region(KLDMS_BASE_DEV, KLDMS_MAX_DEVICES);
	class_destroy(kldms_class);
}

int kldms_init(void)
{
	int ret;

	kldms_table_header = register_sysctl_table(kldms_root_table);

	ret = register_chrdev_region(KLDMS_BASE_DEV, KLDMS_MAX_DEVICES, "kldms");
	if (ret) {
		pr_err("kldms: couldn't register device number\n");
		goto out0;
	}
	kldms_class = class_create(THIS_MODULE, "kldms");
	if (IS_ERR(kldms_class)) {
		ret = PTR_ERR(kldms_class);
		pr_err("kldms: couldn't create class kldms\n");
		goto out1;
	}

	ret = class_create_file(kldms_class, &class_attr_abi_version.attr);
	if (ret) {
		pr_err("kldms: couldn't create abi_version attribute\n");
		goto out2;
	}

	kldms_dev = create_kldms_device();
	if (IS_ERR(kldms_dev)) {
		pr_err("kldms: couldn't create kldms interface file\n");
		ret = PTR_ERR(kldms_dev);
		goto out2;
	}

	spin_lock_init(&set_list_lock);
	INIT_LIST_HEAD(&set_list);
	spin_lock_init(&schema_list_lock);
	INIT_LIST_HEAD(&schema_list);

	pr_info("KLDMS Module Installed\n");
	return 0;
 out2:
	class_destroy(kldms_class);
 out1:
	unregister_chrdev_region(KLDMS_BASE_DEV, KLDMS_MAX_DEVICES);
 out0:
	return ret;
}
MODULE_AUTHOR("Tom Tucker <tom@opengridcomputing.com>");
MODULE_DESCRIPTION("Lightweight Distributed Metric Service");
MODULE_LICENSE("Dual BSD/GPL");
module_init(kldms_init);
module_exit(kldms_cleanup);
