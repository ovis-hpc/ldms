/**
 * Copyright (c) 2015,2017 National Technology & Engineering Solutions
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
#include "label-set.h"
#include "coll/ovis-map.h"
#include <ctype.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include "ovis_util/dstring.h"
#ifdef USE_B64
#include "third/cencode.h"
#else
#include "third/b62encode.h"
#endif

static struct ovis_name ovis_name_null = OVIS_NAME_NULL;

struct ovis_name ovis_name_from_string(const char *s)
{
	struct ovis_name n;
	if (s && (n.len = strlen(s)) != 0) {
		n.name = s;
		n.hash = ovis_map_keyhash(s,n.len);
		return n;
	}
	return ovis_name_null;
}

struct ovis_name ovis_name_from_string2(const char *s, size_t len)
{
	struct ovis_name n;
	if (s && len) {
		n.name = s;
		n.len = len;
		n.hash = ovis_map_keyhash(s,n.len);
		return n;
	}
	return ovis_name_null;
}

/* ascii_munge is intended to give a relatively unsurprising
substitution for all special characters, some of which take two.
*/
static char *ascii_munge[] = {
/* control chars; don't expect them in labels, but who knows. */
"0", "A", "B", "C", "D", "E", "F", "G",
"H", "I", "J", "K", "L", "M", "N", "O",
"P", "Q", "R", "S", "T", "U", "V", "W",
"x", "Y", "Z", "E", "F", "G", "R", "U",
/* blank ! " # $ % & ' */
"B", "1", "q", "H", "S", "P", "J", "f",
/* ( ) * + , - . / */
"PL", "RP", "T", "P", "j", "M", "d", "D",
/* numbers */
"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
/* : ; < = > ? @ */
"c", "s", "AL", "E", "RA", "Q", "A",
/* uppercase */
"A", "B", "C", "D", "E", "F", "G", "H",
"I", "J", "K", "L", "M", "N", "O", "P",
"Q", "R", "S", "T", "U", "V", "W", "X",
"Y", "Z",
/* [ \ ] ^ _ ` */
"SL", "B", "RS", "C", "_", "b",
/* lowercase */
"a", "b", "c", "d", "e", "f", "g", "h",
"i", "j", "k", "l", "m", "n", "o", "p",
"q", "r", "s", "t", "u", "v", "w", "x",
"y", "z",
/* { | } ~ del */
"CL", "p", "RC", "t", "X"
};

static uint8_t
ascii_munge_len[] = {
/* control */
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* blank etc */
1, 1, 1, 1, 1, 1, 1, 1,
/* ( etc */
2, 2, 1, 1, 1, 1, 1, 1,
/* numbers */
1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* : etc */
1, 1, 2, 1, 2, 1, 1,
/* upper */
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* [ etc */
2, 1, 2, 1, 1, 1,
/* lower */
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* { etc */
2, 1, 2, 1, 1
};

#define HASH64_LEN 12 /* excludes null terminator */
/** Convert 8 byte sum to a string.
\param value hash of string to be formatted.
\param result: space for output of size HASH64_LEN.
 */
static void hash64_to_string(uint64_t value, char *result)
{
	if (!result) {
		return;
	}

        char* c = result;
#ifdef USE_B64
        int cnt = 0;
        base64_encodestate s;
        base64_init_encodestate(&s);
        cnt = base64_encode_block((char *)&value, 6, c, &s);
        c += cnt;
        cnt = base64_encode_blockend(c, &s); /* we know this does nothing */
        c += cnt;
#else
	c += b62_encode(result, (unsigned char*)&value, sizeof(value));
#endif
        *c = '\0';
}

#define ABBR_NO_LEAD_UBAR 0x1

struct ovis_label_set {
	enum id_lang lang;
	int maxlen;
	struct ovis_map *m;
};


static
void shorten_dstring(dstring_t *dsp, size_t maxlen, uint64_t hash)
{
	size_t idlen = dstrlen(dsp);
	if (idlen <= maxlen)
		return;
	if (maxlen < HASH64_LEN)
		return;
	const char *pos = dstrval(dsp) + (maxlen - HASH64_LEN);
	hash64_to_string(hash, (char *)pos);
	dstr_trunc(dsp, (int)maxlen);
}

#define ABBR_NO_LEAD_UBAR 0x1 /**< suppress leading _ */
#define ABBR_PCT 0x10 /**< change hex separator from _ to % */

/* smash anything other than _ and alnum to _XX encoding. */
static void ovis_label_to_id_hex(const char *s, int abbr, dstring_t *dsp)
{
	if (!s || !dsp) {
		return;
	}
	int pos = 0;
	int first = 0;

	char sep = '_';
	if (ABBR_PCT & abbr)
		sep = '%';

	if (ABBR_NO_LEAD_UBAR & abbr) {
		/* disallow leading _ */
		first = 1;
	}
	while (s[pos] != '\0') {
		while (s[pos] == '_' || isalnum(s[pos])) {
			++pos;
		}
		if (pos) {
			dstrcat(dsp, s, pos);
			first = 0;
		}
		while (isspace(s[pos])) {
			dstrcat(dsp,"_",1);
			++pos;
			first = 0;
		}
		while ( s[pos] != '\0' &&
			(iscntrl(s[pos]) || ispunct(s[pos]))) {
			char buf[4];
			if (!first) {
				snprintf(buf,sizeof(buf),"%c%X",sep,s[pos]);
			} else {
				snprintf(buf,sizeof(buf),"%X",s[pos]);
			}
			dstrcat(dsp,buf,DSTRING_ALL);
			first = 0;
			++pos;
		}
		s += pos;
		pos = 0;
	}
}


/* Map to amqp. only . is illegal for channel parsing.  Replace all . with o. */
static void ovis_label_to_id_amqp(const char *s, int abbr, dstring_t *dsp)
{
	if (!s || !dsp) {
		return;
	}
	int pos = 0;
	while (s[pos] != '\0') {
		if (s[pos] == '.') {
			dstrcat(dsp, "o", 1);
			pos++;
			s += pos;
			pos = 0;
		}
		while (s[pos] != '.' && s[pos] != '\0') {
			pos++;
		}
		dstrcat(dsp, s, pos);
		s += pos;
		pos = 0;
	}
}

/* Map ascii to portable in a semi-intuitive way.
 UTF8 is ignored (bit 8 squashed).
*/
static void ovis_label_to_id_alnum(const char *s, int abbr, dstring_t *dsp)
{
	if (!s || !dsp) {
		return;
	}
	int first = 0;
	if (ABBR_NO_LEAD_UBAR & abbr) {
		/* disallow leading _ */
		first = 1;
	}
	int pos = 0;
	while (s[pos] != '\0') {
		uint8_t index = s[pos] & 0x7F;
		if (!first) {
			dstrcat(dsp, ascii_munge[index],
				ascii_munge_len[index]);
		} else {
			if (s[pos] != '_') {
				dstrcat(dsp, ascii_munge[index],
					ascii_munge_len[index]);
			} else {
				dstrcat(dsp, ascii_munge['u'],
					ascii_munge_len['u']);
			}
			first = 0;
		}
		pos++;
	}
}

static void ovis_label_to_id_ubar(const char *s, int abbr, dstring_t *dsp)
{
	if (!s || !dsp) {
		return;
	}
	int first = 0;
	if (ABBR_NO_LEAD_UBAR & abbr) {
		/* disallow leading _ */
		first = 1;
	}
	int pos = 0;
	if (first && s[pos] != '\0' && ( s[pos] == '_' ||
		iscntrl(s[pos]) || ispunct(s[pos]) || isspace(s[pos]) ) ) {
		dstrcat(dsp,"u",1);
		++pos;
		first = 0;
	}
	s += pos;
	pos = 0;
	while (s[pos] != '\0') {
		while (s[pos] == '_' || isalnum(s[pos])) {
			++pos;
		}
		if (pos) {
			dstrcat(dsp, s, pos);
		}
		while ( s[pos] != '\0' &&
			(iscntrl(s[pos]) ||
			ispunct(s[pos]) ||
			isspace(s[pos]))
			) {
			dstrcat(dsp,"_",1);
			++pos;
		}
		s += pos;
		pos = 0;
	}
}

/* err = fun(str, abbr, dsp); */
typedef void (*xfun)(const char *, int, dstring_t *dsp);

struct checkdata {
	struct ovis_name id;
	struct ovis_map_element *match;
};

static void checkid(struct ovis_map_element *e, void *user)
{
	struct checkdata *cd = user;
	if (!e || !e->value || !cd || cd->match) {
		return;
	}
	struct ovis_label_id * old = e->value;
	if (old->id.hash == cd->id.hash) {
		if (strcmp(old->id.name,cd->id.name) == 0) {
			cd->match = e;
		}
	}
}

static
int ovis_label_to_id_language(const char *s, struct ovis_label_set * is, dstring_t *dsp)
{
	if (!s || !dsp) {
		return EINVAL;
	}
	enum id_lang language = is->lang;

	xfun f[4] = { NULL, NULL, NULL, NULL };
	int maxlen = is->maxlen;
	unsigned abbr = 0;

	switch (language) {
	case il_least:
		abbr = ABBR_NO_LEAD_UBAR;
		f[0] = ovis_label_to_id_alnum;
		break;
	case il_python:
		f[0] = ovis_label_to_id_ubar;
		f[1] = ovis_label_to_id_alnum;
		f[2] = ovis_label_to_id_hex;
		break;
	case il_r:
		abbr = ABBR_NO_LEAD_UBAR;
		f[0] = ovis_label_to_id_ubar;
		f[1] = ovis_label_to_id_alnum;
		f[2] = ovis_label_to_id_hex;
		break;
	case il_c:
		f[0] = ovis_label_to_id_ubar;
		f[1] = ovis_label_to_id_alnum;
		f[2] = ovis_label_to_id_hex;
		break;
	case il_url:
		abbr = ABBR_PCT;
		/* fallthru */
	case il_file:
		f[0] = ovis_label_to_id_hex;
		break;
	case il_amqp:
		f[0] = ovis_label_to_id_amqp;
		break;
	case il_last:
		return EINVAL;
	}
	int i = 0;
	struct ovis_name label = ovis_name_from_string(s);
	while (f[i] != NULL) {
		f[i](s,abbr,dsp);
		shorten_dstring(dsp, maxlen, label.hash);
		struct ovis_name idtmp = ovis_name_from_string2(dstrval(dsp),
			dstrlen(dsp));
		struct checkdata cd = { idtmp, NULL };
		ovis_map_visit(is->m, checkid, &cd);
		if (!cd.match) {
			break; /* got result */
		}
		dstr_free(dsp);
		i++;
	}
	if (!dstrlen(dsp)) {
		/* collision in spite of retries, possibly on short name.
		 Conservatively uniquify.  */
		ovis_label_to_id_alnum(s,ABBR_NO_LEAD_UBAR,dsp);
		char buf[32];
		sprintf(buf, "%" PRIu64, label.hash);
		dstrcat(dsp, buf, DSTRING_ALL);
		struct ovis_name idtmp = ovis_name_from_string2(dstrval(dsp),
			dstrlen(dsp));
		struct checkdata cd = { idtmp, NULL };
		ovis_map_visit(is->m, checkid, &cd);
		if (cd.match) {
			/* giving up! */
			dstr_free(dsp);
		}
	}
	return 0;
}

struct ovis_label_set *ovis_label_set_create(enum id_lang lang, uint16_t max_id_length)
{
	struct ovis_label_set * ols = malloc(sizeof(struct ovis_label_set));
	if (!ols) {
		return NULL;
	}
	ols->m = ovis_map_create();
	ols->lang = lang;
	ols->maxlen = (max_id_length ? max_id_length : INT_MAX);
	return ols;
}

#define PN_ALL ( PL_COPY | PL_XFER | PI_COPY | PI_XFER)
#define PN_NONE (~PN_ALL)
#define PL_BOTH ( PL_COPY | PL_XFER)
#define PI_BOTH ( PI_COPY | PI_XFER)

static struct ovis_label_id *create_pair(struct ovis_name label, struct ovis_name id, int deep)
{
	if (deep & PN_NONE) {
		return NULL;
		/* bogus depth bit found*/
	}
	struct ovis_label_id * oli = malloc(sizeof(*oli));
	if (!oli)
		return NULL;
	oli->label = label;
	oli->id = id;
	char *tmp1 = (char *)label.name, *tmp2 = (char *)id.name;
	oli->own_label_name = oli->own_id_name = false;

	if (deep & PL_COPY) {
		oli->own_label_name = true;
		tmp1 = malloc(label.len + 1);
		if (!tmp1) {
			free(oli);
			return NULL;
		}
		strncpy(tmp1,label.name, label.len + 1);
	}
	if (deep & PI_COPY) {
		oli->own_id_name = true;
		tmp2 = malloc(id.len + 1);
		if (!tmp2) {
			if (deep & PL_COPY) {
				free(tmp1);
			}
			free(oli);
			return NULL;
		}
		strncpy(tmp2,id.name, id.len + 1);
	}
	if (deep & PL_XFER) {
		tmp1 = (char *)label.name;
		oli->own_label_name = true;
	}
	if (deep & PI_XFER) {
		tmp2 = (char *)id.name;
		oli->own_id_name = true;
	}
	if (!tmp1 || !tmp2) {
		free(oli);
		return NULL;
	}
	oli->label.name = tmp1;
	oli->id.name = tmp2;
	return oli;
}

static void destroypair(struct ovis_map_element *e, void *user)
{
	(void)user;
	if (!e)
		return;
	struct ovis_label_id * oli = ( struct ovis_label_id *)e->value;
	if (oli->own_label_name) {
		free((char *)(oli->label.name));
	}
	if (oli->own_id_name) {
		free((char *)(oli->id.name));
	}
	free(oli);
}

void ovis_label_set_destroy(struct ovis_label_set * is)
{
	if (!is)
		return;
	is->lang = il_last;
	ovis_map_destroy(is->m, destroypair, NULL);
	is->m = NULL;
	free(is);
}

size_t ovis_label_set_size(struct ovis_label_set * is)
{
	if (!is || !is->m) {
		return 0;
	}
	return ovis_map_size(is->m);
}

struct ovis_name ovis_label_set_insert_pair(struct ovis_label_set * is, const struct ovis_name label, const struct ovis_name id, int deep)
{
	if ( (deep & PL_BOTH) == PL_BOTH || (deep & PI_BOTH) == PI_BOTH) {
		return ovis_name_null;
	}
	struct ovis_label_id *value = create_pair(label, id, deep);
	if (!value) {
		return ovis_name_null;
	}
	int err = ovis_map_insert(is->m, label.name, value);
	if (err) {
		return ovis_name_null;
	}
	return value->id;
}

static
struct ovis_name ovis_label_set_insert2(struct ovis_label_set * is, const struct ovis_name label, bool own)
{
	struct ovis_name id;
	struct ovis_map_element ome = { label.name, label.hash, NULL };
	ome = ovis_map_findhash(is->m, ome);
	if (ome.value) {
		return ovis_name_null;
	}

	dstring_t mangle;
	dstr_init(&mangle);
	if (ovis_label_to_id_language(label.name, is, &mangle)) {
		return ovis_name_null;
	}
	if (!dstrlen(&mangle)) {
		return ovis_name_null;
	}
	int ilen = dstrlen(&mangle);
	char *sval = dstr_extract(&mangle);
	if (!sval) {
		return ovis_name_null;
	}
	id = ovis_name_from_string2(sval, ilen);
	int deep = PI_XFER;
	if (own)
		deep |= PL_XFER;
	struct ovis_label_id *value = create_pair(label, id, deep );
	if (!value) {
		return ovis_name_null;
	}

	ome.value = value;
	ovis_map_insert_fast(is->m, ome);
	return id;
}

struct ovis_name ovis_label_set_insert(struct ovis_label_set * s, const struct ovis_name label)
{
	return ovis_label_set_insert2(s, label, false);
}

struct ovis_name ovis_label_set_own(struct ovis_label_set * s, const struct ovis_name label)
{
	return ovis_label_set_insert2(s, label, true);
}

const struct ovis_name ovis_label_set_get_label(struct ovis_label_set * is, const struct ovis_name id)
{
	struct ovis_name label;
	memset(&label,0,sizeof(label));
	/* fixme */
	return label;
}

const struct ovis_name ovis_label_set_get_id(struct ovis_label_set * is, const struct ovis_name label)
{
	struct ovis_name id;
	memset(&id,0,sizeof(id));
	/* fixme */
	return id;
}

struct ovis_label_set_iterator *ovis_label_set_iterator_get(struct ovis_label_set * is)
{
	return NULL;
	/* fixme */
}

const struct ovis_label_id ovis_label_set_next(struct ovis_label_set * is, struct ovis_label_set_iterator *iter)
{
	struct ovis_label_id result;
	memset(&result,0,sizeof(result));
	return result;
	/* fixme */
}

