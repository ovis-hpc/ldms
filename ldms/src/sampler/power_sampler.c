/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
/**
 * \file power_sampler.c
 * \author Narate Taerat (narate at ogc dot us)
 */

#include <stdlib.h>
#include <errno.h>

#include "ldms.h"
#include "ldmsd.h"
#include "timer_base.h"
#include "pwr.h"

struct power_sampler {
	struct timer_base base;
	PWR_Obj pow;
	PWR_Cntxt powctxt;
	struct timeval hfinterval;
	int hfcount;
};

static ldmsd_msg_log_f msglog;

static
void power_sampler_timer_cb(tsampler_timer_t t)
{
	int m;
	ldms_mval_t mv;
	PWR_AttrName pattr;
	PWR_Time pow_time;
	double pv;
	int rc;
	struct power_sampler *ps;

	/* t->sampler, t->set and t->mid (metric-id) are set when the time is
	 * setup in create_metric_set().
	 *
	 * t->idx is the current index in the array provided by timer. This will
	 * keep modulo-increasing call-after-call (per timer).
	 * */

	ps = (void*)t->sampler;

	pattr = (uint64_t)t->ctxt; /* abuse ctxt to hold power attr */
	rc = PWR_ObjAttrGetValue(ps->pow, pattr, &pv, &pow_time);
	if (rc != 0) {
		pv = rc;
	}

	ldms_metric_array_set_double(t->set, t->mid, t->idx, pv);
}

static
int power_sampler_config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *v;
	int rc;
	uint64_t x;
	struct power_sampler *ps = (void*)self;
	struct timeval tv = {0, 100000};

	rc = timer_base_config(self, kwl, avl, msglog);
	if (rc) {
		goto out;
	}

	/* set default values */
	ps->hfinterval.tv_sec = 0;
	ps->hfinterval.tv_usec = 100000;
	ps->hfcount = 600;

	v = av_value(avl, "hfinterval");
	if (v) {
		x = strtoull(v, NULL, 0);
		if (!x) {
			rc = EINVAL;
			goto cleanup;
		}
		ps->hfinterval.tv_sec = x / 1000000;
		ps->hfinterval.tv_usec = x % 1000000;
	}

	v = av_value(avl, "hfcount");
	if (v) {
		ps->hfcount = strtol(v, NULL, 0);
		if (!ps->hfcount) {
			rc = EINVAL;
			goto cleanup;
		}
	}
// Brandt 4-30-2016 Removed due to lack of current interest. Will make configurable
/*	rc = timer_base_add_hfmetric(&ps->base, "PWR_ATTR_POWER_LIMIT_MAX",
				LDMS_V_D64_ARRAY, ps->hfcount, &ps->hfinterval,
				power_sampler_timer_cb,
				(void*)(uint64_t)PWR_ATTR_POWER_LIMIT_MAX);
	if (rc)
		goto cleanup;
*/
	rc = timer_base_add_hfmetric(&ps->base, "PWR_ATTR_POWER",
				LDMS_V_D64_ARRAY, ps->hfcount, &ps->hfinterval,
				power_sampler_timer_cb,
				(void*)(uint64_t)PWR_ATTR_POWER);
	if (rc)
		goto cleanup;

/*	rc = timer_base_add_hfmetric(&ps->base, "PWR_ATTR_ENERGY",
				LDMS_V_D64_ARRAY, ps->hfcount, &ps->hfinterval,
				power_sampler_timer_cb,
				(void*)(uint64_t)PWR_ATTR_ENERGY);
	if (rc)
		goto cleanup;
*/

	rc = timer_base_create_set(&ps->base);
	if (rc) {
		goto cleanup;
	}
	goto out;

cleanup:
	timer_base_cleanup(&ps->base);
out:
	return rc;
}

static
void power_sampler_cleanup(struct power_sampler *ps)
{
	/* terminate timers */
	timer_base_cleanup(&ps->base);

	/* terminate pow after timers */
	if (ps->pow) {
		/* TODO see if pow really needs no cleanup */
		ps->pow = NULL;
	}
	if (ps->powctxt) {
		PWR_CntxtDestroy(ps->powctxt);
	}
}

static
void power_sampler_term(struct ldmsd_plugin *self)
{
	power_sampler_cleanup((void*)self);
}

static const char *power_sampler_usage(struct ldmsd_plugin *self)
{
	return  "config name=power_sampler producer=<prod_name> instance=<inst_name>"
		" [hfinterval=<hfinterval>] [hfcount=<hfcount>] "
		" [component_id=<compid>] [schema=<sname>] [with_jobid=(0|1)]\n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <hfinterval> (Optional) the high-frequency interval (micro second, default: 100000).\n"
		"    <hfcount>    (Optional) the number of elements in the ring buffer (default: 600).\n"
		"    <compid>     (Optional) unique number identifier. Defaults to zero.\n"
		"    <sname>      (Optional) schema name. Defaults to 'sampler_timer'\n"
		"    with_jobid   (Optional) enable(1) or disable(0) job info lookup (default: 1).\n"
		;
}

static int power_sampler_sample(struct ldmsd_sampler *self)
{
	return timer_base_sample(self);
}

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	int rc;
	msglog = pf;
	struct power_sampler *ps = calloc(1, sizeof(*ps));
	if (!ps)
		return NULL;

	/* power stuff */
	rc = PWR_CntxtInit(PWR_CNTXT_DEFAULT, PWR_ROLE_APP, "ldmsd", &ps->powctxt);
	if (rc)
		goto cleanup;
	rc = PWR_CntxtGetEntryPoint(ps->powctxt, &ps->pow);
	if (rc)
		goto cleanup;
	timer_base_init(&ps->base);

	/* override */
	ps->base.base.base.usage = power_sampler_usage;
	ps->base.base.base.term = power_sampler_term;
	ps->base.base.base.config = power_sampler_config;
	ps->base.base.sample = power_sampler_sample;
	snprintf(ps->base.base.base.name, sizeof(ps->base.base.base.name),
			"power_sampler");

	goto out;

cleanup:
	power_sampler_cleanup(ps);
	ps = NULL;
out:
	return (void*)ps;
}
