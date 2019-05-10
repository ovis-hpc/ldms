/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2016,2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2016,2018-2019 Open Grid Computing, Inc. All rights
 * reserved.
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
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>
#include <sys/queue.h>
#include <assert.h>
#include <fcntl.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define NRAW_FIELD 11
#define NDERIVED_FIELD 2

#define NFIELD (NRAW_FIELD + NDERIVED_FIELD)

struct {
	const char *name;
	const char *unit;
} metrics[] = {
	{  "reads_comp",       "times"    },
	{  "reads_merg",       "times"    },
	{  "sect_read",        "sectors"  },  /* SECT_READ_IDX points here */
	{  "time_read",        "msec"     },
	{  "writes_comp",      "times"    },
	{  "writes_merg",      "times"    },
	{  "sect_written",     "sectors"  },  /* SECT_WRITTEN_IDX points here */
	{  "time_write",       "msec"     },
	{  "ios_in_progress",  ""         },
	{  "time_ios",         "msec"     },
	{  "weighted_time",    "msec"     },

	/* derived */
	{  "disk.byte_read",     "bytes"  },  /* sect_read * sector_size */
	{  "disk.byte_written",  "bytes"  },  /* sect_written * sector_size */
};

#define SECT_READ_IDX 2
#define SECT_WRITTEN_IDX 6
#define SECT_READ_BYTES_IDX 11
#define SECT_WRITTEN_BYTES_IDX 12

typedef struct sysclassblock_inst_s *sysclassblock_inst_t;
struct sysclassblock_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */

	char *dev;
	int fd;
	size_t sect_sz;
	char buff[512];
	uint64_t v[NRAW_FIELD];
};

/* ============== Sampler Plugin APIs ================= */

static
int sysclassblock_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	int i, idx;
	for (i = 0; i < NFIELD; i++) {
		idx = ldms_schema_metric_add(schema, metrics[i].name,
				LDMS_V_U64, metrics[i].unit);
		if (idx < 0)
			return -idx;
	}
	return 0;
}

static
int sysclassblock_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set,
			      void *ctxt)
{
	sysclassblock_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int i, n;
	off_t off;
	ssize_t sz;
	off = lseek(inst->fd, 0, SEEK_SET);
	if (off == -1) {
		ldmsd_log(LDMSD_LERROR, "%s: seek error, errno: %d\n",
			  pi->inst_name, errno);
		return errno;
	}
	sz = read(inst->fd, inst->buff, sizeof(inst->buff));
	if (sz < 0) {
		ldmsd_log(LDMSD_LERROR, "%s: read error, errno: %d\n",
			  pi->inst_name, errno);
		return errno;
	}
	n = sscanf(inst->buff, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
		   &inst->v[0], &inst->v[1], &inst->v[2], &inst->v[3],
		   &inst->v[4], &inst->v[5], &inst->v[6], &inst->v[7],
		   &inst->v[8], &inst->v[9], &inst->v[10]);
	if (n < NRAW_FIELD) {
		/* bad format */
		ldmsd_log(LDMSD_LERROR, "%s: bad data format: %s",
			  pi->inst_name, inst->buff);
		return EINVAL;
	}
	for (i = 0; i < NRAW_FIELD; i++) {
		ldms_metric_set_u64(set, samp->first_idx + i, inst->v[i]);
	}
	ldms_metric_set_u64(set, samp->first_idx + SECT_READ_BYTES_IDX,
			    inst->v[SECT_READ_IDX] * inst->sect_sz);
	ldms_metric_set_u64(set, samp->first_idx + SECT_WRITTEN_BYTES_IDX,
			    inst->v[SECT_WRITTEN_IDX] * inst->sect_sz);
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *sysclassblock_desc(ldmsd_plugin_inst_t pi)
{
	return "sysclassblock - /sys/class/DEV/block/stat data sampler";
}

static const char *_help = "\
sysclassblock synopsis:\n\
    config name=<INST> [COMMON_OPTIONS] dev=<DEV>\n\
\n\
Option descriptions:\n\
    dev  The name of the block device to be monitored (e.g. sda)\n\
\n\
";

static
const char *sysclassblock_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void sysclassblock_del(ldmsd_plugin_inst_t pi)
{
	sysclassblock_inst_t inst = (void*)pi;
	if (inst->fd >= 0)
		close(inst->fd);
	if (inst->dev)
		free(inst->dev);
}

static
int sysclassblock_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	sysclassblock_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	const char *dev;
	int rc;
	ssize_t sz;
	int bufsz = PATH_MAX;
	char *buff = malloc(bufsz);
	int bd = -1;
	int dd = -1;
	int fd = -1;

	if (!buff)
		return ENOMEM;

	if (inst->fd >= 0) {
		snprintf(ebuf, ebufsz, "%s: already configured\n",
			 pi->inst_name);
		rc = EALREADY;
		goto out;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		goto out;

	/* dev or device for backward comp */
	dev = json_attr_find_str(json, "dev");
	if (!dev)
		dev = json_attr_find_str(json, "device");
	if (!dev) {
		snprintf(ebuf, ebufsz, "%s: `dev` attribute is needed\n",
			 pi->inst_name);
		rc = EINVAL;
		goto out;
	}

	inst->dev = strdup(dev);
	if (!inst->dev) {
		snprintf(ebuf, ebufsz, "%s: out of memory\n",
			 pi->inst_name);
		rc = ENOMEM;
		goto out;
	}

	/* determine device/parition location in /sys tree */
	bd = open("/sys/class/block", O_RDONLY|O_DIRECTORY);
	if (bd < 0) {
		snprintf(ebuf, ebufsz, "%s: open dir (/sys/class/block) error, "
			 "errno: %d\n", pi->inst_name, errno);
		rc = errno;
		goto out;
	}
	sz = readlinkat(bd, dev, buff, PATH_MAX);
	if (sz == PATH_MAX) {
		snprintf(ebuf, ebufsz, "%s: device path too long\n",
			 pi->inst_name);
		rc = ENAMETOOLONG;
		goto out;
	}
	if (sz < 0) {
		snprintf(ebuf, ebufsz, "%s: read device link error, "
			 "errno: %d\n", pi->inst_name, errno);
		rc = errno;
		goto out;
	}

	/* get sector size */
	dd = openat(bd, buff, O_RDONLY|O_DIRECTORY);
	if (dd == -1) {
		snprintf(ebuf, ebufsz, "%s: cannot open dir `%s`, error: %d\n" ,
			 pi->inst_name, buff, errno);
		rc = errno;
		goto out;
	}

	fd = openat(dd, "queue/hw_sector_size", O_RDONLY);
	if (fd == -1) {
		fd = openat(dd, "../queue/hw_sector_size", O_RDONLY);
	}
	if (fd >= 0) {
		/* OK to reuse buff here, the dir has been opened */
		sz = read(fd, buff, bufsz);
		if (sz < 0) {
			snprintf(ebuf, ebufsz, "%s: sector read error, "
				 "errno: %d\n", pi->inst_name, errno);
			rc = errno;
			goto out;
		}
		inst->sect_sz = atol(buff);
	} else {
		ldmsd_log(LDMSD_LWARNING, "%s; cannot determine sector size "
			  "for device `%s`, use 512 byte\n",
			  pi->inst_name, dev);
		inst->sect_sz = 512;
	}

	inst->fd = openat(dd, "stat", O_RDONLY);
	if (inst->fd < 0) {
		snprintf(ebuf, ebufsz, "%s: cannot open stat file for `%s`, "
			 "error: %d\n" ,
			 pi->inst_name, dev, errno);
		rc = errno;
		goto out;
	}

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema) {
		rc = errno;
		goto out;
	}
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set) {
		rc = errno;
		goto out;
	}
	rc = 0;

out:
	if (buff)
		free(buff);
	if (bd >= 0)
		close(bd);
	if (dd >= 0)
		close(dd);
	if (fd >= 0)
		close(fd);
	return rc;
}

static
int sysclassblock_init(ldmsd_plugin_inst_t pi)
{
	sysclassblock_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = sysclassblock_update_schema;
	samp->update_set = sysclassblock_update_set;

	inst->fd = -1;
	inst->sect_sz = 512;
	inst->dev = NULL;
	return 0;
}

static
struct sysclassblock_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "sysclassblock",

                /* Common Plugin APIs */
		.desc   = sysclassblock_desc,
		.help   = sysclassblock_help,
		.init   = sysclassblock_init,
		.del    = sysclassblock_del,
		.config = sysclassblock_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	sysclassblock_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
