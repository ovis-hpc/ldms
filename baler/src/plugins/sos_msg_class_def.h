/* -*- c-basic-offset: 8 -*-
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
/**
 * \file sos_msg_class_def.h
 * \brief SOS definition for Baler messages.
 *
 * This header file contains the definition of SOS Object definition for
 * Baler message, encapsulated in SOS_TYPE_BLOB.
 */
#ifndef __SOS_MSG_CLASS_DEF_H
#define __SOS_MSG_CLASS_DEF_H

/**
 * Get key from the message.
 */
static
void sos_msg_get_key(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	struct bmsg *b = SOS_TYPE_BLOB__get_fn(attr, obj);
	*(typeof(&b->ptn_id))key->key = b->ptn_id;
	key->keylen = sizeof(b->ptn_id);
}

/**
 * Set \c key corresponding to the given \c value.
 */
static
void sos_msg_set_key(sos_attr_t attr, void *value, sos_key_t key)
{
	/*
	 * According to sos code, this function seems unnecessary.
	 */
	struct bmsg *b = value;
	key->keylen = sizeof(b->ptn_id);
	*(typeof(&b->ptn_id))key->key = b->ptn_id;
}

typedef enum {
	SOS_MSG_SEC=0,
	SOS_MSG_USEC,
	SOS_MSG_COMP_ID,
	SOS_MSG_MSG,
} sos_msg_class_attr_id_t;

/**
 * SOS Object definition for messages.
 *
 * sec - second
 * usec - microsecond
 * comp_id - component id
 * msg - the message (::bmsg)
 */
static
SOS_OBJ_BEGIN(sos_msg_class, "BalerSOSMessageClass")
	SOS_OBJ_ATTR_WITH_KEY("sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("comp_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_UKEY("msg", SOS_TYPE_BLOB,
			       sos_msg_get_key,
			       sos_msg_set_key),
SOS_OBJ_END(4);

#endif
