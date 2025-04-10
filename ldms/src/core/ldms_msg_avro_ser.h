/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2024 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2024 Open Grid Computing, Inc. All rights reserved.
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
 * \file ldms_msg_avro_ser.h
 * \brief Public interfaces for Avro/Serdes support in ldms_msg.
 *
 * Link with `-lldms_stream_avro_ser` to use functions declared in this header.
 */

#ifndef __LDMS_STREAM_AVRO_SER_H__
#define __LDMS_STREAM_AVRO_SER_H__

#include "ldms.h"
#include "avro.h"
#include "libserdes/serdes.h"

/**
 * \brief Publish Avro \c value to LDMS Stream.
 *
 * \remark
 *     If you wish to just publish bytes of Avro/Serdes encoded data, simply
 *     call \c ldms_msg_publish() the encoded data with \c msg_type being
 *     \c LDMS_MSG_AVRO_SER. According to libserdes readme
 *     (https://github.com/confluentinc/libserdes?tab=readme-ov-file#configuration)
 *     the default framing for both serializer and deserializer is \c cp1
 *     (Confluent Platform framing). An alternative is currently \c none, i.e.
 *     no framing, which is useless in our application since the receiver side
 *     does not know the schema beforehand. In the future, \c libserdes may have
 *     other framing implementation to chose from and it is up to application's
 *     setup of \c serdes handle on the framing format it would like to use. The
 *     \c serdes setup of the sender side has to be compatible with the \c
 *     serdes setup of the receiver side for the receiver side to successfully
 *     decode the payload, which is out of LDMS scope.
 *
 * To publish an Avro \c value, a Serdes handle \c serdes (Schema Registry
 * connection) is required to register the schema of the given \c value.
 * The \c sch parameter is an in/out parameter to supply/obtain such schema.
 *
 * If \c sch is NOT \c NULL and the value of \c *sch is also NOT \c NULL, the
 * \c *sch Serdes Schema will be used in the data serialization; skipping the
 * schema extraction and registration (saving time). If \c sch is NOT \c NULL
 * but \c *sch IS \c NULL, a Serdes Schema extraction and registration is
 * performed and the \c *sch is set to the obtained serdes schema so that the
 * application can save it for later use.
 *
 * If \c sch IS \c NULL, a Serdes Schema extraction and registration is still
 * performed but won't be returned to the application.
 *
 * If \c x is not \c NULL, LDMS publishes the \c value to the peer over stream
 * named \c stream_name. Otherwise, this will be local publishing where all
 * matching clients (local and remote) will receive the data.
 *
 * If \c cred is \c NULL (recommended), the process UID/GID is used. Otherwise,
 * the given \c cred is used if the publisher is \c root. If the publisher is
 * not \c root, this will result in an error.
 *
 * \param x The LDMS transport.
 * \param value The Avro value object.
 * \param serdes The Schema Registry handle (required).
 * \param[in,out] sch The serdes schema.
 *
 * \retval 0 If success.
 * \retval ENOPROTOOPT If \c serdes "serializer.framing" is disabled.
 * \retval EIO For other \c serdes -related errors.
 * \retval errno Other errors.
 */
int ldms_msg_publish_avro_ser(ldms_t x, const char *stream_name,
				 ldms_cred_t cred, uint32_t perm,
				 avro_value_t *value, serdes_t *serdes,
				 struct serdes_schema_s **sch);

/**
 * \brief Stream event for Avro/Serdes steam client.
 */
typedef struct ldms_msg_avro_ser_event_s {
	struct ldms_msg_event_s ev; /**< The base event. */
	avro_value_t *avro_value;      /**< The avro value object. */
	serdes_schema_t *serdes_schema; /**< The associated schema. */
} *ldms_msg_avro_ser_event_t;


/**
 * \brief Callback function for Avro/Serdes stream client.
 *
 * The callback function is called when there is an event on the stream. The
 * \c ev is owned by LDMS library and will be invalidated after the application
 * callback function is returned. See \c ldms_msg(7) manual (or
 * \c docs/ldms_msg.rst) for more information and example about stream and
 * callbacks.
 *
 * \c ev->avro_value is the Avro value object decoded from the stream data. This
 * is an independent object from the \c ev, but LDMS library will drop
 * \c ev->avro_value after the callback function returned. If the application
 * wish to keep the Avro object around, it must take a reference by calling
 * \c avro_value_incref() (and later drop the reference with
 * \c avro_value_decref() when it is done with the value).
 *
 * \c ev->serdes_schema is the associated schema from the Schema Registry. The
 * application can free the schema with \c serdes_schema_destroy() when it is
 * done with the schema. The \c ev->serdes_schema will stay in the cache managed
 * by \c libserdes if it is not freed, and will be reused when we encounter the
 * same schema.
 *
 * \param ev The callback event.
 */
typedef int (*ldms_msg_avro_ser_event_cb_t)(ldms_msg_avro_ser_event_t ev, void *cb_arg);

/**
 * \brief Subscribe to a stream with Avro/Serdes decoding.
 *
 * This is essentially the same as \c ldms_msg_subscribe(), but with
 * \c serdes handle to decode data encoded by Avro/Serdes (see also:
 * \c ldms_msg_publish_avro_ser()).
 *
 * \param stream   The stream name or regular expression.
 * \param is_regex 1 if `stream` is a regular expression. Otherwise, 0.
 * \param cb_fn    The callback function for stream data delivery.
 * \param cb_arg   The application context to the `cb_fn`.
 * \param desc     An optional short description of the client of this subscription.
 *                 This could be useful for client stats.
 * \param serdes   The \c serdes handle.
 *
 * \retval NULL  If there is an error. In this case `errno` is set to describe
 *               the error.
 * \retval ptr   The stream client handle.
 */
ldms_msg_client_t
ldms_msg_subscribe_avro_ser(const char *stream, int is_regex,
		      ldms_msg_avro_ser_event_cb_t cb_fn, void *cb_arg,
		      const char *desc, serdes_t *serdes);
#endif
