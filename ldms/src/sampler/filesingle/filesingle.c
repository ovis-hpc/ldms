/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018-2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file filesingle.c
 * \brief reads from named input files that contain only a single metric
 * and may be missing.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

#define LINEMAX 1023
#define NAMEMAX 127
#define TYPEMAX 31
#define VALMAX 31

#define SAMP "filesingle"

struct single {
	char file[LINEMAX+1];
	char name[NAMEMAX+1];
	char nametime[NAMEMAX+6];
	union ldms_value missing_val; /* val if open/read fails */
	enum ldms_value_type t;
	TAILQ_ENTRY(single) entry;
	union ldms_value val; /* most recent read */
	int64_t collect_time; /* how long open/read/close took */
};

typedef struct filesingle_context {
        bool configured;
        ovis_log_t mylog;
        ldms_set_t set;
        int metric_offset; /* Location of first metric from user file */
        int collect_times;
        base_data_t base;
        TAILQ_HEAD(single_list, single) metric_list;
} *filesingle_context_t;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

static void clear_metric_list(filesingle_context_t fsc)
{
	while (!TAILQ_EMPTY(&fsc->metric_list)) {
		struct single *s = TAILQ_FIRST(&fsc->metric_list);
		TAILQ_REMOVE(&fsc->metric_list, s, entry);
		free(s);
	}
}

static void errusage(filesingle_context_t fsc, const char *l, int lno)
{
	ovis_log(fsc->mylog, OVIS_LERROR, "Parsing error in line %d: %s\n", lno, l);
	ovis_log(fsc->mylog, OVIS_LERROR, "Expecting: <name> <path> <type> <default>\n");
	ovis_log(fsc->mylog, OVIS_LERROR, "where type is one of U[8,16,32,64] or S[8,16,32,64] or F32 or F64 or CHAR.\n");
}

/* parse lines of: name source type default */
static int parse_single_conf(filesingle_context_t fsc, const char *conf)
{
	if (!conf)
		return EINVAL;
	int rc = 0;
	FILE *in = fopen(conf, "r");
	if (!in) {
		rc = errno;
		ovis_log(fsc->mylog, OVIS_LERROR, "Cannot open %s\n", conf);
		return errno;
	}
	char *line = NULL;
	int lno = 0;
	char name[NAMEMAX+1];
	char file[LINEMAX+1];
	char defstr[VALMAX+1];
	char typestr[TYPEMAX+1];
	char linebuf[LINEMAX+1];

	while (1) {
		struct single *metric = NULL;
		lno++;
		line = fgets(linebuf, LINEMAX, in);
		if (!line) {
			break;
		}
		while (line < linebuf + LINEMAX && *line != '\0' && isspace(*line))
			line++;
		if (line < linebuf + LINEMAX && line[0] == '#')
			continue; /* skip comments */
		int nitems = sscanf(line, "%" stringify(NAMEMAX) "s "
			       "%" stringify(LINEMAX) "s "
			       "%" stringify(TYPEMAX) "s "
			       "%" stringify(VALMAX) "s",
				name, file, typestr, defstr);
		if (nitems < 4) {
			errusage(fsc, line, lno);
			rc = EINVAL;
			goto out;
		}
		char *u = typestr;
		while (*u != '\0') {
			*u = toupper(*u);
			u++;
		}
		enum ldms_value_type vt = ldms_metric_str_to_type(typestr);
		if (vt == LDMS_V_NONE) {
			rc = EINVAL;
			errusage(fsc, line, lno);
			goto out;
		}
		union ldms_value val;
		val.v_u64 = 0;
		rc = ldms_mval_parse_scalar(&val, vt, defstr);
		if (rc) {
			ovis_log(fsc->mylog, OVIS_LERROR, "%s%d: default %s invalid\n",
				conf, lno, defstr);
			goto out;
		}
		metric = malloc(sizeof(*metric));
		if (!metric) {
			rc = ENOMEM;
			ovis_log(fsc->mylog, OVIS_LERROR, "out of memory parsing %s\n",
				conf);
			goto out;
		}
		strcpy(metric->file, file);
		strcpy(metric->name, name);
		sprintf(metric->nametime, "%s%s", name, ".time");
		metric->missing_val = val;
		metric->val = val;
		metric->collect_time = -1;
		metric->t = vt;
		TAILQ_INSERT_TAIL(&fsc->metric_list, metric, entry);
	}
	goto done;

out:
	clear_metric_list(fsc);
done:
	fclose(in);
	return rc;
}

static int create_metric_set(filesingle_context_t fsc)
{
	ldms_schema_t schema;
	int rc;
	rc = ENOMEM;

	schema = base_schema_new(fsc->base);
	if (!schema) {
		rc = errno;
		ovis_log(fsc->mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, fsc->base->schema_name, errno);
		goto err;
	}

	fsc->metric_offset = ldms_schema_metric_count_get(schema);

	struct single *s;
	TAILQ_FOREACH(s, &fsc->metric_list, entry) {
		rc = ldms_schema_metric_add(schema, s->name, s->t);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	}

	if (fsc->collect_times) {
		TAILQ_FOREACH(s, &fsc->metric_list, entry) {
			rc = ldms_schema_metric_add(schema, s->nametime,
							LDMS_V_S64);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
		}
	}

	fsc->set = base_set_new(fsc->base);
	if (!fsc->set) {
		rc = errno;
		goto err;
	}

	return 0;

err:
	return rc;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
        filesingle_context_t fsc = ldmsd_plug_ctxt_get(handle);
	int rc;

	if (fsc->set) {
		ovis_log(fsc->mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	int tidx = av_idx_of(kwl, "timing");
	if (tidx != -1) {
		fsc->collect_times = 1;
	}
	ovis_log(fsc->mylog, OVIS_LINFO, "timing = %d\n", fsc->collect_times);

	const char *conf = av_value(avl, "conf");
	rc = parse_single_conf(fsc, conf);
	if (rc)
		return rc;

	if (TAILQ_EMPTY(&fsc->metric_list)) {
		ovis_log(fsc->mylog, OVIS_LERROR, "Empty set not allowed. (%s)\n", conf);
		return EINVAL;
	}

	fsc->base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, fsc->mylog);
	if (!fsc->base) {
		rc = ENOMEM;
		goto err;
	}

	rc = create_metric_set(fsc);
	if (rc) {
		ovis_log(fsc->mylog, OVIS_LERROR, "failed to create the metric set.\n");
		goto err;
	}

        fsc->configured = true;

	return 0;

err:
	base_del(fsc->base);
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
        filesingle_context_t fsc = ldmsd_plug_ctxt_get(handle);
	int rc;
	char *l;
	char lbuf[VALMAX+1];
	union ldms_value v;
	int i;
	FILE *mf;
	struct timeval tv[2];
	struct timeval *tv_now = &tv[0];
	struct timeval *tv_prev = &tv[1];
	struct timeval tv_diff;

        if (!fsc->configured) {
                ovis_log(fsc->mylog, OVIS_LERROR, SAMP" plugin not configured\n");
                return EINVAL;
        }

        base_sample_begin(fsc->base);
	i = fsc->metric_offset;
	struct single *s = NULL;
	TAILQ_FOREACH(s, &fsc->metric_list, entry) {

		if (fsc->collect_times) {
			gettimeofday(tv_prev, 0);
		}
		mf = fopen(s->file, "r");
		if (!mf)
			goto skip;

		l = fgets(lbuf, sizeof(lbuf), mf);
		fclose(mf);
		if (!l)
			goto skip;

		if (fsc->collect_times) {
			gettimeofday(tv_now, 0);
			timersub(tv_now, tv_prev, &tv_diff);
			s->collect_time = tv_diff.tv_sec * 1000000 +
				tv_diff.tv_usec;
		}

		rc = ldms_mval_parse_scalar(&(s->val), s->t, l);
		if (rc != 0)
			goto skip;
		ldms_metric_set(fsc->set, i, &(s->val));
		i++;
		continue;
skip:
		ldms_metric_set(fsc->set, i, &(s->missing_val));
		s->collect_time = -1;
		i++;
	}

	if (fsc->collect_times) {
		TAILQ_FOREACH(s, &fsc->metric_list, entry) {
			if (s->collect_time != -1) {
				v.v_s64 = s->collect_time;
				ldms_metric_set(fsc->set, i, &v);
			}
			i++;
		}
	}

	base_sample_end(fsc->base);
	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return "config name= " SAMP BASE_CONFIG_USAGE
		"  conf=<metric definitions file> [timing]\n"
		"  where each line of file is:\n"
		"  <name> <file> <type> <default>\n"
		"  where type is one of U[8,16,32,64] or S[8,16,32,64]\n"
		"  or F32 or F64 or CHAR.\n";

}

static int constructor(ldmsd_plug_handle_t handle)
{
        filesingle_context_t fsc;
        const char *config_name = ldmsd_plug_cfg_name_get(handle);

        fsc = calloc(1, sizeof(*fsc));
        if (fsc == NULL) {
		ovis_log(NULL, OVIS_LERROR,
                         "Failed to allocate context in plugin " SAMP ": %d", errno);
                return ENOMEM;
        }

        char *log_name = malloc(strlen("sampler.")+strlen(config_name)+1);
        sprintf(log_name, "sampler.%s", config_name);
        fsc->mylog = ovis_log_register(log_name, "Message from the " SAMP " plugin");
	if (!fsc->mylog) {
		int rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the log subsystem %s"
                         " for '" SAMP "' plugin. Error %d\n", log_name,  rc);
                free(log_name);
                return errno;
	}
        free(log_name);

	fsc->set = NULL;
	TAILQ_INIT(&fsc->metric_list);
        fsc->configured = false;

        ldmsd_plug_ctxt_set(handle, fsc);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
        filesingle_context_t fsc = ldmsd_plug_ctxt_get(handle);

	if (fsc->base)
		base_del(fsc->base);
	if (fsc->set)
		ldms_set_delete(fsc->set);
	fsc->set = NULL;
	clear_metric_list(fsc);
        ovis_log_deregister(fsc->mylog);
        free(fsc);
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base.name = SAMP,
	.base.type = LDMSD_PLUGIN_SAMPLER,
        .base.constructor = constructor,
        .base.destructor = destructor,
	.base.config = config,
	.base.usage = usage,
	.sample = sample,
};
