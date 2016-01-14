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

#include "kldms.h"
#include "gpcdr_pub.h"
#include "gpcdr_lib.h"

#define DEFAULT_GPCDR_SAMPLE_INTERVAL 1000
static unsigned long component_id = 0;
static unsigned long sample_interval = DEFAULT_GPCDR_SAMPLE_INTERVAL;
static unsigned long min_interval = 1;
static unsigned long max_interval = 100000;
static unsigned long sampler_status;

/* the GPCD metric schema */
static kldms_schema_t gpcdr_schema;

static struct kldms_set *gpcdr_set;

static struct ctl_table_header *kldms_table_header;

static void gpcdr_gather(struct work_struct *work);
static DECLARE_DELAYED_WORK(gpcdr_work, gpcdr_gather);

static int handle_start(ctl_table *table, int write,
			void __user *buffer, size_t *lenp,
			loff_t *ppos)
{
	if (!write)
		return -EINVAL;

	if (!test_and_set_bit(0, &sampler_status)) {
		printk(KERN_INFO "ldms_gpcdr: Starting the GPCDR sampler.\n");
		schedule_delayed_work(&gpcdr_work, sample_interval);
	} else 
		printk(KERN_INFO "ldms_gpcdr: Request ignored because the sampler is already RUNNING.");

	return 0;
}

static int handle_stop(ctl_table *table, int write,
		       void __user *buffer, size_t *lenp,
		       loff_t *ppos)
{
	if (!write)
		return -EINVAL;

	if (test_and_clear_bit(0, &sampler_status)) {
		printk("Stopping the GPCDR sampler.\n");
		cancel_delayed_work(&gpcdr_work);
	} else
		printk(KERN_INFO "ldms_gpcdr: Request ignored because the sampler is already STOPPED.");
		
	return 0;
}

/*
 * entries in /proc/sys/gpcdr
 */
static ctl_table gpcdr_parm_table[] = {
	{
		.procname	= "gpcdr_sample_interval",
		.data		= &sample_interval,
		.maxlen		= sizeof(sample_interval),
		.mode		= 0644,
		.extra1		= (void *)&min_interval,
		.extra2		= (void *)&max_interval,
		.proc_handler	= proc_doulongvec_ms_jiffies_minmax
	},
	{
		.procname	= "gpcdr_component_id",
		.data		= &component_id,
		.maxlen		= sizeof(component_id),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax
	},
	{
		.procname	= "gpcdr_start",
		.data		= 0,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_start
	},
	{
		.procname	= "gpcdr_stop",
		.data		= 0,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= handle_stop
	},
	{ },
};

static ctl_table gpcdr_root_table[] = {
	{
		.procname	= "gpcdr",
		.mode		= 0555,
		.child		= gpcdr_parm_table
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

void kldms_gpcdr_cleanup(void)
{
	unreg_sysctl();
	sample_interval = 0;
	cancel_delayed_work(&gpcdr_work);
	flush_scheduled_work();
	if (gpcdr_set)
		kldms_set_delete(gpcdr_set);
	if (gpcdr_schema)
		kldms_schema_delete(gpcdr_schema);
	gpcdr_exit();
	printk(KERN_INFO "KLDMS GPCDR Module Removed\n");
}

static void gpcdr_gather(struct work_struct *work)
{
	union ldms_value v;
	int i;

	if (sample_interval)
		schedule_delayed_work(&gpcdr_work, sample_interval);

	ldms_transaction_begin(gpcdr_set);

	v.v_u64 = (uint64_t)component_id;
	kldms_metric_set(gpcdr_set, 0, &v);

	/* update register array */
	gpcdr_sample();

	for (i = 0; i < gpcdr_regs_size; i++) {
		v.v_u64 = (uint64_t)gpcdr_current_sample[i];
		kldms_metric_set(gpcdr_set, i+1, &v);
	}
	ldms_transaction_end(gpcdr_set);
}

int kldms_gpcdr_init(void)
{
	int ret = 0;
	int i;
	char *mname;

	gpcdr_init();

	kldms_table_header = register_sysctl_table(gpcdr_root_table);

	gpcdr_schema = kldms_schema_new("gpcdr");
	if (gpcdr_schema == NULL) {
		printk(KERN_ERR "ldms_gpcdr: Could not create schema\n");
		ret = -ENOMEM;
		goto err_1;
	}

	/* Add component id metric */
	ret = kldms_schema_metric_add(gpcdr_schema, "component_id", LDMS_V_U64);
	if (ret < 0) {
		ret = -ENOMEM;
		printk(KERN_ERR
		       "ldms_gpcdr: Could not create component_id "
		       "metric value err %d\n", ret);
		goto err_2;
	}

	/* Add a metric for each element in the register table */
	for (i = 0; i < gpcdr_regs_size; i++) {
		mname = valid_mmrs[i].name;
		ret = kldms_schema_metric_add(gpcdr_schema, mname, LDMS_V_U64);
		if (ret < 0) {
			printk(KERN_ERR
			       "ldms_gpcdr: Could not create '%s' "
			       "metric value err %d\n",
			       mname, -ENOMEM);
			goto err_2;
		}
		gpcdr_regs[i] = valid_mmrs[i].addr;
	}

	gpcdr_set = kldms_set_new("/gpcdr", gpcdr_schema);
	if (gpcdr_set == NULL) {
		printk(KERN_ERR "ldms_gpcdr: Could not create metric set\n");
		ret = -ENOMEM;
		goto err_2;
	}

	printk(KERN_INFO "KLDMS GPCDR Module Installed\n");
	return 0;

 err_2:
	kldms_schema_delete(gpcdr_schema);
 err_1:
	unreg_sysctl();

	return ret;
}

MODULE_AUTHOR("Tom Tucker <tom@opengridcomputing.com>");
MODULE_DESCRIPTION("LDMS GPCD Statistics Collector");
MODULE_LICENSE("Dual BSD/GPL");
module_init(kldms_gpcdr_init);
module_exit(kldms_gpcdr_cleanup);
