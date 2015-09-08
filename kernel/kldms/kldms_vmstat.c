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
#include <linux/sysctl.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmstat.h>
#include <linux/ctype.h>

#include "kldms.h"

#define DEFAULT_VMSTAT_SAMPLE_INTERVAL 1000
static unsigned long component_id = 0;
static unsigned long sample_interval = DEFAULT_VMSTAT_SAMPLE_INTERVAL;
static unsigned long min_interval = 1;
static unsigned long max_interval = 100000;
static unsigned long sampler_status;

static kldms_schema_t	vmstat_schema;
static struct kldms_set	*vmstat_set;

static char *vmstat_txt_buf;
static char ** vmstat_txt;
static int vmstat_count;

static struct ctl_table_header *kldms_table_header;

static void vmstat_gather(struct work_struct *work);
static DECLARE_DELAYED_WORK(vmstat_work, vmstat_gather);

static int handle_start(ctl_table *table, int write,
			void __user *buffer, size_t *lenp,
			loff_t *ppos)
{
	if (!write)
		return -EINVAL;

	if (!test_and_set_bit(0, &sampler_status)) {
		printk(KERN_INFO "ldms_vmstat: Starting the VMSTAT sampler.\n");
		schedule_delayed_work(&vmstat_work, sample_interval);
	} else 
		printk(KERN_INFO "ldms_vmstat: Request ignored because the sampler is already RUNNING.");

	return 0;
}

static int handle_stop(ctl_table *table, int write,
		       void __user *buffer, size_t *lenp,
		       loff_t *ppos)
{
	if (!write)
		return -EINVAL;

	if (test_and_clear_bit(0, &sampler_status)) {
		printk("Stopping the VMSTAT sampler.\n");
		cancel_delayed_work(&vmstat_work);
	} else
		printk(KERN_INFO "ldms_vmstat: Request ignored because the sampler is already STOPPED.");
		
	return 0;
}


static ctl_table vmstat_parm_table[] = {
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

static ctl_table vmstat_root_table[] = {
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

void kldms_vmstat_cleanup(void)
{
	printk(KERN_INFO "KLDMS VMSTAT Module Removed\n");
	unreg_sysctl();
	sample_interval = 0;
	cancel_delayed_work(&vmstat_work);
	flush_scheduled_work();
	if (vmstat_set)
		kldms_set_delete(vmstat_set);
	if (vmstat_schema)
		kldms_schema_delete(vmstat_schema);
	if (vmstat_txt)
		kfree(vmstat_txt);
	if (vmstat_txt_buf)
		kfree(vmstat_txt_buf);
}

int component_id_metric;

static void vmstat_gather(struct work_struct *work)
{
	int i;
	union ldms_value v;

	if (sample_interval)
		schedule_delayed_work(&vmstat_work, sample_interval);

	ldms_transaction_begin(vmstat_set);
	v.v_u64 = (uint64_t)component_id;
	kldms_metric_set(vmstat_set, 0, &v);

	for (i = 0; i < sizeof(vm_stat) / sizeof(vm_stat[0]); i++) {
		long x = atomic_long_read(&vm_stat[i]);
		v.v_u64 = x;
		kldms_metric_set(vmstat_set, i + 1, &v);
	}
	ldms_transaction_end(vmstat_set);
}

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

int parse_proc_vmstat(void)
{
	struct file *filp = NULL;
	char *vmstat_txt_buf = NULL;
	ssize_t fsize = PAGE_SIZE;
	loff_t pos = 0;
	char *s;
	mm_segment_t old_fs = get_fs();
	int i, rc;

	filp = filp_open("/proc/vmstat", O_RDONLY, 0);
	if (IS_ERR(filp))
		goto err;

	fsize = PAGE_SIZE;
	if (fsize <= 0)
		goto err;

	vmstat_txt_buf = kmalloc(fsize, GFP_KERNEL);
	if (!vmstat_txt_buf)
		goto err;
	set_fs(KERNEL_DS);
	fsize = vfs_read(filp, vmstat_txt_buf, fsize-1, &pos);
	set_fs(old_fs);
	if (fsize <= 0)
		goto err;
	vmstat_txt_buf[fsize] = '\0';

	/* Run through the list once and count the strings */
	for (s = strtok(vmstat_txt_buf, "\n", 0); s; s = strtok(NULL, "\n", 0))
		vmstat_count++;
	if (!vmstat_count)
		goto err;

	/* Allocate an array to contain the names */
	vmstat_txt = kzalloc(vmstat_count * sizeof(char *), GFP_KERNEL);
	if (!vmstat_txt)
		goto err;
	
	/* Run through the list again and put the names into the array */
	for (i = 0, s = strtok(vmstat_txt_buf, "\n", 1); s; s = strtok(NULL, "\n", 1)) {
		vmstat_txt[i] = s;		
		while (*s != '\0' && !isspace(*s))
			s++;
		*s = '\0';
		i++;
	}
	rc = 0;
	goto out;

 err:
	rc = -1;
	if (vmstat_txt)
		kfree(vmstat_txt);
	if (vmstat_txt_buf)
		kfree(vmstat_txt_buf);
	if (filp)
		filp_close(filp, current->files);
 out:
	return rc;
}

int kldms_vmstat_init(void)
{
	int ret = 0;
	int i;

	kldms_table_header =
		register_sysctl_table(vmstat_root_table);

	if (parse_proc_vmstat())
		goto err_0;

	vmstat_schema = kldms_schema_new("vmstat");
	if (vmstat_schema == NULL) {
		printk(KERN_ERR "ldms_vmstat: Could not create schema\n");
		ret = -ENOMEM;
		goto err_1;
	}

	/* Add component id metric */
	ret = kldms_schema_metric_add(vmstat_schema, "component_id", LDMS_V_U64);
	if (ret < 0) {
		ret = -ENOMEM;
		printk(KERN_ERR
		       "ldms_vmstat: Could not create component_id "
		       "metric value err %d\n", ret);
		goto err_2;
	}

	/* Add a metric for each element in the vmstat_txt table */
	for (i = 0; i < vmstat_count; i++) {
		ret = kldms_schema_metric_add(vmstat_schema, vmstat_txt[i], LDMS_V_U64);
		if (ret < 0) {
			printk(KERN_ERR
			       "ldms_vmstat: Could not create '%s' "
			       "metric value err %d\n",
			       vmstat_txt[i], -ENOMEM);
			goto err_2;
		}
	}

	/* Create a metric set */
	vmstat_set = kldms_set_new("/vmstat", vmstat_schema);
	if (vmstat_set == NULL) {
		printk(KERN_ERR "ldms_vmstat: Could not create metric set\n");
		ret = -ENOMEM;
		goto err_2;
	}

	printk(KERN_INFO "KLDMS VMSTAT Module Installed\n");
	return 0;
 err_2:
	kldms_schema_delete(vmstat_schema);
 err_1:
	kfree(vmstat_txt_buf);
	kfree(vmstat_txt);
 err_0:
	return ret;
}

MODULE_AUTHOR("Tom Tucker <tom@opengridcomputing.com>");
MODULE_DESCRIPTION("LDMS Virtual Memory Statistics Collector");
MODULE_LICENSE("Dual BSD/GPL");
module_init(kldms_vmstat_init);
module_exit(kldms_vmstat_cleanup);
