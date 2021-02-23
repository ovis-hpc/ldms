# Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2020 NTESS Corporation. All rights reserved.
# Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
# license for use of this work by or on behalf of the U.S. Government.
# Export of this program may require a license from the United States
# Government.
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the BSD-type
# license below:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#      Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#      Redistributions in binary form must reproduce the above
#      copyright notice, this list of conditions and the following
#      disclaimer in the documentation and/or other materials provided
#      with the distribution.
#
#      Neither the name of NTESS Corporation, Open Grid Computing nor
#      the names of any contributors may be used to endorse or promote
#      products derived from this software without specific prior
#      written permission.
#
#      Modified source versions must be plainly marked as such, and
#      must not be misrepresented as being the original software.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import cython
from libc.stdint cimport *
from libc.string cimport *

from posix.types cimport gid_t, pid_t, off_t, uid_t, mode_t

cdef extern from "errno.h" nogil:
    int errno

    enum:
        E2BIG,
        EACCES,
        EADDRINUSE,
        EADDRNOTAVAIL,
        EAFNOSUPPORT,
        EAGAIN,
        EALREADY,
        EBADE,
        EBADF,
        EBADFD,
        EBADMSG,
        EBADR,
        EBADRQC,
        EBADSLT,
        EBUSY,
        ECHILD,
        ECHRNG,
        ECOMM,
        ECONNABORTED,
        ECONNREFUSED,
        ECONNRESET,
        EDEADLK,
        EDEADLOCK,
        EDESTADDRREQ,
        EDOM,
        EDQUOT,
        EEXIST,
        EFAULT,
        EFBIG,
        EHOSTDOWN,
        EHOSTUNREACH,
        EIDRM,
        EILSEQ,
        EINPROGRESS,
        EINTR,
        EINVAL,
        EIO,
        EISCONN,
        EISDIR,
        EISNAM,
        ELOOP,
        EMFILE,
        EMLINK,
        EMSGSIZE,
        EMULTIHOP,
        ENAMETOOLONG,
        ENETDOWN,
        ENETRESET,
        ENETUNREACH,
        ENFILE,
        ENOBUFS,
        ENODATA,
        ENODEV,
        ENOENT,
        ENOEXEC,
        ENOLCK,
        ENOLINK,
        ENOMEM,
        ENOMSG,
        ENONET,
        ENOPKG,
        ENOPROTOOPT,
        ENOSPC,
        ENOSR,
        ENOSTR,
        ENOSYS,
        ENOTBLK,
        ENOTCONN,
        ENOTDIR,
        ENOTEMPTY,
        ENOTSOCK,
        ENOTSUP,
        ENOTTY,
        ENOTUNIQ,
        ENXIO,
        EOPNOTSUPP,
        EOVERFLOW,
        EPERM,
        EPFNOSUPPORT,
        EPIPE,
        EPROTO,
        EPROTONOSUPPORT,
        EPROTOTYPE,
        ERANGE,
        EREMCHG,
        EREMOTE,
        EREMOTEIO,
        ERESTART,
        EROFS,
        ESHUTDOWN,
        ESPIPE,
        ESOCKTNOSUPPORT,
        ESRCH,
        ESTALE,
        ESTRPIPE,
        ETIME,
        ETIMEDOUT,
        EUSERS,
        EWOULDBLOCK,
        EXFULL

cdef extern from "stdio.h" nogil:
    struct FILE:
        pass
    FILE *stderr

cdef extern from "stdarg.h" nogil:
    ctypedef struct va_list:
        pass
    void va_start(va_list, void* arg)
    void va_end(va_list)
    int vprintf(const char *fmt, va_list ap)
    int vfprintf(FILE *f, const char *fmt, va_list ap)

cdef extern from "semaphore.h" nogil:
    ctypedef struct sem_t:
        pass
    struct timespec:
        long tv_sec
        long tv_nsec
    int sem_init(sem_t *sem, int pshared, unsigned int value)
    int sem_wait(sem_t *sem)
    int sem_trywait(sem_t *sem)
    int sem_timedwait(sem_t *sem, const timespec *abs_timeout)
    int sem_post(sem_t *sem)

cdef extern from "time.h" nogil:
    int clock_gettime(int clk_id, timespec *tp)
    enum:
        CLOCK_REALTIME

cdef extern from "ovis_util/util.h" nogil:
    struct attr_value_list:
        pass
    attr_value_list* av_new(size_t size)
    int tokenize(char *cmd, attr_value_list *kwl, attr_value_list *avl)
    int av_add(attr_value_list *avl, const char *name, const char *value)
    void av_free(attr_value_list *avl)

cdef extern from "ldms.h" nogil:
    struct ldms_xprt:
        pass
    ctypedef ldms_xprt *ldms_t
    ctypedef void (*ldms_log_fn_t)(const char *fmt, ...)
    struct ldms_timestamp:
        uint32_t sec
        uint32_t usec

    # --- xprt connection related --- #
    cpdef enum ldms_xprt_event_type:
        EVENT_CONNECTED     "LDMS_XPRT_EVENT_CONNECTED"
        EVENT_REJECTED      "LDMS_XPRT_EVENT_REJECTED"
        EVENT_ERROR         "LDMS_XPRT_EVENT_ERROR"
        EVENT_DISCONNECTED  "LDMS_XPRT_EVENT_DISCONNECTED"
        EVENT_RECV          "LDMS_XPRT_EVENT_RECV"
        EVENT_SEND_COMPLETE "LDMS_XPRT_EVENT_SEND_COMPLETE"
        EVENT_LAST          "LDMS_XPRT_EVENT_LAST"
        LDMS_XPRT_EVENT_CONNECTED
        LDMS_XPRT_EVENT_REJECTED
        LDMS_XPRT_EVENT_ERROR
        LDMS_XPRT_EVENT_DISCONNECTED
        LDMS_XPRT_EVENT_RECV
        LDMS_XPRT_EVENT_SEND_COMPLETE
        LDMS_XPRT_EVENT_LAST
    cdef struct ldms_xprt_event:
        ldms_xprt_event_type type
        char *data
        size_t data_len
    ctypedef ldms_xprt_event *ldms_xprt_event_t
    ctypedef void (*ldms_event_cb_t)(ldms_t x, ldms_xprt_event_t e, void *cb_arg)

    int ldms_init(size_t max_size)
    ldms_t ldms_xprt_new_with_auth(const char *xprt_name, ldms_log_fn_t log_fn,
                                   const char *auth_name,
                                   attr_value_list *auth_av_list)
    void ldms_xprt_put(ldms_t x)
    void ldms_xprt_close(ldms_t x)
    int ldms_xprt_connect_by_name(ldms_t x, const char *host, const char *port,
                                  ldms_event_cb_t cb, void *cb_arg)
    int ldms_xprt_listen_by_name(ldms_t x, const char *host, const char *port,
                                 ldms_event_cb_t cb, void *cb_arg)

    # --- dir operation related --- #
    struct ldms_key_value_s:
        char *key
        char *value
    ctypedef ldms_key_value_s *ldms_key_value_t
    cpdef enum ldms_dir_type:
        DIR_LIST "LDMS_DIR_LIST"
        DIR_DEL  "LDMS_DIR_DEL"
        DIR_ADD  "LDMS_DIR_ADD"
        DIR_UPD  "LDMS_DIR_UPD"
        LDMS_DIR_LIST
        LDMS_DIR_DEL
        LDMS_DIR_ADD
        LDMS_DIR_UPD
    struct ldms_dir_set_s:
        char *inst_name
        char *schema_name
        char *flags
        size_t meta_size
        size_t data_size
        uid_t uid
        gid_t gid
        char *perm
        int card
        int array_card
        uint64_t meta_gn
        uint64_t data_gn
        ldms_timestamp timestamp
        ldms_timestamp duration
        size_t info_count
        ldms_key_value_t info
    struct ldms_dir_s:
        ldms_dir_type type
        int more
        int set_count
        ldms_dir_set_s set_data[0]
    cpdef enum: # empty enum for constant int values
        DIR_F_NOTIFY "LDMS_DIR_F_NOTIFY"
    ctypedef ldms_dir_s *ldms_dir_t
    ctypedef void (*ldms_dir_cb_t)(ldms_t t, int status, ldms_dir_t dir, void *cb_arg)
    int ldms_xprt_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
    void ldms_xprt_dir_free(ldms_t t, ldms_dir_t dir)

    # --- lookup operation related --- #
    cpdef enum ldms_lookup_flags:
        LOOKUP_BY_INSTANCE  "LDMS_LOOKUP_BY_INSTANCE"
        LOOKUP_BY_SCHEMA    "LDMS_LOOKUP_BY_SCHEMA"
        LOOKUP_RE           "LDMS_LOOKUP_RE"
        LOOKUP_SET_INFO     "LDMS_LOOKUP_SET_INFO"
        LDMS_LOOKUP_BY_INSTANCE
        LDMS_LOOKUP_BY_SCHEMA
        LDMS_LOOKUP_RE
        LDMS_LOOKUP_SET_INFO
    cpdef enum ldms_lookup_status:
        LOOKUP_OK      "LDMS_LOOKUP_OK"
        LOOKUP_ERROR   "LDMS_LOOKUP_ERROR"
        LDMS_LOOKUP_OK
        LDMS_LOOKUP_ERROR
    struct ldms_rbuf_desc:
        pass
    ctypedef ldms_rbuf_desc *ldms_set_t
    ctypedef void (*ldms_lookup_cb_t)(ldms_t x, ldms_lookup_status status,
                                      int more, ldms_set_t s, void *arg)
    int ldms_xprt_lookup(ldms_t x, const char *name, ldms_lookup_flags flags,
		         ldms_lookup_cb_t cb, void *cb_arg)
    int ldms_xprt_send(ldms_t x, char *msg_buf, size_t msg_len)
    size_t ldms_xprt_msg_max(ldms_t x)

    # --- set related --- #
    ldms_set_t ldms_set_by_name(const char *set_name)
    void ldms_set_put(ldms_set_t s)
    const char *ldms_set_schema_name_get(ldms_set_t s)
    const char *ldms_set_instance_name_get(ldms_set_t s)
    const char *ldms_set_producer_name_get(ldms_set_t s)
    int ldms_set_producer_name_set(ldms_set_t s, const char *name)
    uint32_t ldms_set_card_get(ldms_set_t s)
    uint32_t ldms_set_uid_get(ldms_set_t s)
    uint32_t ldms_set_gid_get(ldms_set_t s)
    uint32_t ldms_set_perm_get(ldms_set_t s)
    uint32_t ldms_set_meta_sz_get(ldms_set_t s)
    uint32_t ldms_set_data_sz_get(ldms_set_t s)
    const char *ldms_set_name_get(ldms_set_t s)
    uint64_t ldms_set_meta_gn_get(ldms_set_t s)
    uint64_t ldms_set_data_gn_get(ldms_set_t s)

    int ldms_set_is_consistent(ldms_set_t s)

    ldms_timestamp ldms_transaction_timestamp_get(ldms_set_t s)
    ldms_timestamp ldms_transaction_duration_get(ldms_set_t s)

    int ldms_transaction_begin(ldms_set_t s)
    int ldms_transaction_end(ldms_set_t s)

    int ldms_set_info_set(ldms_set_t s, const char *key, const char *value)
    void ldms_set_info_unset(ldms_set_t s, const char *key)
    char *ldms_set_info_get(ldms_set_t s, const char *key)
    ctypedef void (*ldms_update_cb_t)(ldms_t t, ldms_set_t s, int flags, void *arg)
    cpdef enum:
        UPD_F_PUSH       "LDMS_UPD_F_PUSH"
        UPD_F_PUSH_LAST  "LDMS_UPD_F_PUSH_LAST"
        UPD_F_MORE       "LDMS_UPD_F_MORE"
        UPD_ERROR_MASK   "LDMS_UPD_ERROR_MASK"
        LDMS_UPD_F_PUSH
        LDMS_UPD_F_PUSH_LAST
        LDMS_UPD_F_MORE
        LDMS_UPD_ERROR_MASK
    int LDMS_UPD_ERROR(int flags)
    int ldms_xprt_update(ldms_set_t s, ldms_update_cb_t update_cb, void *arg)
    cpdef enum ldms_value_type:
        V_NONE        "LDMS_V_NONE"
        V_CHAR        "LDMS_V_CHAR"
        V_U8          "LDMS_V_U8"
        V_S8          "LDMS_V_S8"
        V_U16         "LDMS_V_U16"
        V_S16         "LDMS_V_S16"
        V_U32         "LDMS_V_U32"
        V_S32         "LDMS_V_S32"
        V_U64         "LDMS_V_U64"
        V_S64         "LDMS_V_S64"
        V_F32         "LDMS_V_F32"
        V_D64         "LDMS_V_D64"
        V_CHAR_ARRAY  "LDMS_V_CHAR_ARRAY"
        V_U8_ARRAY    "LDMS_V_U8_ARRAY"
        V_S8_ARRAY    "LDMS_V_S8_ARRAY"
        V_U16_ARRAY   "LDMS_V_U16_ARRAY"
        V_S16_ARRAY   "LDMS_V_S16_ARRAY"
        V_U32_ARRAY   "LDMS_V_U32_ARRAY"
        V_S32_ARRAY   "LDMS_V_S32_ARRAY"
        V_U64_ARRAY   "LDMS_V_U64_ARRAY"
        V_S64_ARRAY   "LDMS_V_S64_ARRAY"
        V_F32_ARRAY   "LDMS_V_F32_ARRAY"
        V_D64_ARRAY   "LDMS_V_D64_ARRAY"
        V_FIRST       "LDMS_V_FIRST"
        V_LAST        "LDMS_V_LAST"
        LDMS_V_NONE
        LDMS_V_CHAR
        LDMS_V_U8
        LDMS_V_S8
        LDMS_V_U16
        LDMS_V_S16
        LDMS_V_U32
        LDMS_V_S32
        LDMS_V_U64
        LDMS_V_S64
        LDMS_V_F32
        LDMS_V_D64
        LDMS_V_CHAR_ARRAY
        LDMS_V_U8_ARRAY
        LDMS_V_S8_ARRAY
        LDMS_V_U16_ARRAY
        LDMS_V_S16_ARRAY
        LDMS_V_U32_ARRAY
        LDMS_V_S32_ARRAY
        LDMS_V_U64_ARRAY
        LDMS_V_S64_ARRAY
        LDMS_V_F32_ARRAY
        LDMS_V_D64_ARRAY
        LDMS_V_FIRST
        LDMS_V_LAST
    union ldms_value:
        char v_char
        uint8_t v_u8
        int8_t v_s8
        uint16_t v_u16
        int16_t v_s16
        uint32_t v_u32
        int32_t v_s32
        uint64_t v_u64
        int64_t v_s64
        float v_f
        double v_d
        char a_char[0]
        uint8_t a_u8[0]
        int8_t a_s8[0]
        uint16_t a_u16[0]
        int16_t a_s16[0]
        uint32_t a_u32[0]
        int32_t a_s32[0]
        uint64_t a_u64[0]
        int64_t a_s64[0]
        float a_f[0]
        double a_d[0]
    ctypedef ldms_value *ldms_mval_t
    int ldms_metric_by_name(ldms_set_t s, const char *name)
    const char *ldms_metric_name_get(ldms_set_t s, int i)
    int ldms_type_is_array(ldms_value_type t)
    ldms_value_type ldms_metric_type_get(ldms_set_t s, int i)
    ldms_mval_t ldms_metric_get(ldms_set_t s, int i)
    # --- set metric get --- #
    char ldms_metric_get_char(ldms_set_t s, int i)
    uint8_t ldms_metric_get_u8(ldms_set_t s, int i)
    uint16_t ldms_metric_get_u16(ldms_set_t s, int i)
    uint32_t ldms_metric_get_u32(ldms_set_t s, int i)
    uint64_t ldms_metric_get_u64(ldms_set_t s, int i)
    int8_t ldms_metric_get_s8(ldms_set_t s, int i)
    int16_t ldms_metric_get_s16(ldms_set_t s, int i)
    int32_t ldms_metric_get_s32(ldms_set_t s, int i)
    int64_t ldms_metric_get_s64(ldms_set_t s, int i)
    float ldms_metric_get_float(ldms_set_t s, int i)
    double ldms_metric_get_double(ldms_set_t s, int i)
    uint32_t ldms_metric_array_get_len(ldms_set_t s, int i)
    const char *ldms_metric_array_get_str(ldms_set_t s, int i)
    char ldms_metric_array_get_char(ldms_set_t s, int i, int idx)
    uint8_t ldms_metric_array_get_u8(ldms_set_t s, int i, int idx)
    uint16_t ldms_metric_array_get_u16(ldms_set_t s, int i, int idx)
    uint32_t ldms_metric_array_get_u32(ldms_set_t s, int i, int idx)
    uint64_t ldms_metric_array_get_u64(ldms_set_t s, int i, int idx)
    int8_t ldms_metric_array_get_s8(ldms_set_t s, int i, int idx)
    int16_t ldms_metric_array_get_s16(ldms_set_t s, int i, int idx)
    int32_t ldms_metric_array_get_s32(ldms_set_t s, int i, int idx)
    int64_t ldms_metric_array_get_s64(ldms_set_t s, int i, int idx)
    float ldms_metric_array_get_float(ldms_set_t s, int i, int idx)
    double ldms_metric_array_get_double(ldms_set_t s, int i, int idx)

    # --- set schema --- #
    struct ldms_schema_s:
        pass
    ctypedef ldms_schema_s *ldms_schema_t
    ldms_schema_t ldms_schema_new(const char *schema_name)
    void ldms_schema_delete(ldms_schema_t schema)
    int ldms_schema_metric_count_get(ldms_schema_t schema)
    int ldms_schema_array_card_set(ldms_schema_t schema, int card)
    size_t ldms_schema_set_size(const char *instance_name,
                                const ldms_schema_t schema)
    int ldms_schema_metric_add(ldms_schema_t s, const char *name,
                               ldms_value_type t)
    int ldms_schema_meta_add(ldms_schema_t s, const char *name,
                             ldms_value_type t)
    int ldms_schema_metric_array_add(ldms_schema_t s, const char *name,
                                     ldms_value_type t, uint32_t count)
    int ldms_schema_meta_array_add(ldms_schema_t s, const char *name,
                                   ldms_value_type t, uint32_t count)

    # --- set provider --- #
    ldms_set_t ldms_set_new(const char *instance_name, ldms_schema_t schema)
    ldms_set_t ldms_set_new_with_auth(const char *instance_name,
				  ldms_schema_t schema,
				  uid_t uid, gid_t gid, mode_t perm)
    int ldms_set_publish(ldms_set_t set)
    int ldms_set_unpublish(ldms_set_t set)
    void ldms_set_delete(ldms_set_t s)
    void ldms_set_put(ldms_set_t s)
    void ldms_metric_set_char(ldms_set_t s, int i, char v)
    void ldms_metric_set_u8(ldms_set_t s, int i, uint8_t v)
    void ldms_metric_set_u16(ldms_set_t s, int i, uint16_t v)
    void ldms_metric_set_u32(ldms_set_t s, int i, uint32_t v)
    void ldms_metric_set_u64(ldms_set_t s, int i, uint64_t v)
    void ldms_metric_set_s8(ldms_set_t s, int i, int8_t v)
    void ldms_metric_set_s16(ldms_set_t s, int i, int16_t v)
    void ldms_metric_set_s32(ldms_set_t s, int i, int32_t v)
    void ldms_metric_set_s64(ldms_set_t s, int i, int64_t v)
    void ldms_metric_set_float(ldms_set_t s, int i, float v)
    void ldms_metric_set_double(ldms_set_t s, int i, double v)
    void ldms_metric_array_set_str(ldms_set_t s, int mid, const char *str)
    void ldms_metric_array_set_char(ldms_set_t s, int mid, int idx, char v)
    void ldms_metric_array_set_u8(ldms_set_t s, int mid, int idx, uint8_t v)
    void ldms_metric_array_set_u16(ldms_set_t s, int mid, int idx, uint16_t v)
    void ldms_metric_array_set_u32(ldms_set_t s, int mid, int idx, uint32_t v)
    void ldms_metric_array_set_u64(ldms_set_t s, int mid, int idx, uint64_t v)
    void ldms_metric_array_set_s8(ldms_set_t s, int mid, int idx, int8_t v)
    void ldms_metric_array_set_s16(ldms_set_t s, int mid, int idx, int16_t v)
    void ldms_metric_array_set_s32(ldms_set_t s, int mid, int idx, int32_t v)
    void ldms_metric_array_set_s64(ldms_set_t s, int mid, int idx, int64_t v)
    void ldms_metric_array_set_float(ldms_set_t s, int mid, int idx, float v)
    void ldms_metric_array_set_double(ldms_set_t s, int mid, int idx, double v)
