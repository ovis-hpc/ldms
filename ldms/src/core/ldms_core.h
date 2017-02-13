/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-2016 Sandia Corporation. All rights reserved.
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

#ifndef __LDMS_CORE_H__
#define __LDMS_CORE_H__

#include <asm/byteorder.h>

#define LDMS_SETH_F_BE		0x0001
#define LDMS_SETH_F_LE		0x0002

#ifdef	__KERNEL__
#define	__BIG_ENDIAN		4321
#define	__LITTLE_ENDIAN		1234
#ifndef	__BYTE_ORDER__
#ifdef	__LITTLE_ENDIAN_BITFIELD
#define	__BYTE_ORDER__		__LITTLE_ENDIAN
#else
#define	__BYTE_ORDER__		__BIG_ENDIAN
#endif
#endif	/* __BYTE_ORDER__ */
#endif	/* __KERNEL */

#if __BYTE_ORDER__ == __BIG_ENDIAN
#define LDMS_SETH_F_LCLBYTEORDER	LDMS_SETH_F_BE
#else
#define LDMS_SETH_F_LCLBYTEORDER	LDMS_SETH_F_LE
#endif

#define LDMS_SET_NAME_MAX 256
#define LDMS_PRODUCER_NAME_MAX 64 /* including the terminating null byte */

/**
 * \brief Metric value descriptor
 *
 * This structure describes a metric value in the metric set. Metrics
 * are self describing. Value descriptors are aligned on 64 bit boundaries.
 */
#pragma pack(4)
#define LDMS_MDESC_F_DATA	1
#define LDMS_MDESC_F_META	2
typedef struct ldms_value_desc {
	uint64_t vd_user_data;	/*! User defined meta-data */
	uint32_t vd_data_offset;/*! Value offset in data (metric) or meta-data (attr) */
	uint32_t vd_array_count;/*! Number of elements in the array */
	uint8_t vd_type;	/*! The type of the value, enum ldms_value_type */
	uint8_t vd_flags;	/*! Metric or Attribute */
	uint8_t vd_name_len;	/*! The length of the metric name in bytes*/
	char vd_name[0];	/*! The metric name */
} *ldms_mdesc_t;
#pragma pack()

enum ldms_transaction_flags {
	LDMS_TRANSACTION_NONE = 0,
	LDMS_TRANSACTION_BEGIN = 1,
	LDMS_TRANSACTION_END = 2
};

struct ldms_timestamp  {
	uint32_t sec;
	uint32_t usec;
};

struct ldms_transaction {
	struct ldms_timestamp ts;
	struct ldms_timestamp dur;
	uint32_t flags;
};

struct ldms_data_hdr {
	struct ldms_transaction trans;
	uint32_t pad;
	uint64_t gn;		/* Metric-value generation number */
	uint64_t size;		/* Max size of data */	/* FIXME: unused */
	uint64_t meta_gn;	/* Meta-data generation number */
};

/**
 * \brief LDMS Version structure
 */
struct ldms_version {
	uint8_t major;	/* major number */
	uint8_t minor;	/* minor number */
	uint8_t patch;	/* patch number */
	uint8_t flags;	/* version flags */
};

/**
 * An interface to get LDMS version.
 * \param[out] v A buffer to store LDMS version.
 */

void ldms_version_get(struct ldms_version *v);

/* 3.3.0.0 */
#define LDMS_VERSION_MAJOR	 0x03
#define LDMS_VERSION_MINOR	 0x03
#define LDMS_VERSION_PATCH	 0x00
#define LDMS_VERSION_FLAGS	 0x00
#define LDMS_VERSION_SET(version) do {				\
	(version).major = LDMS_VERSION_MAJOR;			\
	(version).minor = LDMS_VERSION_MINOR;			\
	(version).patch = LDMS_VERSION_PATCH;			\
	(version).flags = LDMS_VERSION_FLAGS;			\
} while (0)

#define LDMS_VERSION_EQUAL(version) (				\
	((version).major == LDMS_VERSION_MAJOR) &&		\
	((version).minor == LDMS_VERSION_MINOR) &&		\
	((version).patch == LDMS_VERSION_PATCH) &&		\
	((version).flags == LDMS_VERSION_FLAGS) )

struct ldms_set_hdr {
	/* The unique metric set producer name */
	char producer_name[LDMS_PRODUCER_NAME_MAX];
	uint64_t meta_gn;	/* Meta-data generation number */
	struct ldms_version version;	/* LDMS version */
	uint8_t flags;	/* Set format flags */
	uint8_t pad1;	/* data pad */
	uint8_t pad2;	/* data pad */
	uint8_t pad3;	/* data pad */
	uint32_t card;		/* Size of dictionary */
	uint32_t meta_sz;	/* size of meta data in bytes */
	uint32_t data_sz;	/* size of metric values in bytes */
	uint32_t dict[0];	/* The attr/metric dictionary */
};

/**
 * \brief Metric value union
 *
 * A generic union that encapsulates all of the LDMS value types.
 */
typedef union ldms_value {
	char v_char;
	uint8_t v_u8;
	int8_t v_s8;
	uint16_t v_u16;
	int16_t v_s16;
	uint32_t v_u32;
	int32_t v_s32;
	uint64_t v_u64;
	int64_t v_s64;
	float v_f;
	double v_d;
	char a_char[0];
	uint8_t a_u8[0];
	int8_t a_s8[0];
	uint16_t a_u16[0];
	int16_t a_s16[0];
	uint32_t a_u32[0];
	int32_t a_s32[0];
	uint64_t a_u64[0];
	int64_t a_s64[0];
	float a_f[0];
	double a_d[0];
} *ldms_mval_t;

/**
 * \brief LDMS value type enumeration
 */
enum ldms_value_type {
	LDMS_V_NONE = 0,
	LDMS_V_CHAR,
	LDMS_V_U8,
	LDMS_V_S8,
	LDMS_V_U16,
	LDMS_V_S16,
	LDMS_V_U32,
	LDMS_V_S32,
	LDMS_V_U64,
	LDMS_V_S64,
	LDMS_V_F32,
	LDMS_V_D64,
	LDMS_V_CHAR_ARRAY,
	LDMS_V_U8_ARRAY,
	LDMS_V_S8_ARRAY,
	LDMS_V_U16_ARRAY,
	LDMS_V_S16_ARRAY,
	LDMS_V_U32_ARRAY,
	LDMS_V_S32_ARRAY,
	LDMS_V_U64_ARRAY,
	LDMS_V_S64_ARRAY,
	LDMS_V_F32_ARRAY,
	LDMS_V_D64_ARRAY,
	LDMS_V_FIRST = LDMS_V_CHAR,
	LDMS_V_LAST = LDMS_V_D64_ARRAY
};

typedef struct ldms_name {
	uint8_t len;
	char name[0];
} *ldms_name_t;

#ifndef roundup
/* Convenience macro to roundup a value to a multiple of the _s parameter */
#define roundup(_v,_s) ((_v + (_s - 1)) & ~(_s - 1))
#endif

static inline ldms_name_t get_instance_name(struct ldms_set_hdr *meta)
{
	ldms_name_t name  = (ldms_name_t)(&meta->dict[__le32_to_cpu(meta->card)]);
	return name;
}

static inline ldms_name_t get_schema_name(struct ldms_set_hdr *meta)
{
	ldms_name_t inst = get_instance_name(meta);
	return (ldms_name_t)(&inst->name[inst->len+sizeof(*inst)]);
}

static inline struct ldms_value_desc *get_first_metric_desc(struct ldms_set_hdr *meta)
{
	ldms_name_t name = get_schema_name(meta);
	char *p = &name->name[name->len+sizeof(*name)];
	p = (char *)roundup((uint64_t)p, 8);
	return (struct ldms_value_desc *)p;
}


#define ldms_ptr_(_t, _p, _o) (_t *)&((char *)_p)[_o]
#define ldms_off_(_m, _p) (((char *)_p) - ((char *)_m))

#endif /* __LDMS_CORE_H__ */
