/*
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
%{
#include <stdio.h>
#include <sys/queue.h>
#include <semaphore.h>
#include "ldms.h"

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
	ldms_set_t set;
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
	return Py_None;
}

PyObject *PyObject_FromMetricValue(ldms_mval_t mv, enum ldms_value_type type)
{
	int int_n_float = 1;
	long l = 0;
	double d = 0.0;
	switch (type) {
	case LDMS_V_NONE:
		/* TODO Exception */
		break;
	case LDMS_V_U8:
	case LDMS_V_S8:
		l = (long)mv->v_u8;
		break;
	case LDMS_V_U16:
	case LDMS_V_S16:
		l = (long)mv->v_u16;
		break;
	case LDMS_V_U32:
	case LDMS_V_S32:
		l = (long)mv->v_u32;
		break;
	case LDMS_V_U64:
	case LDMS_V_S64:
		l = (long)mv->v_u64;
		break;
	case LDMS_V_F32:
		int_n_float = 0;
		d = (double)mv->v_f;
		break;
	case LDMS_V_D64:
		int_n_float = 0;
		d = mv->v_d;
		break;
	}
	if (int_n_float)
		return PyLong_FromLong(l);
	return PyFloat_FromDouble(d);
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

ldms_set_t LDMS_xprt_lookup(ldms_t x, const char *name, enum ldms_lookup_flags flags);
PyObject *LDMS_xprt_dir(ldms_t x);

%extend ldms_value {
	inline PyObject *value(enum ldms_value_type type) {
		return PyObject_FromMetricValue(self, type);
	}
}

%extend ldms_set_desc {
	inline size_t __len__() { return ldms_set_card_get(self); }
	inline PyObject *metric_name_get(size_t i) {
		return PyString_FromString(ldms_metric_name_get(self, i));
	}
	inline enum ldms_value_type metric_type_get(size_t i) {
		return ldms_metric_type_get(self, i);
	}
	inline enum ldms_value_type metric_user_data_get(size_t i) {
		return ldms_metric_type_get(self, i);
	}
	inline const char *metric_type_as_str(size_t i) {
		return ldms_metric_type_to_str(ldms_metric_type_get(self, i));
	}
	inline size_t metric_by_name(const char *name) {
		return ldms_metric_by_name(self, name);
	}
	inline PyObject *metric_value_get(size_t i) {
		union ldms_value *v = ldms_metric_get(self, i);
		return PyObject_FromMetricValue(v, ldms_metric_type_get(self, i));
	}
	inline void metric_value_set(size_t i, ldms_mval_t v) {
		ldms_metric_set(self, i, v);
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
	inline PyObject *timestamp_get() {
		struct ldms_timestamp const *ts = ldms_transaction_timestamp_get(self);
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
		struct ldms_timestamp const *ts = ldms_transaction_duration_get(self);
		char dtsz[200];
		sprintf(dtsz, "%d.%06d(s)", ts->sec, ts->usec);
		return PyString_FromString(dtsz);
	}
}

%pythoncode %{
%}
