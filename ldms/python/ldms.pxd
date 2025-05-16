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

cdef extern from * nogil:
    struct ldms_xprt:
        pass
    ctypedef ldms_xprt *ldms_t

    uint16_t be16toh(uint16_t x)
    uint16_t htobe16(uint16_t x)
    uint32_t be32toh(uint32_t x)
    uint32_t htobe32(uint32_t x)
    uint64_t be64toh(uint64_t x)
    uint64_t htobe64(uint64_t x)

    enum:
        AF_INET,
        AF_INET6,

    struct sockaddr:
        uint16_t sa_family
        pass
    struct sockaddr_storage:
        pass
    struct in_addr:
        uint32_t s_addr
    struct sockaddr_in:
        uint16_t sin_port
        in_addr sin_addr
    struct in6_addr:
        pass
    struct sockaddr_in6:
        uint16_t sin6_port
        in6_addr sin6_addr
    ctypedef uint32_t socklen_t

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

cdef extern from "coll/rbt.h" nogil:
    struct rbt:
        pass
    struct rbn:
        pass
    rbn *rbt_min(rbt *rbt)
    rbn *rbn_succ(rbn *rbn)

cdef extern from "ovis_util/util.h" nogil:
    struct attr_value_list:
        pass
    attr_value_list* av_new(size_t size)
    int tokenize(char *cmd, attr_value_list *kwl, attr_value_list *avl)
    int av_add(attr_value_list *avl, const char *name, const char *value)
    void av_free(attr_value_list *avl)

cdef extern from "ovis_log/ovis_log.h" nogil:
    cpdef enum:
        OVIS_LDEFAULT
        OVIS_LQUIET
        OVIS_LDEBUG
        OVIS_LINFO
        OVIS_LWARN
        OVIS_LWARNING
        OVIS_LERROR
        OVIS_LCRITICAL
        OVIS_LCRIT

        LDEFAULT  "OVIS_LDEFAULT"
        LQUIET    "OVIS_LQUIET"
        LDEBUG    "OVIS_LDEBUG"
        LINFO     "OVIS_LINFO"
        LWARN     "OVIS_LWARN"
        LWARNING  "OVIS_LWARNING"
        LERROR    "OVIS_LERROR"
        LCRITICAL "OVIS_LCRITICAL"
        LCRIT     "OVIS_LCRIT"

    int ovis_log_set_level_by_name(const char *subsys_name, int level)

cdef extern from "ovis_thrstats/ovis_thrstats.h" nogil:
    struct timespec:
        long tv_sec
        long tv_nsec

    struct ovis_thrstats_result:
        char *name
        pid_t tid
        uint64_t thread_id
        int waiting
        timespec start
        timespec wait_start
        timespec wait_end
        uint64_t idle_tot
        uint64_t active_tot
        uint64_t dur_tot
        uint64_t interval_us
        uint64_t idle_us
        uint64_t active_us
        void *app_ctxt

cdef extern from "ldms_rail.h" nogil:
    cpdef enum :
        LDMS_UNLIMITED
        RAIL_UNLIMITED "LDMS_UNLIMITED"

    int ldms_xprt_rail_pending_ret_quota_get(ldms_t x, uint64_t *out, int n)
    int ldms_xprt_rail_in_eps_stq_get(ldms_t _r, uint64_t *out, int n)

cdef extern from "ldms_core.h" nogil:
    cpdef enum :
        LDMS_MDESC_F_DATA
        LDMS_MDESC_F_META
        LDMS_MDESC_F_RECORD

cdef extern from "ldms.h" nogil:
    struct ldms_timestamp:
        uint32_t sec
        uint32_t usec

    struct ldms_cred:
        uid_t uid
        gid_t gid

    # --- xprt connection related --- #
    cpdef enum ldms_xprt_event_type:
        EVENT_CONNECTED     "LDMS_XPRT_EVENT_CONNECTED"
        EVENT_REJECTED      "LDMS_XPRT_EVENT_REJECTED"
        EVENT_ERROR         "LDMS_XPRT_EVENT_ERROR"
        EVENT_DISCONNECTED  "LDMS_XPRT_EVENT_DISCONNECTED"
        EVENT_RECV          "LDMS_XPRT_EVENT_RECV"
        EVENT_SET_DELETE    "LDMS_XPRT_EVENT_SET_DELETE"
        EVENT_SEND_COMPLETE "LDMS_XPRT_EVENT_SEND_COMPLETE"
        EVENT_SEND_QUOTA_DEPOSITED "LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED"
        EVENT_QGROUP_ASK    "LDMS_XPRT_EVENT_QGROUP_ASK"
        EVENT_QGROUP_DONATE "LDMS_XPRT_EVENT_QGROUP_DONATE"
        EVENT_QGROUP_DONATE_BACK "LDMS_XPRT_EVENT_QGROUP_DONATE_BACK"
        EVENT_LAST          "LDMS_XPRT_EVENT_LAST"
        LDMS_XPRT_EVENT_CONNECTED
        LDMS_XPRT_EVENT_REJECTED
        LDMS_XPRT_EVENT_ERROR
        LDMS_XPRT_EVENT_DISCONNECTED
        LDMS_XPRT_EVENT_RECV
        LDMS_XPRT_EVENT_SET_DELETE
        LDMS_XPRT_EVENT_SEND_COMPLETE
        LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED
        LDMS_XPRT_EVENT_QGROUP_ASK
        LDMS_XPRT_EVENT_QGROUP_DONATE
        LDMS_XPRT_EVENT_QGROUP_DONATE_BACK
        LDMS_XPRT_EVENT_LAST
    cdef struct ldms_xprt_quota_event_data:
        uint64_t quota
        int      ep_idx
        int      rc
    cdef struct ldms_xprt_set_delete_data:
        void * set
        const char *name
    cdef struct ldms_xprt_event:
        int type
        size_t data_len
        # data, and quota are in union. Cython doesn't care. It just want to
        # know the names of the "fields" it can access in C code.
        char *data
        ldms_xprt_quota_event_data quota
        ldms_xprt_set_delete_data set_delete
    ctypedef ldms_xprt_event *ldms_xprt_event_t
    ctypedef void (*ldms_event_cb_t)(ldms_t x, ldms_xprt_event_t e, void *cb_arg) except *

    int ldms_init(size_t max_size)
    ldms_t ldms_xprt_new_with_auth(const char *xprt_name,
                                   const char *auth_name,
                                   attr_value_list *auth_av_list)
    ldms_t ldms_xprt_rail_new(const char *xprt_name,
			  int n, int64_t recv_quota, int32_t rate_limit,
			  const char *auth_name,
			  attr_value_list *auth_av_list)
    void ldms_xprt_put(ldms_t x, const char *name)
    void ldms_xprt_close(ldms_t x)
    int ldms_xprt_connect_by_name(ldms_t x, const char *host, const char *port,
                                  ldms_event_cb_t cb, void *cb_arg)
    int ldms_xprt_listen_by_name(ldms_t x, const char *host, const char *port,
                                 ldms_event_cb_t cb, void *cb_arg)

    ctypedef void (*app_ctxt_free_fn)(void *ctxt) except *
    void ldms_xprt_ctxt_set(ldms_t x, void *ctxt, app_ctxt_free_fn fn)
    int ldms_xprt_sockaddr(ldms_t x, sockaddr *local_sa,
		           sockaddr *remote_sa,
		           socklen_t *sa_len)

    int ldms_xprt_peer_msg_is_enabled(ldms_t x)

    const char *ldms_metric_type_to_str(ldms_value_type t)

    # --- quota group (qgroup) --- #
    struct ldms_qgroup_s:
        pass
    ctypedef ldms_qgroup_s *ldms_qgroup_t
    ctypedef ldms_qgroup_cfg_s *ldms_qgroup_cfg_t
    struct ldms_qgroup_cfg_s:
        uint64_t quota
        uint64_t ask_mark
        uint64_t ask_amount
        uint64_t ask_usec
        uint64_t reset_usec
        void *app_ctxt

    int ldms_qgroup_cfg_quota_set(uint64_t quota)
    int ldms_qgroup_cfg_ask_usec_set(uint64_t usec)
    int ldms_qgroup_cfg_reset_usec_set(uint64_t usec)
    int ldms_qgroup_cfg_ask_mark_set(uint64_t ask_mark)
    int ldms_qgroup_cfg_ask_amount_set(uint64_t ask_amount)

    int ldms_qgroup_cfg_set(ldms_qgroup_cfg_t cfg)
    ldms_qgroup_cfg_s ldms_qgroup_cfg_get()

    int ldms_qgroup_member_add(const char *xprt_name,
                               const char *host, const char *port,
                               const char *auth_name,
                               attr_value_list *auth_av_list)
    int ldms_qgroup_start()
    int ldms_qgroup_stop()

    uint64_t ldms_qgroup_quota_probe()

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
        char *digest_str
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
    ctypedef void (*ldms_dir_cb_t)(ldms_t t, int status, ldms_dir_t dir, void *cb_arg) except *
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
    ctypedef void (*ldms_lookup_cb_t)(ldms_t x, ldms_lookup_status status, int more, ldms_set_t s, void *arg) except *
    int ldms_xprt_lookup(ldms_t x, const char *name, ldms_lookup_flags flags,
		         ldms_lookup_cb_t cb, void *cb_arg)
    int ldms_xprt_send(ldms_t x, char *msg_buf, size_t msg_len)
    size_t ldms_xprt_msg_max(ldms_t x)
    ctypedef uint64_t pthread_t
    int ldms_xprt_get_threads(ldms_t x, pthread_t *out, int n)
    int ldms_xprt_is_rail(ldms_t x)
    int ldms_xprt_is_remote_rail(ldms_t x)
    int ldms_xprt_rail_eps(ldms_t x)
    int ldms_xprt_rail_send_quota_get(ldms_t x, uint64_t *quota, int n)
    int64_t ldms_xprt_rail_recv_quota_get(ldms_t x)
    int ldms_xprt_rail_recv_quota_set(ldms_t x, uint64_t q)
    int64_t ldms_xprt_rail_send_rate_limit_get(ldms_t x)
    int64_t ldms_xprt_rail_recv_rate_limit_get(ldms_t x)
    int ldms_xprt_rail_recv_rate_limit_set(ldms_t x, uint64_t rate)

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
    uint64_t ldms_set_heap_gn_get(ldms_set_t s)

    int ldms_set_is_consistent(ldms_set_t s)

    ldms_timestamp ldms_transaction_timestamp_get(ldms_set_t s)
    ldms_timestamp ldms_transaction_duration_get(ldms_set_t s)

    int ldms_transaction_begin(ldms_set_t s)
    int ldms_transaction_end(ldms_set_t s)
    void ldms_set_data_copy_set(ldms_set_t s, int on)

    int ldms_set_info_set(ldms_set_t s, const char *key, const char *value)
    void ldms_set_info_unset(ldms_set_t s, const char *key)
    char *ldms_set_info_get(ldms_set_t s, const char *key)
    ctypedef void (*ldms_update_cb_t)(ldms_t t, ldms_set_t s, int flags, void *arg) except *
    struct ldms_list:
        uint32_t head
        uint32_t tail
        uint32_t count
    struct ldms_list_entry:
        uint32_t next
        uint32_t prev
        uint32_t type
        uint32_t count
        uint8_t  value[0]
    cpdef enum:
        UPD_F_PUSH       "LDMS_UPD_F_PUSH"
        UPD_F_PUSH_LAST  "LDMS_UPD_F_PUSH_LAST"
        UPD_F_MORE       "LDMS_UPD_F_MORE"
        UPD_ERROR_MASK   "LDMS_UPD_ERROR_MASK"
        LDMS_UPD_F_PUSH
        LDMS_UPD_F_PUSH_LAST
        LDMS_UPD_F_MORE
        LDMS_UPD_ERROR_MASK
        LDMS_XPRT_PUSH_F_CHANGE "LDMS_XPRT_PUSH_F_CHANGE"
    int LDMS_UPD_ERROR(int flags)
    int ldms_xprt_update(ldms_set_t s, ldms_update_cb_t update_cb, void *arg)
    int ldms_xprt_register_push(ldms_set_t s, int push_flags,
				ldms_update_cb_t cb_fn, void *cb_arg)
    int ldms_xprt_cancel_push(ldms_set_t s)

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
        V_LIST        "LDMS_V_LIST"
        V_LIST_ENTRY  "LDMS_V_LIST_ENTRY"
        V_RECORD_TYPE      "LDMS_V_RECORD_TYPE"
        V_RECORD_INST      "LDMS_V_RECORD_INST"
        V_RECORD_ARRAY     "LDMS_V_RECORD_ARRAY"
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
        LDMS_V_LIST
        LDMS_V_LIST_ENTRY
        LDMS_V_RECORD_TYPE
        LDMS_V_RECORD_INST
        LDMS_V_RECORD_ARRAY
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
        ldms_list v_lh
        ldms_list_entry v_le
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
    const char *ldms_metric_unit_get(ldms_set_t s, int i);
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

    struct ldms_record:
        pass # opaque
    ctypedef ldms_record *ldms_record_t

    # --- set schema --- #
    struct ldms_schema_s:
        pass
    ctypedef ldms_schema_s *ldms_schema_t
    struct ldms_metric_template_s:
        const char *name
        int flags
        ldms_value_type type
        const char *unit
        uint32_t len
        ldms_record_t rec_def
    ctypedef ldms_metric_template_s *ldms_metric_template_t
    ldms_schema_t ldms_schema_new(const char *schema_name)
    void ldms_schema_delete(ldms_schema_t schema)
    int ldms_schema_metric_count_get(ldms_schema_t schema)
    int ldms_schema_array_card_set(ldms_schema_t schema, int card)
    size_t ldms_schema_set_size(const char *instance_name,
                                const ldms_schema_t schema)
    int ldms_schema_metric_add(ldms_schema_t s, const char *name,
                               ldms_value_type t)
    int ldms_schema_metric_add_with_unit(ldms_schema_t s, const char *name,
					    const char *unit, ldms_value_type type);
    int ldms_schema_meta_add(ldms_schema_t s, const char *name,
                             ldms_value_type t)
    int ldms_schema_meta_add_with_unit(ldms_schema_t s, const char *name,
					  const char *unit, ldms_value_type t);
    int ldms_schema_metric_array_add(ldms_schema_t s, const char *name,
                                     ldms_value_type t, uint32_t count)
    int ldms_schema_metric_array_add_with_unit(ldms_schema_t s, const char *name,
						  const char *unit, ldms_value_type t, uint32_t count);
    int ldms_schema_meta_array_add(ldms_schema_t s, const char *name,
                                   ldms_value_type t, uint32_t count)
    int ldms_schema_meta_array_add_with_unit(ldms_schema_t s, const char *name,
						const char *unit, ldms_value_type t, uint32_t count);

    int ldms_schema_metric_template_get(ldms_schema_t schema, int mid,
                                        ldms_metric_template_s *out);
    int ldms_schema_bulk_template_get(ldms_schema_t schema, int len,
                                      ldms_metric_template_s out[])

    # --- schema hash / digest --- #
    ctypedef unsigned char *ldms_digest_t
    ldms_digest_t ldms_set_digest_get(ldms_set_t s)
    const char *ldms_digest_str(ldms_digest_t digest, char *buf, int buf_len)
    cpdef enum:
        LDMS_DIGEST_LENGTH

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

    # --- heap / list related functions --- #
    size_t ldms_list_heap_size_get(ldms_value_type t, size_t item_count,
                                   size_t array_count)
    int ldms_schema_metric_list_add(ldms_schema_t s, const char *name,
				    const char *units, uint32_t heap_sz)
    ldms_mval_t ldms_list_append_item(ldms_set_t s, ldms_mval_t l, ldms_value_type typ, size_t count)
    ldms_mval_t ldms_list_first(ldms_set_t s, ldms_mval_t l, ldms_value_type *typ_out, size_t *count)
    ldms_mval_t ldms_list_next(ldms_set_t s, ldms_mval_t v, ldms_value_type *typ_out, size_t *count)
    size_t ldms_list_len(ldms_set_t s, ldms_mval_t l)
    int ldms_list_remove_item(ldms_set_t s, ldms_mval_t lh, ldms_mval_t v)
    int ldms_list_purge(ldms_set_t s, ldms_mval_t lh)

    # --- record related functions --- #
    ldms_record_t ldms_record_create(const char *name)
    int ldms_record_metric_add(ldms_record_t rec_def, const char *name,
			   const char *unit, ldms_value_type type,
			   size_t count)
    int ldms_schema_record_add(ldms_schema_t s, ldms_record_t rec_def)
    int ldms_schema_record_array_add(ldms_schema_t s, const char *name,
                                     ldms_record_t rec_def, int array_len)
    size_t ldms_record_heap_size_get(ldms_record_t rec_def)

    # --- record instance functions --- #
    int ldms_list_append_record(ldms_set_t set, ldms_mval_t lh, ldms_mval_t rec_inst)
    ldms_mval_t ldms_record_alloc(ldms_set_t set, int metric_id)
    int ldms_record_type_get(ldms_mval_t rec_inst)
    int ldms_record_card(ldms_mval_t rec_inst)
    int ldms_record_metric_find(ldms_mval_t rec_inst, const char *name)
    ldms_mval_t ldms_record_metric_get(ldms_mval_t rec_inst, int metric_id)
    const char *ldms_record_metric_name_get(ldms_mval_t rec_inst, int metric_id)
    const char *ldms_record_metric_unit_get(ldms_mval_t rec_inst, int metric_id)
    ldms_value_type ldms_record_metric_type_get(ldms_mval_t rec_inst,
                                                     int metric_id, size_t *count)
    int ldms_metric_flags_get(ldms_set_t s, int i)
    void ldms_record_metric_set(ldms_mval_t rec_inst, int metric_id,
                                ldms_mval_t val)
    void ldms_record_metric_array_set(ldms_mval_t rec_inst, int metric_id,
                                      ldms_mval_t val, int start, int count)
    char       ldms_record_get_char(ldms_mval_t rec_inst, int metric_id)
    uint8_t      ldms_record_get_u8(ldms_mval_t rec_inst, int metric_id)
    uint16_t    ldms_record_get_u16(ldms_mval_t rec_inst, int metric_id)
    uint32_t    ldms_record_get_u32(ldms_mval_t rec_inst, int metric_id)
    uint64_t    ldms_record_get_u64(ldms_mval_t rec_inst, int metric_id)
    int8_t       ldms_record_get_s8(ldms_mval_t rec_inst, int metric_id)
    int16_t     ldms_record_get_s16(ldms_mval_t rec_inst, int metric_id)
    int32_t     ldms_record_get_s32(ldms_mval_t rec_inst, int metric_id)
    int64_t     ldms_record_get_s64(ldms_mval_t rec_inst, int metric_id)
    float     ldms_record_get_float(ldms_mval_t rec_inst, int metric_id)
    double   ldms_record_get_double(ldms_mval_t rec_inst, int metric_id)
    const char *ldms_record_array_get_str(ldms_mval_t rec_inst, int metric_id)
    char       ldms_record_array_get_char(ldms_mval_t rec_inst, int metric_id, int idx)
    uint8_t      ldms_record_array_get_u8(ldms_mval_t rec_inst, int metric_id, int idx)
    uint16_t    ldms_record_array_get_u16(ldms_mval_t rec_inst, int metric_id, int idx)
    uint32_t    ldms_record_array_get_u32(ldms_mval_t rec_inst, int metric_id, int idx)
    uint64_t    ldms_record_array_get_u64(ldms_mval_t rec_inst, int metric_id, int idx)
    int8_t       ldms_record_array_get_s8(ldms_mval_t rec_inst, int metric_id, int idx)
    int16_t     ldms_record_array_get_s16(ldms_mval_t rec_inst, int metric_id, int idx)
    int32_t     ldms_record_array_get_s32(ldms_mval_t rec_inst, int metric_id, int idx)
    int64_t     ldms_record_array_get_s64(ldms_mval_t rec_inst, int metric_id, int idx)
    float     ldms_record_array_get_float(ldms_mval_t rec_inst, int metric_id, int idx)
    double   ldms_record_array_get_double(ldms_mval_t rec_inst, int metric_id, int idx)
    void   ldms_record_set_char(ldms_mval_t rec_inst, int metric_id,     char val)
    void     ldms_record_set_u8(ldms_mval_t rec_inst, int metric_id,  uint8_t val)
    void    ldms_record_set_u16(ldms_mval_t rec_inst, int metric_id, uint16_t val)
    void    ldms_record_set_u32(ldms_mval_t rec_inst, int metric_id, uint32_t val)
    void    ldms_record_set_u64(ldms_mval_t rec_inst, int metric_id, uint64_t val)
    void     ldms_record_set_s8(ldms_mval_t rec_inst, int metric_id,   int8_t val)
    void    ldms_record_set_s16(ldms_mval_t rec_inst, int metric_id,  int16_t val)
    void    ldms_record_set_s32(ldms_mval_t rec_inst, int metric_id,  int32_t val)
    void    ldms_record_set_s64(ldms_mval_t rec_inst, int metric_id,  int64_t val)
    void  ldms_record_set_float(ldms_mval_t rec_inst, int metric_id,    float val)
    void ldms_record_set_double(ldms_mval_t rec_inst, int metric_id,   double val)
    void    ldms_record_array_set_str(ldms_mval_t rec_inst, int metric_id, const char *val)
    void   ldms_record_array_set_char(ldms_mval_t rec_inst, int metric_id, int idx,     char val)
    void     ldms_record_array_set_u8(ldms_mval_t rec_inst, int metric_id, int idx,  uint8_t val)
    void    ldms_record_array_set_u16(ldms_mval_t rec_inst, int metric_id, int idx, uint16_t val)
    void    ldms_record_array_set_u32(ldms_mval_t rec_inst, int metric_id, int idx, uint32_t val)
    void    ldms_record_array_set_u64(ldms_mval_t rec_inst, int metric_id, int idx, uint64_t val)
    void     ldms_record_array_set_s8(ldms_mval_t rec_inst, int metric_id, int idx,   int8_t val)
    void    ldms_record_array_set_s16(ldms_mval_t rec_inst, int metric_id, int idx,  int16_t val)
    void    ldms_record_array_set_s32(ldms_mval_t rec_inst, int metric_id, int idx,  int32_t val)
    void    ldms_record_array_set_s64(ldms_mval_t rec_inst, int metric_id, int idx,  int64_t val)
    void  ldms_record_array_set_float(ldms_mval_t rec_inst, int metric_id, int idx,    float val)
    void ldms_record_array_set_double(ldms_mval_t rec_inst, int metric_id, int idx,   double val)

    ldms_mval_t ldms_record_array_get_inst(ldms_mval_t rec_array, int idx);
    int ldms_record_array_len(ldms_mval_t rec_array)

    int ldms_record_metric_template_get(ldms_record_t record, int mid,
                                        ldms_metric_template_s *out)
    int ldms_record_bulk_template_get(ldms_record_t record, int len,
                                      ldms_metric_template_s out[])
    const char * ldms_record_name_get(ldms_record_t record)

    # --- ldms_msg --- #
    struct ldms_msg_client_s:
        pass # opaque
    struct json_entity_s:
        pass # opaque
    ctypedef json_entity_s *json_entity_t;
    ctypedef ldms_msg_client_s *ldms_msg_client_t;
    cpdef enum ldms_msg_type_e:
        LDMS_MSG_STRING
        LDMS_MSG_JSON
        LDMS_MSG_AVRO_SER
    enum ldms_msg_event_type:
        LDMS_MSG_EVENT_RECV
        LDMS_MSG_EVENT_SUBSCRIBE_STATUS
        LDMS_MSG_EVENT_UNSUBSCRIBE_STATUS
    struct ldms_addr:
        uint8_t  addr[4];
        uint16_t sin_port;
        uint16_t sa_family;
    struct ldms_msg_recv_data_s:
        ldms_msg_client_t client
        ldms_addr src
        uint64_t msg_gn
        ldms_msg_type_e type
        uint32_t name_len
        uint32_t data_len
        const char *name
        const char *data
        json_entity_t json
        ldms_cred cred
        uint32_t perm
    struct ldms_msg_return_status_s:
        const char *match
        int is_regex
        int status
    struct ldms_msg_event_s:
        ldms_t r
        ldms_msg_event_type type
        ldms_msg_recv_data_s recv
        ldms_msg_return_status_s status
    ctypedef ldms_msg_event_s *ldms_msg_event_t
    ctypedef int (*ldms_msg_event_cb_t)(ldms_msg_event_t ev, void *cb_arg) except *

    void ldms_msg_disable()
    int ldms_msg_is_enabled()
    int ldms_msg_publish(ldms_t x, const char *name,
                            ldms_msg_type_e msg_type,
                            ldms_cred *cred,
                            uint32_t perm,
                            const char *data, size_t data_len)
    ldms_msg_client_t ldms_msg_subscribe(const char *match, int is_regex,
                            ldms_msg_event_cb_t cb_fn, void *cb_arg,
                            const char *desc)

    void ldms_msg_client_close(ldms_msg_client_t c)
    int ldms_msg_remote_subscribe(ldms_t x, const char *match, int is_regex,
                            ldms_msg_event_cb_t cb_fn, void *cb_arg,
                            int64_t rx_rate)
    int ldms_msg_remote_unsubscribe(ldms_t x, const char *match, int is_regex,
                            ldms_msg_event_cb_t cb_fn, void *cb_arg)

    # -- ldms_msg_stats -- #
    struct ldms_msg_counters_s:
        timespec first_ts
        timespec last_ts
        uint64_t  count
        size_t    bytes
    struct ldms_msg_src_stats_s:
        ldms_addr src
        ldms_msg_counters_s rx
    struct ldms_msg_ch_cli_stats_s:
        const char *name
        const char *client_match
        const char *client_desc
        int is_regex
        ldms_msg_counters_s tx
        ldms_msg_counters_s drops
    struct ldms_msg_ch_cli_stats_tq_s:
        pass
    struct ldms_msg_ch_stats_s:
        ldms_msg_counters_s rx
        rbt src_stats_rbt
        ldms_msg_ch_cli_stats_tq_s stats_tq
        const char *name
    struct ldms_msg_ch_stats_tq_s:
        pass
    struct ldms_msg_client_stats_s:
        ldms_msg_counters_s tx
        ldms_msg_counters_s drops
        ldms_msg_ch_cli_stats_tq_s stats_tq
        ldms_addr dest
        int is_regex
        const char *match
        const char *desc
    struct ldms_msg_client_stats_tq_s:
        pass
    int ldms_msg_stats_level_set(int level)
    int ldms_msg_stats_level_get()
    ldms_msg_ch_stats_tq_s * ldms_msg_ch_stats_tq_get(const char *match, int is_regex, int is_reset)
    void ldms_msg_ch_stats_tq_free(ldms_msg_ch_stats_tq_s *tq)
    char *ldms_msg_ch_stats_tq_to_str(ldms_msg_ch_stats_tq_s *tq)
    char *ldms_msg_stats_str(char *match, int is_regex)
    ldms_msg_client_stats_tq_s *ldms_msg_client_stats_tq_get(int is_reset)
    void ldms_msg_client_stats_tq_free(ldms_msg_client_stats_tq_s *tq)
    ldms_msg_client_stats_s *ldms_msg_client_get_stats(ldms_msg_client_t cli, int is_reset)
    void ldms_msg_client_stats_free(ldms_msg_client_stats_s *cs)
    char *ldms_msg_client_stats_tq_to_str(ldms_msg_client_stats_tq_s *tq)
    char *ldms_msg_client_stats_str()

cdef extern from "zap/zap.h" nogil:
    struct zap_thrstat_result_entry:
        ovis_thrstats_result res
        uint64_t n_eps
        uint64_t sq_sz
        int pool_idx

    struct zap_thrstat_result:
        int count
        zap_thrstat_result_entry entries[0]

    zap_thrstat_result *zap_thrstat_get_result(uint64_t interval_s)

cdef extern from "asm/byteorder.h" nogil:
    uint16_t __le16_to_cpu(uint16_t)
    uint16_t __cpu_to_le16(uint16_t)
    uint32_t __le32_to_cpu(uint32_t)
    uint32_t __cpu_to_le32(uint32_t)
    uint64_t __le64_to_cpu(uint64_t)
    uint64_t __cpu_to_le64(uint64_t)
