/*
 * Copyright (c) 2010,2015 Open Grid Computing, Inc. All rights reserved.
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
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/vmstat.h>
#include <linux/ctype.h>

#include "kldms.h"

#define DEFAULT_VMSTAT_SAMPLE_INTERVAL HZ
#define COMP_ID_IDX		0
#define JOB_ID_IDX		1
#define APP_ID_IDX		2
#define FIRST_VMSTAT_IDX	3

static unsigned long component_id = 0;
static unsigned long job_id = 0;
static unsigned long app_id = 0;
static unsigned long sample_interval = DEFAULT_VMSTAT_SAMPLE_INTERVAL;
static unsigned long min_interval = 1;
static unsigned long max_interval = 100000;
static unsigned long sampler_status;
static char instance_name[256];
static char schema_name[256];
static struct file *filp = NULL;

static char vmstat_buf[2 * PAGE_SIZE];
static ssize_t vmstat_buf_size = sizeof(vmstat_buf);
static struct kldms_set	*vmstat_set;

static struct ctl_table_header *kldms_table_header;

static void vmstat_gather(struct work_struct *work);
static DECLARE_DELAYED_WORK(vmstat_work, vmstat_gather);

static int issep(char c, char *sep)
{
	while (*sep) {
		if (*sep == c)
			return 1;
		sep++;
	}
	return 0;
}

static char *strtok(char *b, char *sep, int z)
{
	static char *_b, *rv;
	char *s;
	if (b)
		_b = b;

	if (*_b == '\0')
		return NULL;

	rv = s = _b;
	while (!issep(*s, sep) && *s != '\0')
		s++;
	if (*s != '\0') {
		if (z)
			*s = '\0';
		_b = s+1;
	} else
		_b = s;
	return rv;
}

static int create_set(void)
{
	int i, ret;
	loff_t pos = 0;
	char *s;
	const char *name;
	kldms_schema_t vmstat_schema;

	if (vmstat_set)
		return 0;

	filp = filp_open("/proc/vmstat", O_RDONLY, 0);
	if (IS_ERR(filp)) {
		ret = -ENOENT;
		goto err_0;
	}

	vmstat_schema = kldms_schema_new(schema_name);
	name = schema_name;
	if (vmstat_schema == NULL) {
		pr_err("kldms_vmstat: Could not create schema\n");
		ret = -ENOMEM;
		goto err_1;
	}

	/* Add component id metric */
	name = "component_id";
	ret = kldms_schema_metric_add(vmstat_schema, name, LDMS_V_U64, "");
	if (ret < 0) {
		ret = -ENOMEM;
		goto err_2;
	}

	name = "job_id";
	ret = kldms_schema_metric_add(vmstat_schema, name, LDMS_V_U64, "");
	if (ret < 0) {
		ret = -ENOMEM;
		goto err_2;
	}

	name = "app_id";
	ret = kldms_schema_metric_add(vmstat_schema, name, LDMS_V_U64, "");
	if (ret < 0) {
		ret = -ENOMEM;
		goto err_2;
	}

	ret  = kernel_read(filp, vmstat_buf, vmstat_buf_size-1, &pos);
	if (ret <= 0)
		goto err_2;
	vmstat_buf[ret] = '\0';

	/* Run through the file and add the metric names to the set */
	for (i = 0, s = strtok(vmstat_buf, "\n", 1); s; s = strtok(NULL, "\n", 1)) {
		char *name = s;
		while (*s != '\0' && !isspace(*s))
			s++;
		*s = '\0';
		pr_info("Adding metric '%s' to vmstat\n", name);
		ret = kldms_schema_metric_add(vmstat_schema, name, LDMS_V_U64, "");
		if (ret < 0)
			goto err_2;
		i++;
	}

	/* Create a metric set */
	vmstat_set = kldms_set_new(instance_name, vmstat_schema);
	if (vmstat_set == NULL) {
		ret = -ENOMEM;
		goto err_2;
	}

	kldms_schema_delete(vmstat_schema);
	kldms_set_publish(vmstat_set);
	return 0;
 err_2:
	pr_err("kldms_vmstat: Error %d creating resource %s\n", ret, name);
	kldms_schema_delete(vmstat_schema);
 err_1:
	filp_close(filp, current->files);
 err_0:
	return ret;
}

static int handle_start(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp,
			loff_t *ppos)
{
	if (!write)
		return -EINVAL;

	if (create_set())
		return -ENOENT;

	if (!test_and_set_bit(0, &sampler_status)) {
		pr_info("ldms_vmstat: Starting the VMSTAT sampler.\n");
		schedule_delayed_work(&vmstat_work, sample_interval);
	} else
		pr_info("ldms_vmstat: Request ignored because "
		       "the sampler is already RUNNING.");

	return 0;
}

static int handle_stop(struct ctl_table *table, int write,
		       void __user *buffer, size_t *lenp,
		       loff_t *ppos)
{
	if (!write)
		return -EINVAL;

	if (test_and_clear_bit(0, &sampler_status)) {
		pr_info("Stopping the VMSTAT sampler.\n");
		cancel_delayed_work(&vmstat_work);
	} else {
		pr_info("ldms_vmstat: Request ignored because "
			"the sampler is already STOPPED.");
	}

	return 0;
}


struct ctl_table vmstat_parm_table[] = {
	{
		.procname	= "vmstat_instance_name",
		.data		= &instance_name,
		.maxlen		= sizeof(instance_name),
		.mode		= 0644,
		.proc_handler	= proc_dostring
	},
	{
		.procname	= "vmstat_schema_name",
		.data		= &schema_name,
		.maxlen		= sizeof(schema_name),
		.mode		= 0644,
		.proc_handler	= proc_dostring
	},
	{
		.procname	= "vmstat_sample_interval",
		.data		= &sample_interval,
		.maxlen		= sizeof(sample_interval),
		.mode		= 0644,
		.extra1		= (void *)&min_interval,
		.extra2		= (void *)&max_interval,
		.proc_handler	= proc_doulongvec_ms_jiffies_minmax
	},
	{
		.procname	= "vmstat_component_id",
		.data		= &component_id,
		.maxlen		= sizeof(component_id),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax
	},
	{
		.procname	= "vmstat_job_id",
		.data		= &job_id,
		.maxlen		= sizeof(job_id),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax
	},
	{
		.procname	= "vmstat_app_id",
		.data		= &app_id,
		.maxlen		= sizeof(app_id),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax
	},
	{
		.procname	= "vmstat_start",
		.data		= 0,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_start
	},
	{
		.procname	= "vmstat_stop",
		.data		= 0,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_stop
	},
	{ },
};

static struct ctl_table vmstat_root_table[] = {
	{
		.procname	= "kldms",
		.mode		= 0555,
		.child		= vmstat_parm_table
	},
	{ },
};

static void unreg_sysctl(void)
{
	if (kldms_table_header) {
		unregister_sysctl_table(kldms_table_header);
		kldms_table_header = NULL;
	}
}

static void kldms_vmstat_cleanup(void)
{
	pr_info("KLDMS VMSTAT Module Removed\n");
	unreg_sysctl();
	sample_interval = 0;
	cancel_delayed_work(&vmstat_work);
	flush_scheduled_work();
	if (vmstat_set)
		kldms_set_delete(vmstat_set);
}

static void vmstat_gather(struct work_struct *work)
{
	int rc, i, m_idx;
	union ldms_value v;
	loff_t pos = 0;
	char *s;

	if (sample_interval)
		schedule_delayed_work(&vmstat_work, sample_interval);

	pr_debug("Reading vmstat from file %p\n", filp);
	kldms_transaction_begin(vmstat_set);

	v.v_u64 = (uint64_t)component_id;
	kldms_metric_set(vmstat_set, COMP_ID_IDX, &v);

	v.v_u64 = (uint64_t)job_id;
	kldms_metric_set(vmstat_set, JOB_ID_IDX, &v);

	v.v_u64 = (uint64_t)app_id;
	kldms_metric_set(vmstat_set, APP_ID_IDX, &v);

	m_idx = FIRST_VMSTAT_IDX;

	rc = kernel_read(filp, vmstat_buf, vmstat_buf_size-1, &pos);
	if (rc <= 0)
		goto err;
	vmstat_buf[rc] = '\0';

	/* Run through the file and set the metric values in the set */
	for (i = 0, s = strtok(vmstat_buf, "\n", 1); s; s = strtok(NULL, "\n", 1)) {
		while (*s != '\0' && !isspace(*s))
			s++;
		while (*s != '\0' && !isdigit(*s))
			s++;
		if (kstrtou64(s, 0, &v.v_u64))
			v.v_u64 = 0;
		kldms_metric_set(vmstat_set, m_idx++, &v);
	}
 err:
	kldms_transaction_end(vmstat_set);
}

static int kldms_vmstat_init(void)
{
	kldms_table_header =
		register_sysctl_table(vmstat_root_table);

	strcpy(instance_name, "vmstat"); /* default set name */
	strcpy(schema_name, "vmstat"); /* default schema name */

	pr_info("KLDMS VMSTAT Module Installed\n");
	return 0;
}

MODULE_AUTHOR("Tom Tucker <tom@opengridcomputing.com>");
MODULE_DESCRIPTION("LDMS Virtual Memory Statistics Collector");
MODULE_LICENSE("Dual BSD/GPL");
module_init(kldms_vmstat_init);
module_exit(kldms_vmstat_cleanup);
