/* -*- c-basic-offset: 8 -*- */
/* Copyright 2022 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <string.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"
#include "variorum.h"
#include "jansson.h"
#include "variorum_topology.h"

#define SAMP "variorum_sampler"

static ldms_set_t set = NULL;
static ovis_log_t mylog;
static base_data_t base;
static int nsockets;
static int i_node;
static int i_sock;
static int i_cpu;
static int i_gpu;
static int i_mem;
static int lh_idx;
static ldms_mval_t *rec_idxs;
static char *result_string;

static int create_metric_set(base_data_t base)
{
        int rc, i, metric, socket;
        char metric_name[28];
        char socket_num[11];
        ldms_schema_t schema;
        ldms_mval_t rec_inst;

        // allocate space for record pointers
        if (!rec_idxs) {
                rec_idxs = malloc(nsockets * sizeof(ldms_mval_t));
        }

        schema = base_schema_new(base);
        if (!schema) {
                ovis_log(mylog, OVIS_LERROR,
                "%s: The schema '%s' could not be created, errno=%d.\n",
                __FILE__, base->schema_name, errno);
                rc = errno;
                goto err;
        }

        // create record
        ldms_record_t rec_def = ldms_record_create("variorum_socket_record");
        i_node = ldms_record_metric_add(rec_def, "node_watts", "watts", LDMS_V_D64, 0);
        i_sock = ldms_record_metric_add(rec_def, "socket_ID", "number", LDMS_V_U64, 0);
        i_cpu = ldms_record_metric_add(rec_def, "cpu_watts", "watts", LDMS_V_D64, 0);
        i_gpu = ldms_record_metric_add(rec_def, "gpu_watts", "watts", LDMS_V_D64, 0);
        i_mem = ldms_record_metric_add(rec_def, "mem_watts", "watts", LDMS_V_D64, 0);

        // calculate required heap size to support 1 record per socket
        size_t heap_sz = nsockets * ldms_record_heap_size_get(rec_def);

        // add record definition to the schema
        int rec_def_idx = ldms_schema_record_add(schema, rec_def);

        // add the metric list to the schema
        int lh_idx = ldms_schema_metric_list_add(schema, "power", NULL, heap_sz);

        set = base_set_new(base);
        if (!set) {
                rc = errno;
                goto err;
        }

        ldms_mval_t lh = ldms_metric_get(set, lh_idx);

        for (socket = 0; socket < nsockets; socket++) {
                // create a new record
                rec_inst = ldms_record_alloc(set, rec_def_idx);
                rec_idxs[socket] = rec_inst;
                // set the socket number
                ldms_record_set_u64(rec_inst, i_sock, socket);
                // put the record into the list
                ldms_list_append_record(set, lh, rec_inst);
        }

        // allocate space for sampling JSON data depending on number of sockets
        result_string = (char *)malloc((nsockets * 150 + 500) * sizeof(char));

        return 0;

err:
        return rc;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{

        int rc;
        int depth;

        if (set) {
                ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
                return EINVAL;
        }

        // determine number of sockets
        nsockets = variorum_get_num_sockets();

        // prepare the base for metric collection
        base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
        if (!base) {
                rc = errno;
                goto err;
        }

        rc = create_metric_set(base);
        if (rc) {
                ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
                goto err;
        }

        return 0;
err:
        base_del(base);
        return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
        json_t *power_obj = NULL;
        json_t *node_obj = NULL;
        int ret, socket;

        if (!set) {
                ovis_log(mylog, OVIS_LERROR, "plugin not initialized\n");
                return EINVAL;
        }

        base_sample_begin(base);

        // Get Variorum data
        ret = variorum_get_power_json(&result_string);
        if (ret != 0) {
                ovis_log(mylog, OVIS_LERROR, "unable to obtain JSON object data\n");
                return EINVAL;
        }

        power_obj = json_loads(result_string, JSON_DECODE_ANY, NULL);
        void *iter = json_object_iter(power_obj);
        while (iter) {
                node_obj = json_object_iter_value(iter);
                if (node_obj == NULL)
                {
                        msglog(LDMSD_LERROR, SAMP ": JSON object not found.\n");
                        exit(0);
                }
                /* The following should return NULL after the first call per our object. */
                iter = json_object_iter_next(power_obj, iter);
        }

        double power_node = -1.0;
        double power_cpu = -1.0;
        double power_gpu = -1.0;
        double power_mem = -1.0;
        int num_gpus_per_socket = -1;
        char socketID[20];

        // If we're on a GPU-only build, we don't have power_node_watts.
        if (json_object_get(node_obj, "power_node_watts") != NULL) {
                power_node = json_real_value(json_object_get(node_obj, "power_node_watts"));
        }

        // If we're on a CPU-only build, we don't have num_gpus_per_socket
        if (json_object_get(node_obj, "num_gpus_per_socket") != NULL) {
                num_gpus_per_socket = json_integer_value(json_object_get(node_obj,
                                                                         "num_gpus_per_socket"));
        }

        // Update each record
        for (socket = 0; socket < nsockets; socket++) {
                // Node power is same on both sockets.
                ldms_record_set_double(rec_idxs[socket], i_node, power_node);

                // Obtain Socket Object
                snprintf(socketID, 20, "socket_%d", socket);
                json_t *socket_obj = json_object_get(node_obj, socketID);
                if (socket_obj == NULL) {
                        msglog(LDMSD_LERROR, SAMP ":socket object not found.\n");
                        exit(0);
                }

                // If we're on a GPU-only build, we don't have power_cpu_watts
                if (json_object_get(socket_obj, "power_cpu_watts") != NULL) {
                        power_cpu = json_real_value(json_object_get(socket_obj, "power_cpu_watts"));
                }

                // If we're on a GPU-only build on an unsupported platform,
                // we don't have power_mem_watts.
                if (json_object_get(socket_obj, "power_mem_watts") != NULL) {
                        power_mem = json_real_value(json_object_get(socket_obj, "power_mem_watts"));
                }

                // If we have GPUs, obtatin the GPU object
                if (num_gpus_per_socket > 0) {
                        json_t *gpu_obj = json_object_get(socket_obj, "power_gpu_watts");
                        if (gpu_obj == NULL) {
                                msglog(LDMSD_LERROR, SAMP ":GPU object not found.\n");
                                exit(0);
                        }
                        const char *key;
                        json_t *value;
                        power_gpu = 0.0;

                        json_object_foreach(gpu_obj, key, value) {
                                // We will add power of GPUs at socket-level.
                                power_gpu += json_real_value(value);
                        }
                }

                // Set the LDMS records for the socket
                ldms_record_set_double(rec_idxs[socket], i_cpu, power_cpu);
                ldms_record_set_double(rec_idxs[socket], i_gpu, power_gpu);
                ldms_record_set_double(rec_idxs[socket], i_mem, power_mem);
        }
        ldms_metric_modify(set, lh_idx);

        json_decref(power_obj);
        base_sample_end(base);

        return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
        return "config name=" SAMP " " BASE_CONFIG_USAGE;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
        if (result_string) {
                free(result_string);
        }
        if (rec_idxs) {
                free(rec_idxs);
        }
        if (base) {
                base_del(base);
        }
        if (set) {
                ldms_set_delete(set);
        }
        set = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface = {
        .base.type = LDMSD_PLUGIN_SAMPLER,
        .config = config,
        .usage= usage,
        .constructor = constructor,
        .destructor = destructor,
        .sample = sample,
};
