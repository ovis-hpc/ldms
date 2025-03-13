/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2018 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __LDMS_H__
#define __LDMS_H__
#include <limits.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <string.h>
#include <netinet/in.h>
#include <byteswap.h>
#include <asm/byteorder.h>
#include <openssl/sha.h>
#include <regex.h>
#include <assert.h>
#include <time.h>
#include "zap/zap.h"
#include "ovis-ldms-config.h"
#include "ldms_core.h"
#include "coll/rbt.h"
#include "ovis_util/os_util.h"
#include "ovis_util/util.h"
#include "ovis_ev/ev.h"
#include "ovis_log/ovis_log.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct ldms_xprt *ldms_t;
typedef struct ldms_set *ldms_set_t;
typedef struct ldms_schema_s *ldms_schema_t;
typedef struct ldms_record *ldms_record_t;

/**
 * \mainpage LDMS
 *
 * LDMS means Lightweight Distributed Metric Service. It is an
 * Application Programming Interface for publishing and gathering
 * system metrics in a clustered environment. A metric set provider
 * publishes metric sets to remote peers. The remote peers update
 * their local copies of these metrics whenever they choose, i.e. the
 * metric set published does not push the data, it is pulled by the
 * client. Only the metric set publisher can change the contents of
 * the metric set. Although the client can change its local copy, the
 * data will be overwritten the next time the set is updated.
 *
 * LDMS supports multiple network transports. The desired transport is
 * elected when the transport handle is created. If the RDMA transport
 * is available on your platform, then updating the contents of the
 * metric set is done by the hardware via RDMA_READ and the metric set
 * publisher's CPU is unaware and uninvolved with the
 * transfer; i.e. with RDMA the metric set updates have zero
 * CPU overhead.
 *
 * A \b Metric \b Set is a named collection of \b Metric values. A
 * Metric is a named and typed value. The contents of a metric set are
 * updated as a single entity, i.e. it is not possible to fetch just
 * one value from the server.
 *
 * Metric sets have a generation number that allows the client to
 * determine if any data in the metric set has changed since the last
 * time it was updated. This is useful because the data is pulled by
 * the client, not pushed by the producer.
 *
 * \section conn_mgmt Connection Management
 *
 * The LDMS connection management model is similar to the tranditional
 * sockets API. Peers are addressed with a struct sockaddr, servers \c
 * listen for incoming connection requests and clients \c connect to servers.
 *
 * The connection management API consists of the following functions:
 *
 * \li \b ldms_xprt_create() Create a transport instance.
 * \li \b ldms_xprt_create_with_auth() Create a transport instance with
 * authentication.
 * \li \b ldms_xprt_listen() Create a listening endpoint and respond to
 * queries from peers.
 * \li \b ldms_xprt_connect() Request a connection with a remote peer.
 * \li \b ldms_xprt_close() Close a connection with a remote peer.
 *
 * \section schema Defining Metric Set Schema
 * A Schema defines the metrics that comprise a metric set.  A metric
 * represents a value in the set that changes over time. It's most
 * recent value is retrieved with the ldms_xprt_update() function. A
 * meta-metric is a named value that is part of the meta-data of the
 * set and does not typically change. When it does change, it will
 * trigger a fetch of both the meta-data and the data portions of a
 * set when ldms_xprt_update() is called.
 *
 * Once a Schema is defined, it can be used to create one or more metric sets
 * of the same type. The principle functions for managing schema are the
 * following:
 *
 * \li \b ldms_schema_new() Create a Schema
 * \li \b ldms_schema_delete() Destroy a Schema
 * \li \b ldms_schema_metric_add() Add a metric to the Schema
 * \li \b ldms_schema_meta_add() Add a meta-metric to the Schema
 * \li \b ldms_schema_metric_count_get() Return the number of Metrics in the Schema
 *
 * \section metric_sets Creating Metric Sets
 *
 * Metric Sets are created with a schema and updated in local memory. The
 * principle functions for creating and destroying local metric sets are the
 * following:
 *
 * \li \b ldms_set_new() Create a new metric set from a Schema with global default
 * authentication values.
 * \li \b ldms_set_new_with_auth() Create a new metric set from a Schema with
 * specified authentication values.
 * \li \b ldms_set_delete() Destroy a metric set.
 *
 * \section query Querying Metric Sets
 *
 * A Metric Set consumer uses these function to query the server for
 * the publisehd Metric Sets:
 *
 * \li \b ldms_xprt_dir() Return a list of published metric set names.''
 * \li \b ldms_xprt_lookup() Lookup and gather the detail of a particular
 * metric set.
 * \li \b ldms_xprt_update() Update the contents of a previously looked-up
 * metric set.
 *
 * \section metrics Setting and Getting Metric Values.
 *
 * A Metric Set producer sets the values of the metrics after creating
 * the metric set and periodically thereafter as required. The client
 * updates the metric set and gets the values of the metrics.
 * The functions for doing this are as follows:
 *
 * \li \b ldms_metric_by_name() Find the index for a metric
 * \li \b ldms_metric_set() Set the value of a metric.
 * \li \b ldms_metric_get_X() Get the value of a metric where the X
 * specifies the data type
 * \li \b ldms_metric_set_S() Set the value of a metric where the X
 * specifies the data type
 *
 * \section notification Push Notifications
 *
 * Nominally, LDMS is a pull oriented transport, i.e. the Metric Set
 * consumer calls a function to pull the data from the provider and
 * update it's local copy of the data. There is, however, a way for
 * the producer to initiate communication using Notifications.
 *
 * \li \b ldms_register_notify_cb() Register a callback function to be
 * called when Notification Events are received for a particular
 * Metric Set.
 * \li \b ldms_cancel_notify() Stop receiving notification events for
 * a metric set.
 * \li \b ldms_notify() Send a notification event to all consumers
 * registered to receive events on a metric set.
 *
 *
 * \section list_section LDMS Metric List
 *
 * LDMS Metric List (\c LDMS_V_LIST) is a metric that contains other metrics as
 * list entries similar to \c LIST in \c sys/queue.h. The elements in the list
 * do not have to be of the same type. It is also possible to have a list of
 * list.
 *
 * Synopsis:
 * \li \b ldms_schema_metric_list_add(schema,"name","units",heap_sz) adds a
 *     metric list to the LDMS schema.
 * \li \b ldms_list_heap_size_get(type, item_count, array_len) calculates the
 *     heap size (\c heap_sz) for \c item_count list entries of \c type.
 * \li \c ldms_mval_t\ lh=ldms_metric_get(set,i) gets the metric list handle
 *     from the LDMS set.
 * \li \b ldms_mval_t\ ent=ldms_list_append_item(set,lh,type,array_len)
 *     allocates a list entry of type \c type and append to the tail of the list
 *     \c lh. \c array_len is used only for \c type being an array type.
 * \li \b ldms_list_remove_item(set,lh,ent) removes the entry \c ent from the
 *     list \c lh and release the LDMS heap memory consumed by \c ent.
 * \li \b ldms_mval_t\ ent=ldms_list_first(set,lh,&type,&array_len) gets the
 *     first entry of the list (along with the \c type and \c array_len).
 * \li \b ldms_mval_t\c next_ent=ldms_list_next(set,ent,&type,&array_len) gets
 *     the next entry in the list (along with the \c type and \c array_len).
 *
 * \c ldms_schema_metric_list_add(schema, "name", "units", heap_sz) adds a
 * metric list into the \c schema, similar to \c ldms_schema_metric_add(),
 * except for the \c heap_sz that specifies the maximum size of the memory
 * for the list entries. The \c heap_sz information will be added to the
 * \c schema so that \c ldms_set_new() knows how much memory to allocate for the
 * set (which consists of metadata part, data part and the heap part).
 * \c ldms_list_heap_size_get(type, item_count, array_count) determines the
 * \c heap_sz for \c item_count entries of \c type in the list (with
 * \c array_count array length if \c type is an array). The entries in the list
 * can be of different types. In such case, the \c heap_sz can be calculated by
 * adding up \c ldms_list_heap_size_get() for each of the expected elements.
 *
 * \c ldms_metric_get(set, i) returns the pointer to the metric raw data. In the
 * case of list, it is the handle to the list head. The returned list handle
 * (\c lh) can then be used with \c ldms_list_append_item() to
 * allocate-and-append a new metric entry to the list. \c ldms_list_first() and
 * \c ldms_list_next() are used for iterating through the entries in the list.
 * \c ldms_list_remove_item() removes an entry from the list and release the
 * heap memory consumed by the entry.
 *
 * The following example illustrates how to use LDMS Metric List. The setup in
 * the example is to read 4 counters/device from made-up devices (16 devices
 * max)
 *
 * Example:
 * \code
 * ldms_schema_t schema = ldms_schema_new("example");
 * size_t heap_sz = 0;
 *
 * // To support at most 16 sublists
 *
 * heap_sz += ldms_list_heap_size_get(LDMS_V_LIST, 16, 1);
 * heap_sz += ldms_list_heap_size_get(LDMS_V_CHAR_ARRAY, 16, 32);
 * heap_sz += ldms_list_heap_size_get(LDMS_V_U64_ARRAY, 16, 4);
 *
 * int lh_idx = ldms_schema_metric_list_add(schema, "my_list", NULL, heap_sz);
 *
 * ldms_set_t set = ldms_set_new("my_set", schema);
 *
 * ldms_mval_t lh = ldms_metric_get(set, lh_idx);
 * ldms_mval_t l0, l1; // list in lh
 *
 * ldms_mval_t new_list(ldms_set_t set, ldms_mval_t lh, const char *name)
 * {
 *     ldms_mval_t ll = ldms_list_append_item(set, lh, LDMS_V_LIST, 1);
 *
 *     // add `name` and counters to the sublist `ll`
 *     ldms_mval_t nm = ldms_list_append_item(set, ll, LDMS_V_CHAR_ARRAY, 32);
 *     ldms_mval_t ctr = ldms_list_append_item(set, ll, LDMS_V_U64_ARRAY, 4);
 *
 *     strncpy(nm->a_char, name, strlen(name)+1);
 *
 *     return ll;
 * }
 *
 * void read_counters(ldms_set_t set, ldms_mval_t ll)
 * {
 *     enum ldms_value_type typ;
 *     size_t len;
 *     int i;
 *     ldms_mval_t nm = ldms_list_first(set, ll, NULL, NULL);
 *     ldms_mval_t ctr = ldms_list_next(set, nm, &typ, &len);
 *     assert(typ == LDMS_V_U64_ARRAY);
 *     assert(len == 4);
 *     for (i = 0; i < 4; i++) {
 *         // LDMS store data in little-endian format
 *         ctr->a_u64[i] = __cpu_to_le64( READ_SOME_COUNTER(nm.a_char, i) );
 *     }
 *
 * }
 *
 * l0 = new_list(set, lh, "dev0");
 * l1 = new_list(set, lh, "dev1");
 *
 * read_counters(set, l0);
 * read_counters(set, l1);
 *
 * ldms_metric_modify(set, lh_idx); // just update data_gn
 *
 * \endcode
 *
 *
 * \section record_section LDMS Record
 *
 * Struct-like data for LDMS. The LDMS record allows application to add
 * structure-like data into the LDMS list.
 *
 * Synopsis:
 * \li \b ldms_record_t\ rec_def=ldms_record_create("rec_name") creates a record
 *     definition.
 * \li \b ldms_record_metric_add(rec_def,"name","units",type,array_len) add a
 *     member "name" of type \c type (with \c array_len length in the case of
 *     array type) into the record definition.
 * \li \b ldms_record_heap_size_get(rec_def) determines the memory size in the
 *     heap required for an instance of the given record.
 * \li \b ldms_schema_record_add(schema,rec_def) adds the record definition into
 *     the LDMS schema.
 * \li \b ldms_record_alloc(set,metric_id) allocates an instance of the record
 *     in the heap memory of the set.
 * \li \b ldms_list_append_record(set,lh,rec_inst) appends the record instance
 *     into the list. The record instance not belonging to any list won't be
 *     accessible to the remote peer.
 * \li \b ldms_mval_t\ mval=ldms_record_metric_get(rec_inst, i) gets the raw
 *     metric pointer of the i_th member of the record instance.
 * \li \b ldms_record_get_XXX(rec_inst,i) are convenient functions for getting
 *     metric value from the record instance and converting into host format.
 * \li \b ldms_record_set_XXX(rec_inst,i,val) are convenient functions for
 *     setting metric value, converting into LDMS format, and increment LDMS
 *     data_gn.
 * \li \b ldms_record_array_get_XXX(rec_inst,i,j) same as above, but for array
 *     metric type.
 * \li \b ldms_record_array_set_XXX(rec_inst,i,j,val) same as above, but for
 *     array metric type.
 * \li \b enum\ ldms_value_type\ type=ldms_record_metric_type_get(rec_inst,i,&array_len)
 *     gets the metric type, and \c array_len of the i_th member of the record
 *     instance.
 * \li \b int\ i=ldms_record_metric_find(rec_inst, "name") returns the index
 *     of the member "name" in the record.
 * \li \b const\ char\ *name=ldms_record_metric_name_get(rec_inst, i) returns
 *     the name of the i_th member of the record.
 * \li \b const\ char\ *name=ldms_record_metric_unit_get(rec_inst, i) returns
 *     the unit of the i_th member of the record.
 *
 * To use the record, first the application needs to create a record
 * definition (\c rec_def) with \c ldms_record_create() and add
 * members into the record definition with \c ldms_record_metric_add().
 * Once all record members are added, the \c rec_def must be added to
 * the schema with \c ldms_schema_record_add().
 *
 * An instance of a record is dynamically created and resides in \c set-heap
 * memory. \c ldms_record_heap_size_get() returns the size of the record in
 * the LDMS heap. This is useful when computing the appropriate \c heap-size
 * value for the \c ldms_set_new_with_heap() function.
 *
 * \c ldms_record_alloc() allocate a new record instance (\c rec_inst).
 * A \c rec_inst must be a member of a list, \c ldms_list_append_record()
 * or an ldms_record_array.
 *
 * \c ldms_record_metric_get() returns an ldms_mval_t that can be used
 * to access the metric in the record. A set of convenience functions
 * \c ldms_record_get_XXX() and \c ldms_record_array_get_XXX() will
 * return values from records.
 *
 * \c ldms_record_set_XXX() and \c ldms_record_array_set_XXX() are a
 * set of record metric setters that handle data conversion and data
 * generation number increment. If the application decides to
 * manipulate the metric value directly, it must call \c
 * ldms_metric_modify() to increment the data generation number.
 *
 * Example:
 * \code
 * // defining record similar to the following structure:
 * // struct my_record {
 * //     char     name[32];
 * //     uint64_t counters[4];
 * // }
 *
 * ldms_record_t rec_def = ldms_record_create("my_record");
 * int i_name = ldms_record_metric_add(rec_def, "name", NULL, LDMS_V_CHAR_ARRAY, 32);
 * int i_ctrs = ldms_record_metric_add(rec_def, "counters", NULL, * LDMS_V_U64_ARRAY, 4);
 *
 * // Calculate the required heap size to support 16 devices (records)
 * size_t heap_sz = 16 * ldms_record_heap_size_get(rec_def);
 *
 * // Create a schema
 * ldms_schema_t schema = ldms_schema_new("my_schema");
 *
 * // Add a record definition to the schema
 * int rec_def_idx = ldms_schema_record_add(schema, rec_def);
 *
 * // Add a list to the schema with the heap_sz calculated above.
 * int lh_idx = ldms_schema_metric_list_add(schema, "my_list", NULL, heap_sz);
 *
 * ldms_set_t set = ldms_set_new("my_set", schema);
 *
 * ldms_mval_t new_record(ldms_set_t set, const char *name)
 * {
 *     // Allocate a record
 *     ldms_mval_t rec_inst = ldms_record_alloc(set, rec_def_idx);
 *
 *     ldms_mval_t nm = ldms_record_metric_get(rec_inst, i_name);
 *     // Set the name
 *     strncpy(nm.a_char, name, strlen(name) + 1);
 *     return rec_inst;
 * }
 *
 * void read_counters(ldms_mval_t rec_inst)
 * {
 *     ldms_mval_t nm = ldms_record_metric_get(rec_inst, i_name);
 *     ldms_mval_t ctrs = ldms_record_metric_get(rec_inst, i_ctrs);
 *     int i;
 *     uint64_t v;
 *     for (i = 0; i < 4; i++) {
 *         // LDMS store data in little-endian format
 *         v = READ_SOME_COUNTER(nm.a_char, i);
 *         ctrs->a_u64[i] = __cpu_to_le64(v);
 *         // or use: ldms_record_array_set_u64(rec_inst, i_ctrs, i, v);
 *     }
 * }
 *
 * ldms_mval_t rec0 = new_record(set, "rec0");
 * ldms_mval_t rec1 = new_record(set, "rec1");
 *
 * // Don't forget to put the record into the list.
 * ldms_mval_t lh = ldms_metric_get(set, lh_idx);
 * ldms_list_append_record(set, lh, rec0);
 * ldms_list_append_record(set, lh, rec1);
 *
 * // Iterate through the records in the list and update the record contents
 * enum ldms_value_type type;
 * size_t array_len;
 * ldms_mval_t rec;
 * for (rec = ldms_list_first(set, lh, &type, &array_len);
 *      rec; rec = ldms_list_next(set, rec, &type, &array_len)) {
 *     assert( type == LDMS_V_RECORD_INST );
 *     assert( array_len == 1);
 *     read_counters(rec);
 * }
 * ldms_metric_modify(set, lh_idx); // update data_gn
 *
 * \endcode
 *
 */

/**
 * \brief Initialize LDMS
 *
 *  Pre-allocate a memory region for metric sets
 *  \param max_size The maximum size of the pre-allocated memory
 *  \retval 0     If success
 *  \retval errno If error
 */
int ldms_init(size_t max_size);

/**
 * \brief Take a reference on a transport
 *
 * \param x	The transport handle
 * \returns	The transport handle
 */
ldms_t ldms_xprt_get(ldms_t x);
void ldms_xprt_put(ldms_t x);

/**
 * \brief Terminate underlying LDMS Transport threads
 *
 * \param timeout_sec The time, in seconds, to wait for the threads to
 *                    terminate, 0 for waiting indefinitely.
 *
 * \retval 0         The threads terminated successfully.
 * \retval ETIMEDOUT A timeout occurred before the threads terminated.
 */
int ldms_xprt_term(int timeout_sec);

/**
 * \brief Get the LDMS library version
 *
 * \param Pointer to an ldms_version structure
 */
void ldms_version_get(struct ldms_version *v);

/**
 * \brief Get the LDMS library version
 *
 * Compare the LDMS library version to the provided version.
 *
 * \param Pointer to an ldms_version structure
 * \retval !0 The versions match
 * \retval 0 The versions do not match
 */
int ldms_version_check(const struct ldms_version *v);

/**
 * \brief Find a transport that matches the specified address.
 *
 * The function will only compare the address if the port is 65535.
 *
 * \param sa	Specifies the address that should match the remote peer's ip address.
 * \returns	The matching transport endpoint or NULL if no match was found.
 */
ldms_t ldms_xprt_by_remote_sin(struct sockaddr *sa);

/**
 * \brief Get the local and remote names of a transport
 *
 * \param x    A transport
 * \param lcl_name  A buffer to receive the local name.
 * \param lcl_name_sz    The size of \c lcl_name
 * \param lcl_port  A buffer to receive the local port
 * \param lcl_port_sz    The size of \c lcl_port
 * \param rem_name  A buffer to receive the remote name.
 * \param rem_name_sz    The size of \c rem_name
 * \param rem_port  A buffer to receive the remote port.
 * \param rem_port_sz    The size of \c rem_port
 * \param flags     Flags modifies the behavior of getnameinfo()
 *
 * \return 0 on success. Otherwise, an error is returned.
 */
int ldms_xprt_names(ldms_t x, char *lcl_name, size_t lcl_name_sz,
				char *lcl_port, size_t lcl_port_sz,
				char *rem_name, size_t rem_name_sz,
				char *rem_port, size_t rem_port_sz,
				int flags);

/**
 * \brief Returns the 'first' transport endpoint
 *
 * The ldms_xprt_first() and ldms_xprt_next() functions are used to iterate
 * among all transport endpoints in the system. The ldms_xprt_first() function
 * returns the first endpoint and takes a reference on the handle. This reference
 * should be released by calling ldms_xprt_put() when the caller has finished
 * with the endpoint.
 *
 * \returns The first transport endpoint or NULL if there are no open transports.
 */
ldms_t ldms_xprt_first();

/**
 * \brief Returns the 'next' transport endpoint
 *
 * The ldms_xprt_first() and ldms_xprt_next() functions are used to iterate
 * among all transport endpoints in the system. The ldms_xprt_first() function
 * returns the first endpoint and takes a reference on the handle. This reference
 * should be released by calling ldms_xprt_put() when the caller has finished
 * with the endpoint.
 *
 * \returns The first transport endpoint or NULL if there are no open transports.
 */
ldms_t ldms_xprt_next(ldms_t);

enum ldms_lookup_status {
	LDMS_LOOKUP_ERROR = 1,
	LDMS_LOOKUP_OK = 0,
	LDMS_LOOKUP_NOTIFY = 1,
};

/**
 * \brief Prototype for the function called when lookup completes.
 *
 * This function is called when the lookup completes.
 *
 * \param t	 The transport endpoint.
 * \param status LDMS_LOOKUP_0 if the lookup was successful, ENOENT if the
 *		 specified set does not exist, ENOMEM if there is insufficient
 *		 memory to instantiate the set locally.
 * \param more   If the more parameter is non-zero, additional lookup
 *               results are outstanding for the request.
 * \param s	 The metric set handle.
 * \param cb_arg The callback argument specified in the call to \c ldms_lookup.
 */
typedef void (*ldms_lookup_cb_t)(ldms_t t, enum ldms_lookup_status status,
				 int more, ldms_set_t s, void *arg);

/*
 * Values for the flags field in the ldms_set structure below.
 */
#define LDMS_SET_F_MEMMAP	0x0001
#define LDMS_SET_F_FILEMAP	0x0002
#define LDMS_SET_F_LOCAL	0x0004
#define LDMS_SET_F_REMOTE	0x0008
#define LDMS_SET_F_PUSH_CHANGE	0x0010
#define LDMS_SET_F_DATA_COPY	0x0020 /* set array data copy on transaction begin */
#define LDMS_SET_F_SNAPSHOT	0x10000 /* A read-only light copy of a set, which must not be published or shared with remote clients. */
#define LDMS_SET_F_PUBLISHED	0x100000 /* Set is in the set tree. */
#define LDMS_SET_ID_DATA	0x1000000

/**
 * Round up \c _sz_ to the \c _align_.
 *
 * \c _align_ must be a power of 2.
 */
#define LDMS_ROUNDUP(_sz_, _align_) (((_sz_) + (_align_) - 1) & ~((_align_)-1))

/**
 * \addtogroup ldms_conn_mgmt LDMS Connection Management
 *
 * These functions initiate, terminate and manage connections with
 * LDMS peers.
 * \{
 */

/**
 * LDMS log function definition.
 *
 * Users can implememnt customized log function using this API.
 *
 * \param fmt The format of the printing string (as in \c printf).
 */
typedef void (*ldms_log_fn_t)(ovis_log_t subsys, int level, const char *fmt, ...);

/**
 * \brief Create a transport handle
 *
 * Metric sets are exported on the network through a transport. A
 * transport handle is required to communicate on the network.
 *
 * \param name	The name of the transport type to create without authentication.
 * \param log_fn An optional function to call when logging transport messages
 *
 * \returns	A transport handle on success.
 * \returns	0 If the transport could not be created.
 */
extern ldms_t ldms_xprt_new(const char *name);

/**
 * \brief Create a transport handle with authentication
 *
 * This is like ::ldms_xprt_new(), but with authentication plugin attached to
 * the transport.
 *
 * \param xprt_name The transport type name string.
 * \param log_fn An optional function to call when logging transport messages.
 * \param auth_name The name of the authentication plugin.
 * \param auth_av_list The attribute-value list containing options for the
 *                     authentication plugin. Please consult the plugin manual
 *                     for the options.
 */
ldms_t ldms_xprt_new_with_auth(const char *xprt_name,
			       const char *auth_name,
			       struct attr_value_list *auth_av_list);

/**
 * Unlimited value for rail 'quota' and 'rate'.
 */
#define LDMS_UNLIMITED (-1)

/**
 * \brief Create a new LDMS Rail handle.
 *
 * \param xprt_name The transport type name string.
 * \param n The number of endpoints in the rail.
 * \param recv_quota Our recv buffer quota in bytes, -1 for unlimited.
 * \param rate_limit Our transfer rate limit in bytes per second.
 * \param auth_name The authentication plugin name (e.g. munge).
 * \param auth_av_list The options for the authentication.
 *
 * \retval rail The rail handle, if there is no error.
 * \retval NULL If there is an error. \c errno is set to describe the error.
 */
ldms_t ldms_xprt_rail_new(const char *xprt_name,
			  int n, int64_t recv_quota, int64_t rate_limit,
			  const char *auth_name,
			  struct attr_value_list *auth_av_list);

/**
 * \brief Set application's context
 *
 * LDMS calls the given function \c fn to free the context when the reference
 * reaches zero.
 *
 * \param x      LDMS transport
 * \param ctxt   Application's context
 * \param fn     Application's function to free the context
 */
typedef void (*app_ctxt_free_fn)(void *ctxt);
void ldms_xprt_ctxt_set(ldms_t x, void *ctxt, app_ctxt_free_fn fn);

/**
 * \brief Get application's context
 *
 * \param x      LDMS transport
 */
void *ldms_xprt_ctxt_get(ldms_t x);

/**
 * \brief Return the unique connection id for a transport instance
 *
 * \param x The transport handle
 * \returns The unique connection id for the transport
 */
uint64_t ldms_xprt_conn_id(ldms_t x);

/**
 * \brief Return the transport type name string
 * \param x The transport handle
 * \returns The transport type name string
 */
const char *ldms_xprt_type_name(ldms_t x);

/**
 * \brief Set the ldms transport priority
 *
 * An transport can be HIGH or NORMAL priority. By default
 * transports are NORMAL priority. Events on HIGH priority transports
 * are delivered before events for NORMAL priority transports.
 *
 * A non-zero value for the \c prio argument will set the priority to
 * HIGH.
 *
 * \param x	The transport handle
 * \param prio	The priority level
 */
void ldms_xprt_priority_set(ldms_t x, int prio);

enum ldms_xprt_event_type {
	/*! A new connection is established */
	LDMS_XPRT_EVENT_CONNECTED,
	/*! A connection request is rejected */
	LDMS_XPRT_EVENT_REJECTED,
	/*! A connection request is failed */
	LDMS_XPRT_EVENT_ERROR,
	/*! A connection is disconnected */
	LDMS_XPRT_EVENT_DISCONNECTED,
	/*! Receive data from a remote host */
	LDMS_XPRT_EVENT_RECV,
	/*! Lookup set has been deleted at peer */
	LDMS_XPRT_EVENT_SET_DELETE,
	/*! A send request is completed */
	LDMS_XPRT_EVENT_SEND_COMPLETE,
	/*! The send quota is deposited.  */
	LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED,
	/*! Peer ask us for quota donation. */
	LDMS_XPRT_EVENT_QGROUP_ASK,
	/*! Peer donates quota to us. */
	LDMS_XPRT_EVENT_QGROUP_DONATE,
	/*! For donating back the unused donated quota. */
	LDMS_XPRT_EVENT_QGROUP_DONATE_BACK,
	LDMS_XPRT_EVENT_LAST
};

struct ldms_xprt_set_delete_data {
	ldms_set_t set;		/*! The local set looked up at peer */
	const char *name;	/*! The name of the set */
};

struct ldms_xprt_quota_event_data {
	uint64_t quota; /* current quota */
	int      ep_idx; /* the index of the endpoint in the rail */
};

typedef struct ldms_xprt_event {
	/*! ldms event type */
	enum ldms_xprt_event_type type;
	/*! Pointer to message data. This buffer is owned by ldms and
	 * may be freed when the callback returns.
	 * \c data is NULL if the type is not LDMS_CONN_EVENT_RECV.
	 */
	union {
		/*! The length of \c data in bytes.
		 * \c data_len is 0 if \c type is not LDMS_CONN_EVENT_RECV.
		 */
		char *data;
		struct ldms_xprt_set_delete_data set_delete;
		struct ldms_xprt_quota_event_data quota;
	};
	size_t data_len;
} *ldms_xprt_event_t;

typedef struct ldms_cred {
	uid_t uid;
	gid_t gid;
} *ldms_cred_t;

extern const char *ldms_xprt_event_type_to_str(enum ldms_xprt_event_type t);

/**
 * Definition of callback function for ldms_xprt_connect and ldms_xprt_listen.
 *
 * The caller that requests a connection will be notified through a
 * callback function if the connection is successful. The event type
 * <tt>e->type</tt> indicates success or failure as follows:
 * - LDMS_CONN_EVENT_CONNECTED The transport is now connected,
 * - LDMS_CONN_EVENT_REJECTED The connection request is rejected by the server,
 * - LDMS_CONN_EVENT_ERROR The connection attempt failed, or
 * - LDMS_CONN_EVENT_DISCONNECTED A connection is disconnected.
 *
 * Servers will be notified through a callback function if there is
 * a new connection or a connection is disconnected. The event type <tt>e->type</tt>
 * indicates as follows:
 * - LDMS_CONN_EVENT_CONNECTED There is a new connection, or
 * - LDMS_CONN_EVENT_DISCONNECTED A connection is disconnected.
 *
 * ldms also notifies servers and clients in case they receives data from
 * a remote host.
 * - LDMS_CONN_EVENT_RECV Server or client receives data from a remote host.
 *
 * \param x The ldms transport handle
 * \param e The ldms event
 * \param cb_arg The \c cb_arg specified when ::ldms_xprt_connect() or
 *               ::ldms_xprt_listen() was called
 *
 * \see ldms_xprt_connect, ldms_xprt_connect_by_name, ldms_xprt_listen,
 *      ldms_xprt_listen_by_name
 */
typedef void (*ldms_event_cb_t)(ldms_t x, ldms_xprt_event_t e, void *cb_arg);

/**
 * \brief Request a connection to an LDMS host.
 *
 * Connect to the remote peer specified by it's host
 * name and service address (port number). If the <tt>cb</tt> function
 * is not NULL, the function will return immediately and call the
 * <tt>cb</tt> function when the connection completes with the provided
 * cb_arg as an argument. See the ldms_connect_cb_t() function for more
 * details.
 *
 * If <tt>cb</tt> is NULL, the function waits until the connection
 * completes before returning and the returned value indicates the
 * success or failure of the connection.
 *
 * \param x	The transport handle
 * \param sa	Socket address specifying the host address and port.
 * \param sa_len The length of the socket address.
 * \param cb	The callback function.
 * \param cb_arg An argument to be passed to \c cb when it is called.
 * \retval	0 if the request is posted successfully. Please note that this
 *		doesn't mean that the transport is connected.
 * \retval	An error indicating why the request failed.
 */
extern int ldms_xprt_connect(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
			     ldms_event_cb_t cb, void *cb_arg);

/**
 * \brief Connect to a hostname and service
 *
 * Connect to the remote peer specified by it's host
 * name and service address (port number). If the <tt>cb</tt> function
 * is not NULL, the function will return immediately and call the
 * <tt>cb</tt> function when the connection completes with the provided
 * cb_arg as an argument. See the ldms_connect_cb_t() function for more
 * details.
 *
 * If <tt>cb</tt> is NULL, the function waits until the connection
 * completes before returning and the returned value indicates the
 * success or failure of the connection.
 *
 * \param x	The transport handle
 * \param host  The hostname
 * \param port	The port number (service) as a string
 * \param cb    The function to call when the connection completes.
 * \param cb_arg A user-supplied argument to pass to the callback function.
 *
 * \retval 0            The connection succeeded
 * \retval ENETRESET    The peer responsed with a reset (no listener)
 * \retval EHOSTUNREACH No route to host
 */
int ldms_xprt_connect_by_name(ldms_t x, const char *host, const char *port,
			      ldms_event_cb_t cb, void *cb_arg);
/**
 * \brief Listen for connection requests from LDMS peers.
 *
 * \param x	The transport handle
 * \param sa	Socket address specifying the host address and port.
 * \param sa_len The length of the socket address.
 * \param cb	The callback function that receives an ldms event.
 *              If it is NULL, all events, except LDMS_RECV_COMPLETE, will be
 *              handled by ldms.
 * \param cb_arg An argument to be passed to \c cb when it is called.
 *               If the \c cb is NULL, \c cb_arg is ignored.
 * \returns	0 if a listening endpoint was successfully created.
 * \returns	An error indicating why the listen failed.
 */
extern int ldms_xprt_listen(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
		ldms_event_cb_t cb, void *cb_arg);
extern int ldms_xprt_listen_by_name(ldms_t x, const char *host, const char *port,
		ldms_event_cb_t cb, void *cb_arg);

/**
 * \brief Set the event callback and the callback context
 *
 * This is useful when an application wants to change the callback function
 * after it accepts a connection request.
 *
 * \param x   The transport handle
 * \param cb  The callback function that receives an ldms event.
 *            If it is NULL, all events, except LDMS_RECV_COMPLETE, will be handled by ldms.
 * \param cb_arg An argument to be passed to \c cb when it is called.
 */
void ldms_xprt_event_cb_set(ldms_t x, ldms_event_cb_t cb, void *cb_arg);

/**
 * \brief Check if an LDMS transport endpoint is connected.
 *
 * \param x     The transport handle
 *
 * \retval      0 if the transport endpoint is connected. Otherwise, 1 is returned.
 */
extern int ldms_xprt_connected(ldms_t x);

/**
 * \brief Get local and remote sockaddr from the xprt.
 *
 * \retval 0 If success.
 * \retval errno If failed.
 */
int ldms_xprt_sockaddr(ldms_t x, struct sockaddr *local_sa,
		       struct sockaddr *remote_sa,
		       socklen_t *sa_len);

/* currently only support IPv4 and IPv6 */
struct ldms_addr {
	sa_family_t sa_family; /* host-endian */
	in_port_t   sin_port;  /* network-endian */
	uint8_t     addr[16];  /* addr[0-3] for IPv4,
				  addr[0-15] for IPv6 */
};

/**
 * \brief Get local and remote address in \c ldms_addr struct from the xprt
 *
 * \param x   LDMS Transport pointer
 * \param local_addr  Local address (re-entrant)
 * \param remote_addr Remote address (re-entrant)
 *
 * \return 0 on success.
 */
int ldms_xprt_addr(ldms_t x, struct ldms_addr *local_addr,
			    struct ldms_addr *remote_addr);

/**
 * \brief Get addresss string
 *
 * \param addr LDMS address
 * \param buff String buffer to receive the address string
 * \param sz   Size of \c buff
 *
 * \return \c buff on success; otherwise, NULL is returned.
*/
const char *ldms_addr_ntop(struct ldms_addr *addr, char *buff, size_t sz);


/**
 * \brief Convert a CIDR IP address string to \c ldms_addr
 *
 * The address is stored in \c addr, and the prefix length is stored in \c prefix_len.
 *
 * \param addr   ldms_addr pointer
 * \param prefix_len   Integer pointer
 *
 * \retval 0 if success. Otherwise, an errno is returned.
 */
int ldms_cidr2addr(const char *cdir_str, struct ldms_addr *addr, int *prefix_len);

/**
 * \brief Verify if \c sa is in \net_addr with the prefix \c prefix_len
 *
 * \param ip_addr    IP Address
 * \param net_addr   Network Address
 * \param prefix_len Prefix length for masking
 *
 * \return 1 if the IP address is in the network address. Otherwise, 0 is returned.
 */
int ldms_addr_in_network_addr(struct ldms_addr *ip_addr,
				struct ldms_addr *net_addr, int prefix_len);
/**
 * \brief Close a connection to an LDMS host.
 *
 * The function will disconnect the connection and free the allocated memory
 * for the transport \c x.
 *
 * \param x	The transport handle
 *
 * \see ldms_xprt_delete
 */
extern void ldms_xprt_close(ldms_t x);

/**
 * \brief Send a message to an LDMS peer
 *
 * See the ldms_xprt_recv() function for information on how to receive
 * messages.
 *
 * \param x       The transport handle
 * \param msg_buf Pointer to the buffer containing the message
 * \param msg_len The length of the message buffer in bytes
 */
extern int ldms_xprt_send(ldms_t x, char *msg_buf, size_t msg_len);

/**
 * \brief Get the maximum size of send/recv message.
 * \param x The transport handle.
 * \retval sz The maximum message size.
 */
size_t ldms_xprt_msg_max(ldms_t x);


/**
 * \brief Get the threads associated with the xprt.
 *
 * \param x   The transport handle.
 * \param out The buffer array to store the results.
 * \param n   The length of the buffer array \c out.
 *
 * \retval N  A positive integer indicating the number of elemnts in \c out that
 *            has been written.
 * \retval -EINVAL if \c n <= 0
 * \retval -ENOMEM if the buffer is to small.
 */
int ldms_xprt_get_threads(ldms_t x, pthread_t *out, int n);

/**
 * \brief Return the main underlying zap endpoint associated with the xprt.
 */
zap_ep_t ldms_xprt_get_zap_ep(ldms_t x);

/**
 * \brief Check if \c x is a rail.
 *
 * \param x  The transport handle.
 *
 * \retval 1 If \c x is a rail.
 * \retval 0 If \c x is not a rail.
 */
int ldms_xprt_is_rail(ldms_t x);

/**
 * \brief Check if the remote peer is also using a rail transport.
 *
 * \param x  The transport handle.
 *
 * \retval 1 If the remote peer of \c x is a rail.
 * \retval 0 If the remote peer of \c x is not a rail.
 */
int ldms_xprt_is_remote_rail(ldms_t x);


/**
 * \brief Get the number of endpoints in the rail \c x.
 *
 * \param x The rail transport handle.
 *
 * \retval N       The number of endpoints in the rail.
 * \retval -EINVAL If \c x is not a rail.
 */
int ldms_xprt_rail_eps(ldms_t x);

/**
 * \brief Get the receive quota of an endpoint
 *
 * \param x The transport handle
 *
 * \retval Receive limit is retunred.
 * \retval -EINVAL if \c x is NULL or not a rail
 */
int64_t ldms_xprt_rail_recv_quota_get(ldms_t x);

/**
 * \brief Get the receive rate limit of an endpoint
 *
 * \param x The transport handle
 *
 * \retval Receive limit is retunred.
 * \retval -EINVAL if \c x is NULL or not a rail
 */
int64_t ldms_xprt_rail_recv_rate_limit_get(ldms_t x);

/**
 * \brief Get the send rate limit of an endpoint (set by peer)
 *
 * \param x The transport handle
 *
 * \retval rate_limit The send rate limit.
 * \retval -EINVAL If \c x is NULL or not a rail
 */
int64_t ldms_xprt_rail_send_rate_limit_get(ldms_t x);

/**
 * \brief Set (reconfig) the new rate limit
 *
 * \param rate The new rate
 *
 * \retval      0 If succeeded, or
 * \retval -errno If failed.
 */
int ldms_xprt_rail_recv_rate_limit_set(ldms_t x, uint64_t rate);

/**
 * \brief Get the send quota
 *
 * \param[in]  x       The rail transport handle.
 * \param[out] quota   The array to receive the send-quota for each endpoint in
 *                     the rail.
 * \param[in]  n       The size of \c quota array. Must be greater than the
 *                     number of endpoints in the rail.
 *
 * \retval       0 If there is no error.
 * \retval -EINVAL If \c x is not a rail.
 * \retval -ENOMEM If \c n is less than the number of endpoints in the rail.
 */
int ldms_xprt_rail_send_quota_get(ldms_t x, uint64_t *quota, int n);

/**
 * Set a new recv quota value.
 *
 * This function notifies the peer of the new recv quota \c q so that the peer
 * can adjust its send quota accordingly.
 *
 * \param x The rail transport handle.
 * \param q The new recv quota value.
 *
 * \retval 0     If succeeded,
 * \retval -errno If failed.
 */
int ldms_xprt_rail_recv_quota_set(ldms_t x, uint64_t q);

/* A convenient sockaddr union for IPv4 and IPv6 (for now) */
union ldms_sockaddr {
	struct sockaddr     sa;
	struct sockaddr_in  sin;
	struct sockaddr_in6 sin6;
	struct sockaddr_storage storage;
};

/**
 * A utility to convert \c host, \c port/service to \c sockaddr.
 *
 * \c host could be:
 * - "IP4_ADDR", e.g. "192.168.0.5"
 * - "IP6_ADDR", e.g. "::1"
 * - "HOSTNAME", e.g. "node05"
 *
 * \note It is recommended to supply \c sockaddr_storage structure for \c sa.
 *
 * \param         host   The host string.
 * \param         port   The port or service string (see \c getaddrinfo(3)).
 * \param[out]    sa     The sockaddr output buffer. It is recommended to
 *                       supply \c sockaddr_storage structure for \c sa.
 * \param[in,out] sa_len The length of the \c sa buffer. On return, \c sa_len
 *                       is set to the length of the returned \c sa.
 *
 * \retval 0 If there is no error.
 */
int ldms_getsockaddr(const char *host, const char *port,
		     struct sockaddr *sa, socklen_t *sa_len);

/**
 * Same as \c ldms_getsockaddr(), but only returns AF_INET4 family.
 */
int ldms_getsockaddr4(const char *host, const char *port,
		      struct sockaddr *sa, socklen_t *sa_len);

/**
 * Same as \c ldms_getsockaddr(), but only returns AF_INET6 family.
 */
int ldms_getsockaddr6(const char *host, const char *port,
		      struct sockaddr *sa, socklen_t *sa_len);

/** \} */

/**
 * \addtogroup ldms_qgroup LDMS Quota Group (qgroup)
 *
 * LDMS Quota Group, or qgroup for short, is a feature in LDMS library that
 * enables the control of the amount of the stream data going through the
 * processes participating in the "group". \c qgroup mechanism leverages
 * \c rail.recv_quota to limit the stream data. Hence, the endpoints in our
 * process (a member of \c qgroup) has to be created with
 * \c * ldms_xprt_rail_new() with \c recv_quota. When the peer publishes a
 * stream data, it has to take quota from the said \c recv_quota. When our
 * process done processing the stream data (all stream callbacks on the data
 * have returned), the quota is issued back to the peer. This limits the amount
 * of buffer in our process. With \c qgruop, before our process returns the
 * quota, we first consults \c qgroup.quota. The returning quota has to be taken
 * from \c qgroup.quota. If the \c qgroup.quota is not enough (less than the
 * required returning quota), the returning quota is held off. If we hold off
 * enough returning quota, the peer will run out of quota and cannot publish
 * further stream data. \c qgroup.cfg.quota determines the initial amount of
 * \c qgroup.quota. When \c qgroup.quota goes below \c qgroup.cfg.ask_mark, the
 * qgroup mechanism asks members for quota donations (with
 * \c qgroup.cfg.ask_amount) every \c qgroup.cfg.ask_usec interval. The donation
 * also takes from \c qgroup.quota. Eventually, all \c qgroup.quota in all
 * members will run out. The \c qgroup.quota gets reset back to
 * \c qgroup.cfg.quota every \c qgroup.cfg.reset_usec interval. Roughly, the
 * amount of data that goes through members of \c qgroup (including us) is:
 *
 *   \c N * \c qgroup.cfg.quota over \c qgroup.cfg.reset_usec time interval
 *
 * provided that all members use the same parameters.
 *
 *
 * To use qgroup feature, we have to
 * 1) set qgroup paramters of our process (\c ldms_qgroup_cfg_set()),
 * 2) add member processes (\c ldms_qgroup_member_add()), and
 * 3) start the qgroup feature (\c ldms_qgroup_start()).
 *
 * The member processes can use \c ldms_qgroup_cfg_set() to set the parameters
 * at once, or use the following functions to set them individually:
 * - \c ldms_qgroup_cfg_quota_set()
 * - \c ldms_qgroup_cfg_ask_usec_set()
 * - \c ldms_qgroup_cfg_ask_mark_set()
 * - \c ldms_qgroup_cfg_ask_amount_set()
 * - \c ldms_qgroup_cfg_reset_usec_set()
 *
 * After \c ldms_qgroup_start(), the process has to call \c ldms_qgroup_stop()
 * before altering the member list or modifying \c qgroup.cfg.
 *
 * These functions and types manage and manipulate LDMS Quota Group.
 * \{
 */

typedef enum ldms_qgroup_state_e {
	LDMS_QGROUP_STATE_STOPPED = 0,
	LDMS_QGROUP_STATE_STOPPING,
	LDMS_QGROUP_STATE_STARTED,
	LDMS_QGROUP_STATE_BUSY, /* qgroup is in execution routine */
	LDMS_QGROUP_STATE_LAST,
} ldms_qgroup_state_t;
const char *ldms_qgroup_state_str(ldms_qgroup_state_t state);

typedef enum ldms_qgroup_member_state_e {
	LDMS_QGROUP_MEMBER_STATE_DISCONNECTED,
	LDMS_QGROUP_MEMBER_STATE_CONNECTING,
	LDMS_QGROUP_MEMBER_STATE_CONNECTED,
	LDMS_QGROUP_MEMBER_STATE_LAST,
} ldms_qgroup_member_state_t;

typedef struct ldms_qgroup_s *ldms_qgroup_t;

const char *ldms_qgroup_member_state_str(ldms_qgroup_member_state_t state);

typedef struct ldms_qgroup_cfg_s *ldms_qgroup_cfg_t;
struct ldms_qgroup_cfg_s {
	uint64_t quota;
	uint64_t ask_mark;
	uint64_t ask_amount;
	uint64_t ask_usec;
	uint64_t reset_usec;
};

/**
 * \brief Set the qgroup config with the provided \c cfg.
 *
 * \retval 0 Success
 * \retval EBUSY If the `qgroup` is not in the STOPPED state.
 */
int ldms_qgroup_cfg_set(ldms_qgroup_cfg_t cfg);

/**
 * \brief Retrieve the current qgroup config.
 *
 * \retval cfg The `ldms_qgroup_cfg_s` structure.
 */
struct ldms_qgroup_cfg_s ldms_qgroup_cfg_get();

/**
 * \brief Set the `cfg.quota` of the qgroup.
 *
 * \retval 0 Success
 * \retval EBUSY If the `qgroup` is not in the STOPPED state.
 */
int ldms_qgroup_cfg_quota_set(uint64_t quota);

/**
 * \brief Set the `cfg.ask_usec` of the qgroup.
 *
 * \retval 0 Success
 * \retval EBUSY If the `qgroup` is not in the STOPPED state.
 */
int ldms_qgroup_cfg_ask_usec_set(uint64_t usec);

/**
 * \brief Set the `cfg.reset_usec` of the qgroup.
 *
 * \retval 0 Success
 * \retval EBUSY If the `qgroup` is not in the STOPPED state.
 */
int ldms_qgroup_cfg_reset_usec_set(uint64_t usec);

/**
 * \brief Set the `cfg.ask_mark` of the qgroup.
 *
 * \retval 0 Success
 * \retval EBUSY If the `qgroup` is not in the STOPPED state.
 */
int ldms_qgroup_cfg_ask_mark_set(uint64_t ask_mark);

/**
 * \brief Set the `cfg.ask_amount` of the qgroup.
 *
 * \retval 0 Success
 * \retval EBUSY If the `qgroup` is not in the STOPPED state.
 */
int ldms_qgroup_cfg_ask_amount_set(uint64_t ask_amount);

/**
 * \brief Add a member into the qgroup.
 *
 * \param xprt_name The name of the xprt type (e.g. "sock").
 * \param host The host of the member.
 * \param port The port of the member. If \c port is \c NULL, "411" is supplied
 *             as a default.
 * \param auth_name The name of the authentication type (e.g. "munge"). The
 *                  default is "none".
 * \param auth_av_list The list attribute-value options for \c auth_name. The
 *                     default is \c NULL.
 *
 *
 * \retval 0 Success.
 * \retval EBUSY If the `qgroup` is not in the STOPPED state.
 * \retval EINVAL Invalid argument (e.g.\c host being \c NULL).
 * \retval ENAMETOOLONG A parameter value is too long (e.g. \c host is longer
 *                      than 256 bytes).
 * \retval EEXIST The peer "host:port" already exist in the member list.
 * \retval ENOMEM Not enough memory.
 */
int ldms_qgroup_member_add(const char *xprt_name,
			   const char *host, const char *port,
			   const char *auth_name,
			   struct attr_value_list *auth_av_list);

/**
 * \brief Remove a member from the qgroup.
 *
 * \param host The host of the member.
 * \param port The port of the member. If \c port is \c NULL, "411" is supplied
 *             as a default.
 *
 * \retval 0 Success.
 * \retval EBUSY If the `qgroup` is not in the STOPPED state.
 * \retval ENOENT If the "host:port" member does not exist in the list.
 */
int ldms_qgroup_member_del(const char *host, const char *port);

/**
 * \brief Start the qgroup service.
 *
 * \retval 0 Success.
 * \retval EBUSY If the `qgroup` is not in the STOPPED state.
 * \retval EINVAL If the config is invalid (e.g. \c cfg.quota=0).
 */
int ldms_qgroup_start();

/**
 * \brief Stop the qgroup service.
 */
int ldms_qgroup_stop();

/* probe the current quota value */
uint64_t ldms_qgroup_quota_probe();

typedef struct ldms_qgroup_member_info_s {
	STAILQ_ENTRY(ldms_qgroup_member_info_s) entry;
	ldms_qgroup_member_state_t state;
	char   c_host[256]; /* see rfc1034 */
	char   c_port[32];  /* port or service name */
	char   c_xprt[32];  /* the transport type */
	char   c_auth[32];  /* auth type */
	struct attr_value_list *c_auth_av_list;
} *ldms_qgroup_member_info_t;

typedef struct ldms_qgroup_info_s {
	ldms_qgroup_state_t state;
	struct ldms_qgroup_cfg_s cfg;
	uint64_t quota; /* the current quota */
	STAILQ_HEAD(, ldms_qgroup_member_info_s) member_stq;
} *ldms_qgroup_info_t;

ldms_qgroup_info_t ldms_qgroup_info_get();
void ldms_qgroup_info_free(ldms_qgroup_info_t qinfo);

/** \} */ /* ldms_qgroup */

/**
 * \addtogroup ldms_stream LDMS Stream Functions
 *
 * These functions manage and manipulate LDMS Stream.
 * \{
 */

typedef enum ldms_stream_type_e {
	LDMS_STREAM_STRING,
	LDMS_STREAM_JSON,
	LDMS_STREAM_AVRO_SER,
	LDMS_STREAM_LAST, /* the last enumureation; not a real type */
} ldms_stream_type_t;

/**
 * \brief Stream Type Symbol.
 *
 * This function returns a constant string symbol (e.g. "LDMS_STREAM_STRING") of
 * the given Stream type \c t. If the given type is out of valid range,
 * "UNKNOWN" is returned.
 *
 * \param t The Stream type.
 *
 * \retval s The symbol (e.g. "LDMS_STREAM_STRING").
 */
const char *ldms_stream_type_sym(ldms_stream_type_t t);

/**
 * \brief Publish stream data.
 *
 * If \c x is \c NULL, publish to all subscribers. Otherwise, publish directly
 * to the peer of \c x.
 *
 * \param x            NULL for loopback, or a valid rail handle.
 * \param stream_name  The name of the stream.
 * \param stream_type  The type of the stream (STRING or JSON).
 * \param cred         The credential of the publisher. This can be \c NULL, and
 *                       the \c euid and \c egid are used.
 * \param perm         The permission on the stream data.
 * \param data         The data to be published.
 * \param data_len     The length of the data.
 *
 * \retval 0        If there is no error.
 * \retval EAGAIN   If there is not enough send quota.
 * \retval ENOSTR   If the the handle not valid for publishing a stream.
 */
int ldms_stream_publish(ldms_t x, const char *stream_name,
                        ldms_stream_type_t stream_type,
			ldms_cred_t cred,
			uint32_t perm,
                        const char *data, size_t data_len);

/**
 * Like \c ldms_stream_publsh(), but publish the content of a file.
 *
 * \param x            NULL for loopback, or a valid rail handle.
 * \param stream_name  The name of the stream.
 * \param stream_type  The type of the stream (STRING or JSON).
 * \param cred         The credential of the publisher. This can be \c NULL, and
 *                       the \c euid and \c egid are used.
 * \param perm         The permission on the stream data.
 * \param file         The FILE handle.
 *
 * \retval 0        If there is no error.
 * \retval EAGAIN   If there is not enough send quota.
 * \retval ENOSTR   If the the handle not valid for publishing a stream.
 */
int ldms_stream_publish_file(ldms_t x, const char *stream_name,
                        ldms_stream_type_t stream_type,
			ldms_cred_t cred,
			uint32_t perm,
			FILE *file);

typedef struct ldms_stream_client_s *ldms_stream_client_t;
typedef struct json_entity_s *json_entity_t;

enum ldms_stream_event_type {
	LDMS_STREAM_EVENT_RECV, /* stream data received */
	LDMS_STREAM_EVENT_SUBSCRIBE_STATUS, /* reporting subscription status */
	LDMS_STREAM_EVENT_UNSUBSCRIBE_STATUS, /* reporting unsubscription status */
	LDMS_STREAM_EVENT_CLOSE, /* reporting stream client close event.
				  * This is the last event to deliver from a
				  * client. */

	LDMS_STREAM_EVENT_LAST, /* The last enumeration; not a real event */
};

/**
 * \brief String symbol of event type \c t for printing.
 *
 * \retval s The string symbol of the given event type (e.g. "LDMS_STREAM_EVENT_RECV").
 * \retval "UNKNOWn" If the event type \c t is out of range.
 */
const char *ldms_stream_event_type_sym(enum ldms_stream_event_type t);

/* For stream data delivery to the application */
struct ldms_stream_recv_data_s {
	ldms_stream_client_t client;
	struct ldms_addr src;
	uint64_t msg_gn;
	ldms_stream_type_t type;
	uint32_t name_len;
	uint32_t data_len;
	const char *name; /* stream name */
	const char *data; /* stream data */
	json_entity_t json; /* json entity */
	struct ldms_cred cred; /* credential */
	uint32_t perm; /* permission */
	uint32_t name_hash; /* stream name hash */
};

/* To report subscrube / unsubscribe return status */
struct ldms_stream_return_status_s {
	const char *match; /* name or regex */
	int is_regex;
	int status;
};

/* For stream close event */
struct ldms_stream_close_event_s {
	ldms_stream_client_t client;
};

typedef struct ldms_stream_event_s {
	ldms_t r; /* rail */
	enum ldms_stream_event_type type;
	struct timespec recv_ts;
	uint32_t hop_num;
	union {
		struct ldms_stream_recv_data_s recv;
		struct ldms_stream_return_status_s status;
		struct ldms_stream_close_event_s close;
	};
} *ldms_stream_event_t;

typedef int (*ldms_stream_event_cb_t)(ldms_stream_event_t ev, void *cb_arg);

/**
 * \brief Subscribe to a stream.
 *
 * Subscribe to the matching streams that go through our process. The callback
 * function is called when a matching stream data reaches our process. The
 * `cb_fn` must not be NULL.
 *
 * \param stream   The stream name or regular expression.
 * \param is_regex 1 if `stream` is a regular expression. Otherwise, 0.
 * \param cb_fn    The callback function for stream data delivery.
 * \param cb_arg   The application context to the `cb_fn`.
 * \param desc     An optional short description of the client of this subscription.
 *                 This could be useful for client stats.
 *
 * \retval NULL  If there is an error. In this case `errno` is set to describe
 *               the error.
 * \retval ptr   The stream client handle.
 */
ldms_stream_client_t
ldms_stream_subscribe(const char *stream, int is_regex,
		      ldms_stream_event_cb_t cb_fn, void *cb_arg,
		      const char *desc);

/**
 * \brief Terminate the stream client.
 *
 * If the client `c` is a remote subscriber, an "unsubscribe" request will also
 * be sent to the remote peer.
 *
 * \param c  The subscription handle.
 */
void ldms_stream_close(ldms_stream_client_t c);

/**
 * \brief Request a remote stream subscritpion.
 *
 * This function send a remote stream subscription request to the remote peer.
 * The callback function, if not \c NULL, is called to notify the application
 * whether or not the remote stream subscription is a success. After a
 * successful subscription, the remote peer will send the matching stream data
 * to our process. Please note that the application still need a stream client
 * (from \c ldms_stream_subscribe()) to process the data.
 *
 * \param x        The rail handle.
 * \param stream   The stream name or regular expression.
 * \param is_regex 1 if `stream` is a regular expression. Otherwise, 0.
 * \param cb_fn    The callback function for return status notification (could
 *                 be \c NULL).
 * \param cb_arg   The application context to the `cb_fn`.
 * \param rate     The rate limit (bytes/sec). Use `LDMS_UNLIMITED` for
 *                 unlimited.
 *
 * \retval 0     If succeeded.
 * \retval errno If synchronously failed.
 */
int ldms_stream_remote_subscribe(ldms_t x, const char *stream, int is_regex,
		      ldms_stream_event_cb_t cb_fn, void *cb_arg, int64_t rate);

/**
 * \brief Request a remote stream unsubscription.
 *
 * This function unsubscribe the previous subscription. The subscription
 * parameters (\c stream and \c is_regex) must be the same as the previously
 * given at the subscription time. The \c cb_fn is called to let the application
 * know about the unsubscription status.
 */
int ldms_stream_remote_unsubscribe(ldms_t x, const char *stream, int is_regex,
		      ldms_stream_event_cb_t cb_fn, void *cb_arg);


struct ldms_stream_counters_s {
	struct timespec first_ts; /* Timestamp of the first message */
	struct timespec last_ts;  /* Timestamp of the last message  */
	uint64_t        count;    /* The number of messages         */
	size_t          bytes;    /* Total bytes of messages        */
};

#define LDMS_STREAM_COUNTERS_INITIALIZER ((struct ldms_stream_counters_s){{INT64_MAX, 999999999}, {0, 0}, 0, 0})
#define LDMS_STREAM_COUNTERS_INIT(p) do { \
			*(p) = LDMS_STREAM_COUNTERS_INITIALIZER; \
		} while (0)

struct ldms_stream_hop {
	struct timespec recv_ts;
	struct timespec send_ts;
};

#define STREAM_MAX_PROFILE_HOPS 8
struct ldms_stream_profile {
	uint32_t hop_cnt;
	struct ldms_stream_hop hops[OVIS_FLEX];
};
struct ldms_stream_profile_ent {
	TAILQ_ENTRY(ldms_stream_profile_ent) ent;
	struct ldms_stream_profile profiles;
};
TAILQ_HEAD(ldms_stream_profile_list, ldms_stream_profile_ent);

/* stream statistics by src */
struct ldms_stream_src_stats_s {
	struct rbn rbn; /* key ==> src */
	struct ldms_addr src;
	struct ldms_stream_counters_s rx; /* total rx from src */
	struct ldms_stream_profile_list profiles;
};

/* stats of stream-client pair */
struct ldms_stream_client_pair_stats_s {
	TAILQ_ENTRY(ldms_stream_client_pair_stats_s) entry;

	const char *stream_name; /* allocated with the structure, don't free */
	const char *client_match; /* allocated with the structure, don't free */
	const char *client_desc; /* allocated with the structure, don't free */
	int is_regex; /* client is a regular expression */

	/* client transmission counters for the stream */
	struct ldms_stream_counters_s tx;
	/* client drop counters for the stream */
	struct ldms_stream_counters_s drops;
};
TAILQ_HEAD(ldms_stream_client_pair_stats_tq_s, ldms_stream_client_pair_stats_s);

/* stats of a stream */
struct ldms_stream_stats_s {
	TAILQ_ENTRY(ldms_stream_stats_s) entry;
	struct ldms_stream_counters_s rx; /* total rx regardless of src */
	struct rbt src_stats_rbt; /* tree of statistics by src; the nodes are `struct ldms_stream_src_stats_s` */

	struct ldms_stream_client_pair_stats_tq_s pair_tq; /* stats by client */
	const char *name; /* allocated with the structure, don't free it */
};
TAILQ_HEAD(ldms_stream_stats_tq_s, ldms_stream_stats_s);

/* stats of a stream client */
struct ldms_stream_client_stats_s {
	TAILQ_ENTRY(ldms_stream_client_stats_s) entry;
	struct ldms_stream_counters_s tx;
	struct ldms_stream_counters_s drops;
	struct ldms_stream_client_pair_stats_tq_s pair_tq; /* stats by stream */
	struct ldms_addr dest;
	int is_regex;
	const char *match; /* the matching string; allocated with the structure */
	const char *desc; /* the short description; allocated with the structure */
};
TAILQ_HEAD(ldms_stream_client_stats_tq_s, ldms_stream_client_stats_s);

/**
 * Set the stream statistics collection level.
 *
 * When `ldms_init()` is called, the `LDMS_STATS_LEVEL` environment variable is
 * read and \c ldms_stream_stats_level_set()` is called accordingly. If
 * `LDMS_STATS_LEVEL` is not defined, the default level is 1.
 *
 * Levels:
 * - 0: disabled; no stats collection.
 * - 1: shallow collection; only collect "cumulative" stats.
 * - 2: deep collection; collects stream stats by `src`, also collects
 *      delivery stats by client for the stream. For each client stat, this also
 *      collects the client rx stats by stream name.
 *
 * \param level The level of stats collection.
 *
 * \retval 0     If there is no error, or
 * \retval errno describing an error.
 */
int ldms_stream_stats_level_set(int level);

/**
 * Obtain the current stream stats level.
 */
int ldms_stream_stats_level_get();

/**
 * \brief Get the statuses/statistics of the matching streams in this process.
 *
 * \param match    The stream name or a regular expression.
 * \param is_regex 1 if \c match is a regular expression; otherwise, 0.
 * \param is_reset 1 means to reset the streams' statistics
 *
 * \retval tq   The collection (tailq) of statistics of the matching entries, or
 * \retval NULL if there is an error. \c errno is also set to describe the error.
 *
 * \note The caller is responsible for freeing the \c rbt and the entries in it.
 *       \c ldms_stream_stats_tq_free() is a helping function for this.
 */
struct ldms_stream_stats_tq_s *
ldms_stream_stats_tq_get(const char *match, int is_regex, int is_reset);

/**
 * \brief Free all of the entries in the given \c tq and the \c tq itself.
 *
 * \note \c tq must be the one returned from \c ldms_stream_stats_tq_get().
 */
void ldms_stream_stats_tq_free(struct ldms_stream_stats_tq_s *tq);

/**
 * Returns the JSON-formatted text of the stream stats in \c tq.
 *
 * \remarks The caller is responsible for freeing the returned string.
 */
char *ldms_stream_stats_tq_to_str(struct ldms_stream_stats_tq_s *tq);

/**
 * \brief Returns a JSON-formatted text describing statuses/statistics of the matching streams in this process.
 *
 * \param  match    The stream name or a regular expression.
 * \param  is_regex 1 if \c match is a regular expression; otherwise, 0.
 * \param  is_reset 0 means not to reset the statistics.
 *                  A non-zero value means to reset the statistics.
 *
 * \retval str The string describing the stats.
 *
 * \note The caller is responsible for freeing the returned string.
 */
char *ldms_stream_stats_str(const char *match, int is_regex, int is_reset);

/**
 * Returns a collection of stats of stream clients.
 *
 * \param is_reset   A non-zero value means to reset the statistics
 */
struct ldms_stream_client_stats_tq_s *ldms_stream_client_stats_tq_get(int is_reset);

/**
 * Free the stats entries in the \c tq and the \c tq itself.
 */
void ldms_stream_client_stats_tq_free(struct ldms_stream_client_stats_tq_s *tq);

/**
 * Get stats from a client.
 *
 * \param is_reset    A non-zero value means to reset the statistics
 */
struct ldms_stream_client_stats_s *
ldms_stream_client_get_stats(ldms_stream_client_t cli, int is_reset);

/**
 * Free the stream client stats obtained form \c ldms_stream_client_get_stats().
 */
void ldms_stream_client_stats_free(struct ldms_stream_client_stats_s *cs);

/**
 * Returns the JSON-formatted text of the stream client stats in \c tq.
 *
 * \remarks The caller is responsible for freeing the returned string.
 */
char *ldms_stream_client_stats_tq_to_str(struct ldms_stream_client_stats_tq_s *tq);

/**
 * \brief Return a string describing statuses/statistics of stream clients in this process.
 * \retval str The string describing the stats.
 * \note The caller is responsible for freeing the returned string.
 *
 * \param is_reset non-zero means to reset the statistics
 */
char *ldms_stream_client_stats_str(int is_reset);

/**
 * \brief Reset the statistics of streams and their clients
 */
void ldms_stream_n_client_stats_reset();

/** \} */

/**
 * \addtogroup ldms_query LDMS Query Functions
 *
 * These functions query LDMS peers for published metric sets.
 * \{
 */

/**
 * \brief ldms_dir callback function
 *
 * This function is called in response to a call to ldms_xprt_dir()
 * and, if requested, thereafter whenever there is an update to the
 * set directory.
 *
 * The application should call \c ldms_xprt_dir_free() when it is finished
 * processing the directory to free the associated resources.
 *
 * There are three different types of directory updates:
 * LDMS_DIR_LIST, LDMS_DIR_ADD, and LDMS_DIR_REPLY. An LDMS_DIR_LIST
 * contains a list of all sets published by the server.LDMS_DIR_ADD
 * contains a list of newly added sets since the callback was last
 * called, and LDMS_DIR_REM contains a list of those sets removed. The
 * user can expect a LDMS_DIR_LIST directory when the callback is
 * first called, followed by any number of LDMS_DIR_ADD and
 * LDMS_DIR_REM directory types. See the ldms_dir_s structure for more
 * information.
 *
 * \param x	 The transport handle
 * \param cb_arg The callback argument specified in the call to \c ldms_dir
 * \param status If zero, the query was successful, ENOMEM indicates
 *		 that there was insufficient memory available to build the
 *		 directory.
 * \param dir	 Pointer to an ldms_dir_s structure on success, NULL
 *		 otherwise.
 *
 */

/**
 * \brief The directory update type
 */
enum ldms_dir_type {
	LDMS_DIR_LIST,		/*! A complete list of available metric sets */
	LDMS_DIR_DEL,		/*! The listed metric sets have been deleted */
	LDMS_DIR_ADD,		/*! The listed metric sets have been added */
	LDMS_DIR_UPD,		/*! The set_info of the listed metric set have been updated */
};

typedef struct ldms_key_value_s {
	char *key;
	char *value;
} *ldms_key_value_t;

/**
 * \brief The data for a set in a the directory
 */
typedef struct ldms_dir_set_s {
	char *inst_name;	/*! Instance name */
	char *schema_name;	/*! Schema name */
	char *digest_str;	/*! The schema digest string */
	char *flags;		/*! Set state flags */
	size_t meta_size;	/*! Set meta-data size */
	size_t data_size;	/*! Set data size */
	size_t heap_size;	/*! Set heap size */
	uid_t uid;		/*! Set owner user-id  */
	gid_t gid;		/*! Set owner group-id */
	char *perm;		/*! Set owner permission string */
	int card;		/*! Number of metrics */
	int array_card;		/*! Number of set buffers */
	uint64_t meta_gn;	/*! Meta-data generation number */
	uint64_t data_gn;	/*! Data generation number  */
	struct ldms_timestamp timestamp; /*! Update transaction timestamp */
	struct ldms_timestamp duration;	 /*! Update transaction duration  */
	size_t info_count;
	ldms_key_value_t info;
} *ldms_dir_set_t;

/**
 * \brief The format of the directory data returned by
 * \c ldms_dir request.
 */
typedef struct ldms_dir_s {
	/** the type of update */
	enum ldms_dir_type type;

	/** !0 if this is the first of multiple updates */
	int more;

	/** count of sets in the set_name array */
	int set_count;

	/** Array of ldms_dir_set_s structures */
	struct ldms_dir_set_s set_data[OVIS_FLEX];

} *ldms_dir_t;

typedef void (*ldms_dir_cb_t)(ldms_t t, int status, ldms_dir_t dir, void *cb_arg);

/**
 * \brief Free the resources consumed by a directory
 *
 * This function is called by the application to release the resources
 * used by an ldms_dir_t.
 *
 * \param t	 The transport handle
 * \param dir	 Pointer to an ldms_dir_s structure to be released.
 */
void ldms_xprt_dir_free(ldms_t t, ldms_dir_t dir);

 char *ldms_dir_set_info_get(ldms_dir_set_t dset, const char *key);

/**
 * \brief Cancel LDMS directory updates
 *
 * This function cancels updates to the LDMS directory initiated by a
 * call to ldms_xprt_dir().
 *
 * \param t	The transport handle
 *
 * \returns	0 if the query was submitted successfully
 */
int ldms_xprt_dir_cancel(ldms_t t);

/**
 * \brief Query the sets published by a host.
 *
 * This function queries the peer for the set of published metric sets.
 *
 * If the <tt>cb</tt> function is not NULL, the function will return
 * immediately and call the <tt>cb</tt> function when the connection
 * completes with the provided cb_arg as an argument.  If the
 * <tt>LDMS_DIR_F_NOTIFY</tt> flag is specified, the callback function
 * will be invoked whenever the peer updates its set of published
 * metrics. See the ldms_connect_cb_t() function for more details.
 *
 * If <tt>cb</tt> is NULL, the function waits until the query
 * completes before returning and the returned value indicates the
 * success or failure of the query. Note that the <tt>flags</tt>
 * parameter is ignored if the <tt>cb</tt> parameter is NULL.
 *
 * See the ldms_xprt_dir_cancel() function to cancel directory updates.
 *
 * \param x	 The transport handle
 * \param cb	 The callback function to invoke when the directory is
 *		 returned by the peer.
 * \param cb_arg A user context that will be provided as a parameter
 *		 to the \c cb function.
 * \param flags	 If set to LDMS_DIR_F_NOTIFY, the specified callback
 *		 function will be invoked whenever the directory changes on the
 *		 peer.
 * \returns	0 if the query was submitted successfully
 */
#define LDMS_DIR_F_NOTIFY	1
extern int ldms_xprt_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags);

#define LDMS_XPRT_LIBPATH_DEFAULT PLUGINDIR
#define LDMS_DEFAULT_PORT	LDMSDPORT
#define LDMS_LOOKUP_PATH_MAX	511

enum ldms_lookup_flags {
	LDMS_LOOKUP_BY_INSTANCE = 0,
	LDMS_LOOKUP_BY_SCHEMA = 1,
	LDMS_LOOKUP_RE = 2,
	LDMS_LOOKUP_SET_INFO = 4,
};

/**
 * \brief Query the contents of a metric set.
 *
 * This function queries the peer for the detail of the metric set \c
 * path. If the query is successful, the function puts the set handle
 * in the pointer provided by the \c s parameter.
 *
 * If the <tt>cb</tt> function is not NULL, the function will return
 * immediately and call the <tt>cb</tt> function when the lookup
 * completes. See the ldms_lookup_cb_t() function for more details.
 *
 * If <tt>cb</tt> is NULL, the function waits until the lookup
 * completes before returning and the returned value indicates the
 * success or failure of the lookup.
 *
 * The <tt>flags</tt> parameter specifies if the <tt>name</tt> is a
 * regular expression. If <tt>name</tt> is an RE, then the <tt>cb</tt> function
 * will be called for every matching metric set on the peer. The
 * <tt>cb</tt> parameter cannot be NULL if LDMS_LOOKUP_RE is set in
 * <tt>flags.</tt>
 *
 * <tt>flags</tt> is a combination of the following values:
 * - LDMS_LOOKUP_RE The name parameter is a regular expression
 * - LDMS_LOOKUP_BY_INSTANCE The <tt>name</tt> refers to the set instance
 * - LDMS_LOOKUP_BY_SCHEMA The <tt>name</tt> refers to the set schema
 *
 * See the ldms_xprt_dir() function for detail on how to query a host for
 * the list of published metric sets.
 *
 * \param t	 The transport handle
 * \param name   The name to look up. The name refers to either the schema or instance name based on the value of <tt>flags</tt>
 * \param flags  The lookup options
 * \param cb	 The callback function to invoke when the lookup has
 *		 completed.
 * \param cb_arg A user context that will be provided as a parameter
 *		 to the \c cb function.
 * \returns	 0 if the query was submitted successfully.
 */
extern int ldms_xprt_lookup(ldms_t t, const char *name, enum ldms_lookup_flags flags,
		       ldms_lookup_cb_t cb, void *cb_arg);

/** \} */

/**
 * \addtogroup ldms_set LDMS Metric Set Management
 *
 * These functions are used to create and destroy local metric sets
 * and to update the contents of remote metric sets.
 * \{
 */

/** The update is the result of a peer push */
#define LDMS_UPD_F_PUSH		0x10000000
/** This is final push update for this set */
#define LDMS_UPD_F_PUSH_LAST	0x20000000
/** Indicate more outstanding update completion on the set */
#define LDMS_UPD_F_MORE		0x40000000

#define LDMS_UPD_ERROR_MASK 0x00FFFFFF

#define LDMS_UPD_ERROR(s) ((s) & LDMS_UPD_ERROR_MASK)

/**
 * \brief Prototype for the function called when update completes.
 *
 * You cannot call back into the transport from this function or it will
 * deadlock.
 *
 * \param t	The transport endpoint.
 * \param s	The metric set handle updated.
 * \param flags One or more of the LDMS_UPD_F_xxx flags bitwise-OR
 * 		the error code in case of an error.
 * \param arg	The callback argument specified in the call to \c ldms_update.
 */
typedef void (*ldms_update_cb_t)(ldms_t t, ldms_set_t s, int flags, void *arg);

/**
 * \brief Update the metric set contents.
 *
 * Updates the local copy of the metric set.
 *
 * \param s	The metric set handle to update.
 * \param update_cb The function to call when the update has completed
 *		    and the metric data has been updated.
 * \param cb_arg A user defined context value to provide to the update_cb function.
 * \returns	0 on success or a non-zero value to indicate an error.
 */
extern int ldms_xprt_update(ldms_set_t s, ldms_update_cb_t update_cb, void *arg);

#define LDMS_XPRT_PUSH_F_CHANGE	1
/**
 * \brief Register a remote set for push notifications
 *
 * Registers a remote set to receive push updates from the peer. A
 * remote set is one that was returned by a call to
 * ldms_xprt_lookup(). Passing a set created with the ldms_set_new()
 * to this function will return the error EINVAL.
 *
 * If the <tt>push_flags</tt> parameter contains
 * LDMS_XPRT_PUSH_CHANGE, the the cb_fn() function will be called
 * whenever the peer calls ldms_transaction_end() on the remote set,
 * i.e. when peer set updates are complete and coherent. If the
 * LDMS_XPRT_PUSH_CHANGE flag is not set, then the the cb_fn()
 * function will be called only when the peer calls ldms_xprt_push().
 *
 * See the ldms_xprt_cancel_push() function to stop receiving push
 * notifications from the peer if LDMS_XPRT_PUSH_CHANGE is requested.
 *
 * \param s	The set handle returned by ldms_xprt_lookup()
 * \param push_change If !0, the peer will call ldms_xprt_push() for
 *              this set whenever ldms_transaction_end() is called at
 *              the peer for this set.
 * \param cb_fn The function to call when push updates are
 *              received. If <null>, no notifications will be provided
 *              when the set is updated by the peer.
 * \param cb_arg A value provided to the cb_fn() when notifications
 *              are delivered to the application.
 * \returns	0 on success or a non-zero value to indicate an error.
 */
extern int ldms_xprt_register_push(ldms_set_t s, int push_flags,
				   ldms_update_cb_t cb_fn, void *cb_arg);

/**
 * \brief Cancel push updates from the peer for this set.
 *
 * Note that there are implicit race conditions that the caller should
 * be aware of. Specifically, the caller may receive one or more calls
 * to the cb_fn() function after this function has returned depending
 * on whether or not there were outstanding updates in flight at the
 * time the request was processed at the peer. The caller _must_
 * consult the <tt>flags</tt> parameter to the cb_fn() function to
 * know when the last call to cb_fn() has been received.
 *
 * \param s The set handle provided in a previous call to
 *          ldms_xprt_register_push().
 * \retval 0 Success
 * \retval ENOENT The specified set is not registered for push updates
 * \retval EINVAL The specified set is a local set or otherwise invalid
 * \retval ENOTCONN The transport is not connected
 */
extern int ldms_xprt_cancel_push(ldms_set_t s);

/**
 * \brief Send a metric set's contents to a remote peer
 *
 * This function will send push updates to all peers that are
 * registered for push updates. If there are no peers registered for
 * peer updates, this function does nothing.
 *
 * \param s	The metric set handle to push.
 * \returns	0 on success or a non-zero value to indicate an error.
 */
extern int ldms_xprt_push(ldms_set_t s);

typedef struct ldms_stats_entry {
	uint64_t count;
	uint64_t total_us;
	uint64_t min_us;
	uint64_t max_us;
	uint64_t mean_us;
} *ldms_stats_entry_t;

typedef enum ldms_xprt_ops_e {
	LDMS_XPRT_OP_LOOKUP,
	LDMS_XPRT_OP_UPDATE,
	LDMS_XPRT_OP_PUBLISH,
	LDMS_XPRT_OP_SET_DELETE,
	LDMS_XPRT_OP_DIR_REQ,
	LDMS_XPRT_OP_DIR_REP,
	LDMS_XPRT_OP_SEND,
	LDMS_XPRT_OP_RECV,
	LDMS_XPRT_OP_STREAM_PUBLISH,
	LDMS_XPRT_OP_STREAM_SUBSCRIBE,
	LDMS_XPRT_OP_STREAM_UNSUBSCRIBE,
	LDMS_XPRT_OP_COUNT
} ldms_xprt_ops_t;

struct  ldms_op_ctxt {
	enum ldms_xprt_ops_e op_type;
	union {
		struct lookup_profile_s {
			struct timespec app_req_ts;
			struct timespec req_send_ts;
			struct timespec req_recv_ts;
			struct timespec share_ts;
			struct timespec rendzv_ts;
			struct timespec read_ts;
			struct timespec complete_ts;
			struct timespec deliver_ts;
		} lookup_profile;
		struct update_profile {
			struct timespec app_req_ts;
			struct timespec read_ts;
			struct timespec read_complete_ts;
			struct timespec deliver_ts;
		} update_profile;
		struct set_delete_profile_s {
			struct timespec send_ts;
			struct timespec recv_ts;
			struct timespec ack_ts;
		} set_del_profile;
		struct send_profile_s {
			struct timespec app_req_ts;
			struct timespec send_ts;
			struct timespec complete_ts;
			struct timespec deliver_ts;
		} send_profile;
		struct strm_publish_profile_s {
			uint32_t hop_num;
			struct timespec recv_ts;
			struct timespec send_ts; /*  to remote client */
		} stream_pub_profile;
	};
	TAILQ_ENTRY(ldms_op_ctxt) ent;
};
TAILQ_HEAD(ldms_op_ctxt_list, ldms_op_ctxt);

#define PROFILING_CFG_DISABLED    0
#define PROFILING_CFG_ENABLED     1
#define PROFILING_CFG_UNSUPPORTED 2

/**
 * Enable/disable LDMS operations' profiling
 *
 * If profiling is enabled, LDMS collects the following timestamps:
 *   for LOOKUP: when ldms_xprt_lookup() is called,
 *               when LDMS sends the lookup request to the peer,
 *               when the peer receives the lookup request,
 *               when the peer shares the set memory,
 *               when LDMS receives the shared memory,
 *               when LDMS reads the memory,
 *               when LDMS receives the read completion,
 *               and when LDMS delivers the lookup data to the application
 *   for UPDATE: when ldms_xprt_update() is called,
 *               when LDMS reads the set data,
 *               when LDMS receives the updated set data,
 *               when LDMS delivers the update completion to the application
 *   for SEND:   when ldms_xprt_send() is called,
 *               when LDMS sends the data to the peer,
 *               when LDMS receives the send completion event,
 *               when LDMS delivers the send completion to the application
 *   for STREAM_PUBLISH: when ldms_stream_publish() is called,
 *                       when LDMS publishes the stream data,
 *                       when LDMS delivers the stream data to clients
 *                       NOTE: LDMS collects the timestamps at each hop where stream data gets forwarded
 *
 * \param ops_cnt  Number of operations in \c ops.
 *                 -1 to enable/disable profiling of all operations
 * \param ops      Array of operations to enable their profiling
 * \param ops_err  Array to store an error of each given operation
 *
 * \return 0 on success; Otherwise, -1 is given.
 *         In this case, an error code will be assigned in the \c ops_err
 *            ENOSYS if the operation does not support profiling;
 *            EINVAL if the given operation does not exist.
 */
int ldms_profiling_enable(int ops_cnt, enum ldms_xprt_ops_e *ops, int *ops_err);
int ldms_profiling_disable(int ops_cnt, enum ldms_xprt_ops_e *ops, int *ops_err);

extern const char *ldms_xprt_op_names[];

struct ldms_xprt_rate_data {
	double connect_rate_s;
	double connect_request_rate_s;
	double disconnect_rate_s;
	double reject_rate_s;
	double auth_fail_rate_s;
	double duration;
};

enum ldms_thrstat_op_e {
	LDMS_THRSTAT_OP_OTHER, /* Ignore these operations, e.g., notify */
	LDMS_THRSTAT_OP_CONNECT_SETUP,
	LDMS_THRSTAT_OP_DIR_REQ,
	LDMS_THRSTAT_OP_DIR_REPLY,
	LDMS_THRSTAT_OP_LOOKUP_REQ,
	LDMS_THRSTAT_OP_LOOKUP_REPLY,
	LDMS_THRSTAT_OP_UPDATE_REQ,
	LDMS_THRSTAT_OP_UPDATE_REPLY,
	LDMS_THRSTAT_OP_STREAM_MSG,
	LDMS_THRSTAT_OP_STREAM_CLIENT,
	LDMS_THRSTAT_OP_PUSH_REQ,
	LDMS_THRSTAT_OP_PUSH_REPLY,
	LDMS_THRSTAT_OP_SET_DELETE_REQ,
	LDMS_THRSTAT_OP_SET_DELETE_REPLY,
	LDMS_THRSTAT_OP_SEND_MSG,
	LDMS_THRSTAT_OP_RECV_MSG,
	LDMS_THRSTAT_OP_AUTH,
	LDMS_THRSTAT_OP_DISCONNECTED,
	LDMS_THRSTAT_OP_COUNT
};

/*
 * TODO: Revise the comment to explain the intended use of the thr stats structure
 *
 * ldms_xprt ensures that the thread statistics reported account for
 * the time from ldms_xprt receiving an event from Zap until it returns
 * its Zap callback. The time duration includes the time in its
 * application callback. Applications are responsible for keeping
 * track of the time usages by its operations. It may cache the data
 * in \c app_stats field.
 */

struct ldms_thrstat_entry {
	uint64_t total; /* Operation's Aggregated time in micro-seconds */
	int count;
};

struct ldms_thrstat {
	struct timespec last_op_start;
	struct timespec last_op_end;
	enum ldms_thrstat_op_e last_op;
	struct ldms_thrstat_entry ops[LDMS_THRSTAT_OP_COUNT];
};

struct ldms_thrstat_result_entry {
	struct zap_thrstat_result_entry *zap_res;
	uint64_t idle;
	uint64_t zap_time;
	uint64_t ops[LDMS_THRSTAT_OP_COUNT];
	void *app_ctxt;
};

struct ldms_thrstat_result {
	int count;
	struct zap_thrstat_result *_zres;
	struct ldms_thrstat_result_entry entries[0];
};

/**
 * \brief Convert \c enum ldms_thrstat_op_e to a string
 *
 * \return A string of the operation name
 */
char *ldms_thrstat_op_str(enum ldms_thrstat_op_e e);

/**
 * \brief Return thread usage information
 *
 * Return an ldms_thrstat_result structure or NULL on memory allocation failure.
 * This result must be freed with the ldms_thrstat_free_result() function.
 *
 * \return A pointer to an ldms_thrstat_result structure
 */
struct ldms_thrstat_result *ldms_thrstat_result_get();

/**
 * \brief Free an ldms_thrstat_result returned by \c ldms_thrstat_result_get
 */
void ldms_thrstat_result_free(struct ldms_thrstat_result *res);

/**
 * Query daemon telemetry data across transports
 *
 * \param data A pointer to the ldms_xprt_rate_data structure in which
 *             the results will be returned
 * \param reset Set to a non-zero value to reset the stats after
 *             after computing them.
 */
void ldms_xprt_rate_data(struct ldms_xprt_rate_data *data, int reset);

typedef struct ldms_xprt_stats {
	struct timespec connected;
	struct timespec disconnected;
	struct timespec last_op;
	struct ldms_stats_entry ops[LDMS_XPRT_OP_COUNT];
	struct ldms_op_ctxt_list op_ctxt_lists[LDMS_XPRT_OP_COUNT];
} *ldms_xprt_stats_t;

#define LDMS_PERF_M_STATS 1
#define LDMS_PERF_M_PROFILNG 2
#define LDMS_PERF_M_ALL LDMS_PERF_M_STATS | LDMS_PERF_M_PROFILNG

/**
 * \brief Retrieve transport request statistics
 *
 * The function gets the statistics and then reset it if \c reset is not 0.
 * To only reset the statistics, \c stats must be NULL.
 *
 * \param x The transport handle
 * \param stats Pointer to an ldms_xprt_stats structure
 * \param reset Reset the statistics after getting the statistics if not 0
 *
 */
extern void ldms_xprt_stats(ldms_t x, ldms_xprt_stats_t stats, int mask, int reset);

/*
 * Metric template for:
 * - ldms_schema_from_template()
 * - ldms_schema_metric_add_template()
 * - ldms_record_from_template()
 * - ldms_record_metric_add_template()
 */
typedef struct ldms_metric_template_s {
	const char *name;
	int flags;
	enum ldms_value_type type;
	const char *unit;
	uint32_t len; /* array_len for ARRAY, or heap_sz for LIST */
	ldms_record_t rec_def; /* for LDMS_V_RECORD_TYPE or LDMS_V_RECORD_ARRAY */
} *ldms_metric_template_t;

/**
 * \brief Create a metric set schema
 *
 * Create a metric set schema. The schema can later be used to create
 * a metric set. The schema name must be unique.
 *
 * \param name	The set schema name
 * \retval !0 The schema handle.
 * \retval ENOMEM There were insufficient resources to create the schema
 */
extern ldms_schema_t ldms_schema_new(const char *schema_name);

/**
 * \brief Create a metric set schema from metric templates.
 *
 * \param     name The name of the schema.
 * \param[in]  tmp An array of metric templates. The array must be terminated
 *                 with {0,0,0,0}.
 * \param[out] mid An array of int to receive the metric IDs corresponding to
 *                 the metric member in the record.
 *
 * \retval sch  The schema handle.
 * \retval NULL If there is an error (\c errno is also set).
 */
ldms_schema_t ldms_schema_from_template(const char *name,
			struct ldms_metric_template_s tmp[],
			int mid[]);

/**
 * \brief Return the schema name
 *
 * \param schema The schema handle
 * \returns The schema name
 */
const char *ldms_schema_name_get(ldms_schema_t schema);

/**
 * \brief Write a JSON representation of the schema to a file
 *
 * \param schema The schema handle
 * \param fp The FILE pointer
 * \return int 0 on success or errno
 */
extern int ldms_schema_fprint(ldms_schema_t schema, FILE *fp);

 /**
 * \brief Destroy a schema
 *
 * Release the resources consumed by the schema.
 *
 * This has no affect on any metric sets that were created with
 * this schema.
 *
 * \param schema The schema handle.
 */
extern void ldms_schema_delete(ldms_schema_t schema);

/**
 * \brief Return the number of metrics in the schema
 *
 * \param schema
 * \returns The number of metrics in the schema
 */
extern int ldms_schema_metric_count_get(ldms_schema_t schema);

/**
 * \brief Populate \c out with information of metric \c mid in \c schema.
 *
 * Please note that the pointer members in \c out (e.g. \c out->name) point to
 * memory that is owned by \c schema. So, please do *NOT* modify or free the
 * members of \c out. The \c out itself can be freed.
 *
 * \param [in]  schema The schema handle.
 * \param [in]  mid    The metric ID.
 * \param [out] out    The metric template structure output.
 *
 * \retval 0      If succeeded, or
 * \retval ENOENT if \c mid does not exist.
 */
extern int ldms_schema_metric_template_get(ldms_schema_t schema, int mid,
				struct ldms_metric_template_s *out);

/**
 * \brief Like \c ldms_schema_metric_template_get(), but in bulk.
 *
 * This function copies metric template information from \c schema into \c out
 * template array up to \c len metrics. The pointers in the template structure
 * are owned by \c schema, please do not modify or free them. The \c out array,
 * however, can be freed. Similar to \c snprintf(), this function returns a
 * number less than or equal to \c len if succeeded, or a number greater than
 * \c len to indicate the required array length. In the latter case, the
 * template array \c out contains \c len metric templates.
 *
 * \param [in]  schema The schema handle.
 * \param [in]  len    The length of the \c out array buffer.
 * \param [out] out    The output template array.
 *
 * \retval N<=len If succeeded, or
 * \retval N>len  if the output array is not long enough
 *
 */
extern int ldms_schema_bulk_template_get(ldms_schema_t schema, int len,
				struct ldms_metric_template_s out[]);

/**
 * \brief Set the cardinality of the set array created by this schema.
 *
 * \param schema The schema handle.
 * \param card   The cardinality.
 *
 * \retval 0      If succeeded.
 * \retval EINVAL If \c card is invalid.
 */
extern int ldms_schema_array_card_set(ldms_schema_t schema, int card);

/**
 * Create a record type definition.
 *
 * Metric members can be added into the record type definition using
 * \c ldms_record_metric_add(). The record definition must be added into the
 * schema with \c ldms_schema_record_add().
 *
 * \param name   The name of the record type.
 *
 * \retval rec_def The handle of the record type definition.
 */
ldms_record_t ldms_record_create(const char *name);

/**
 * \brief Delete the record type definition.
 *
 * \param rec_def The record type definition handle.
 */
void ldms_record_delete(ldms_record_t rec_def);

/**
 * Add a metric member into the record.
 *
 * \param rec_def   The handle returned by \c ldms_record_create().
 * \param name      The name of the metric. (May not be NULL)
 * \param unit      The unit of the metric. (May be NULL)
 * \param type      The type of the metric. Only support char, basic number
 *                  types and their arrays: LDMS_V_CHAR, LDMS_V_CHAR_ARRAY,
 *                  LDMS_V_U8, LDMS_V_S8, LDMS_V_U8_ARRAY, LDMS_V_S8_ARRAY,
 *                  LDMS_V_U16, LDMS_V_S16, LDMS_V_U16_ARRAY, LDMS_V_S16_ARRAY,
 *                  LDMS_V_U32, LDMS_V_S32, LDMS_V_U32_ARRAY, LDMS_V_S32_ARRAY,
 *                  LDMS_V_U64, LDMS_V_S64, LDMS_V_U64_ARRAY, LDMS_V_S64_ARRAY,
 *                  LDMS_V_F32, LDMS_V_D64, LDMS_V_F32_ARRAY, LDMS_V_D64_ARRAY.
 * \param array_len The number of elements in the case of ARRAY type.
 *
 * \retval metric_id  The metric ID in the record instance to be used with
 *                    ldms_record_XXX APIs.
 */
int ldms_record_metric_add(ldms_record_t rec_def, const char *name,
			   const char *unit, enum ldms_value_type type,
			   size_t array_len);

/**
 * Get the number of metrics in record definition \c rec_def.
 *
 * \note \c ldms_record_card() does the same thing on \c mavl (\c record_inst or
 * \c record_type) from an LDMS set.
 *
 * \param rec_def  The record definition handle (from \c ldms_record_alloc()).
 *
 * \retval N The number of metrics in the record.
 */
int ldms_record_metric_card(ldms_record_t rec_def);


/**
 * Create a record type definition from the record template entries.
 *
 * This is a convenient function that creates a record type definition and add
 * metric members in one go. The \c tmp array must be terminated with
 * {0}. The \c flags field of the template entries is ignored because
 *  a record types is always a meta metric.
 *
 * REMARK: A record metric must NOT be a record or a list.
 *
 * \param     name The name of the record type.
 * \param[in]  tmp An array of metric templates. The array must be terminated
 *                 with {0,0,0,0}.
 * \param[out] mid An array of int to receive the metric IDs corresponding to
 *                 the metric member in the record.
 *
 * \retval rec_def If there is no error, the handle of the record type def.
 * \retval NULL    If there is an error. In this case \c errno will also be set.
 */
ldms_record_t ldms_record_from_template(const char *name,
			struct ldms_metric_template_s tmp[],
			int mid[]);

/**
 * \brief Like \c ldms_record_metric_add(), but using metric template.
 *
 * The \c flags field of the template entries is ignored because a record type
 *  is always a meta metric.
 *
 * \param        s The schema handle.
 * \param[in]  tmp The array of metric templates (terminated with {0}).
 * \param[out] mid The integer array output of metric IDs corresponding to the
 *                 metrics in \c tmp. This can be \c NULL.
 *
 * \retval 0      If there is no error.
 * \retval -errno If there is an error.
 */
int ldms_record_metric_add_template(ldms_record_t rec_def,
			struct ldms_metric_template_s tmp[], int mid[]);

/**
 * \brief Return the heap memory required by a record type
 *
 * This function returns the heap size required by an instance of a
 * record type.
 *
 * \param rec_def  The handle returned by \c ldms_record_create().
 *
 * \retval bytes The size of the record type in the heap.
 */
size_t ldms_record_heap_size_get(ldms_record_t rec_def);

/**
 * \brief Get the size (bytes) of the heap memory storing the record metric values.
 *
 * \param rec_def  The handle returned by \c ldms_record_create().
 *
 * \retval bytes The size of the heap memory
 */
size_t ldms_record_value_size_get(ldms_record_t rec_def);

/**
 * \brief Populate \c out with information of metric \c mid in \c record.
 *
 * Please note that the pointer members in \c out (e.g. \c out->name) point to
 * memory that is owned by \c record. So, please do *NOT* modify or free the
 * members of \c out. The \c out itself can be freed.
 *
 * \param [in]  record The record handle.
 * \param [in]  mid    The metric ID.
 * \param [out] out    The metric template structure output.
 *
 * \retval 0      If succeeded, or
 * \retval ENOENT if \c mid does not exist.
 */
extern int ldms_record_metric_template_get(ldms_record_t record, int mid,
				struct ldms_metric_template_s *out);

/**
 * \brief Like \c ldms_record_metric_template_get(), but in bulk.
 *
 * This function copies metric template information from \c record into \c out
 * template array up to \c len metrics. The pointers in the template structure
 * are owned by \c record, please do not modify or free them. The \c out array,
 * however, can be freed. Similar to \c snprintf(), this function returns a
 * number less than or equal to \c len if succeeded, or a number greater than
 * \c len to indicate the required array length. In the latter case, the
 * template array \c out contains \c len metric templates.
 *
 * \param [in]  record The record handle.
 * \param [in]  len    The length of the \c out array buffer.
 * \param [out] out    The output template array.
 *
 * \retval N<=len If succeeded, or
 * \retval N>len  if the output array is not long enough
 *
 */
extern int ldms_record_bulk_template_get(ldms_record_t record, int len,
				struct ldms_metric_template_s out[]);

extern const char *ldms_record_name_get(ldms_record_t record);

void _ldms_set_ref_get(ldms_set_t s, const char *reason, const char *func, int line);
int _ldms_set_ref_put(ldms_set_t s, const char *reason, const char *func, int line);

/**
 * \brief Get the set reference
 *
 * \param _s   LDMS set handle
 * \param _r   Reason string
 */
#define ldms_set_ref_get(_s_, _r_) _ldms_set_ref_get((_s_), (_r_), __func__, __LINE__)

/**
 * \brief Put the set reference
 *
 * \param _s   LDMS set handle
 * \param _r   Reason string
 */
#define ldms_set_ref_put(_s_, _r_) _ldms_set_ref_put((_s_), (_r_), __func__, __LINE__)

/**
 * \brief Create a Metric set
 *
 * Create a metric set on the local host. The metric set is added to
 * the data base of metric sets exported by this host. The <tt>instance_name</tt>
 * parameter specifies the name of the metric set as it will appear to
 * hosts who query the set dictionary with ldms_dir().
 *
 * Multiple metric sets of the same type (schema) may be created
 * provided that they have different instance names.
 *
 * Upon creation, the metric set will have the authentication values
 * (uid, gid, perm) currently set as the global defaults. After creation
 * but before publishing the metric set, the authentication values may be
 * adjusted from the defaults be using the ldms_set_{uid|gid|perm}_set()
 * functions.
 *
 * The remote peer will not be able to get the set in the directory listing,
 * nor be able to perform \c ldms_xprt_lookup() without a proper
 * owner/group/permission. The permission is 9-bit UNIX style
 * (owner-group-other read-write-execute).
 *
 * \param instance_name	The metric set instance name.
 * \param schema	The metric set schema being created.
 * \param s		Pointer to ldms_set_t handle that will be set to the new handle.
 * \returns Pointer to the new metric set or NULL if there is an error. Errno will be set
 * as appropriate as follows:
 * - ENOMEM Insufficient resources
 * - EEXIST The specified instance name is already used.
 * - EINVAL A parameter or the schema itself is invalid
 */
extern ldms_set_t ldms_set_new(const char *instance_name, ldms_schema_t schema);
extern uint64_t ldms_set_id(ldms_set_t set);

/**
 * \brief Create an LDMS metric set with a heap of the given heap size \c heap_sz
 *
 * ::ldms_set_new() creates a set with a heap of the size cached in the schema \c schema.
 * A set with a different heap size can be created by calling ::ldms_set_new_with_heap().
 * It is useful with a bigger heap is needed. For example, an LDMS_V_LIST metric
 * needs to be bigger than the anticipated size at schema creation time.
 * The existing set must be deleted by calling ::ldms_set_delete() before creating
 * a new set (of the same name) with a bigger heap by calling ::ldms_set_new_with_heap().
 * The new set must be published manually by calling ::ldms_set_publish().
 *
 * \param instance_name   The metric set instance name
 * \param schema          The metric set schema
 * \param heap_sz         The size of the set heap
 *
 * \return Pointer to the new metric set or NULL if there is an error.
 *         Errno will be set as appropriate as follows:
 *         - ENOMEM   Insufficient resources
 *         - EEXIST   The specified instance name is already used.
 *         - EINVAL   A parameter or the schema itself is invalid.
 *
 * \see ldms_set_new(), ldms_set_new_with_auth(), ldms_schema_metric_list_add(),
 *      ldms_set_delete()
 */
extern ldms_set_t ldms_set_new_with_heap(const char *instance_name,
					ldms_schema_t schema,
					uint32_t heap_sz);

/**
 * \brief Create an LDMS metric set with owner and permission
 *
 * Create a metric set, like ::ldms_set_new(), but with a specified owner \c
 * uid-gid and a permission \c perm. The remote peer will not be able to get the
 * set in the directory listing, nor be able to perform \c ldms_xprt_lookup()
 * without a proper owner/group/permission. The permission is 9-bit UNIX style
 * (owner-group-other read-write-execute).
 *
 * \param instance_name The name of the metric set.
 * \param schema        The schema of the set.
 * \param uid           The user ID of the set owner.
 * \param gid           The group ID of the set owner.
 * \param perm          The UNIX mode_t bits (see chmod)
 *
 * \retval NULL If failed.
 * \retval setp The set pointer, if success.
 */
ldms_set_t ldms_set_new_with_auth(const char *instance_name,
				  ldms_schema_t schema,
				  uid_t uid, gid_t gid, mode_t perm);

/**
 * \brief Create an LDMS metric set with owner, permission, and heap size
 *
 * Create a metric set, but with customized \c uid, \c gid, \c perm, and \c heap_sz
 *
 * \param instance_name   The name of the metric set
 * \param schema          The schema of the set
 * \param uid             The user ID of the set owner
 * \param gid             The group ID of the set owner
 * \param perm            The UNIX mode_t bits (see chmod)
 * \param heap_sz         The size of the set heap. If 0 is given,
 *                        the heap size is the size set in \c schema.
 *
 * \return A pointer to a metric set or NULL if there is an error.
 *         Errno will be set as appropriate as follows:
 *         - ENOMEM   Insufficient resources
 *         - EEXIST   The specified instance name is already used.
 *         - EINVAL   A parameter or the schema itself is invalid.
 *
 * \see ldms_set_new(), ldms_set_new_with_auth(), ldms_set_new_with_heap()
 */
ldms_set_t ldms_set_create(const char *instance_name,
				  ldms_schema_t schema,
				  uid_t uid, gid_t gid, mode_t perm,
				  uint32_t heap_sz);


/**
 * \brief Create a lightweight snapshot of an LDMS set
 *
 * A set snapshot is a read-only copy that contains only the metric values, types,
 * and set_info content. It is intended for local use only and cannot be shared
 * with remote clients.
 *
 * Snapshots are implemented as special LDMS sets with the LDMS_SET_F_SNAPSHOT flag.
 * This allows them to work with most LDMS API functions for reading metrics and set info.
 *
 * IMPORTANT: Snapshots should be managed using the dedicated snapshot functions:
 * - ldms_set_snapshot_create() - Create a new snapshot
 * - ldms_set_snapshot_delete() - Delete a snapshot
 * - ldms_set_snapshot_get() - Get a reference to a snapshot
 * - ldms_set_snapshot_put() - Release a reference to a snapshot
 *
 * Do NOT use ldms_set_delete() or ldms_set_put() with snapshots.
 *
 * The standard ldms_metric_* and ldms_set_info_* functions can be used to access metric
 * and set information from a snapshot.
 *
 * Snapshot properties:
 * - Always have an array cardinality of 1. If the original set has a larger
 *   cardinality, the snapshot will contain metric values from the current
 *   index of the original set at the time of creation.
 * - Cannot be published or shared with remote clients
 * - Writing to snapshot metrics is not supported
 * - Maintain their own copies of meta data, data, heap, and set_info
 *
 * \param src   The source set to create a snapshot from
 * \return   A new snapshot with reference count of 1, or NULL on error (ENOMEM)
 *
 * \see ldms_set_snapshot_delete(), ldms_set_snapshot_get(), ldms_set_snapshot_put()
 */
extern ldms_set_t ldms_set_snapshot_create(ldms_set_t src);

/**
 * \brief Delete a lightweight snapshot of an LDMS set
 *
 * Releases the reference held by the creator of the snapshot.
 * When the reference count reaches zero, all memory associated
 * with the snapshot will be freed.
 *
 * \param snapshot  The snapshot to be deleted
 *
 * \note Always use this function to delete snapshots, not ldms_set_delete().
 *
 * \see ldms_set_snapshot_create(), ldms_set_snapshot_put()
 */
extern void ldms_set_snapshot_delete(ldms_set_t snapshot);

/**
 * \brief Acquire a reference to a snapshot
 *
 * Increments the reference count of the snapshot to prevent
 * its deletion while it's in use.
 *
 * \param snapshot   The snapshot to get a reference to
 * \param ref_name   A string identifier for the reference (for debugging)
 *
 * \note Always use this function to get a reference to snapshots,
 *       not the regular set reference functions.
 *
 * \see ldms_set_snapshot_put()
 */
extern void ldms_set_snapshot_get(ldms_set_t snapshot, const char *ref_name);

/**
 * \brief Release a reference to a snapshot
 *
 * Decrements the reference count of the snapshot. When the reference
 * count reaches zero, all memory associated with the snapshot is freed.
 *
 * \param snapshot   The snapshot to release a reference to
 * \param ref_name   The string identifier used when acquiring the reference
 *
 * \note Always use this function to release references to snapshots,
 *       not ldms_set_put().
 *
 * \see ldms_set_snapshot_get()
 */
extern void ldms_set_snapshot_put(ldms_set_t snapshot, const char *ref_name);

/**
 * \brief Return the number of metric sets
 * \returns The number of metric sets
 */
extern int ldms_set_count();
/**
 * \brief Return the number of sets pending deletion
 * \returns The number of sets pending deletion
 */
extern int ldms_set_deleting_count();

/**
 * \addtogroup ldms_set_config LDMS Set Configuration
 *
 * This is a collection of set configuration API.
 *
 * All functions in this group return \c errno to describe the error, or \c 0
 * if they succeed.
 *
 * \{
 */

/**
 * Configure set authorization.
 *
 * If this is not configured, the default values are as following:
 * - \c uid:  \c -1
 * - \c gid:  \c -1
 * - \c perm: \c 0440
 *
 * \param uid  UID
 * \param gid  GID
 * \param perm The UNIX mode_t bits (see chmod)
 * \param flags The bit-wise OR of DEFAULT_AUTHZ_SET_UID, DEFAULT_AUTHZ_SET_GID,
 *              DEFAULT_AUTHZ_SET_PERM, corresponding to the parameters to be set.
 * \retval errno If failed.
 * \retval 0     If succeeded.
 */
int ldms_set_config_auth(ldms_set_t set, uid_t uid, gid_t gid,
			 mode_t perm, int flags);

/**
 * \}  (ldms_set_config)
 */

/**
 * \brief Publish the LDMS set.
 * \param set The set handle.
 * \retval 0      If succeeded.
 * \retval EEXIST If the set has already been published.
 */
int ldms_set_publish(ldms_set_t set);

/**
 * \brief Unpublish the LDMS set.
 * \param set The set handle.
 * \retval 0      If succeeded.
 * \retval ENOENT If the set has not yet published.
 */
int ldms_set_unpublish(ldms_set_t set);

/**
 * \brief Delete the set
 *
 * The set will be deleted when all set references are released.
 *
 * \param s	The metric set handle.
 */
extern void ldms_set_delete(ldms_set_t s);

/**
 * @brief Drop the reference obtained by ldms_set_by_name()
 *
 * @param s The metric set handle
 */
void ldms_set_put(ldms_set_t s);

/**
 * \brief Get the schema name for the set
 *
 * \param s	The set handle
 * \retval !0	Pointer to a string containing the schema name
 * \retval 0	The set handle invalid
 */
extern const char *ldms_set_schema_name_get(ldms_set_t s);

/**
 * \brief Get the instance name for the set
 *
 * \param s	The set handle
 * \retval !0	Pointer to a string containing the instance name
 * \retval 0	The set handle invalid
 */
extern const char *ldms_set_instance_name_get(ldms_set_t s);

/**
 * \brief Get the producer name for the set
 *
 * \param s	The set handle
 * \returns	The producer name for the set.
 */
extern const char *ldms_set_producer_name_get(ldms_set_t s);

/**
 * \brief Set the producer name for the set
 *
 * \param s	The set handle
 * \param id	The producer name for the set.
 *
 * \returns	0 on success
 * \returns	EINVAL if the given name including
 *		the terminating null byte is longer
 *		than the LDMS_PRODUCER_NAME_MAX.
 */
extern int ldms_set_producer_name_set(ldms_set_t s, const char *name);

/**
 * \brief Map a metric set for remote access
 *
 * This service is used to map a local metric set for access on the
 * network. The <tt>addr</tt> parameter specifies the address of the memory
 * containing the metric set. This service is typically used to export
 * metric sets that are created in the kernel.
 *
 * \param meta_addr	Address of the metric set meta data
 * \param data_addr	Address of the metric set data
 * \param s		Pointer to memory to receive handle.
 * \returns 0		Success
 */
extern int ldms_mmap_set(void *meta_addr, void *data_addr, ldms_set_t *s);

/**
 * \brief Get the set name
 *
 * \param s The ldms_set_t handle.
 * \returns Pointer to the set name
 */
extern const char *ldms_set_name_get(ldms_set_t s);

/**
 * \brief Get the number of metrics in the set.
 *
 * \param s	The ldms_set_t handle.
 * \return The number of metrics in the set
 */
extern uint32_t ldms_set_card_get(ldms_set_t s);

/**
 * \brief Return a copy of the digest of the set schema
 *
 * This function returns a copy of the metric set's
 * schema digest. The \c digest is a SHA256 hash of the
 * set schema's metric names and types. This has can be
 * used to confirm that two metric sets have the same
 * schema.
 *
 * The ldms_set_digest_cmp() function should be used to
 * compare two digests.
 *
 * \param s The set handle
 * \return The schema digest
 */
#define LDMS_DIGEST_LENGTH SHA256_DIGEST_LENGTH
#define LDMS_DIGEST_STR_LENGTH ((2 * LDMS_DIGEST_LENGTH) + 1)
struct ldms_digest_s {
	unsigned char digest[LDMS_DIGEST_LENGTH];
};
typedef struct ldms_digest_s *ldms_digest_t;
extern ldms_digest_t ldms_set_digest_get(ldms_set_t s);

/**
 * \brief Return a digest formatted as a string
 *
 * \param digest  The digest
 * \param buf     The output buffer
 * \param buf_len The buffer length - must be >= LDMS_DIGEST_STR_LENGTH
 *
 * \retval NULL If there is an error (\c errno describing the error)
 * \retval buf  If succeeded, the output buffer containing formatted digest
 */
extern const char *ldms_digest_str(ldms_digest_t digest, char *buf, int buf_len);

/**
 * Populate \c digest according to hex string representation.
 *
 * \param [in]  str    The hexadecimal string representation of the digest.
 * \param [out] digest The digest.
 *
 * \retval 0     If there are no errors, or
 * \retval errno If there is an error.
 */
int ldms_str_digest(const char *str, ldms_digest_t digest);

/**
 * \brief Compare LDMS digests
 *
 * This function compares two digests and
 * returns 0 if they are equal or !0 if they are
 * not equal.
 *
 * \param a The lhs digest
 * \param b The rhs digest
 * \return 0 The digests are equal
 * \return <0 The lhs is less than the rhs
 * \return >0 The rhs is greater than the lhs
 */
extern int ldms_digest_cmp(ldms_digest_t a, ldms_digest_t b);

/**
 * \brief Retreive the UID of the LDMS set.
 * \param s The set handle.
 * \retval uid The UID of the set.
 */
uint32_t ldms_set_uid_get(ldms_set_t s);

/**
 * \brief Set the UID of the LDMS set.
 * \param s The set handle.
 * \param uid The UID to set
 * \retval errno If failed.
 * \retval 0     If succeeded.
 */
int ldms_set_uid_set(ldms_set_t s, uid_t uid);

/**
 * \brief Retreive the GID of the LDMS set.
 * \param s The set handle.
 * \retval gid The GID of the set.
 */
uint32_t ldms_set_gid_get(ldms_set_t s);

/**
 * \brief Set the GID of the LDMS set.
 * \param s The set handle.
 * \param uid The GID to set
 * \retval errno If failed.
 * \retval 0     If succeeded.
 */
int ldms_set_gid_set(ldms_set_t s, gid_t gid);

/**
 * \brief Retreive the permission of the LDMS set.
 * \param s The set handle.
 * \retval perm The permission of the set.
 */
uint32_t ldms_set_perm_get(ldms_set_t s);

/**
 * \brief Set the permissions of the LDMS set.
 * \param s The set handle.
 * \param perm The UNIX mode_t bits (see chmod)
 * \retval errno If failed.
 * \retval 0     If succeeded.
 */
int ldms_set_perm_set(ldms_set_t s, mode_t perm);

#define DEFAULT_AUTHZ_SET_UID 0x4
#define DEFAULT_AUTHZ_SET_GID 0x2
#define DEFAULT_AUTHZ_SET_PERM 0x1
#define DEFAULT_AUTHZ_SET_ALL (SET_DEFAULT_AUTHZ_UID|SET_DEFAULT_AUTHZ_GID|SET_DEFAULT_AUTHZ_PERM)
#define DEFAULT_AUTHZ_READONLY 0

/**
 * \brief Set the security parameters of the LDMS set matched the given regex
 *
 * The API changes the UID, GID, and permissions of the sets that their names
 * are matched the regular expression. However, the API ignores remote sets.
 * Remote sets are sets that have been aggregated from a remote daemon.
 *
 * \param regex     A regular expression handle
 * \param uid       UID to set if DEFAULT_AUTHZ_SET_UID bit is set in set_flags
 * \param gid       GID to set if DEFAULT_AUTHZ_SET_GID bit is set in set_flags
 * \param perm      Permissions to set if DEFAULT_AUTHZ_SET_PERM bit is set in set_flags
 * \param set_flags
 *
 * \retval 0 If succeeded.
 * \retval errno If failed.
 */
int ldms_set_regex_sec_set(regex_t regex, uid_t uid, gid_t gid,
			   mode_t perm, int set_flags);

/**
 * \brief Atomically set or get one or more default authorization values for LDMS sets.
 *
 * The uid, gid, and perm options can each individually be inputs or outputs. To make
 * uid, gid, or perm as inputs set the bits DEFAULT_AUTHZ_SET_UID,
 * DEFAULT_AUTHZ_SET_GID, or SET_DEFAULT_SET_AUTHZ_PERM bits respectively in the
 * set_flags option.  DEFAULT_AUTHZ_SET_ALL is provided as a convenience to set all three
 * bits.
 *
 * Any option for which the associated set_flags is not set will be an _output_,
 * atomically reporting the current value.
 *
 * \param uid UID default to set if SET_DEFAULT_AUTHZ_UID bit is set in set_flags
 * \param gid GID default to set if SET_DEFAULT_AUTHZ_GID bit is set in set_flags
 * \param perm Permissions default to set if SET_DEFAULT_AUTHZ_PERM bit is set in set_flags
 * \param set_flags
 */
void ldms_set_default_authz(uid_t *uid, gid_t *gid, mode_t *perm, int set_flags);

/**
 * \brief Get the size in bytes of the set's meta data
 * \param s	The ldms_set_t handle.
 * \return The size of the meta-data in bytes
 */
extern uint32_t ldms_set_meta_sz_get(ldms_set_t s);

/**
 * \brief Get the size in bytes of the set's data (including heap)
 * \param s	The ldms_set_t handle.
 * \return The size of the set's data in bytes.
 */
extern uint32_t ldms_set_data_sz_get(ldms_set_t s);

/**
 * \brief Get a set by name.
 *
 * Find a local metric set by name. A local set is one that is in
 * local memory either through ldms_xprt_lookup() or ldms_set_new().
 *
 * \param set_name	The set name.
 * \returns		The ldms_set_t handle or 0 if not found.
 */
extern ldms_set_t ldms_set_by_name(const char *set_name);

/**
 * \brief Get a set by name on a particular transport
 *
 * \param s		The transport set handle
 * \param set_name	The set name
 * \returns		The ldms_set_t handle or 0 if not found
 */
extern ldms_set_t ldms_xprt_set_by_name(ldms_t x, const char *set_name);

/**
 * \brief Get the metric schema generation number.
 *
 * A metric set has a \c generation number that chnages when metrics
 * are added or removed from the metric set.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The 64bit meta data generation number.
 */
uint64_t ldms_set_meta_gn_get(ldms_set_t s);

/**
 * \brief Get the metric data generation number.
 *
 * A metric set has a \c generation number that chnages when metric
 * values are modified.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The 64bit data generation number.
 */
uint64_t ldms_set_data_gn_get(ldms_set_t s);

/**
 * \brief Get the heap generation number.
 *
 * The heap generation number is incremented when \c ldms_heap_alloc() or
 * \c ldms_heap_free() are called.
 *
 * \param s	The ldms_set_t handle.
 * \returns	The 64bit heap generation number.
 */
uint64_t ldms_set_heap_gn_get(ldms_set_t s);

/**
 * \brief Get the heap size
 *
 * \param s    The ldms_set_t handle.
 * \return     The size of the set's heap
 */
uint64_t ldms_set_heap_size_get(ldms_set_t s);

/**
 * \brief Tell LDMS to copy previous data in the set array on transaction begin.
 *
 * When \c ldms_transaction_begin() is called, the set data points to the next
 * data slot in the set array and the metric modification by the application
 * will be applied to the new data slow. By default, the new data slot is left
 * as-is is. This presumes that the \c sample() function will update all list
 * entry data at each invocation.
 *
 * By calling \c ldms_set_data_copy_set(s, 1), LDMS will copy the data from the
 * previous slot into the new slot at \c ldms_transaction_begin().
 *
 * For the the heap data (in which ldms_list and its elements reside),
 * if the heap structure has changed (i.e. `ldms_list_append_item()` or
 * `ldms_list_remove_item()` was called), the heap section will be copied over to the
 * new slot regardless of the data copy flag to preserve the heap structure.
 * Note that in the case of data manipulation w/o heap structure changes (no
 * calling to `ldms_list_append_item()` nor `ldms_list_remove_item()`), the data won't be
 * copied over if the copy flag is not set to on.
 *
 * \param s  The \c ldms_set_t handle.
 * \param on_n_off \c 1 for turning data copy flag on, or \c 0 for turning it off.
 */
void ldms_set_data_copy_set(ldms_set_t s, int on_n_off);

/** \} */

/**
 * \addtogroup ldms_metric LDMS Metric Manaegment
 *
 * These functions are used to create and destroy local metrics
 * and to get and set the value of a metric.
 * \{
 */

/**
 * \brief Begin an LDMS transaction
 *
 * Start a metric set update transaction. This marks the set as
 * inconsistent. A remote peer that fetches a metric set can use the
 * ldms_set_is_consistent() function to determine if the metric was in
 * the process of being updated when it was fetched.
 *
 * \param s     The ldms_set_t handle.
 * \returns 0   If the transaction was started.
 * \returns !0  If the specified metric set is invalid.
 */
extern int ldms_transaction_begin(ldms_set_t s);

/**
 * \brief Complete an LDMS transaction
 *
 * Marks the metric set as consistent and time-stamps the data.
 *
 * \param s     The ldms_set_t handle.
 * \returns 0   If the transaction was started.
 * \returns !0  If the specified metric set is invalid.
 */
extern int ldms_transaction_end(ldms_set_t s);

/**
 * \brief Get the time the transaction ended
 *
 * Returns an ldms_timestamp structure that specifies when
 * ldms_transaction_end() was last called by the metric set provider. If
 * the metric set provider does not update it's metric sets inside
 * transactions, then this value is invalid. This value is undefined
 * if the metric set is not consistent, see ldms_set_is_consistent().
 *
 * \param s     The metric set handle
 * \returns ts  A pointer to a timestamp structure.
 */
extern struct ldms_timestamp ldms_transaction_timestamp_get(ldms_set_t s);

/**
 * \brief Get the duration of the last transaction
 *
 * Returns an ldms_timestamp structure that specifies the time between
 * ldms_transaction_begin() and ldms_transaction_end(). This
 * measures how long the sampler took to update the metric set.
 *
 * \param s     The metric set handle
 * \returns ts  A pointer to a timestamp structure.
 */
extern struct ldms_timestamp ldms_transaction_duration_get(ldms_set_t s);

/**
 * \brief Get the time difference as a double in seconds.
 *
 * Computes the always positive (or zero) time stamp difference.
 * Negative differences are clipped to zero.
 *
 * \param after timestamp that is more recent than before.
 * \param before timestamp that is older or equal to after.
 * \returns dt the time difference in seconds, or -1.0 if NULL input.
 */
extern double ldms_difftimestamp(const struct ldms_timestamp *after, const struct ldms_timestamp *before);

/**
 * \brief Returns TRUE if the metric set is consistent.
 *
 * A metric set is consistent if it is not in the middle of being
 * updated. This is indicated by the metric set provider if they are
 * using transaction boundaries on metric set updates: see \c
 * ldms_begin_transaction and \c ldms_end_transaction. Using
 * transactions to update metric sets is computatationaly inexpensive,
 * but optional.
 */
extern int ldms_set_is_consistent(ldms_set_t s);

#define LDMS_SET_INFO_F_LOCAL 0
#define LDMS_SET_INFO_F_REMOTE 1

/**
 * \brief Add an key-value pair set information
 *
 * If the key exists, the function resets the value to the new value \c value.
 * Adding a key-value pair potentially hides the key-value pair
 * gotten from a lookup response that has the same key.
 *
 * \c ldms_set_info_unset() can be used to unset the value. Afterward, \c ldms_set_info_get()
 * can be used to get the value gotten from the lookup reply.
 *
 * \param s	The set handle
 * \param key	The name of the information
 * \param value	The information
 *
 * \return 0 on success. ENOMEM if malloc fails. EINVAL if \c s does not exist.
 *
 * \see ldms_set_info_unset, ldms_set_info_get
 */
extern int ldms_set_info_set(ldms_set_t s, const char *key, const char *value);

/**
 * \brief Unset the value of of the given key.
 *
 * A key value pair which is set by \c ldms_set_info_set will be unset. Applications
 * may use the function to unset a value to access the value received from a lookup reply
 * with the same key.
 *
 * The key-value pairs from lookup replies will be untouched.
 *
 * \param s	The set handle
 * \param key	The key string
 *
 * \see ldms_set_info_set
 */
extern void ldms_set_info_unset(ldms_set_t s, const char *key);

/**
 * \brief Return a copy of the value of the given key
 *
 * \param s	The set handle
 * \param key	The key
 *
 * \return The value of the key. NULL is returned if the key does not exist or
 *         there is an error.
 */
extern char *ldms_set_info_get(ldms_set_t s, const char *key);

/**
 * \brief Walk through the set information key-value pairs
 *
 * \param s	The set handle
 * \param cb	The callback function to perform for each key value pair
 * \param flag  LDMS_SET_INFO_F_LOCAL or LDMS_SET_INFO_F_REMOTE
 * \param cb_arg	The callback argument
 *
 * \return 0 on success. Otherwise, an error code is returned.
 */
typedef int (*ldms_set_info_traverse_cb_fn)(const char *key, const char *value,
								void *cb_arg);
extern int ldms_set_info_traverse(ldms_set_t s, ldms_set_info_traverse_cb_fn cb,
							int flag, void *cb_arg);

/**
 * \brief Return the thread ID handles the lookup and update complete events of the set
 *
 * \param s     The set handle
 *
 * \return The thread ID. -1 is returned if the set is not created by lookup.
 */
extern pid_t ldms_set_thread_id_get(ldms_set_t s);

/**
 * \brief Add a metric to schema
 *
 * Adds a metric to a metric set schema. The \c name of the metric must be
 * unique within the metric set.
 *
 * \param s	The schema handle.
 * \param name	The name of the metric.
 * \param t	The type of the metric.
 * \retval >=0  The metric index.
 * \retval <0	Insufficient resources or duplicate name
 */
extern int ldms_schema_metric_add(ldms_schema_t s, const char *name, enum ldms_value_type t);

/**
 * \brief Like \c ldms_schema_metric_add(), but using metric template.
 *
 * \param        s The schema handle.
 * \param[in]  tmp The array of metric templates (terminated with {0}).
 * \param[out] mid The integer array output of metric IDs corresponding to the
 *                 metrics in \c tmp. This can be \c NULL.
 *
 * \retval      0 If there is no error.
 * \retval -errno If there is an error.
 */
extern int ldms_schema_metric_add_template(ldms_schema_t s,
				struct ldms_metric_template_s tmp[], int mid[]);

/**
 * \brief Add an attribute to schema
 *
 * Adds a meta-metric to a set schema. The \c name of the meta-metric must be
 * unique within the set.
 *
 * \param s	The ldms_set_t handle.
 * \param name	The name of the attribute.
 * \param t	The type of the attribute.
 * \retval >=0  The attribute index.
 * \retval <0	Insufficient resources or duplicate name
 */
extern int ldms_schema_meta_add(ldms_schema_t s,
				const char *name, enum ldms_value_type t);

/**
 * \brief Add an array metric to schema
 *
 * Adds a metric of an array type to a metric set schema.
 * The \c name of the metric must be
 * unique within the metric set.
 *
 * \param s	The ldms_set_t handle.
 * \param name	The name of the metric.
 * \param t	The type of the metric.
 * \param count The number of elements in the array
 * \retval >=0  The metric index.
 * \retval <0	Insufficient resources or duplicate name
 */
extern int ldms_schema_metric_array_add(ldms_schema_t s, const char *name,
		enum ldms_value_type t, uint32_t count);
extern int ldms_schema_meta_array_add(ldms_schema_t s, const char *name,
		enum ldms_value_type t, uint32_t count);

/**
 * \brief Add a metric/meta and its unit to schema
 *
 * Adds a metric to a metric set schema. The \c name of the metric must be
 * unique within the metric set.
 *
 * The APIs complements \c ldms_schema_metric_add
 * and \c ldms_schema_meta_add in the sense that
 * the caller could provide the metric unit in this API.
 *
 * \param s	The ldms_set_t handle.
 * \param name	The name of the metric.
 * \param unit  The metric unit.
 * \param t	The type of the metric.
 * \retval >=0  The metric index.
 * \retval <0	Insufficient resources or duplicate name
 *
 * \see ldms_schema_metric_add
 */
extern int ldms_schema_metric_add_with_unit(ldms_schema_t s, const char *name,
					    const char *unit, enum ldms_value_type type);
extern int ldms_schema_meta_add_with_unit(ldms_schema_t s, const char *name,
					  const char *unit, enum ldms_value_type t);

size_t ldms_metric_value_size_get(enum ldms_value_type t, uint32_t count);

/**
 * \brief Return the heap bytes required
 *
 * Given an expected list size and entry type, return the number of
 * heap bytes required. This is useful for providing hints to the
 * ldms_schema_metric_list_add() function. Providing enough memory for
 * the heap when the set is created will avoid unnecessary set memory
 * relocations. A set relocation results in the destruction of the
 * set, and associated ldms_xprt_lookup, and set recreation at the
 * peer.
 *
 * \param type The type of entries added to the list
 * \param item_count The expected list cardinatity
 * \param array_count if \c type is an array, the expected size of each array
 * \returns The heap size required for item_count entries of type
 */
size_t ldms_list_heap_size_get(enum ldms_value_type type, size_t item_count, size_t array_count);

/**
 * \brief Add a metric list to schema
 *
 * Adds a metric list to a metric set schema.
 * The \c name of the metric must be unique within the metric set.
 *
 * \param s	The ldms_set_t handle.
 * \param name	The name of the metric.
 * \param units A 7-character unit string. May be \c NULL.
 * \param heap_sz The number of heap bytes to reserve for the list
 * \retval >=0  The metric index.
 * \retval <0	Insufficient resources or duplicate name
 */
int ldms_schema_metric_list_add(ldms_schema_t s, const char *name,
				const char *units, uint32_t heap_sz);

/**
 * Add the record definition into the schema.
 *
 * \param s       The schema handle
 * \param rec_def The record definition handle.
 *
 * \retval >=0  The metric index in the schema referring to the record type
 * \retval <0	Insufficient resources or duplicate name
 */
int ldms_schema_record_add(ldms_schema_t s, ldms_record_t rec_def);

/**
 * Add an array of records to the schema.
 *
 * The \c rec_def must be completed (i.e. no more metrics adding to it)
 * and must already be added to the schema (via \c ldms_schema_record_add()).
 *
 * \param s         The schema handle.
 * \param rec_def   The record definition.
 * \param array_len The length of the array.
 *
 * \retval >=0  The metric index.
 * \retval <0   The negative error number (\c -errno) describing the error.
 */
int ldms_schema_record_array_add(ldms_schema_t s, const char *name, ldms_record_t rec_def, int array_len);

/**
 * \brief Add an array metric/meta with the unit to schema
 *
 * Adds a metric of an array type to a metric set schema.
 * The \c name of the metric must be
 * unique within the metric set.
 *
 * The APIs complements \c ldms_schema_metric_array_add
 * and \c ldms_schema_meta_array_add in the sense that
 * the caller could provide the metric unit in this API.
 *
 * \param s	The ldms_set_t handle.
 * \param name	The name of the metric.
 * \param unit  The metric unit.
 * \param t	The type of the metric.
 * \param count The number of elements in the array
 * \retval >=0  The metric index.
 * \retval <0	Insufficient resources or duplicate name
 */
extern int ldms_schema_metric_array_add_with_unit(ldms_schema_t s, const char *name,
						  const char *unit,
						  enum ldms_value_type t,
						  uint32_t count);
extern int ldms_schema_meta_array_add_with_unit(ldms_schema_t s, const char *name,
						const char *unit,
						enum ldms_value_type t,
						uint32_t count);


/**
 * \brief Get the metric index given a name
 *
 * Returns the metric index  for the metric with the specified
 * name. This index can then be used with the ldms_metric_get_type() functions
 * to return the value of the metric.
 *
 * \param s	The metric set handle
 * \param name	The name of the metric.
 * \returns	The metric set handle or -1 if there is none was found.
 */
extern int ldms_metric_by_name(ldms_set_t s, const char *name);

/**
 * \brief Returns the name of a metric.
 *
 * Returns the name of the metric specified by the handle.
 *
 * \param s	The set handle
 * \param i	The metric index
 * \returns	A character string containing the name of the metric.
 */
extern const char *ldms_metric_name_get(ldms_set_t s, int i);

/**
 * \brief Returns the unit of a metric
 *
 * Returns the unit of the metric specified by the handle. NULL is returned if
 * the metric unit does not exist.
 *
 * \param s	The set handle
 * \param i	The metric index
 * \returns	A character string containing the unit of the metric.
 *              NULL if the unit does not specified.
 */
extern const char *ldms_metric_unit_get(ldms_set_t s, int i);

/**
 * \brief Returns !0 (true) if the type is an array
 *
 * \param t	The metric type
 * \returns	TRUE(!0) if the type is an array
 */
int ldms_type_is_array(enum ldms_value_type t);

/**
 * \brief Returns the type of a metric.
 *
 * Returns the type of the metric specified by the handle.
 *
 * \param s	The set handle
 * \param i	The metric index
 * \returns	The ldms_value_type for the metric.
 */
extern enum ldms_value_type ldms_metric_type_get(ldms_set_t s, int i);
/**
 * \brief Return the metric's flags
 *
 * The metric flags specify how the metric data is
 * stored. LDMS_MDESC_F_DATA is stored in the data section and is
 * retrieved whenever ldms_xprt_update() is called. LDMS_MDESC_F_META
 * metric's data is stored in the meta-data section and is only
 * retrieved when it is modified.
 *
 * \param s The metric set handle
 * \param i The metric id
 * \returns The metric's flags
 */
int ldms_metric_flags_get(ldms_set_t s, int i);

/**
 * \brief Get a metric type as a string.
 *
 * Returns a string representing the data type.
 *
 * \param t	The metric value type.
 * \returns	A character string representing the metric value type.
 */
extern const char *ldms_metric_type_to_str(enum ldms_value_type t);

/**
 * \brief Get a metric type primitive base
 *
 * Returns the scalar ldms_value_type related to t.
 *
 * \param t	The metric value type which may be array or scalar.
 * \returns	the scalar type.
 */
extern enum ldms_value_type ldms_metric_type_to_scalar_type(enum ldms_value_type t);

/**
 * \brief Convert a string to an ldms_value_type
 *
 * \param name	Character string representing the type name.
 * \returns	The associated ldms_value_type.
 */
extern enum ldms_value_type ldms_metric_str_to_type(const char *name);

/**
 * \brief Set the user-data associated with a metric
 *
 * Sets the user-defined meta data associated with a metric.
 *
 * \param s     The set handle.
 * \param i	The metric index
 * \param u     An 8B user-data value.
 */
void ldms_metric_user_data_set(ldms_set_t s, int i, uint64_t u);

/**
 * \brief Get the user-data associated with a metric
 *
 * Returns the user-defined metric meta-data set with
 * \c ldms_set_user_data.
 *
 * \param s     The set handle
 * \param i	The metric index
 * \returns u   The user-defined metric meta data value.
 */
uint64_t ldms_metric_user_data_get(ldms_set_t s, int i);

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the value contained in the union
 * \c v. The value type in the union must match the value type used to
 * create the metric. See the ldms_get_metric_type() for information
 * on how to discover the type of a metric.
 *
 * \param s	The set handle.
 * \param i	The metric index
 * \param v	An ldms_value union specifying the value.
 */
void ldms_metric_set(ldms_set_t s, int i, ldms_mval_t v);

/**
 * \brief Mark a metric set as modified
 *
 * If the application wishes to modify the metric value directly, it
 * must indicate to the containing set that a change has been
 * made. This allows a peer using ldms_xprt_update() to recognize that
 * a set's contents have changed. If the metric is a meta-metric, the
 * meta-data generation number is updated. If the metric is a
 * data-metric, the data generation number is updated.
 *
 * \param s The metric set handle
 * \param i The metric index
 */
void ldms_metric_modify(ldms_set_t s, int i);

/**
 * \brief Set multiple values of an array
 *
 * \param s		The set handle.
 * \param metric_idx	The metric index
 * \param v		An ldms_value union specifying the value.
 * \param start		The first index in the array
 * \param count         The number of elements in the array to set
 */
void ldms_metric_array_set(ldms_set_t s, int metric_idx, ldms_mval_t v,
			   size_t start, size_t count);

/**
 * \brief Set the value of an element in the array metric
 *
 * \param s		The set handle.
 * \param metric_idx	The metric index
 * \param v		An ldms_value union specifying the value.
 */
void ldms_metric_array_set_val(ldms_set_t s,
			       int metric_idx, int array_idx, ldms_mval_t src);


/**
 * \brief Tell arrayness of metric.
 *
 * \param s		The set handle.
 * \param metric_idx	The metric index
 * \retval 0 if ith metric is not an array.
 */
int ldms_metric_is_array(ldms_set_t s, int i);

/**
 * \brief Return the ldms_mval_t for the specified metric
 *
 * \note ldms expects the elements in the array to be little-endian.
 *
 * \param s The set handle.
 * \param i The metric Id.
 * \retval ldms_mval_t for the metric.
 */
ldms_mval_t ldms_metric_get(ldms_set_t s, int i);

/**
 * \brief Get the address of the array metric in ldms set \c s.
 *
 * \note ldms expects the elements in the array to be little-endian.
 * For per-element get/set please see ldms_aray_metric_get_*TYPE*()
 * and ldms_metric_array_set_*TYPE*() functions.
 *
 * \param s The set handle.
 * \param i The metric ID.
 * \retval ptr The pointer to the array in the set.
 */
ldms_mval_t ldms_metric_array_get(ldms_set_t s, int i);

/**
 * \brief Get length of the array metric.
 *
 * \param s The set handle.
 * \param i The metric ID.
 * \retval len The length (number of elements) of the array.
 */
uint32_t ldms_metric_array_get_len(ldms_set_t s, int i);

/**
 * \brief Set the value of a metric.
 *
 * Set the specified metric \c m to the value specified
 * by \c v.
 *
 * \param s	The set handle.
 * \param i	The metric index
 * \param v	The value.
 */
void ldms_metric_set_char(ldms_set_t s, int i, char v);
void ldms_metric_set_u8(ldms_set_t s, int i, uint8_t v);
void ldms_metric_set_u16(ldms_set_t s, int i, uint16_t v);
void ldms_metric_set_u32(ldms_set_t s, int i, uint32_t v);
void ldms_metric_set_u64(ldms_set_t s, int i, uint64_t v);
void ldms_metric_set_s8(ldms_set_t s, int i, int8_t v);
void ldms_metric_set_s16(ldms_set_t s, int i, int16_t v);
void ldms_metric_set_s32(ldms_set_t s, int i, int32_t v);
void ldms_metric_set_s64(ldms_set_t s, int i, int64_t v);
void ldms_metric_set_float(ldms_set_t s, int i, float v);
void ldms_metric_set_double(ldms_set_t s, int i, double v);

void ldms_metric_array_set_str(ldms_set_t s, int mid, const char *str);
void ldms_metric_array_set_char(ldms_set_t s, int mid, int idx, char v);
void ldms_metric_array_set_u8(ldms_set_t s, int mid, int idx, uint8_t v);
void ldms_metric_array_set_u16(ldms_set_t s, int mid, int idx, uint16_t v);
void ldms_metric_array_set_u32(ldms_set_t s, int mid, int idx, uint32_t v);
void ldms_metric_array_set_u64(ldms_set_t s, int mid, int idx, uint64_t v);
void ldms_metric_array_set_s8(ldms_set_t s, int mid, int idx, int8_t v);
void ldms_metric_array_set_s16(ldms_set_t s, int mid, int idx, int16_t v);
void ldms_metric_array_set_s32(ldms_set_t s, int mid, int idx, int32_t v);
void ldms_metric_array_set_s64(ldms_set_t s, int mid, int idx, int64_t v);
void ldms_metric_array_set_float(ldms_set_t s, int mid, int idx, float v);
void ldms_metric_array_set_double(ldms_set_t s, int mid, int idx, double v);

/**
 * \brief Get the value of a metric.
 *
 * Get the specified metric \c m
 *
 * \param s	The set handle.
 * \param i	The metric index
 * \returns	Unsigned byte value from the metric.
 */
char ldms_metric_get_char(ldms_set_t s, int i);
uint8_t ldms_metric_get_u8(ldms_set_t s, int i);
uint16_t ldms_metric_get_u16(ldms_set_t s, int i);
uint32_t ldms_metric_get_u32(ldms_set_t s, int i);
uint64_t ldms_metric_get_u64(ldms_set_t s, int i);
int8_t ldms_metric_get_s8(ldms_set_t s, int i);
int16_t ldms_metric_get_s16(ldms_set_t s, int i);
int32_t ldms_metric_get_s32(ldms_set_t s, int i);
int64_t ldms_metric_get_s64(ldms_set_t s, int i);
float ldms_metric_get_float(ldms_set_t s, int i);
double ldms_metric_get_double(ldms_set_t s, int i);

const char *ldms_metric_array_get_str(ldms_set_t s, int id);
char ldms_metric_array_get_char(ldms_set_t s, int id, int idx);
uint8_t ldms_metric_array_get_u8(ldms_set_t s, int id, int idx);
uint16_t ldms_metric_array_get_u16(ldms_set_t s, int id, int idx);
uint32_t ldms_metric_array_get_u32(ldms_set_t s, int id, int idx);
uint64_t ldms_metric_array_get_u64(ldms_set_t s, int id, int idx);
int8_t ldms_metric_array_get_s8(ldms_set_t s, int id, int idx);
int16_t ldms_metric_array_get_s16(ldms_set_t s, int id, int idx);
int32_t ldms_metric_array_get_s32(ldms_set_t s, int id, int idx);
int64_t ldms_metric_array_get_s64(ldms_set_t s, int id, int idx);
float ldms_metric_array_get_float(ldms_set_t s, int id, int idx);
double ldms_metric_array_get_double(ldms_set_t s, int id, int idx);

/**
 * \brief Set the value of an ldms_mval_t.
 *
 * Set \c mval to the value specified by \c v.
 * When working with an array, \c mval must have enough space to set the value \c v
 * at the index \c idx.
 *
 * \param mv	A metric value
 * \param v	The value
 * \param idx	Array index
 * \param count	Array length
 */
void ldms_mval_set_char(ldms_mval_t mv, char v);
void ldms_mval_set_u8(ldms_mval_t mv, uint8_t v);
void ldms_mval_set_u16(ldms_mval_t mv, uint16_t v);
void ldms_mval_set_u32(ldms_mval_t mv, uint32_t v);
void ldms_mval_set_u64(ldms_mval_t mv, uint64_t v);
void ldms_mval_set_s8(ldms_mval_t mv, int8_t v);
void ldms_mval_set_s16(ldms_mval_t mv, int16_t v);
void ldms_mval_set_s32(ldms_mval_t mv, int32_t v);
void ldms_mval_set_s64(ldms_mval_t mv, int64_t v);
void ldms_mval_set_float(ldms_mval_t mv, float v);
void ldms_mval_set_double(ldms_mval_t mv, double v);
void ldms_mval_set_ts(ldms_mval_t mv, struct ldms_timestamp v);
void ldms_mval_array_set_str(ldms_mval_t mv, const char *str, size_t count);
void ldms_mval_array_set_char(ldms_mval_t mv, int idx, char v);
void ldms_mval_array_set_u8(ldms_mval_t mv, int idx, uint8_t v);
void ldms_mval_array_set_u16(ldms_mval_t mv, int idx, uint16_t v);
void ldms_mval_array_set_u32(ldms_mval_t mv, int idx, uint32_t v);
void ldms_mval_array_set_u64(ldms_mval_t mv, int idx, uint64_t v);
void ldms_mval_array_set_s8(ldms_mval_t mv, int idx, int8_t v);
void ldms_mval_array_set_s16(ldms_mval_t mv, int idx, int16_t v);
void ldms_mval_array_set_s32(ldms_mval_t mv, int idx, int32_t v);
void ldms_mval_array_set_s64(ldms_mval_t mv, int idx, int64_t v);
void ldms_mval_array_set_float(ldms_mval_t mv, int idx, float v);
void ldms_mval_array_set_double(ldms_mval_t mv, int idx, double v);
void ldms_mval_array_set_ts(ldms_mval_t mv, int idx, struct ldms_timestamp v);

/**
 * \brief Get the value from a metric value
 *
 * When working with an array value, \c idx must not exceed the array length.
 *
 * \param mval    The metric value handle
 * \param idx       The array index
 */
char ldms_mval_get_char(ldms_mval_t mv);
uint8_t ldms_mval_get_u8(ldms_mval_t mv);
uint16_t ldms_mval_get_u16(ldms_mval_t mv);
uint32_t ldms_mval_get_u32(ldms_mval_t mv);
uint64_t ldms_mval_get_u64(ldms_mval_t mv);
int8_t ldms_mval_get_s8(ldms_mval_t mv);
int16_t ldms_mval_get_s16(ldms_mval_t mv);
int32_t ldms_mval_get_s32(ldms_mval_t mv);
int64_t ldms_mval_get_s64(ldms_mval_t mv);
float ldms_mval_get_float(ldms_mval_t mv);
double ldms_mval_get_double(ldms_mval_t mv);
struct ldms_timestamp ldms_mval_get_ts(ldms_mval_t mv);
const char *ldms_mval_array_get_str(ldms_mval_t mv);
char ldms_mval_array_get_char(ldms_mval_t mv, int idx);
uint8_t ldms_mval_array_get_u8(ldms_mval_t mv, int idx);
uint16_t ldms_mval_array_get_u16(ldms_mval_t mv, int idx);
uint32_t ldms_mval_array_get_u32(ldms_mval_t mv, int idx);
uint64_t ldms_mval_array_get_u64(ldms_mval_t mv, int idx);
int8_t ldms_mval_array_get_s8(ldms_mval_t mv, int idx);
int16_t ldms_mval_array_get_s16(ldms_mval_t mv, int idx);
int32_t ldms_mval_array_get_s32(ldms_mval_t mv, int idx);
int64_t ldms_mval_array_get_s64(ldms_mval_t mv, int idx);
float ldms_mval_array_get_float(ldms_mval_t mv, int idx);
double ldms_mval_array_get_double(ldms_mval_t mv, int idx);
struct ldms_timestamp ldms_mval_array_get_ts(ldms_mval_t mv, int idx);

/**
 * \brief Convert ldms_mval_t mv of type \c type to respective data type
 *
 * \param mv   The metric value handle
 * \param type The type of the metric value handle
 * \param idx  If \c type is ARRAY, \c idx is the index of the value.
 *             Otherwise, \c idx is ignored.
 */
char ldms_mval_as_char(ldms_mval_t mv, enum ldms_value_type type, int idx);
uint8_t ldms_mval_as_u8(ldms_mval_t mv, enum ldms_value_type type, int idx);
int8_t ldms_mval_as_s8(ldms_mval_t mv, enum ldms_value_type type, int idx);
uint16_t ldms_mval_as_u16(ldms_mval_t mv, enum ldms_value_type type, int idx);
int16_t ldms_mval_as_s16(ldms_mval_t mv, enum ldms_value_type type, int idx);
uint32_t ldms_mval_as_u32(ldms_mval_t mv, enum ldms_value_type type, int idx);
int32_t ldms_mval_as_s32(ldms_mval_t mv, enum ldms_value_type type, int idx);
uint64_t ldms_mval_as_u64(ldms_mval_t mv, enum ldms_value_type type, int idx);
int64_t ldms_mval_as_s64(ldms_mval_t mv, enum ldms_value_type type, int idx);
float ldms_mval_as_float(ldms_mval_t mv, enum ldms_value_type type, int idx);
double ldms_mval_as_double(ldms_mval_t mv, enum ldms_value_type type, int idx);
struct ldms_timestamp ldms_mval_as_timestamp(ldms_mval_t mv, enum ldms_value_type type, int idx);

/**
 * \brief Append a new value to a list
 *
 * Append a new value entry to a list metric. The list handle \c lh must be
 * - the metric handle obtained by calling \c ldms_metric_get(s, i) where the ith
 *   metric is a list, or
 * - the metric handle returned by \c ldms_list_append_item(s, some_lh, LDMS_V_LIST, 1) or
 * - the metric handle returned by \c ldms_list_first(s, some_lh, &otyp, &c)
 *   where the returned \c otyp must be \c LDMS_V_LIST, or
 * - the metric handle returned by \c ldms_list_next(s, some_lh, &otyp, &c) where
 *   the returned \c otyp must be \c LDMS_V_LIST.
 * Basically, please make sure that \c lh is the metric handle to the type
 * \c LDMS_V_LIST. If \c lh is not a list, the function call will corrupt the
 * memory.
 *
 * If the requested element type \c typ is \c LDMS_V_LIST, the \c count is
 * ignored and the handle to the new list inside the list \c lh is returned.
 *
 * If the requested element type \c typ is an array type, the \c count is
 * the array length (number of elements). Otherwise, if \c typ is a regular
 * type, \c count is also ignored.
 *
 * \param s	The set handle.
 * \param lh	The metric handle of the list.
 * \param typ	The type of the value to append.
 * \param count	The element count if the type is an array.
 *
 * \retval mval The metric handle to the newly allocated entry in the list.
 */
ldms_mval_t ldms_list_append_item(ldms_set_t s, ldms_mval_t lh, enum ldms_value_type typ, size_t count);

/**
 * \brief Get the first list entry.
 *
 * \c lh must be a list. If \c lh is not a list, the function call results in
 * a garbage value returned or a segmentation fault.
 *
 * \param [in]  s     The ldms set handle.
 * \param [in]  lh    The list handle.
 * \param [out] typ   If not NULL, \c *typ value is set to the type of the first entry of the list.
 * \param [out] count If not NULL, \c *count is set to the array length if the
 *                    entry is an array.
 *
 * \retval mval The metric handle of the first entry, or
 * \retval NULL if the list is empty.
 */
ldms_mval_t ldms_list_first(ldms_set_t s, ldms_mval_t lh, enum ldms_value_type *typ, size_t *count);

/**
 * \brief Get the last list entry.
 *
 * \c lh must be a list. If \c lh is not a list, the function call results in
 * a garbage value returned or a segmentation fault.
 *
 * \param [in]  s     The ldms set handle.
 * \param [in]  lh    The list handle.
 * \param [out] typ   If not NULL, \c *typ value is set to the type of the first entry of the list.
 * \param [out] count If not NULL, \c *count is set to the array length if the
 *                    entry is an array.
 *
 * \retval mval The metric handle of the first entry, or
 * \retval NULL if the list is empty.
 */
ldms_mval_t ldms_list_last(ldms_set_t s, ldms_mval_t lh, enum ldms_value_type *typ, size_t *count);

/**
 * \brief Get the next list entry.
 *
 * \c v must be a list entry. In other words, \c v is a handle returned from \c
 * ldms_list_first() or \c ldms_list_next(). Calling this function with a
 * non-list-entry \c v results in a garbage value returned or a segmentation
 * fault.
 *
 * \param [in]  s     The ldms set handle.
 * \param [in]  v     The metric handle returned from
 *                    \c ldms_list_first() or \c ldms_list_next().
 * \param [out] typ   If not NULL, \c *typ value is set to the type of the next entry of the list.
 * \param [out] count If not NULL, \c *count is set to the array length if the
 *                    next entry is an array, or 1 if it is not.
 *
 * \retval mval The metric handle of the next entry, or
 * \retval NULL if there is no more list entry.
 */
ldms_mval_t ldms_list_next(ldms_set_t s, ldms_mval_t v, enum ldms_value_type *typ, size_t *count);

/**
 * \brief Get the number of entries in the list.
 *
 * \param s  The ldms set handle.
 * \param lh The list handle.
 *
 * \retval n The number of entries in the list.
 */
size_t ldms_list_len(ldms_set_t s, ldms_mval_t lh);

/**
 * \brief Remove entry \c v from the list \c lh.
 *
 * \c lh must be a list and \c v must be a list entry. In otherwords, \c lh must
 * be obtained by \c ldms_metric_get(), \c ldms_list_append_item(),
 * \c ldms_list_first(), or \c ldms_list_next() where the metric type
 * is \c LDMS_V_LIST. \c v must be a list entry obtained from
 * \c ldms_list_append_item(), \c ldms_list_first(), or \c ldms_list_next().
 * Calling this function with non-list \c lh, or non-list-entry \c v will
 * corrupt the memory.
 *
 * The handle \c v is not valid after the call since the memory of the list
 * entry is put back into the heap. If \c v is an \c LDMS_V_LIST,
 * the members of \c v will be recursively deleted.
 *
 * \param s  The set handle.
 * \param lh The list handle.
 * \param v  The list entry handle.
 *
 * \retval 0     If delete succeeded, or
 * \retval error if an error occur.
 *
 */
int ldms_list_remove_item(ldms_set_t s, ldms_mval_t lh, ldms_mval_t v);

/**
 * \brief Recursively purge all elements from the list \c lh.
 *
 * \c lh must be a list. In otherwords, \c lh must be obtained by
 * \c ldms_metric_get(), \c ldms_list_append_item(), \c ldms_list_first(), or
 * \c ldms_list_next() where the metric type is \c LDMS_V_LIST.
 * This function is equivalent to the following pseudo code:
 * ```
 * while v = ldms_list_first(s, lh, NULL, NULL):
 *     ldms_list_remove_item(s, lh, v)
 * ```
 *
 * \param s  The set handle.
 * \param lh The list handle.
 *
 * \retval 0     If delete succeeded, or
 * \retval error if an error occur.
 */
int ldms_list_purge(ldms_set_t s, ldms_mval_t lh);

/**
 * Allocate a new record in the set.
 *
 * The returned \c rec_inst must later be appended into a list (see
 * \c ldms_list_append_record()). Otherwise, the remote peer won't be able to
 * reach it.
 *
 * \param set       The handle of the LDMS set hosting the record instance.
 * \param metric_id The metric ID referring to the record type (from
 *                  \c ldms_schema_record_add()).
 *
 * \retval rec_inst A record instance handle.
 * \retval NULL     If there is an error (e.g. not enough memory). In such case,
 *                  the \c errno will also be set to describe the nature of the
 *                  error.
 */
ldms_mval_t ldms_record_alloc(ldms_set_t set, int metric_id);

/**
 * Get the set metric ID of the record type of the given record instance.
 *
 * \param  rec_inst  The record instance handle.
 *
 * \retval mid>=0    The metric ID (in the set) referring to the record type
 *                   used to create the record instance.
 * \retval -EINVAL   If the \c rec_inst is detected to be a not a record
 *                   instance.
 */
int ldms_record_type_get(ldms_mval_t rec_inst);

/**
 * Get the number of members in the record instance.
 *
 * \param rec    The record instance or the record type handle.
 *
 * \retval n The number of members in the record instance.
 */
int ldms_record_card(ldms_mval_t rec);

/**
 * Get the ID of the metric in the record instance.
 *
 * This is similar to \c ldms_metric_by_name() for a metric in a set.
 *
 * \param rec_inst The record instance.
 * \param name     The name of the metric.
 *
 * \retval metric_id The ID for the metric in the record instance.
 * \retval -ENOENT   If the metric "name" is not found in the record instance.
 */
int ldms_record_metric_find(ldms_mval_t rec_inst, const char *name);

/**
 * Obtain the pointer to the metric value in the instance.
 *
 * This is similar to \c ldms_metric_get(). The application may access/modify
 * the raw value of the metric, but please be aware of the endianness. For the
 * direct modification, the LDMS set data generation number won't change. To
 * increment the set data generation number, the
 * application shall call \c ldms_metric_modify(set,lh_id), where \c lh_id is
 * the metric ID of the LIST where \c rec_inst resided.
 *
 * \param  rec_inst  The record instance handle.
 * \param  metric_id The ID of the metric in the record instance.
 *
 * \retval mval      The pointer to the metric value in the instance.
 */
ldms_mval_t ldms_record_metric_get(ldms_mval_t rec_inst, int metric_id);

/**
 * Get the name of the metric in the record instance.
 *
 * \param rec       The record instance or the record type handle.
 * \param metric_id The metric ID in the record instance.
 *
 * \retval name The name of the metric in the record instance.
 */
const char *ldms_record_metric_name_get(ldms_mval_t rec, int metric_id);

/**
 * Get the unit of the metric in the record instance.
 *
 * \param rec_inst The record instance or the record type handle.
 * \param metric_id The metric ID in the record instance.
 *
 * \retval unit The unit of the metric in the record instance.
 */
const char *ldms_record_metric_unit_get(ldms_mval_t rec, int metric_id);

/**
 * Get the type of the metric in the record instance (and count for array).
 *
 * \param[in]  rec_inst  The record instance or the record type handle.
 * \param[in]  metric_id The metric ID in the record instance.
 * \param[out] array_len The number of elements if the type is an ARRAY.
 *
 * \retval type The type of the metric in the record instance.
 *
 */
enum ldms_value_type ldms_record_metric_type_get(ldms_mval_t rec,
					int metric_id, size_t *array_len);

/**
 * Set the value of the metric in the record instance.
 *
 * This is similar to \c ldms_metric_set(set, metric_id, v).
 *
 * \param rec_inst  The record instance handle.
 * \param metric_id The metric ID in the record instance.
 * \param val       The value to set to the metric in the record.
 */
void ldms_record_metric_set(ldms_mval_t rec_inst, int metric_id,
			    ldms_mval_t val);


/**
 * Set value to elements in the array metric in the record instance.
 *
 * This is similar to \c ldms_metric_array_set(set, metric_id, val, start, count).
 * The metric referred to by \c metric_id in the record instance must be an
 * array. The value of the elements from \c start to \c start+count-1 will be
 * set to \c val.
 *
 * \param rec_inst  The record instance handle.
 * \param metric_id The metric ID in the record instance.
 * \param val       The value to set to the metric in the record.
 * \param start     The first element to set the value.
 * \param count     The number of elements from the \c start to set values.
 *
 */
void ldms_record_metric_array_set(ldms_mval_t rec_inst, int metric_id,
				  ldms_mval_t val, int start,
				  int count);

/**
 * Get the record instance in the record array.
 *
 * \param rec_array The record array handle.
 * \param idx       The index.
 *
 * \retval rec_inst The record instance "rec_array[idx]".
 * \retval NULL     If there is an error. \c errno is also set to describe the
 *                  error.
 */
ldms_mval_t ldms_record_array_get_inst(ldms_mval_t rec_array, int idx);

/**
 * Get the length of the record array.
 *
 * \param rec_array The record array handle.
 *
 * \retval len The length of the record array.
 */
int ldms_record_array_len(ldms_mval_t rec_array);

/**
 * Append the record instance to the list.
 *
 * Please note that \c ldms_list_remove_item() immediately free the \c rec_inst.
 *
 * \param set      The set handle.
 * \param lh       The list handle.
 * \param rec_inst The record instance handle.
 *
 * \retval 0      If the record instance is added to the list successfully,
 * \retval EINVAL If the \c list_handle or \c rec_inst is an invalid handle.
 * \retval EBUSY  If the\c rec_inst is already in a list.
 */
int ldms_list_append_record(ldms_set_t set, ldms_mval_t lh, ldms_mval_t rec_inst);

/* Convenient getters
 *
 * Get the metric value from the record instance with endianness conversion to
 * CPU endianness.
 *
 * \param rec_inst  The record instance handle.
 * \param metric_id The metric ID in the record instance.
 * \param idx       The element index in an array.
 *
 * \retval val The value of the metric, or the metric array element.
 */
char       ldms_record_get_char(ldms_mval_t rec_inst, int metric_id);
uint8_t      ldms_record_get_u8(ldms_mval_t rec_inst, int metric_id);
uint16_t    ldms_record_get_u16(ldms_mval_t rec_inst, int metric_id);
uint32_t    ldms_record_get_u32(ldms_mval_t rec_inst, int metric_id);
uint64_t    ldms_record_get_u64(ldms_mval_t rec_inst, int metric_id);
int8_t       ldms_record_get_s8(ldms_mval_t rec_inst, int metric_id);
int16_t     ldms_record_get_s16(ldms_mval_t rec_inst, int metric_id);
int32_t     ldms_record_get_s32(ldms_mval_t rec_inst, int metric_id);
int64_t     ldms_record_get_s64(ldms_mval_t rec_inst, int metric_id);
float     ldms_record_get_float(ldms_mval_t rec_inst, int metric_id);
double   ldms_record_get_double(ldms_mval_t rec_inst, int metric_id);

const char *ldms_record_array_get_str(ldms_mval_t rec_inst, int metric_id);

char       ldms_record_array_get_char(ldms_mval_t rec_inst, int metric_id, int idx);
uint8_t      ldms_record_array_get_u8(ldms_mval_t rec_inst, int metric_id, int idx);
uint16_t    ldms_record_array_get_u16(ldms_mval_t rec_inst, int metric_id, int idx);
uint32_t    ldms_record_array_get_u32(ldms_mval_t rec_inst, int metric_id, int idx);
uint64_t    ldms_record_array_get_u64(ldms_mval_t rec_inst, int metric_id, int idx);
int8_t       ldms_record_array_get_s8(ldms_mval_t rec_inst, int metric_id, int idx);
int16_t     ldms_record_array_get_s16(ldms_mval_t rec_inst, int metric_id, int idx);
int32_t     ldms_record_array_get_s32(ldms_mval_t rec_inst, int metric_id, int idx);
int64_t     ldms_record_array_get_s64(ldms_mval_t rec_inst, int metric_id, int idx);
float     ldms_record_array_get_float(ldms_mval_t rec_inst, int metric_id, int idx);
double   ldms_record_array_get_double(ldms_mval_t rec_inst, int metric_id, int idx);


/* Convenient setters
 *
 * Set the metric value in the record instance with endianness conversion if
 * needed. The \c val is in CPU endianness.
 *
 * \param rec_inst  The record instance handle.
 * \param metric_id The metric ID in the record instance.
 * \param idx       The element index in an array.
 * \param val       The value to set to the metric in the record.
 */
void   ldms_record_set_char(ldms_mval_t rec_inst, int metric_id,     char val);
void     ldms_record_set_u8(ldms_mval_t rec_inst, int metric_id,  uint8_t val);
void    ldms_record_set_u16(ldms_mval_t rec_inst, int metric_id, uint16_t val);
void    ldms_record_set_u32(ldms_mval_t rec_inst, int metric_id, uint32_t val);
void    ldms_record_set_u64(ldms_mval_t rec_inst, int metric_id, uint64_t val);
void     ldms_record_set_s8(ldms_mval_t rec_inst, int metric_id,   int8_t val);
void    ldms_record_set_s16(ldms_mval_t rec_inst, int metric_id,  int16_t val);
void    ldms_record_set_s32(ldms_mval_t rec_inst, int metric_id,  int32_t val);
void    ldms_record_set_s64(ldms_mval_t rec_inst, int metric_id,  int64_t val);
void  ldms_record_set_float(ldms_mval_t rec_inst, int metric_id,    float val);
void ldms_record_set_double(ldms_mval_t rec_inst, int metric_id,   double val);

void    ldms_record_array_set_str(ldms_mval_t rec_inst, int metric_id, const char *val);

void   ldms_record_array_set_char(ldms_mval_t rec_inst, int metric_id, int idx,     char val);
void     ldms_record_array_set_u8(ldms_mval_t rec_inst, int metric_id, int idx,  uint8_t val);
void    ldms_record_array_set_u16(ldms_mval_t rec_inst, int metric_id, int idx, uint16_t val);
void    ldms_record_array_set_u32(ldms_mval_t rec_inst, int metric_id, int idx, uint32_t val);
void    ldms_record_array_set_u64(ldms_mval_t rec_inst, int metric_id, int idx, uint64_t val);
void     ldms_record_array_set_s8(ldms_mval_t rec_inst, int metric_id, int idx,   int8_t val);
void    ldms_record_array_set_s16(ldms_mval_t rec_inst, int metric_id, int idx,  int16_t val);
void    ldms_record_array_set_s32(ldms_mval_t rec_inst, int metric_id, int idx,  int32_t val);
void    ldms_record_array_set_s64(ldms_mval_t rec_inst, int metric_id, int idx,  int64_t val);
void  ldms_record_array_set_float(ldms_mval_t rec_inst, int metric_id, int idx,    float val);
void ldms_record_array_set_double(ldms_mval_t rec_inst, int metric_id, int idx,   double val);

/** \} */

/**
 * \addtogroup notify LDMS Notifications
 *
 * These functions are used register for and deliver Notification
 * Events.
 * \{
 */

/**
 * \brief Notification event type
 *
 * The ldms_notify_event_t should be initialized using one of the
 * following functions:
 *
 * * ldms_init_notify_modified
 * * ldms_init_notify_user_data
 *
 */
typedef enum ldms_notify_event_type {
		LDMS_SET_MODIFIED = 1,
		LDMS_USER_DATA = 2,
} ldms_notify_event_type_t;
typedef struct ldms_notify_event_s {
	ldms_notify_event_type_t type;
	size_t len;		/*! The size of the event in bytes */
	unsigned char u_data[OVIS_FLEX];/*! User-data for the LDMS_USER_DATA
				  type */
} *ldms_notify_event_t;

/**
 * \brief Initialize a SET_MODIFIED notification
 *
 * \param e	Pointer to the event
 */
static inline void
ldms_init_notify_modified(ldms_notify_event_t e) {
	e->type = LDMS_SET_MODIFIED;
	e->len = sizeof(struct ldms_notify_event_s);
}

/**
 * \brief Initialize a USER_DATA notification
 *
 * \param s	The set handle
 * \param e	Pointer to the event
 * \param u_data Pointer to the user-data
 * \param sz	Length of the user-data in bytes
 */
static inline void
ldms_init_notify_user_data(ldms_notify_event_t e,
			   unsigned char *u_data, size_t sz)
{
	e->type = LDMS_USER_DATA;
	e->len = sizeof(struct ldms_notify_event_s) + sz;
	memcpy(e->u_data, u_data, sz);
}

/**
 * \brief Nofication callback handler function
 *
 * This is the function protototype for registered notification
 * handlers.
 *
 * \param x The transport endpoint.
 * \param s The metric set handle
 * \param e The notification event
 * \param arg The user-supplied argument provided when the callback
 *	      was registered.
 */
typedef void (*ldms_notify_cb_t)(ldms_t x, ldms_set_t s,
				 ldms_notify_event_t e, void *arg);

/**
 * \brief Register callback handler to receive update notifications
 *
 * If the metric set producer supports update notifications, the
 * registered callback handler is invoked when the producer calls the
 * \c ldms_notify service.
 *
 * \param x	The transport endpoint.
 * \param s	The metric set handle
 * \param flags	The events of interest. 0 means all events.
 * \param cb_fn	Pointer to the function to call to receive
 *		notifications
 * \param cb_arg User-supplied argument to the cb_fn function.
 *
 * \returns 0 on success
 * \returns !0 on failure. Refer to \c errno for error details
 */
int ldms_register_notify_cb(ldms_t x, ldms_set_t s, int flags,
			    ldms_notify_cb_t cb_fn, void *cb_arg);

/**
 * \brief Release the resources consumed by an event
 *
 * This function is called by the application to release the resources
 * used by an ldms_notify_event_t.
 *
 * \param t	 The transport handle
 * \param dir	 Pointer to an ldms_notify_event_t structure to be released.
 */
void ldms_event_release(ldms_t x, ldms_notify_event_t e);

/**
 * \brief Cancel notifications
 *
 * Cancel notifications for a metric set
 *
 * \param x	The transport endpoint.
 * \param s	The metric set handle
 * \returns 0	Success
 * \returns !0	An error was encountered. See errno for details.
 */
int ldms_cancel_notify(ldms_t x, ldms_set_t s);

/**
 * \brief Notify registered clients
 *
 * Notify clients that have registered callback handlers that the
 * metric set has been modified. Note that this will unconditionally
 * notify the clients whether or not a change has been made to the
 * metric set. The expected usage is:
 *
 *    ldms_set_metric(m0, ...);
 *    ldms_set_metric(m1, ...);
 *    ldms_set_metric(m2, ...);
 *    ...
 *    ldms_notify(s);
 *
 * \param s	The metric set handle.
 * \param e	The event
 */
void ldms_notify(ldms_set_t s, ldms_notify_event_t e);

/**
 * \brief Get ldms credentials to both ends of the transport
 *
 * \param x The LDMS transport handle.
 * \param [out] lcl Credential output for the local end of the transport. This
 *                  paramater can be NULL.
 * \param [out] rmt Credential output for the remote end of the transport. This
 *                  parameter can be NULL.
 *
 * \note If the ldms transport \c x is not connected, the data in \c rmt is
 * invalid. For the listening endpoint, please use ::ldms_local_cred_get() to
 * get the credential.
 */
void ldms_xprt_cred_get(ldms_t x, ldms_cred_t lcl, ldms_cred_t rmt);

/**
 * \brief Get the local credential
 *
 * \param x The LDMS transport handle.
 * \param [out] lcl Credential output for the local end of the transport.
 */
void ldms_local_cred_get(ldms_t x, ldms_cred_t lcl);

#define LDMS_ACCESS_READ 0444
#define LDMS_ACCESS_WRITE 0222
#define LDMS_ACCESS_EXECUTE 0111

/**
 * \brief Check if the access is permitted for the object over the transport
 *
 * This is a convenient function to check whether the remote peer should be able
 * to access (\c LDMS_ACCESS_READ, \c LDMS_ACCESS_WRITE, or
 * \c LDMS_ACCESS_EXECUTE) an object (ldms' or application's) that owns by \c
 * obj_uid/obj_gid with permission \c obj_perm.
 *
 * \param x The LDMS transport handle
 * \param acc The access request flag (one of the LDMS_ACCESS_READ,
 *            LDMS_ACCESS_WRITE and LDMS_ACCESS_EXECUTE).
 * \param obj_uid The UID of the object in question.
 * \param obj_gid The GID of the object in question.
 * \param obj_perm The permission of the object in question.
 *
 * \retval 0 if the access is granted.
 * \retval EACCES if the access is denied.
 */
int ldms_access_check(ldms_t x, uint32_t acc, uid_t obj_uid, gid_t obj_gid,
		      int obj_perm);
/**
 * \}
 */

/**
 * \brief Return the time difference in microseconds
 *
 * Computes the number of microseconds in the interval end - start.
 * Note that the result may be negative.
 *
 * \param start Pointer to struct timespec
 * \param end Pointer to struct timespec
 * \returns The number of microseconds in the interval end - start
 */
static inline int64_t ldms_timespec_diff_us(struct timespec *start, struct timespec *end)
{
	int64_t secs_ns;
	int64_t nsecs;
	secs_ns = (end->tv_sec - start->tv_sec) * 1000000000;
	nsecs = end->tv_nsec - start->tv_nsec;
	return (secs_ns + nsecs) / 1000;
}

static inline double ldms_timespec_diff_s(struct timespec *start, struct timespec *end)
{
	return (double)ldms_timespec_diff_us(start, end) / (double)1000000.0;
}

#ifdef __cplusplus
}
#endif

#endif
