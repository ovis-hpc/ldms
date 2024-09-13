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
#include "sampler_base.h"
#include "variorum.h"
#include "jansson.h"
#include "variorum_topology.h"

#define SAMP "variorum_sampler"

static ldms_set_t set = NULL;
static ovis_log_t mylog;
static base_data_t base;
static int nsockets;
static const char *SOCKET_METRICS[] = {"power_cpu_watts", "power_gpu_watts", "power_mem_watts"};
static char** metric_names = NULL;
static int i_node;
static int i_sock;
static int i_cpu;
static int i_gpu;
static int i_mem;
static int lh_idx;
static ldms_mval_t* rec_idxs;
static char* result_string;

static int create_metric_set(base_data_t base)
{
        int rc, i, metric, socket;
        char metric_name[28];
        char socket_num[11];
        ldms_schema_t schema;
        ldms_mval_t rec_inst;

        // allocate space for metric names
        if (!metric_names) {
                metric_names = malloc(3 * nsockets * sizeof(char*));
        }
        for (metric = 0; metric < (3 * nsockets); metric++) {
                metric_names[metric] = malloc(39);
        }

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

        for(socket = 0; socket < nsockets; socket++) {
                // create a new record
                rec_inst = ldms_record_alloc(set, rec_def_idx);
                rec_idxs[socket] = rec_inst;
                // set the socket number
                ldms_record_set_u64(rec_inst, i_sock, socket);
                // put the record into the list
                ldms_list_append_record(set, lh, rec_inst);
                // create metric name list (for querying json object later on)
                //TP NOTE: This is not needed anymore, as we don't need to append the ID here. 
                // Will fix this once the initial build works.
                for(metric = 0; metric < 3; metric++) {
                        strcpy(metric_name,SOCKET_METRICS[metric]);
                        sprintf(socket_num,"%d",socket);
                        strcat(metric_name,socket_num);
                        strcpy(metric_names[(metric*nsockets)+socket], metric_name);
                }
        }

        // allocate space for sampling JSON data depending on number of sockets
        result_string = (char *) malloc((nsockets * 150 + 500) * sizeof(char));

        return 0;

 err:
        return rc;

}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{

        int rc;
        int depth;

        if (set) {
                ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
                return EINVAL;
        }

        // determine number of sockets
        nsockets = variorum_get_num_sockets();

        // 9/12/2024: TP Note: We need to know number of GPUs per socket here
        // so we can create a metric set that also gives per GPU power.
        // We don't have a good solution for this. We could call the JSON API 
        // once and parse that value out, but the overhead for that can be high.
        // To be addressed after the first pass works.

        // prepare the base for metric collection
        base = base_config(avl, SAMP, SAMP, mylog);
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

static int sample(struct ldmsd_sampler *self)
{
        json_t *power_obj = NULL;
	json_t *node_obj = NULL;
        int ret, socket;

        if (!set) {
                ovis_log(mylog, OVIS_LERROR, "plugin not initialized\n");
                return EINVAL;
        }

        base_sample_begin(base);

        // get variorum data
        ret = variorum_get_power_json(&result_string);
        if (ret != 0) {
                ovis_log(mylog, OVIS_LERROR, "unable to obtain JSON object data\n");
                return EINVAL;
        }

        power_obj = json_loads(result_string, JSON_DECODE_ANY, NULL);
	void *iter = json_object_iter(power_obj);
	while (iter) {
		node_obj = json_object_iter_value(iter);
		if (node_obj == NULL) {
			printf("JSON object not found");
			exit(0);
		}
		/* The following should return NULL after the first call per our object. */
		iter = json_object_iter_next(power_obj, iter);
	}

        double power_node = -1.0; 
        double power_cpu = -1.0;
        // We will add power of multiple GPUs as a first cut. See note on line 135.
        double power_gpu = -1.0; 
        double power_mem = -1.0;
        int num_gpus_per_socket = -1;
        char socketID[20];

         // If we're on a GPU-only build, we don't have power_node_watts.
        if (json_object_get(node_obj, "power_node_watts") != NULL)
        {
                power_node = json_real_value(json_object_get(node_obj, "power_node_watts"));
         //       printf("Node Power: %0.2lf Watts\n", power_node);
        }

        // If we're on a CPU-only build, we don't have num_gpus_per_socket
        if (json_object_get(node_obj, "num_gpus_per_socket") != NULL)
        {
                num_gpus_per_socket = json_integer_value(json_object_get(node_obj,
                                      "num_gpus_per_socket"));
                // printf("Number of GPUs per socket: %d\n", num_gpus_per_socket);
        }

        // update each record
        for(socket = 0; socket < nsockets; socket++) 
        {
                // Node power is same on both sockets.
                ldms_record_set_double(rec_idxs[socket], i_node, power_node);

                // Obtain Socket Object
                snprintf(socketID, 20, "socket_%d", i);
                json_t *socket_obj = json_object_get(node_obj, socketID);
                if (socket_obj == NULL)
                {
                        printf("Socket object not found!\n");
                        exit(0);
                }

                // If we're on a GPU-only build, we don't have power_cpu_watts
                if (json_object_get(socket_obj, "power_cpu_watts") != NULL)
                {                        
                        power_cpu = json_real_value(json_object_get(socket_obj, "power_cpu_watts"));
                        // printf("Socket %d, CPU Power: %0.2lf Watts\n", i, power_cpu);
                }

                // If we're on a GPU-only build on an unsupported platform, 
                // we don't have power_mem_watts.
                if (json_object_get(socket_obj, "power_mem_watts") != NULL)
                {
                        power_mem = json_real_value(json_object_get(socket_obj, "power_mem_watts"));
                        //  printf("Socket %d, Mem Power: %0.2lf Watts\n", i, power_mem);
                }     
                
                // If we have GPUs, obtatin the GPU object
                // As a first cut, add up the power of multiple GPUs on that socket.
                if (num_gpus_per_socket > 0)
                {
                    json_t *gpu_obj = json_object_get(socket_obj, "power_gpu_watts");
                    if (gpu_obj == NULL)
                        {
                                printf("GPU object not found! \n");
                                exit(0);
                        }
                        const char *key;
                        json_t *value;
                        power_gpu = 0.0;

                        json_object_foreach(gpu_obj, key, value)
                        {
                                power_gpu += json_real_value(value); 
                                // printf("Socket %d, %s Power: %0.2lf Watts\n", i, key, json_real_value(value));
                        }
                }

                // Set the LDMS records for the socket
                ldms_record_set_double(rec_idxs[socket], i_cpu, power_cpu);
                ldms_record_set_double(rec_idxs[socket], i_gpu, power_gpu);
                ldms_record_set_double(rec_idxs[socket], i_mem, power_mem);
            }
        }   
              /*   
                ldms_record_set_double(rec_idxs[socket], i_node, power_node);
                power_cpu = json_real_value(json_object_get(power_obj, metric_names[socket]));
                ldms_record_set_double(rec_idxs[socket], i_cpu, power_cpu);
                power_gpu = json_real_value(json_object_get(power_obj, metric_names[nsockets+socket]));
                ldms_record_set_double(rec_idxs[socket], i_gpu, power_gpu);
                power_mem = json_real_value(json_object_get(power_obj, metric_names[(2*nsockets)+socket]));
                ldms_record_set_double(rec_idxs[socket], i_mem, power_mem);
         */

        ldms_metric_modify(set, lh_idx);

        json_decref(power_obj);
        base_sample_end(base);

        return 0;

}

static void term(struct ldmsd_plugin *self)
{
        int metric;

        if (metric_names) {
                for (metric = 0; metric < 3 * nsockets; metric++) {
                        free(metric_names[metric]);
                }
                free(metric_names);
        }
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

static const char *usage(struct ldmsd_plugin *self)
{
        return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}

static struct ldmsd_sampler variorum_sampler_plugin = {
        .base = {
            .name = SAMP,
            .type = LDMSD_PLUGIN_SAMPLER,
            .term = term,
            .config = config,
            .usage= usage,
        },
        .sample = sample,
};

struct ldmsd_plugin *get_plugin()
{
	mylog = ovis_log_register("sampler."SAMP, "Messages for the " SAMP " plugin");
	if (!mylog) {
		ovis_log(NULL, OVIS_LWARN, "Failed to create the " SAMP " plugin's log subsystem");
	}
	return &variorum_sampler_plugin.base;
}
