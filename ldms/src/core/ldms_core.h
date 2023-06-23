/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2019 Open Grid Computing, Inc. All rights reserved.
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

#ifndef __KERNEL__
#include <stdint.h>
#include "ovis-ldms-config.h"
#else
#define OVIS_FLEX	0
#define OVIS_FLEX_UNION	0
#endif

#include <asm/byteorder.h>
#include "ldms_heap.h"

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

#pragma pack(4)
#define LDMS_MDESC_F_DATA	1
#define LDMS_MDESC_F_META	2
#define LDMS_MDESC_F_RECORD	4
/**
 * \brief Metric value descriptor
 *
 * This structure describes a metric value in the metric set. Metrics
 * are self describing. Value descriptors are aligned on 64 bit boundaries.
 */
typedef struct ldms_value_desc {
	uint64_t vd_user_data;	/*! User defined meta-data */
	uint32_t vd_data_offset;/*! Value offset in data (metric) or meta-data (attr) or record */
	uint32_t vd_array_count;/*! Number of elements in the array */
	uint32_t vd_reserved[2];
	uint8_t vd_type;	/*! The type of the value, enum ldms_value_type */
	uint8_t vd_flags;	/*! Metric or Attribute */
	uint8_t vd_name_unit_len; 	/*! The length of the vd_name_unit string */
	char vd_name_unit[OVIS_FLEX];	/*! The format is \<name\>\\0 or
					 *  \<name\>\\0\<unit\>\\0 if unit was specified.
					 */
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
	uint32_t set_off;	/* Offset from the beginning of the set */
	uint64_t gn;		/* Metric-value generation number */
	uint64_t size;		/* Size of data + the heap */
	uint64_t meta_gn;	/* Meta-data generation number */
	uint32_t curr_idx;      /* Current set array index */
	struct ldms_heap heap;
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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * An interface to get LDMS version.
 * \param[out] v A buffer to store LDMS version.
 */

void ldms_version_get(struct ldms_version *v);

#ifdef __cplusplus
}
#endif

/*
 * LDMS network protocol version
 *
 * The LDMS transport operations between LDMS applications with different
 * major and/or minor version numbers may not be compatible.
 */
#define LDMS_VERSION_MAJOR	 0x04
#define LDMS_VERSION_MINOR	 0x02
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

#define LDMS_SET_HDR_F_LIST	1
struct ldms_set_hdr {
	/* The unique metric set producer name */
	char producer_name[LDMS_PRODUCER_NAME_MAX];
	uint64_t meta_gn;	/* Meta-data generation number */
	struct ldms_version version;	/* LDMS version */
	uint8_t flags;		/* Set format flags */
	uint8_t pad1;		/* data pad */
	uint8_t pad2;		/* data pad */
	uint8_t pad3;		/* data pad */
	uint32_t card;		/* Size of dictionary */
	uint32_t meta_sz;	/* size of meta data in bytes */
	uint32_t data_sz;	/* size of metric values in bytes */
	uint32_t uid;           /* UID */
	uint32_t gid;           /* GID */
	uint32_t perm;          /* permission */
	uint32_t array_card;    /* number of sets in the set array */
	uint32_t heap_sz;	/* size of the heap */
	uint32_t reserved[7];	/* area reserved for compatible core updates */
	uint32_t dict[OVIS_FLEX];/* The attr/metric dictionary */
};

#define LDMS_LIST_HEAP	512	/* Per list heap increment */
typedef struct ldms_list {
	uint32_t head;		/* Offset of first entry in list */
	uint32_t tail;		/* Offset of last entry in list */
	uint32_t count;		/* Entry count */
} *ldms_list_t;

typedef struct ldms_list_entry {
	uint32_t next;		/* Offset of next entry */
	uint32_t prev;		/* Offset of previous entry */
	uint32_t type:8;	/* Type of element */
	uint32_t count:24;	/* Count of elements if type is array */
	uint8_t value[0];
} *ldms_list_entry_t;


/**
 * \brief LDMS value type enumeration
 * Note: the numeric values must be < 255, as enum ldms_value_type fitting
 * into a byte is assumed in some transmission protocols.
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
	LDMS_V_LIST,
	LDMS_V_LIST_ENTRY,
	LDMS_V_RECORD_TYPE,
	LDMS_V_RECORD_INST,
	LDMS_V_RECORD_ARRAY,
	LDMS_V_TIMESTAMP,
	LDMS_V_FIRST = LDMS_V_CHAR,
	LDMS_V_LAST = LDMS_V_TIMESTAMP
};

#define LDMS_RECORD_F_TYPE 1
#define LDMS_RECORD_F_INST 2
typedef struct ldms_record_hdr {
	int flags;
	int pad;
} *ldms_record_hdr_t;

/* This structure is the value of LDMS_V_RECORD_TYPE. It describes members of the
 * records (name, unit, type, and array length).
 *
 * FORMAT:
 * ```
 * ---------
 *  n
 *  sz
 * ---------
 *  dict[0] (refers to mdesc[0])
 *  dict[1]
 *  ...
 *  dict[N-1]
 * ---------
 *  mdesc[0] // mdesc or ldms_value_desc has variable-length //
 *  mdesc[1]
 *  ...
 *  mdesc[N-1]
 * ---------
 * ```
 */
typedef struct ldms_record_type {
	struct ldms_record_hdr hdr;
	int n;			/* number of members */
	int inst_sz;		/* the size of the record instance */
	int dict[OVIS_FLEX];	/* dict[i] is an offset to mdesc[i] */
} *ldms_record_type_t;

typedef struct ldms_record_inst {
	struct ldms_record_hdr hdr;
	uint32_t set_data_off;	/* offset from data section */
	uint32_t rec_type;	/* index or record type */
	char rec_data[OVIS_FLEX_UNION]; /* data of the record */
} *ldms_record_inst_t;

typedef struct ldms_record_array {
	int inst_sz;
	int rec_type;		/* reference to rec_type */
	int array_len;
	char data[OVIS_FLEX];
} *ldms_record_array_t;

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
	struct ldms_timestamp v_ts;
	struct ldms_list v_lh;
	struct ldms_list_entry v_le;
	struct ldms_record_inst v_rec_inst;
	struct ldms_record_type v_rec_type;
	struct ldms_record_array v_rec_array;
	char a_char[OVIS_FLEX_UNION];
	uint8_t a_u8[OVIS_FLEX_UNION];
	int8_t a_s8[OVIS_FLEX_UNION];
	uint16_t a_u16[OVIS_FLEX_UNION];
	int16_t a_s16[OVIS_FLEX_UNION];
	uint32_t a_u32[OVIS_FLEX_UNION];
	int32_t a_s32[OVIS_FLEX_UNION];
	uint64_t a_u64[OVIS_FLEX_UNION];
	int64_t a_s64[OVIS_FLEX_UNION];
	float a_f[OVIS_FLEX_UNION];
	double a_d[OVIS_FLEX_UNION];
	struct ldms_timestamp a_ts[OVIS_FLEX_UNION];
} *ldms_mval_t;

typedef struct ldms_name {
	uint8_t len;
	char name[OVIS_FLEX];
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
	return (ldms_name_t)(&inst->name[inst->len]);
}

static inline struct ldms_value_desc *get_first_metric_desc(struct ldms_set_hdr *meta)
{
	ldms_name_t name = get_schema_name(meta);
	char *p = &name->name[name->len];
	p = (char *)roundup((uint64_t)p, 8);
	return (struct ldms_value_desc *)p;
}

/** \brief convert string to scalar member of mval as directed by vt.
 */
int ldms_mval_parse_scalar(ldms_mval_t v, enum ldms_value_type vt, const char *str);

#define ldms_ptr_(_t, _p, _o) (_t *)&((char *)_p)[_o]
#define ldms_off_(_m, _p) (((char *)_p) - ((char *)_m))

#endif /* __LDMS_CORE_H__ */
