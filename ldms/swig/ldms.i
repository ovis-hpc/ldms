/*
 * Copyright (c) 2015-2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2017 Sandia Corporation. All rights reserved.
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
%module ldms
%include "cpointer.i"
%include "cstring.i"
%include "carrays.i"
%include "exception.i"
%include "typemaps.i"
%{
#include <stdio.h>
#include <sys/queue.h>
#include <semaphore.h>
#include <errno.h>
#include <pthread.h>
#include "ldms.h"
#include "ovis_util/util.h"

__attribute__((constructor))
void __init__()
{
	PyEval_InitThreads();
}

struct xprt_arg {
        ldms_t x;
        enum {
        	INIT = 1,
        	CONNECTED, /* Received a CONNECTED event */
        	DISCONNECTED, /* Received a DISCONNECTED event */
        	DESTROYED, /* Python deletes the transport object. (LDMS_xprt_destroy() is called. */
        } state;
        LIST_ENTRY(xprt_arg) entry;
};
LIST_HEAD(xprt_arg_list, xprt_arg) xprt_arg_list;
pthread_mutex_t xprt_arg_list_lock;

struct xprt_arg * __xprt_arg_new(ldms_t x)
{
        struct xprt_arg *arg = malloc(sizeof(*arg));
        if (!arg)
                return NULL;
        arg->x = x;
        arg->state = INIT;
        pthread_mutex_lock(&xprt_arg_list_lock);
        LIST_INSERT_HEAD(&xprt_arg_list, arg, entry);
        pthread_mutex_unlock(&xprt_arg_list_lock);
        return arg;
}

struct xprt_arg *__xprt_arg_find(ldms_t x)
{
        struct xprt_arg *arg;
        pthread_mutex_lock(&xprt_arg_list_lock);
        arg = LIST_FIRST(&xprt_arg_list);
        while (arg) {
                if (arg->x == x)
                        goto out;
                arg = LIST_NEXT(arg, entry);
        }
out:
        pthread_mutex_unlock(&xprt_arg_list_lock);
        return arg;
}

void __xprt_arg_destroy(struct xprt_arg *arg)
{
        pthread_mutex_lock(&xprt_arg_list_lock);
        LIST_REMOVE(arg, entry);
        pthread_mutex_unlock(&xprt_arg_list_lock);
        arg->x = NULL;
        free(arg);
}

ldms_t LDMS_xprt_new(const char *xprt)
{
        ldms_t x;
        x = ldms_xprt_new(xprt, NULL);
        (void) __xprt_arg_new(x);
        return x;
}

ldms_t LDMS_xprt_new_with_auth(const char *xprt, const char *auth_name,
			       PyObject *auth_opt)
{
	ldms_t x = NULL;
	PyObject *key, *value, *str;
	Py_ssize_t pos = 0;
	int n = 0;
	struct attr_value_list *av_list = NULL;
	char *s;

	if (!auth_name) {
		/* use default */
		return LDMS_xprt_new(xprt);
	}

	if (!auth_opt || auth_opt == Py_None)
		goto call;

	if (!PyObject_TypeCheck(auth_opt, &PyDict_Type)) {
		/* Bad type */
		PyErr_SetString(PyExc_TypeError, "auth_opt must be a dict");
		goto out;
	}

	n = PyDict_Size(auth_opt);
	if (!n)
		goto call;

	av_list = av_new(n);
	if (!av_list)
		goto nomem;

	while (PyDict_Next(auth_opt, &pos, &key, &value)) {
		/* key and value are borrowed, not a new reference. No need to
		 * call Py_DECREF().
		 *
		 * In addition, PyString_AsString() returns the internal
		 * pointer. The caller must not modify or free it.
		 * PyString_AsString() also automatically raise TypeError if the
		 * object is not a String.
		 */
		s = PyString_AsString(key);
		if (!s)
			goto cleanup;
		av_list->list[av_list->count].name = s;
		s = PyString_AsString(value);
		if (!s)
			goto cleanup;
		av_list->list[av_list->count].value = s;
		av_list->count++;
	}

call:
	x = ldms_xprt_new_with_auth(xprt, NULL, auth_name, av_list);
	(void) __xprt_arg_new(x);
	goto cleanup;

nomem:
	PyErr_SetString(PyExc_RuntimeError, "Out of memory");
	goto cleanup;

cleanup:
	if (av_list)
		free(av_list);
out:
	return x;
}

ldms_set_t LDMS_xprt_lookup(ldms_t x, const char *name, enum ldms_lookup_flags flags)
{
	ldms_set_t set;
	int rc = ldms_xprt_lookup(x, name, flags, NULL, &set);
	if (!rc)
		return set;
	return NULL;
}

struct dir_arg {
	PyObject *setList;
	sem_t sem;
	int status;
};

void dir_cb(ldms_t t, int status, ldms_dir_t dir, void *cb_arg)
{
	struct dir_arg *arg = cb_arg;
	int i, more = 0;

	if (status) {
		arg->status = status;
		goto out;
	}

	for (i = 0; i < dir->set_count; i++) {
		PyObject *py_str = PyString_FromString(dir->set_names[i]);
		if (py_str) {
			PyList_Append(arg->setList, py_str);
		} else {
			arg->status = ENOMEM;
			goto out;
		}
	}
	more = dir->more;
out:
	ldms_xprt_dir_free(t, dir);
	if (!more)
		sem_post(&arg->sem);
}

PyObject *LDMS_xprt_dir(ldms_t x)
{
	PyObject *setList = PyList_New(0);
	struct dir_arg arg;

	if (!setList)
		goto err;

	arg.setList = setList;
	sem_init(&arg.sem, 0, 0);
	arg.status = 0;

	int rc = ldms_xprt_dir(x, dir_cb, &arg, 0);
	if (rc) {
		Py_DECREF(setList);
		goto err;
	}

	sem_wait(&arg.sem);
	sem_destroy(&arg.sem);

	if (arg.status) {
		Py_DECREF(setList);
		goto err;
	}
	return setList;
 err:
	Py_RETURN_NONE;
}

struct recv_buf {
        ldms_t x;
        TAILQ_ENTRY(recv_buf) entry;
        size_t data_len;
        char data[OVIS_FLEX];
};

struct recv_arg {
        ldms_t x;
        sem_t sem_ready;
        pthread_mutex_t lock;
        TAILQ_HEAD(recv_buf_q, recv_buf) recv_buf_q;
        LIST_ENTRY(recv_arg) entry;
};
LIST_HEAD(recv_arg_list, recv_arg) recv_arg_list;
pthread_mutex_t recv_arg_list_lock;

struct recv_buf *__recv_buf_new(ldms_t x, struct recv_arg *arg,
                                        size_t data_len, char *data)
{
        struct recv_buf *buf = malloc(sizeof(*buf) + data_len);
        if (!buf)
                return NULL;
        buf->x = x;
        buf->data_len = data_len;
        memcpy(buf->data, data, data_len);
        pthread_mutex_lock(&arg->lock);
        TAILQ_INSERT_TAIL(&arg->recv_buf_q, buf, entry);
        pthread_mutex_unlock(&arg->lock);
        return buf;
}

void __recv_buf_destroy(struct recv_buf *buf, struct recv_arg *arg)
{
        pthread_mutex_lock(&arg->lock);
        TAILQ_REMOVE(&arg->recv_buf_q, buf, entry);
        pthread_mutex_unlock(&arg->lock);
        buf->x = NULL;
        free(buf);
}

struct recv_buf *__recv_buf_next(struct recv_arg *arg)
{
        struct recv_buf *buf;
        pthread_mutex_lock(&arg->lock);
        buf = TAILQ_FIRST(&arg->recv_buf_q);
        pthread_mutex_unlock(&arg->lock);
        return buf;
}

void __cleanup_recv_buf_q(struct recv_arg *arg)
{
        pthread_mutex_lock(&arg->lock);
        struct recv_buf *buf = TAILQ_FIRST(&arg->recv_buf_q);
        while (buf) {
                __recv_buf_destroy(buf, arg);
                buf = TAILQ_FIRST(&arg->recv_buf_q);
        }
        pthread_mutex_unlock(&arg->lock);
}

struct recv_arg * __recv_arg_new(ldms_t x)
{
        struct recv_arg *arg = malloc(sizeof(*arg));
        if (!arg)
                return NULL;
        arg->x = x;
        sem_init(&arg->sem_ready, 0, 0);
        pthread_mutex_init(&arg->lock, 0);
        TAILQ_INIT(&arg->recv_buf_q);
        pthread_mutex_lock(&recv_arg_list_lock);
        LIST_INSERT_HEAD(&recv_arg_list, arg, entry);
        pthread_mutex_unlock(&recv_arg_list_lock);
        return arg;
}

/* Caller must hold the recv arg list lock */
struct recv_arg *__recv_arg_find(ldms_t x)
{
        struct recv_arg *arg;
        arg = LIST_FIRST(&recv_arg_list);
        while (arg) {
                if (arg->x == x)
                        goto out;
        }
out:
        return arg;
}

/* Caller must hold the recv arg list lock */
void __recv_arg_destroy(struct recv_arg *arg)
{
        LIST_REMOVE(arg, entry);
        __cleanup_recv_buf_q(arg);
        arg->x = NULL;
        sem_post(&arg->sem_ready); /* In case someone is waiting for a data chunk */
        sem_destroy(&arg->sem_ready);
        pthread_mutex_destroy(&arg->lock);
        free(arg);
}

PyObject *LDMS_xprt_recv(ldms_t x)
{
        PyObject *data;
        struct recv_buf *buf;
        struct xprt_arg *xprt_arg = __xprt_arg_find(x);
        if (!xprt_arg) {
                PyErr_SetString(PyExc_RuntimeError, "The transport context is missing");
                return NULL;
        }
        if (xprt_arg->state != CONNECTED)
                Py_RETURN_NONE;
        pthread_mutex_lock(&recv_arg_list_lock);
        struct recv_arg *arg = __recv_arg_find(x);
        if (!arg) {
                pthread_mutex_unlock(&recv_arg_list_lock);
                if (!xprt_arg->state != CONNECTED)
                        Py_RETURN_NONE;
                PyErr_SetString(PyExc_RuntimeError, "The receive context is missing");
                return NULL;
        }
        pthread_mutex_unlock(&recv_arg_list_lock);
        if (xprt_arg->state != CONNECTED)
                Py_RETURN_NONE;
        sem_wait(&arg->sem_ready);
        buf = __recv_buf_next(arg);
        if (buf) {
                data = PyByteArray_FromStringAndSize(buf->data, buf->data_len);
                __recv_buf_destroy(buf, arg);
                pthread_mutex_unlock(&recv_arg_list_lock);
                return data;
        }
        Py_RETURN_NONE;
}

struct passive_event_arg {
        ldms_t listener_x;
        ldms_t connected_x;
        sem_t sem;
        struct recv_arg *recv_arg;
        LIST_ENTRY(passive_event_arg) entry;
};
LIST_HEAD(passive_event_arg_list, passive_event_arg) passive_event_arg_list;
pthread_mutex_t pevent_arg_list_lock;

struct passive_event_arg *__passive_event_arg_new(ldms_t x)
{
        struct passive_event_arg *event_arg = malloc(sizeof(*event_arg));
        event_arg->listener_x = x;
        sem_init(&event_arg->sem, 0, 0);
        pthread_mutex_lock(&pevent_arg_list_lock);
        LIST_INSERT_HEAD(&passive_event_arg_list, event_arg, entry);
        pthread_mutex_unlock(&pevent_arg_list_lock);
        return event_arg;
}

struct passive_event_arg *__passive_event_arg_find(ldms_t listener_x,
                                                ldms_t connected_x)
{
        struct passive_event_arg *arg;
        pthread_mutex_lock(&pevent_arg_list_lock);
        arg = LIST_FIRST(&passive_event_arg_list);
        while (arg) {
                if (listener_x && arg->listener_x == listener_x)
                        goto out;
                else if (connected_x && arg->connected_x == connected_x)
                        goto out;
                arg = LIST_NEXT(arg, entry);
        }
out:
        pthread_mutex_unlock(&pevent_arg_list_lock);
        return arg;
}

void __passive_event_arg_destroy(struct passive_event_arg *arg)
{
        pthread_mutex_lock(&pevent_arg_list_lock);
        LIST_REMOVE(arg, entry);
        pthread_mutex_unlock(&pevent_arg_list_lock);
        sem_destroy(&arg->sem);
        free(arg);
}

static void __passive_event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
        struct passive_event_arg *event_arg;
        struct recv_buf *recv_buf;
        struct recv_arg *recv_arg;
        event_arg = (struct passive_event_arg *)cb_arg;
        switch(e->type) {
        case LDMS_XPRT_EVENT_CONNECTED:
                recv_arg = __recv_arg_new(x);
                if (!recv_arg) {
                        printf("Out of memory\n");
                        assert(0);
                }
                event_arg->recv_arg = recv_arg;
                event_arg->connected_x = x;
                sem_post(&event_arg->sem);
                break;
        case LDMS_XPRT_EVENT_DISCONNECTED:
                pthread_mutex_lock(&recv_arg_list_lock);
                __recv_arg_destroy(event_arg->recv_arg);
                pthread_mutex_unlock(&recv_arg_list_lock);
                free(event_arg);
                ldms_xprt_put(x);
                break;
        case LDMS_XPRT_EVENT_RECV:
                recv_buf = __recv_buf_new(x, event_arg->recv_arg,
                                                e->data_len, e->data);
                if (!recv_buf) {
                        printf("Out of memory\n");
                        assert(0);
                }
                sem_post(&event_arg->recv_arg->sem_ready);
                break;
        default:
                printf("Unhandled/Unrecognized ldms_conn_event %d", e->type);
                assert(0);
        }
}

int LDMS_xprt_listen_by_name(ldms_t x, const char *host, const char *port)
{
        int rc;
        struct passive_event_arg *arg = __passive_event_arg_new(x);
        if (!arg)
                return ENOMEM;
        rc = ldms_xprt_listen_by_name(x, host, port,
                        __passive_event_cb, arg);
        if (rc) {
                return rc;
        }
        return 0;
}

ldms_t LDMS_xprt_accept(ldms_t x)
{
        struct passive_event_arg *arg = __passive_event_arg_find(x, NULL);
        if (!arg)
                assert(0);
        sem_wait(&arg->sem);
        return arg->connected_x;
}

struct active_event_arg {
        sem_t sem;
        int errcode;
        struct recv_arg *recv_arg;
};

static void __active_event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
        struct active_event_arg *event_arg = cb_arg;
        struct recv_arg *recv_arg;
        struct recv_buf *recv_buf;
        struct xprt_arg *xprt_arg;
        xprt_arg = __xprt_arg_find(x);
        if (!xprt_arg) {
                PyErr_SetString(PyExc_RuntimeError, "The transport context is missing.");
                return;
        }
        switch(e->type) {
        case LDMS_XPRT_EVENT_CONNECTED:
                xprt_arg->state = CONNECTED;
                recv_arg = __recv_arg_new(x);
                if (!recv_arg) {
                        printf("Out of memory\n");
                        assert(0);
                }
                event_arg->recv_arg = recv_arg;
                sem_post(&event_arg->sem);
                break;
        case LDMS_XPRT_EVENT_DISCONNECTED:
                if (xprt_arg->state != DESTROYED) {
                        /* Application hasn't delete the transport object yet */
                        xprt_arg->state = DISCONNECTED;
                        pthread_mutex_lock(&recv_arg_list_lock);
                        __recv_arg_destroy(event_arg->recv_arg);
                        pthread_mutex_unlock(&recv_arg_list_lock);
                } else {
                        __xprt_arg_destroy(xprt_arg);
                        ldms_xprt_put(x);
                }
                sem_destroy(&event_arg->sem);
                free(event_arg);
                break;
        case LDMS_XPRT_EVENT_ERROR:
        case LDMS_XPRT_EVENT_REJECTED:
                event_arg->errcode = ECONNREFUSED;
                sem_post(&event_arg->sem);
                break;
        case LDMS_XPRT_EVENT_RECV:
                recv_buf = __recv_buf_new(x, event_arg->recv_arg,
                                        e->data_len, e->data);
                if (!recv_buf) {
                        printf("Out of memory\n");
                        assert(0);
                }
                sem_post(&event_arg->recv_arg->sem_ready);
                break;
        default:
                printf("Unhandled/Unrecognized ldms_conn_event %d\n", e->type);
                assert(0);
        }
}

int LDMS_xprt_connect_by_name(ldms_t x, const char *host, const char *port)
{
        struct active_event_arg *arg = malloc(sizeof(*arg));
        sem_init(&arg->sem, 0, 0);
        arg->errcode = 0;
        int rc = ldms_xprt_connect_by_name(x, host, port,
                        __active_event_cb, arg);
        sem_wait(&arg->sem);
        if (rc) {
                sem_destroy(&arg->sem);
                free(arg);
        }
        ldms_xprt_get(x);
        return arg->errcode;
}

/* Python __del__ will call this API */
void LDMS_xprt_destroy(ldms_t x)
{
        int close = 0;
        struct xprt_arg *arg = __xprt_arg_find(x);
        if (!arg) {
                PyErr_SetString(PyExc_RuntimeError, "The transport context is missing.");
                return;
        }
        if (arg->state == CONNECTED)
                close = 1;
        arg->state = DESTROYED;
        if (close) {
                ldms_xprt_close(x);
        } else {
                __xprt_arg_destroy(arg);
                ldms_xprt_put(x); /* Put back the reference taken when the transport is created */
        }
}

PyObject *PyObject_FromMetricValue(ldms_mval_t mv, enum ldms_value_type type)
{
        /*
         * NOTE: Assuming that the 'mv' is in an LDMS set--implying
         *       little-endian data format. Native Python code
         *       is very likely NOT to create an ldms_mval object.
         */
	int is_int = 1;
	long l = 0;
        uint32_t tmp32;
        uint64_t tmp64;
	double d = 0.0;
	switch (type) {
	case LDMS_V_U8:
		l = (long)mv->v_u8;
		break;
	case LDMS_V_S8:
		l = (long)mv->v_s8;
		break;
	case LDMS_V_U16:
		l = (uint16_t)__le16_to_cpu(mv->v_u16);
		break;
	case LDMS_V_S16:
		l = (int16_t)__le16_to_cpu(mv->v_s16);
		break;
	case LDMS_V_U32:
		l = (uint32_t)__le32_to_cpu(mv->v_u32);
		break;
	case LDMS_V_S32:
		l = (int32_t)__le32_to_cpu(mv->v_s32);
		break;
	case LDMS_V_U64:
		l = (uint64_t)__le64_to_cpu(mv->v_u64);
		break;
	case LDMS_V_S64:
		l = (int64_t)__le64_to_cpu(mv->v_s64);
		break;
	case LDMS_V_F32:
		is_int = 0;
		tmp32 = __le32_to_cpu(*(uint32_t*)&mv->v_f);
                d = *(float*)&tmp32;
		break;
	case LDMS_V_D64:
		is_int = 0;
		tmp64 = __le64_to_cpu(*(uint32_t*)&mv->v_d);
                d = *(double*)&tmp64;
		break;
	default:
		SWIG_exception(SWIG_TypeError, "Unrecognized ldms_value type");
	}
	if (is_int)
		return PyLong_FromLong(l);
	return PyFloat_FromDouble(d);
 fail:
	Py_RETURN_NONE;
}

%}

%{
typedef struct ldms_update_ctxt {
	sem_t sem;
	int rc;
} *ldms_update_ctxt_t;

void __update_cb(ldms_t xprt, ldms_set_t set, int flags, void *_ctxt)
{
	ldms_update_ctxt_t ctxt = _ctxt;
	if (flags & LDMS_UPD_F_MORE)
		return; /* wait for the last update */
	ctxt->rc = LDMS_UPD_ERROR(flags);
	sem_post(&ctxt->sem);
}

SWIGINTERN PyObject *
ldms_rbuf_desc_array_metric_value_get(struct ldms_rbuf_desc *, size_t, size_t);

struct __uctxt {
	PyObject *cb;
	PyObject *set;
	PyObject *ctxt;
};

struct __uctxt *__uctxt_new(PyObject *cb, PyObject *set, PyObject *ctxt)
{
	struct __uctxt *uctxt = calloc(1, sizeof(*uctxt));
	if (!uctxt)
		return NULL;
	Py_INCREF(cb);
	Py_INCREF(set);
	Py_INCREF(ctxt);
	uctxt->cb = cb;
	uctxt->set = set;
	uctxt->ctxt = ctxt;
	return uctxt;
}

void __uctxt_free(struct __uctxt *uctxt)
{
	Py_DECREF(uctxt->cb);
	Py_DECREF(uctxt->set);
	Py_DECREF(uctxt->ctxt);
	free(uctxt);
}

void __update_cb2(ldms_t xprt, ldms_set_t set, int flags, void *_ctxt)
{
	struct __uctxt *uctxt = _ctxt;
	PyObject *result;
	PyObject *args;
	PyObject *kwargs;
	PyGILState_STATE gstate;

	gstate = PyGILState_Ensure();

	/* build arguments */
	args = Py_BuildValue("()");
	kwargs = Py_BuildValue("{s:O, s:O, s:i}",
				"ldms_set", uctxt->set,
				"ctxt", uctxt->ctxt,
				"flags", flags);

	/* callback */
	result = PyObject_Call(uctxt->cb, args, kwargs);

	if (!result) {
		/* print here as this won't be caught by python thread */
		PyErr_Print();
	}

	/* cleanup */
	Py_XDECREF(result);
	Py_XDECREF(kwargs);
	Py_XDECREF(args);
	if (0 == (flags & LDMS_UPD_F_MORE)) {
		__uctxt_free(uctxt);
	}
	PyGILState_Release(gstate);
}
%}

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;

%include "ldms.h"
%include "ldms_core.h"

ldms_t LDMS_xprt_new(const char *xprt);
ldms_t LDMS_xprt_new_with_auth(const char *xprt, const char *auth_name,
			       PyObject *auth_opt);

int LDMS_xprt_listen_by_name(ldms_t x, const char *host, const char *port);
ldms_t LDMS_xprt_accept(ldms_t x);
int LDMS_xprt_connect_by_name(ldms_t x, const char *host, const char *port);
void LDMS_xprt_destroy(ldms_t x);
PyObject *LDMS_xprt_recv(ldms_t x);

ldms_set_t LDMS_xprt_lookup(ldms_t x, const char *name, enum ldms_lookup_flags flags);
PyObject *LDMS_xprt_dir(ldms_t x);

%extend ldms_value {
	inline PyObject *value(enum ldms_value_type type) {
		return PyObject_FromMetricValue(self, type);
	}
}

%nodefaultctor ldms_rbuf_desc;
%nodefaultdtor ldms_rbuf_desc;
struct ldms_rbuf_desc {};
typedef struct ldms_update_ctxt *ldms_update_ctxt_t;

%extend ldms_rbuf_desc {
	inline size_t __len__() { return ldms_set_card_get(self); }
	inline PyObject *metric_name_get(size_t i) {
		return PyString_FromString(ldms_metric_name_get(self, i));
	}
	inline enum ldms_value_type metric_type_get(size_t i) {
		return ldms_metric_type_get(self, i);
	}
	inline uint64_t metric_user_data_get(size_t i) {
		return ldms_metric_user_data_get(self, i);
	}
	inline void metric_user_data_set(size_t i, long udata) {
		ldms_metric_user_data_set(self, i, (uint64_t)udata);
	}
	inline const char *metric_type_as_str(size_t i) {
		return ldms_metric_type_to_str(ldms_metric_type_get(self, i));
	}
	inline size_t metric_by_name(const char *name) {
		return ldms_metric_by_name(self, name);
	}
	inline PyObject *metric_value_get(size_t i) {
		union ldms_value *v = ldms_metric_get(self, i);
		enum ldms_value_type t = ldms_metric_type_get(self, i);
		int j, n;
		PyObject *lst, *obj;
		if (!ldms_type_is_array(t)) {
			return PyObject_FromMetricValue(v, t);
		}
		n = ldms_metric_array_get_len(self, i);
		lst = PyList_New(n);
		if (!lst) {
			PyErr_SetString(PyExc_RuntimeError, "Out of memory.");
			return NULL;
		}
		for (j = 0; j < n; j++) {
			obj = ldms_rbuf_desc_array_metric_value_get(self, i, j);
			if (!obj) {
				PyErr_SetString(PyExc_RuntimeError,
						"Out of memory.");
				Py_DECREF(lst);
				return NULL;
			}
			PyList_SetItem(lst, j, obj); /* This steals obj ref */
		}
		return lst;
	}
	inline PyObject *metric_value_get_by_name(const char *name) {
		int i = ldms_metric_by_name(self, name);
		if (i < 0) {
			Py_INCREF(Py_None);
			return Py_None;
		}
		return ldms_rbuf_desc_metric_value_get(self, i);
	}
	inline PyObject *__getitem__(PyObject *key) {
		if (PyInt_Check(key)) {
			/* access by index */
			int i = PyInt_AS_LONG(key);
			return ldms_rbuf_desc_metric_value_get(self, i);
		}
		if (PyLong_Check(key)) {
			/* access by index */
			int i = PyLong_AsLong(key);
			return ldms_rbuf_desc_metric_value_get(self, i);
		}
		if (PyString_Check(key)) {
			char *s = PyString_AS_STRING(key);
			return ldms_rbuf_desc_metric_value_get_by_name(self, s);
		}
		PyErr_SetString(PyExc_TypeError, "Unsupported key type");
		return NULL;
	}
	inline PyObject *array_metric_value_get(size_t mid, size_t idx) {
                enum ldms_value_type t = ldms_metric_type_get(self, mid);
                switch (t) {
                case LDMS_V_U8_ARRAY:
                        return PyLong_FromLong(ldms_metric_array_get_u8(self, mid, idx));
                case LDMS_V_S8_ARRAY:
                        return PyLong_FromLong(ldms_metric_array_get_s8(self, mid, idx));
                case LDMS_V_U16_ARRAY:
                        return PyLong_FromLong(ldms_metric_array_get_u16(self, mid, idx));
                case LDMS_V_S16_ARRAY:
                        return PyLong_FromLong(ldms_metric_array_get_s16(self, mid, idx));
                case LDMS_V_U32_ARRAY:
                        return PyLong_FromLong(ldms_metric_array_get_u32(self, mid, idx));
                case LDMS_V_S32_ARRAY:
                        return PyLong_FromLong(ldms_metric_array_get_s32(self, mid, idx));
                case LDMS_V_U64_ARRAY:
                        return PyLong_FromLong(ldms_metric_array_get_u64(self, mid, idx));
                case LDMS_V_S64_ARRAY:
                        return PyLong_FromLong(ldms_metric_array_get_s64(self, mid, idx));
                case LDMS_V_F32_ARRAY:
                        return PyFloat_FromDouble(ldms_metric_array_get_float(self, mid, idx));
                case LDMS_V_D64_ARRAY:
                        return PyFloat_FromDouble(ldms_metric_array_get_double(self, mid, idx));
		default:
			SWIG_exception(SWIG_TypeError, "Unrecognized ldms_value type");
                }
                return PyLong_FromLong(0);
	fail:
		Py_RETURN_NONE;
	}
	inline void metric_value_set(size_t i, PyObject *o) {
		enum ldms_value_type t = ldms_metric_type_get(self, i);
		union ldms_value v;
		switch (t) {
		case LDMS_V_U8:
		case LDMS_V_U16:
		case LDMS_V_U32:
		case LDMS_V_U64:
			v.v_u64 = PyLong_AsLong(o);
			break;
		case LDMS_V_S8:
		case LDMS_V_S16:
		case LDMS_V_S32:
		case LDMS_V_S64:
			v.v_s64 = PyLong_AsLong(o);
			break;
		case LDMS_V_F32:
			v.v_f = PyFloat_AsDouble(o);
			break;
		case LDMS_V_D64:
			v.v_d = PyFloat_AsDouble(o);
			break;
		default:
			SWIG_exception(SWIG_TypeError, "Unrecognized ldms_value type");
		}
		ldms_metric_set(self, i, &v);
	fail:
		return;
	}
        inline
        void array_metric_value_set(size_t mid, size_t idx, PyObject *o) {
		enum ldms_value_type t = ldms_metric_type_get(self, mid);
		union ldms_value v;
		switch (t) {
		case LDMS_V_U8_ARRAY:
		case LDMS_V_U16_ARRAY:
		case LDMS_V_U32_ARRAY:
		case LDMS_V_U64_ARRAY:
			v.v_u64 = PyLong_AsLong(o);
			break;
		case LDMS_V_S8_ARRAY:
		case LDMS_V_S16_ARRAY:
		case LDMS_V_S32_ARRAY:
		case LDMS_V_S64_ARRAY:
			v.v_s64 = PyLong_AsLong(o);
			break;
		case LDMS_V_F32_ARRAY:
			v.v_f = PyFloat_AsDouble(o);
			break;
		case LDMS_V_D64_ARRAY:
			v.v_d = PyFloat_AsDouble(o);
			break;
		default:
			SWIG_exception(SWIG_TypeError, "Unrecognized ldms_value type");
		}
		ldms_metric_array_set_val(self, mid, idx, &v);
	fail:
		return;
        }
	inline const char *instance_name_get() {
		return ldms_set_instance_name_get(self);
	}
	inline const char *schema_name_get() {
		return ldms_set_schema_name_get(self);
	}
	inline const char *producer_name_get() {
		return ldms_set_producer_name_get(self);
	}
	inline void producer_name_set(const char *name) {
		ldms_set_producer_name_set(self, name);
	}
	inline PyObject *is_consistent() {
		if (ldms_set_is_consistent(self))
			return Py_True;
		return Py_False;
	}
	inline size_t meta_sz_get() {
		return ldms_set_meta_sz_get(self);
	}
	inline size_t data_sz_get() {
		return ldms_set_data_sz_get(self);
	}
	inline uint64_t meta_gn_get() {
		return ldms_set_meta_gn_get(self);
	}
	inline uint64_t data_gn_get() {
		return ldms_set_data_gn_get(self);
	}
	inline struct ldms_timestamp ts_get() {
		struct ldms_timestamp const _ts = ldms_transaction_timestamp_get(self);
		return _ts;
	}
	inline PyObject *timestamp_get() {
		struct ldms_timestamp const _ts = ldms_transaction_timestamp_get(self);
		struct ldms_timestamp const *ts = &_ts;
		struct tm *tm;
		char dtsz[200];
		char usecs[16];
		time_t ti = ts->sec;
		tm = localtime(&ti);
		strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S", tm);
		sprintf(usecs, ".%06d %d", ts->usec, 1900 + tm->tm_year);
		strcat(dtsz, usecs);
		return PyString_FromString(dtsz);
	}
	inline PyObject *transaction_duration_get() {
		struct ldms_timestamp const _ts = ldms_transaction_duration_get(self);
		struct ldms_timestamp const *ts = &_ts;
		char dtsz[200];
		sprintf(dtsz, "%d.%06d(s)", ts->sec, ts->usec);
		return PyString_FromString(dtsz);
	}
	inline void _update_blocking() {
		int rc;
		ldms_update_ctxt_t ctxt;
		char err_buff[128];

		ctxt = malloc(sizeof(*ctxt));
		if (!ctxt) {
			snprintf(err_buff, sizeof(err_buff),
				 "malloc() failed, errno: %d",
				 errno);
			goto err0;
		}
		rc = sem_init(&ctxt->sem, 0, 0);
		if (rc) {
			snprintf(err_buff, sizeof(err_buff),
				 "sem_init() failed, errno: %d",
				 errno);
			goto err1;
		}
		rc = ldms_xprt_update(self, __update_cb, ctxt);
		if (rc) {
			snprintf(err_buff, sizeof(err_buff),
				 "ldms_xprt_update() failed, errno: %d",
				 errno);
			goto err2;
		}
		rc = sem_wait(&ctxt->sem);
		if (rc) {
			snprintf(err_buff, sizeof(err_buff),
				 "sem_wait() failed, errno: %d",
				 errno);
			goto err2;
		}
		if (ctxt->rc) {
			snprintf(err_buff, sizeof(err_buff), "Update failed");
			goto err2;
		}
		sem_destroy(&ctxt->sem);
		free(ctxt);
		return;

	err2:
		sem_destroy(&ctxt->sem);
	err1:
		free(ctxt);
	err0:
		PyErr_SetString(PyExc_TypeError, err_buff);
	}
	inline void _update_nonblocking(PyObject *py_self, PyObject *cb,
					PyObject *py_ctxt) {
		int rc;
		struct __uctxt *ctxt;

		if (!PyCallable_Check(cb)) {
			PyErr_SetString(PyExc_TypeError,
					"callabck must be callable");
			return;
		}
		ctxt = __uctxt_new(cb, py_self, py_ctxt);
		if (!ctxt) {
			PyErr_SetString(PyExc_RuntimeError, "Out of memory.");
			return;
		}
		rc = ldms_xprt_update(self, __update_cb2, ctxt);
		if (rc) {
			char buff[64];
			snprintf(buff, sizeof(buff), "update failed, rc: %d", rc);
			PyErr_SetString(PyExc_RuntimeError, buff);
			__uctxt_free(ctxt);
		}
	}
}

%extend ldms_timestamp {
	inline int __cmp__(const struct ldms_timestamp *other) {
		if (self->sec < other->sec)
			return -1;
		if (self->sec > other->sec)
			return 1;
		if (self->usec < other->usec)
			return -1;
		if (self->usec > other->usec)
			return 1;
		return 0;
	}
	inline double __sub__(const struct ldms_timestamp *other) {
		if (ldms_timestamp___cmp__(self, other) < 0) {
			return - ldms_difftimestamp(other, self);
		} else {
			return ldms_difftimestamp(self, other);
		}
	}
	inline PyObject *__str__() {
		/* Use `buff` because PyString_FromFormat() does not support
		 * leading-zeroes formatting */
		char buff[64];
		snprintf(buff, sizeof(buff), "%d.%06d", self->sec, self->usec);
		return PyString_FromString(buff);
	}
}

%nodefaultctor ldms_xprt;
struct ldms_xprt {};

%extend ldms_xprt {
	inline int msg_max_get() {
		return ldms_xprt_msg_max(self);
	}
	inline PyObject *state_get() {
		struct xprt_arg *arg = __xprt_arg_find(self);
		if (arg->state == INIT)
			return PyString_FromString("INIT");
		else if (arg->state == CONNECTED)
			return PyString_FromString("CONNECTED");
		else if (arg->state == DISCONNECTED)
			return PyString_FromString("DISCONNECTED");
		else if (arg->state == DESTROYED)
			return PyString_FromString("DESTROYED");
		else
			PyErr_SetString(PyExc_TypeError, "Unrecognized ldms transport state");
	}
}

%pythoncode %{
def set_update(self, cb=None, ctxt=None):
	if not cb:
		return _ldms.ldms_rbuf_desc__update_blocking(self)
	return _ldms.ldms_rbuf_desc__update_nonblocking(self, self, cb, ctxt)
setattr(ldms_rbuf_desc, "update", set_update)

%}
