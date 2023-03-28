/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
 *
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
/*
 * auth.h
 *
 *  Created on: May 18, 2015
 */

#ifndef OVIS_AUTH_H_
#define OVIS_AUTH_H_

#include <inttypes.h>
#include "ovis_log/ovis_log.h"

#define MAX_SECRET_WORD_LEN 512 /*! The maximum length of the secret word */
#define MIN_SECRET_WORD_LEN 8  /*! The minimum length of the secret word */

struct ovis_auth_challenge {
	uint32_t lo; 		/*! The last 32 bits */
	uint32_t hi;		/*! The first 32 bits */
};

typedef void (*ovis_auth_log_fn_t)(const char *fmt, ...);

uint64_t ovis_auth_gen_challenge();

struct ovis_auth_challenge *ovis_auth_pack_challenge(uint64_t challenge,
				struct ovis_auth_challenge *chl);


uint64_t ovis_auth_unpack_challenge(struct ovis_auth_challenge *chl);

#define MAX_LINE_LEN (MAX_SECRET_WORD_LEN+16)

char *ovis_auth_get_secretword(const char *path, ovis_log_t log);

int ovis_get_rabbit_secretword(const char *file, char *buf, int buflen,
	ovis_auth_log_fn_t msglog);

char *ovis_auth_encrypt_password(const uint64_t challenge,
				const char *secretword);


#endif /* OVIS_AUTH_H_ */
