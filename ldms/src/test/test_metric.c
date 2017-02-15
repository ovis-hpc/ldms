/* Copyright (c) 2017 Sandia Corporation. All rights reserved.
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

/* A program to test set/get of uint64_t, float, double, uint32_t in
 * local data sets. It may pick up issues of endianness or union
 * layout not normally seen on x64/gcc platforms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include "ldms.h"

int main(int argc, char **argv) {

	ldms_init(1024*8);
	ldms_schema_t schema = NULL;
	ldms_set_t set = NULL;

	schema = ldms_schema_new("set_get_test");
	if (!schema)
		return ENOMEM;
	int rc = ldms_schema_metric_add(schema, "u64", LDMS_V_U64);
	if (rc < 0) {
	        rc = ENOMEM;
	        goto err;
	}
	rc = ldms_schema_metric_add(schema, "int", LDMS_V_S32);
	if (rc < 0) {
	        rc = ENOMEM;
	        goto err;
	}
	rc = ldms_schema_metric_add(schema, "double", LDMS_V_D64);
	if (rc < 0) {
	        rc = ENOMEM;
	        goto err;
	}
	rc = ldms_schema_metric_add(schema, "float", LDMS_V_F32);
	if (rc < 0) {
	        rc = ENOMEM;
	        goto err;
	}

	set = ldms_set_new("set_get_test", schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	uint64_t u64 = 0x708090a0b0c0d0e0;
	int32_t i32 =  0xA0B0C0D0;
	double d = 3.123456789012345;
	float f = 2.71234567;
	ldms_metric_set_u64(set, 0, u64);
	ldms_metric_set_s32(set, 1, i32);
	ldms_metric_set_double(set, 2, d);
	ldms_metric_set_float(set, 3, f);

	ldms_mval_t mv;
	int err = 0;
	mv = ldms_metric_get(set, 0);
	if (mv->v_u64 != u64) {
		err++;
		printf("error in uint64_t value storage in set\n");
	}
	mv = ldms_metric_get(set, 1);
	if (mv->v_s32 != i32) {
		err++;
		printf("error in int32_t value storage in set\n");
	}
	mv = ldms_metric_get(set, 2);
	double tol = mv->v_d - d;
	if (isnan(tol) || fabs(tol) > 1e-14) {
		err++;
		printf("error in double value storage in set\n");
	}
	mv = ldms_metric_get(set, 3);
	float stol = mv->v_f - f;
	if (isnan(stol) || fabsf(stol) > 1e-7) {
		err++;
		printf("error in float value storage in set\n");
	}
	rc = err;

err:
 	if (set) {
		 ldms_set_delete(set);
	}
	ldms_schema_delete(schema);
	return rc;
}

