/**
 * Copyright (c) 2015-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
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

#ifndef ovis_util_label_set_h_seen
#define ovis_util_label_set_h_seen

/**
The purpose of this library is to provide unique language-compatible
equivalents for LDMS metric labels that come from human-readable sources
and are not intended for use as programming language identifiers.

The general problem is that metric labels can be morphed either:
a) reliably (in context, uniquely) but often unreadably to any
given maximum length, or
b) heuristically, still in moderately readable form, but requiring
unlimited length, or
c) heuristically to a maximum length, at risk of readability loss, or
d) by humans.
After such mappings, the original label must be tracked and presented
to non-programmer humans anywhere needed to maintain data understanding.
LDMS metric metadata is the place for that.

Known language issues:
C99 defines at least 31 characters in an external identifier as significant.
Python uses . as object notation.
amqp uses . as topic delimiters.
R allows _ and . as id characters, but not initial characters.
Perl in some versions has a length limit of about 252.
HTML and shells interpret # and / and :.
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/** Opaque container of labels and identifiers.  Single-thread use only.
 */
struct ovis_label_set;

struct ovis_name {
	const char *name;
	uint64_t hash;
	size_t len;
};

#define OVIS_NAME_NULL { NULL, 0, 0 }

struct ovis_label_id {
	struct ovis_name label;
	struct ovis_name id;
	bool own_label_name;
	bool own_id_name;
};

/* the enum values must be consecutive from 1. */
enum id_lang {
	il_least = 1,	/**< least common denominator [A-z0-9_] */
	il_python,	/**< python */
	il_url,		/**< legal url name. */
	il_r,		/**< r language */
	il_c,		/**< c */
	il_amqp,	/**< amqp */
	il_file,	/**< unix filename without escapes for any shell. */
	il_last,	/**< end of enum. */
};

struct ovis_label_set_iterator;

/** Compute the ovis_name from s.
No dynamic allocation is performed.
 */
extern struct ovis_name ovis_name_from_string(const char *s);

/** Init the ovis_name from s and len where known.
No dynamic allocation is performed.
 */
extern struct ovis_name ovis_name_from_string2(const char *s, size_t len);

/**
 * \brief Create an empty label translation set.
 * \param id_lang the transformation target language.
 * \param max_id_len the identifier length limit, or 0 if unlimited.
 * \return object.
 */
extern struct ovis_label_set *ovis_label_set_create(enum id_lang, uint16_t max_id_length);

/**
 * \brief Destroy the set.
 */
extern void ovis_label_set_destroy(struct ovis_label_set * set);

/**
 * \brief Get number of labels in set.
 */
extern size_t ovis_label_set_size(struct ovis_label_set * set);

/** Bits to compose deep argument of ovis_label_set_insert_pair.
	PL_COPY and PL_XFER are mutually exclusive.
	PI_COPY and PI_XFER are mutually exclusive.
	PB_REF and any other bits are mutually exclusive.
 */
#define PB_REF 0x0 /**< Both label and id are caller guaranteed for set lifetime */
#define PL_COPY 0x1 /**< duplicate string in label argument */
#define PL_XFER 0x2 /**< transfer ownership of label string to set */
#define PI_COPY 0x10 /**< duplicate string in id argument */
#define PI_XFER 0x20 /**< transfer ownership of id string to set */
/**
 * \brief Add a label/id pair if you mangle manually.
 *
 * Uniqueness and length are not checked.
 * Caller is responsible for ensuring the ovis_names given (string pointers)
 * remain valid during life of the set and are cleaned up at the end
 * in a manner consistent with the value of deep given.
 * \param s uniqueness group.
 * \param label human string.
 * \param id program identifier for label.
 * \param deep composed of one or two bits from the PB/PL/PI_ flags above.
 * \return stored ovis_name info for id.
 */
extern struct ovis_name ovis_label_set_insert_pair(struct ovis_label_set * s, const struct ovis_name label, const struct ovis_name id, int deep);

/**
 * \brief Add a label, transforming as needed.
 *
 * Caller is responsible for ensuring the ovis_name given (string pointer)
 * remains valid during life of the set.
 *
 * \return ovis_name from derived id, or OVIS_NAME_NULL and set errno.
 */
extern struct ovis_name ovis_label_set_insert(struct ovis_label_set * s, const struct ovis_name label);

/**
 * \brief Add a label henceforth owned by set, transforming as needed.
 *
 * Set becomes responsible for ensuring the ovis_name given's string pointer
 * is freed at end of set life.
 *
 * \return ovis_name from derived id, or OVIS_NAME_NULL and set errno.
 */
extern struct ovis_name ovis_label_set_own(struct ovis_label_set * s, const struct ovis_name label);

/**
 * \brief Find the label for an id.
 * \return pair, if id present, or OVIS_NAME_NULL
 */
extern const struct ovis_name ovis_label_set_get_label(struct ovis_label_set * is, const struct ovis_name id);

/**
 * \brief Find the id for a label.
 * \return pair, if label present, or OVIS_NAME_NULL.
 */
extern const struct ovis_name ovis_label_set_get_id(struct ovis_label_set * is, const struct ovis_name label);

/**
 * \brief Get iterator.
 * Iteration stops if set content is changed.
 * \return Argument for ovis_label_set_next.
 */
extern struct ovis_label_set_iterator *ovis_label_set_iterator_get(struct ovis_label_set * is);

/**
 * \brief Get next pair from iterator.
 * Iteration stops if set content is changed.
 * \return NULL if done or changed, else next pair.
 */
extern const struct ovis_label_id ovis_label_set_next(struct ovis_label_set * is, struct ovis_label_set_iterator *iter);


#endif /* ovis_util_label_set_h_seen */
