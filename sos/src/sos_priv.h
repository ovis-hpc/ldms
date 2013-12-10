/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
 * Author: Tom Tucker tom at ogc dot us
 */

#ifndef __SOS_PRIV_H
#define __SOS_PRIV_H

#include <stdint.h>
#include <sys/queue.h>
#include <endian.h>

#ifndef htobe32
#include <byteswap.h>
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define htobe16(x) __bswap_16 (x)
#  define htole16(x) (x)
#  define be16toh(x) __bswap_16 (x)
#  define le16toh(x) (x)

#  define htobe32(x) __bswap_32 (x)
#  define htole32(x) (x)
#  define be32toh(x) __bswap_32 (x)
#  define le32toh(x) (x)

#  define htobe64(x) __bswap_64 (x)
#  define htole64(x) (x)
#  define be64toh(x) __bswap_64 (x)
#  define le64toh(x) (x)
# else
#  define htobe16(x) (x)
#  define htole16(x) __bswap_16 (x)
#  define be16toh(x) (x)
#  define le16toh(x) __bswap_16 (x)

#  define htobe32(x) (x)
#  define htole32(x) __bswap_32 (x)
#  define be32toh(x) (x)
#  define le32toh(x) __bswap_32 (x)

#  define htobe64(x) (x)
#  define htole64(x) __bswap_64 (x)
#  define be64toh(x) (x)
#  define le64toh(x) __bswap_64 (x)
# endif
#endif

#include "sos.h"
#include "oidx.h"
#include <coll/idx.h>

/*
 * An object is just a blob of bytes. It is opaque without the
 * associated class definition
 */
struct sos_obj_s {
	unsigned char data[0];
};

typedef struct sos_blob_obj_s* sos_blob_obj_t;

/**
 * \brief SOS Blob data type.
 * This data structure is stored in SOS Object ODS as a value of blob attribute.
 */
struct sos_blob_obj_s {
	uint64_t len; /**< Length of the blob. */
	uint64_t off; /**< Reference of the blob object in blob ODS. */
};

/* Maintains first and last entries in the store */
/* Deprecated.
typedef struct sos_idx_entry_s {
	uint64_t first;
	uint64_t last;
} *sos_idx_entry_t;
*/

struct sos_oidx_s {
	oidx_t oidx;
};

/**
 * \brief Linked list of references to objects of same key.
 */
typedef struct sos_link_s {
	uint64_t prev; /**< offset to previous link entry (in index ODS). */
	uint64_t obj_offset; /**< offset to the object (in object ODS). */
	uint64_t next; /**< offset to next link entry (in index ODS). */
} *sos_link_t;

typedef struct sos_dattr_s *sos_dattr_t;
struct sos_dattr_s {
	char name[SOS_ATTR_NAME_LEN];
	uint32_t type;		/* enum sos_type_e */
	uint32_t data;		/* Offset into object of the attribute's data */
	uint32_t link;		/* Offset into object of the attribute's link */
				/* data. Not used if !has_idx */
	uint32_t has_idx;	/* !0 if index is to be maintained */
};

#define SOS_SIGNATURE "SOS_OBJ_STORE"
#define SOS_OBJ_BE	1
#define SOS_OBJ_LE	2
typedef struct sos_meta_s *sos_meta_t;
struct sos_meta_s {
	char signature[16];
	char classname[SOS_CLASS_NAME_LEN];	/* name of object class */
	uint32_t byte_order;
	uint32_t ods_extend_sz;
	uint32_t obj_sz;	/* size of object */
	uint32_t attr_cnt;	/* attributes in object class */
	struct sos_dattr_s attrs[0];
};

struct sos_s {
	/* "Path" to the file. This is used as a prefix for all the
	 *  real file paths */
	const char *path;

	/* ODS containing all index objects, i.e the objects pointed
	 * to by the indices. */
	ods_t ods;

#define SOS_ODS_EXTEND_SZ (16*1024*1024);
	/* The meta-data to associate with the object store */
	sos_meta_t meta;
	/* The size of the meta-data.  */
	size_t meta_sz;

	/* In-memory object class descrioption */
	sos_class_t classp;
};

struct sos_iter_s {
	sos_t sos;
	sos_attr_t attr;
	oidx_iter_t iter;
	uint64_t start;
	uint64_t end;
	uint64_t next;
	uint64_t prev;
};

#define SOS_ATTR_GET_BE32(_v, _a, _o) \
{ \
	*(uint32_t *)(_v) = htobe32(*(uint32_t *)sos_attr_get(_a, _o));	\
}

#define SOS_ATTR_GET_BE64(_v, _a, _o) \
{ \
	*(uint64_t *)(_v) = htobe64(*(uint64_t *)sos_attr_get(_a, _o));	\
}

#define SOS_KEY_SET_BE32(_k, _v) \
{ \
	*(uint32_t *)(_k->key) = htobe32(*(uint32_t *)_v);	\
}

#define SOS_KEY_SET_BE64(_k, _v) \
{ \
	*(uint64_t *)(_k->key) = htobe64(*(uint64_t *)_v); \
}

#endif

