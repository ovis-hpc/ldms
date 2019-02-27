/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file procnetdev.c
 * \brief procnetdev sampler.
 */

#include <net/if.h>

#include "coll/rbt.h"

#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_sampler.h"

static
const char *procnetdev_desc(ldmsd_plugin_inst_t i);
static
const char *procnetdev_help(ldmsd_plugin_inst_t i);
static
int procnetdev_init(ldmsd_plugin_inst_t i);
static
void procnetdev_del(ldmsd_plugin_inst_t i);
static
int procnetdev_config(ldmsd_plugin_inst_t i, struct attr_value_list *avl,
		struct attr_value_list *kwl, char *ebuf, int ebufsz);
static
int procnetdev_update_schema(ldmsd_plugin_inst_t inst, ldms_schema_t schema);

static
int procnetdev_update_set(ldmsd_plugin_inst_t i, ldms_set_t set, void *ctxt);

/** For specifying wanted interfaces. */
struct if_rbn_s {
	struct rbn rbn;
	char name[IFNAMSIZ]; /* from net/if.h */
};
typedef struct if_rbn_s *if_rbn_t;

/**
 * procnetdev plugin instance extending ::ldmsd_plugin_inst_s.
 */
struct procnetdev_inst_s {
	/** Instance base structure. */
	struct ldmsd_plugin_inst_s base;

	/** `/proc/net/dev` file handle. */
	FILE *f;

	/** The device to sample. */
	char *dev;
};
typedef struct procnetdev_inst_s *procnetdev_inst_t;

static
struct procnetdev_inst_s __inst = {
	.base = {
		.version.version = LDMSD_PLUGIN_VERSION,

		.type_name   = "sampler",
		.plugin_name = "procnetdev",

		.desc   = procnetdev_desc,
		.help   = procnetdev_help,
		.init   = procnetdev_init,
		.del    = procnetdev_del,
		.config = procnetdev_config,
	},
};

void *new()
{
	procnetdev_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return inst;
}

static
int procnetdev_init(ldmsd_plugin_inst_t i)
{
	procnetdev_inst_t inst = (void*)i;
	ldmsd_sampler_type_t samp = (void*)i->base;
	samp->update_schema = procnetdev_update_schema;
	samp->update_set = procnetdev_update_set;

	inst->f = fopen("/proc/net/dev", "r");
	if (!inst->f)
		return errno;
	setbuf(inst->f, NULL); /* set no buffer */

	return 0;
}

static
const char *procnetdev_desc(ldmsd_plugin_inst_t i)
{
	return "procnetdev (/proc/net/dev) sampler.";
}

const char *_help = "\
procnetdev config sysnopsis:\n\
    config name=INST [COMMON_OPTIONS] dev=DEVICE\n\
\n\
Descriptions:\n\
    Configure `procnetdev` sampler plugin instance `INST` to sample metrics\n\
    for network device `DEVICE`. Both `name` and `dev` attributes are \n\
    required. procnetdev plugin currently supports only single device per\n\
    plugin instance.\n\
\n\
Example:\n\
    load name=net_lo plugin=procnetdev\n\
    config name=net_lo dev=lo\n\
    start name=net_lo\n\
";

static
const char *procnetdev_help(ldmsd_plugin_inst_t i)
{
	return _help;
}

static
void procnetdev_del(ldmsd_plugin_inst_t i)
{
	procnetdev_inst_t inst = (void*)i;
	ldmsd_sampler_type_t samp = (void*)i->base;

	pthread_mutex_lock(&samp->lock);
	if (inst->f) {
		fclose(inst->f);
		inst->f = NULL;
	}
	pthread_mutex_unlock(&samp->lock);
}

static
int procnetdev_config(ldmsd_plugin_inst_t i, struct attr_value_list *avl,
		      struct attr_value_list *kwl, char *ebuf, int ebufsz)
{
	procnetdev_inst_t inst = (void*)i;
	ldmsd_sampler_type_t samp = (void*)i->base;
	ldms_set_t set;
	char *val;
	int rc;

	rc = samp->base.config(i, avl, kwl, ebuf, ebufsz);
	if (rc)
		goto out;

	val = av_value(avl, "dev");
	if (!val) {
		rc = EINVAL;
		snprintf(ebuf, ebufsz, "`dev=DEVICE` attribute is required.\n");
		goto out;
	}
	inst->dev = strdup(val);
	if (!inst->dev) {
		rc = ENOMEM;
		snprintf(ebuf, ebufsz, "Not enough memory.\n");
		goto out;
	}

	if (samp->schema)
		ldms_schema_delete(samp->schema);
	samp->schema = samp->create_schema(i);
	if (!samp->schema) {
		rc = errno;
		snprintf(ebuf, ebufsz, "Cannot create schema, rc: %d.\n", rc);
		goto out;
	}

	set = samp->create_set(i, samp->set_inst_name, samp->schema, NULL);
	if (!set) {
		rc = errno;
		snprintf(ebuf, ebufsz, "Cannot create set, rc: %d.\n", rc);
		goto out;
	}

 out:
	return rc;
}

static
struct {
	const char *name;
	const char *units;
} metric_entries[] = {
	{ "rx_bytes"      , "bytes"   }  ,
	{ "rx_packets"    , "packets" }  ,
	{ "rx_errs"       , "pakcets"   }  ,
	{ "rx_drop"       , "packets"   }  ,
	{ "rx_fifo"       , "pakcets"   }  ,
	{ "rx_frame"      , "pakcets"  }  ,
	{ "rx_compressed" , "packets"   }  ,
	{ "rx_multicast"  , "packets"   }  ,
	{ "tx_bytes"      , "bytes"   }  ,
	{ "tx_packets"    , "packets" }  ,
	{ "tx_errs"       , "packets"   }  ,
	{ "tx_drop"       , "packets"   }  ,
	{ "tx_fifo"       , "packets"   }  ,
	{ "tx_colls"      , "packets"   }  ,
	{ "tx_carrier"    , "packets"   }  ,
	{ "tx_compressed" , "packets"   }  ,
	{ NULL            , NULL      }
};
/* NOTE:
 *   According to `<linuxtree>/net/core/net-procfs.c` and
 *   `<linuxtree>/include/uapi/linux/if_link.h` it seems like all metrics are in
 *   "packets", except for rx_bytes and tx_bytes.
 */

static
int procnetdev_update_schema(ldmsd_plugin_inst_t i, ldms_schema_t sch)
{
	const char *name;
	const char *units;
	int idx, midx;
	for (idx = 0; metric_entries[idx].name; idx++) {
		name = metric_entries[idx].name;
		units = metric_entries[idx].units;
		midx = ldms_schema_metric_add(sch, name, LDMS_V_U64, units);
		if (midx < 0)
			return -midx; /* midx is -errno */
	}
	return 0;
}

static
int procnetdev_update_set(ldmsd_plugin_inst_t i, ldms_set_t set, void *ctxt)
{
	procnetdev_inst_t inst = (void*)i;
	ldmsd_sampler_type_t samp = (void*)i->base;
	int rc, n, idx;
	char name[IFNAMSIZ];
	char buff[256];
	uint64_t val[16]; /* 16 metrics / device */

	rc = fseek(inst->f, 0, SEEK_SET);
	if (rc)
		return errno;
	/* discard first two lines */
	fgets(buff, sizeof(buff), inst->f);
	fgets(buff, sizeof(buff), inst->f);

	/* look for the device we want */
	rc = ENOENT;
	while (fgets(buff, sizeof(buff), inst->f)) {
		sscanf(buff, " %[^:]: %n", name, &n);
		/* skip unwanted interface */
		if (strcmp(name, inst->dev))
			continue;
		rc = 0;
		break;
	}
	if (rc)
		return rc;
	n = sscanf(buff + n,
		   "%lu %lu %lu %lu %lu"
		   "%lu %lu %lu %lu %lu"
		   "%lu %lu %lu %lu %lu"
		   "%lu\n",
		   &val[0],  &val[1],  &val[2],  &val[3],  &val[4],
		   &val[5],  &val[6],  &val[7],  &val[8],  &val[9],
		   &val[10], &val[11], &val[12], &val[13], &val[14],
		   &val[15]);
	if (n < 16) {
		ldmsd_log(LDMSD_LINFO, "%s: wrong number of fields in sscanf\n",
				       i->inst_name);
		return EINVAL; /* bad format */
	}

	for (idx = 0; idx < 16; idx++) {
		ldms_metric_set_u64(set, samp->first_idx+idx, val[idx]);
	}

	return 0;
}
