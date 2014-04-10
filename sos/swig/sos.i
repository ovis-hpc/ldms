/*
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
/* File: sos.i */
%module sos
%include "cpointer.i"
%{
#define SWIG_FILE_WITH_INIT
#include "sos.h"
#include "obj_idx.h"

#define OBJ_FIXED_KEY_SET_DEF(T) \
void obj_key_set_ ## T (obj_key_t key, T v) \
{ \
	obj_key_set(key, &v, sizeof(v)); \
}

OBJ_FIXED_KEY_SET_DEF(int32_t)
OBJ_FIXED_KEY_SET_DEF(int64_t)
OBJ_FIXED_KEY_SET_DEF(uint32_t)
OBJ_FIXED_KEY_SET_DEF(uint64_t)
OBJ_FIXED_KEY_SET_DEF(float)
OBJ_FIXED_KEY_SET_DEF(double)

void obj_key_set_string(obj_key_t key, const char *str, size_t sz)
{
	obj_key_set(key, (void*)str, sz);
}

%}

%include "sos.h"
%include "obj_idx.h"

void obj_key_set_int32_t (obj_key_t key, int32_t v);
void obj_key_set_int64_t (obj_key_t key, int64_t v);
void obj_key_set_uint32_t (obj_key_t key, uint32_t v);
void obj_key_set_uint64_t (obj_key_t key, uint64_t v);
void obj_key_set_float (obj_key_t key, float v);
void obj_key_set_double (obj_key_t key, double v);
void obj_key_set_string(obj_key_t key, const char *str, size_t sz);

/* These typedef will make swig knows standard integers */
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
