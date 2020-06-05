/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018-2019 National Technology & Engineering Solutions
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

#include <ctype.h>
#include "ldmsd_plugattr.h"
#include "coll/idx.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include "ovis_util/dstring.h"

#ifdef TEST_PLUGATTR
#define ldmsd_log(e, f, ...) printf(f, ##__VA_ARGS__ )
#endif

/* add -DDBG_LPA=1 to CFLAGS to enable logging lots of stuff. */
#ifndef DBG_LPA
#define DBG_LPA 0
#endif

struct list_pair {
	struct attr_value_list *avl;
	struct attr_value_list *kvl;
	char *key; /* also used for plugin_name in overrides */
	int free_key;
};

struct plugattr {
	char *fname; /* file */
	struct list_pair overrides;
	idx_t idx; /* keyed as "key1 key2", values are struct list_pair * */
	char *buf;
};


static void list_pair_destroy(struct list_pair *lp, void *cbarg)
{
	(void)cbarg;
	if (!lp)
		return;
	av_free(lp->avl);
	av_free(lp->kvl);
	if (lp->free_key)
		free(lp->key);
	free(lp);
}

/* this stores or frees key (independent of return code) and copies avl, kwl. */
static struct list_pair *list_pair_create(char *key, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	struct list_pair *p = calloc(1, sizeof(*p));
	if (!p) {
		free(key);
		errno = ENOMEM;
		return NULL;
	}
	p->key = key;
	p->free_key = 1;
	
	if (avl) {
		p->avl = av_copy(avl);
		if (!p->avl)
			goto out;
	}
	if (kwl) {
		p->kvl = av_copy(kwl);
		if (!p->kvl)
			goto out;
	}
	return p;
out:
	av_free(p->avl);
	av_free(p->kvl);
	free(p->key);
	free(p);
	errno = ENOMEM;
	return NULL;
}

/* convert comments and line continuations.  */
static int canonical(char *buf, size_t len)
{
	if (!buf || !len)
		return EINVAL;
	char *linestart;
	char *s;
	char *lineend;
	char *bufend;
	/* first merge continuations */
	bufend = buf + len;
	linestart = buf;
	lineend = strchr(linestart, '\n');
	while (lineend != NULL && linestart < bufend) {
		s = lineend - 1;
		while ( s >= linestart && isspace(*s))
			s--;
		if (*s == '\\') {
			s[0] = ' ';
			*lineend  = ' ';
		}
		linestart = lineend + 1;
		lineend = strchr(linestart, '\n');
	}
	/* then clear comments */
	s = buf;
	while (s < bufend && *s != '\0') {
		while (isspace(*s)) s++;
		if (*s == '#') {
			s[0] = ' ';
			while (s < bufend) {
				s++;
				if (*s != '\n') {
					s[0] = ' ';
				} else {
					break;
				}
			}
		} else {
			while (s < bufend) {
				s++;
				if (*s == '\n') {
					break;
				}
			}
		}
		s++;
	}
	return 0;
}

/* varargs voodoo to construct key, from va_args
 * as macro because we cannot do as a function portably.
 * in: avl!=NULL, numkeys!=0, key=null, rc=0
 * out: key, rc  assigned
 * rc: ENOKEY if a key is missing; EINVAL if va_arg returns NULL.
*/
#define VGETKEY(AVL, NUMKEYS, KEY, RC) \
{ \
	unsigned nk = 0; \
	dstring_t ds; \
	dstr_init(&ds); \
	va_list ap; \
	va_start(ap, NUMKEYS); \
	while (nk < NUMKEYS) { \
		char *k = va_arg(ap, char *); \
		if (!k) { \
			ldmsd_log(LDMSD_LERROR, \
				"ldmsd_plugattr: key list input contains NULL.\n"); \
			RC = EINVAL; \
			break; \
		} \
		char *v = av_value(AVL, k); \
		if (!v) { \
			RC = ENOKEY; \
			break; \
		} \
		dstrcat(&ds, v, DSTRING_ALL); \
		if (nk < NUMKEYS -1) \
			dstrcat(&ds, "/", 1); \
		nk++; \
	} \
	va_end(ap); \
	if (RC) { \
		dstr_free(&ds); \
	} else { \
		KEY = dstr_extract(&ds); \
	} \
}

static int attr_deprecated(struct attr_value_list *avl, struct attr_value_list *kwl, struct pa_deprecated *dep, const char *context, const char *al, const char *kl)
{
	if (!avl && !kwl)
		return 0;
	if (!dep)
		return 0;
	if (!context) {
		ldmsd_log(LDMSD_LERROR, "%s %s: attr_deprecated miscalled.\n", 
			al ? al : "", kl ? kl : "");
		return 1;
	}
	if (al == NULL || strcmp(al, "(empty)") == 0)
		al = "";
	if (kl == NULL || strcmp(kl, "(empty)") == 0)
		kl = "";
	int badcount = 0;
	struct pa_deprecated *d = dep;
	const char *last_name = NULL;
	while (d->name != NULL) {
		int i = (avl ? av_idx_of(avl, d->name) : -1);
		int j = (kwl ? av_idx_of(kwl, d->name) : -1);
		if (i != -1) {
			if (last_name && 0 == strcmp(last_name, d->name)) {
				goto skip_avl;
			}
			const char *v = av_value(avl, d->name);
			if (d->value == NULL || strcmp(d->value, v) == 0) {
				last_name = d->name;
				if (d->error != 0) {
					badcount++;
					ldmsd_log(LDMSD_LERROR,
						"No longer supported '%s=%s' in %s at line with: %s %s\n",
						d->name, v, context, al, kl);
					if (d->msg) {
						ldmsd_log(LDMSD_LERROR, "%s\n",
						d->msg);
					}
				} else {
					ldmsd_log(LDMSD_LWARNING,
						"deprecated '%s=%s' in %s at line with: %s %s\n",
						d->name, v, context, al, kl);
					if (d->msg) {
						ldmsd_log(LDMSD_LERROR, "%s\n",
						d->msg);
					}
				}
			}
			skip_avl: ;
		}
		if (j != -1) {
			if (false && last_name && 0 == strcmp(last_name, d->name)) {
				goto skip_kwl;
			}
			last_name = d->name;
			if (d->error != 0) {
				badcount++;
				ldmsd_log(LDMSD_LERROR,
					"No longer supported '%s' in %s at line with: %s %s\n",
					d->name, context, al, kl);
				if (d->msg) {
					ldmsd_log(LDMSD_LERROR, "%s\n", d->msg);
				}
			} else {
				ldmsd_log(LDMSD_LWARNING,
					"Deprecated '%s' in %s at line with: %s %s\n",
					d->name, context, al, kl);
				if (d->msg) {
					ldmsd_log(LDMSD_LERROR, "%s\n", d->msg);
				}
			}
			skip_kwl: ;
		}
		d++;
	}
	if (badcount)
		return 1;
	return 0;
}

/* return 1 if blacklisted attr/kw found in avl/kwl, else 0 */
static int attr_blacklist(const char **bad, struct attr_value_list *l, const char *context, const char *prefix)
{
	if (!bad || !l || !context || !prefix) {
		ldmsd_log(LDMSD_LERROR, "%s: attr_blacklist miscalled.\n", prefix);
		return 1;
	}
	int badcount = 0;
	const char **p = bad;
	while (*p != NULL) {
		int i = av_idx_of(l, *p);
		if (i != -1) {
			badcount++;
			char *val = av_value(l, *p);
			if (val)
				ldmsd_log(LDMSD_LERROR, "bad '%s=%s' in %s at line with: %s\n",
					*p, val, context, prefix);
			else
				ldmsd_log(LDMSD_LERROR, "bad '%s' in %s at line with: %s\n",
					*p, context, prefix);
		}
		p++;
	}
	if (badcount)
		return 1;
	return 0;
}

int ldmsd_plugattr_add(struct plugattr *pa, struct attr_value_list *avl, struct attr_value_list *kwl, const char **avban, const char **kwban, struct pa_deprecated *dep, unsigned numkeys, ...)
{
	if (!pa || !numkeys || (!avl && !kwl)) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_add: bad call\n");
		return EINVAL;
	}

	int rc = 0;
	if (dep) {
		char *alist = av_to_string(avl, 0);
		char *klist = av_to_string(kwl, 0);
		rc = attr_deprecated(avl, kwl, dep, "ldmsd_plugattr_add", 
			alist, klist);
		free(alist);
		free(klist);
		if (rc)
			return rc;
	}

	if (kwl && kwban) {
		char *as = av_to_string(kwl, 0);
		rc = attr_blacklist(kwban, kwl, "ldmsd_plugattr_add", as);
		free(as);
		if (rc)
			return rc;
	}

	if (avl && avban) {
		char *as = av_to_string(avl, 0);
		rc = attr_blacklist(avban, avl, "ldmsd_plugattr_add", as);
		free(as);
		if (rc)
			return rc;
	}

	char *key = NULL;
	VGETKEY(avl, numkeys, key, rc);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_add: key error\n");
		return rc;
	}

	struct list_pair *lp;
	lp = idx_find(pa->idx, (void *)key, strlen(key));
	if (lp) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_add: key exist\n");
		return EEXIST;
	}

	lp = list_pair_create(key, avl, kwl);
	if (!lp) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_add: lpc failed\n");
		return ENOMEM;
	}
	rc = idx_add(pa->idx, lp->key, strlen(lp->key), lp);
	if (rc)
		list_pair_destroy(lp, NULL);
#if DBG_LPA
	else
		ldmsd_log(LDMSD_LDEBUG, "ldmsd_plugattr_add: %s succeeded\n", lp->key);
#endif

	return rc;
}

struct plugattr *ldmsd_plugattr_create(const char *filename, const char *plugin_name, struct attr_value_list *avl, struct attr_value_list *kwl, const char **avban, const char **kwban, struct pa_deprecated *dep, unsigned numkeys, ...)
{
	int rc = 0;
	if (!plugin_name) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create: bad call\n");
		errno = EINVAL;
		return NULL;
	}
	if (dep) {
		char *alist = av_to_string(avl, 0);
		char *klist = av_to_string(kwl, 0);
		rc = attr_deprecated(avl, kwl, dep, "ldmsd_plugattr_create", 
			alist, klist);
		free(alist);
		free(klist);
		if (rc) {
			errno = EINVAL;
			return NULL;
		}
	}
	if (avl && avban) {
		char *as = av_to_string(avl, 0);
		rc = attr_blacklist(avban, avl, plugin_name, as);
		free(as);
		if (rc)
			return NULL;
	}

	if (kwl && kwban) {
		char *as = av_to_string(kwl, 0);
		rc = attr_blacklist(kwban, kwl, plugin_name, as);
		free(as);
		if (rc)
			return NULL;
	}
	struct attr_value_list *iavl = NULL;
	struct attr_value_list *ikvl = NULL;
	struct plugattr *pa = calloc(1, sizeof(*pa));
	if (!pa) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create: oom pa\n");
		errno = ENOMEM;
		return NULL;
	}
	pa->idx = idx_create();
	if (!pa->idx) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create: oom idx\n");
		errno = ENOMEM;
		goto err;
	}
	pa->overrides.avl = av_copy(avl);
	pa->overrides.kvl = av_copy(kwl);
	pa->overrides.key = strdup(plugin_name);
	if ( !pa->overrides.key || (avl && !pa->overrides.avl) ||
		(kwl && !pa->overrides.kvl) ) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create: oom overrides\n");
		errno = ENOMEM;
		goto err;
	}
	if (!filename) {
		ldmsd_log(LDMSD_LDEBUG, 
			"ldmsd_plugattr_create: called without conf file.\n");
		return pa;
	}

	pa->fname = strdup(filename);
	if (!pa->fname) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create: oom fname\n");
		errno = ENOMEM;
		goto err;
	}
	FILE *f = fopen(filename, "r");
	if (!f) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR, "Cannot open attrs file %s\n", filename);
		goto fout;
        }
	rc = fseek(f, 0, SEEK_END);
	if (rc) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR, "Cannot seek attrs file %s\n", filename);
		goto fout;
        }
	long len = ftell(f);
	if (!len) {
		ldmsd_log(LDMSD_LWARNING, "Empty attrs file %s\n", filename);
		goto fout;
	}

	rewind(f);
	if (len < 0) {
		rc = errno;
		goto fout;
	}
	pa->buf = malloc(len+1);
	if (!pa->buf) {
		ldmsd_log(LDMSD_LERROR, "Out of memory reading %s\n", filename);
		rc = ENOMEM;
		goto fout;
	}
	pa->buf[len] = '\0';
	size_t nb = fread(pa->buf, sizeof(char), len, f);
	if (nb != len) {
		ldmsd_log(LDMSD_LERROR, "Fail reading attrs file %s %zu %ld \n",
			filename, nb, len);
		rc = EINVAL;
		goto fout;
	}
	canonical(pa->buf, len);
	char *bufend = pa->buf + len;
	char *lineend;
	char *linestart = pa->buf;
	while (linestart < bufend) {
		lineend = strchr(linestart, '\n');
		if (!lineend)
			lineend = bufend;
		*lineend = '\0';
			
		int size = 1;
		char *t = linestart;
		int spacelast = 0;
		while (t[0] != '\0') {
			if (isspace(t[0])) {
				if (!spacelast) {
					size++;
					spacelast = 1;
				}
			} else {
				spacelast = 0;
			}
			t++;
		}
		if (size < 2)
			goto next; /* blank line or no-attrs name */
		iavl = av_new(size);
		ikvl = av_new(size);
		if (!iavl || !ikvl) {
			ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create oom1\n");
			rc = ENOMEM;
			goto fout;
		}
		rc = tokenize(linestart, ikvl, iavl);
		if (rc) {
			ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create tokenize failed\n");
			goto fout;
		}
		char *key = NULL;
		if (iavl->count == 0 && ikvl->count == 0) {
			av_free(iavl);
			av_free(ikvl);
			iavl = ikvl = NULL;
			goto next; /* blank line */
		}
		if (iavl->count >= numkeys) {
			VGETKEY(iavl, numkeys, key, rc);
		} else {
			rc = ENOKEY;
		}
		if (!key) {
			if (rc == EINVAL) {
				ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create misused.\n");
				goto fout;
			}	
			int npos = av_idx_of(ikvl, plugin_name);
			if (npos == -1) {
				char *as, *ks;
				as = av_to_string(iavl, 0);
				ks = av_to_string(ikvl, 0);
				ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create: line with neither %s nor match of key found. line contains %s %s\n",
					plugin_name, as, ks);
				free(as);
				free(ks);
				goto fout;
			}
			key = strdup(plugin_name);
			if (!key) {
				ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_create oom2\n");
				rc = ENOMEM;
				goto fout;
			}
		}

		if (kwban) {
			char *as = av_to_string(iavl, 0); /* yes iavl */
			rc = attr_blacklist(kwban, ikvl, filename, as);
			free(as);
			if (rc)
				goto fout;
		} else {
			ldmsd_log(LDMSD_LDEBUG, "no token ban for kw\n");
		}
		if (avban) {
			char *as = av_to_string(iavl, 0);
			rc = attr_blacklist(avban, iavl, filename, as);
			free(as);
			if (rc)
				goto fout;
		} else {
			ldmsd_log(LDMSD_LDEBUG, "no token ban for attr\n");
		}

		struct list_pair *lp;
		lp = list_pair_create(key, iavl, ikvl);
		av_free(iavl);
		av_free(ikvl);
		iavl = ikvl = NULL;
		if (!lp) {
			ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr: fail lpc\n");
			rc = ENOMEM;
			goto fout;
		}
		rc = idx_add(pa->idx, lp->key, strlen(lp->key), lp);
		if (rc == EEXIST) {
			ldmsd_log(LDMSD_LERROR, "Duplicate key %s in %s\n",
				lp->key, pa->fname);
			goto fout;
		}
next:
		linestart = lineend + 1;
	}
	rc = 0;
fout:
	av_free(iavl);
	av_free(ikvl);
	if (f)
		fclose(f);
	if (rc) {
		errno = rc;
		goto err;
	}
	return pa;
err:
	ldmsd_plugattr_destroy(pa);
	return NULL;
}

void ldmsd_plugattr_destroy(struct plugattr *pa)
{
	if (!pa)
		return;
	free(pa->fname);
	av_free(pa->overrides.avl);
	av_free(pa->overrides.kvl);
	free(pa->overrides.key);
	idx_traverse(pa->idx, (idx_cb_fn_t)list_pair_destroy, NULL);
	idx_destroy(pa->idx);
	free(pa->buf);
	free(pa);
}

const char *ldmsd_plugattr_plugin(struct plugattr *pa)
{
	if (!pa)
		return NULL;
	return pa->overrides.key;
}

const char * ldmsd_plugattr_value(struct plugattr *pa, const char *attr, const char *key)
{
	if (!pa || !attr ) {
		ldmsd_log(LDMSD_LERROR,
			"ldmsd_plugattr_value: bad call(%p,%p).\n", pa, attr);
		return NULL;
	}
#if DBG_LPA
	ldmsd_log(LDMSD_LDEBUG,"ldmsd_plugattr_value(%p, %s, %s)\n",
		pa, attr, key  ? key : "NULL");
#endif
	const char *c;
	/* check given key */
	struct list_pair *lp;
	if (key) {
		lp = idx_find(pa->idx, (void *)key, strlen(key));
		if (lp) {
			c = av_value(lp->avl, attr);
			if (c) {
#if DBG_LPA
				ldmsd_log(LDMSD_LDEBUG,
					"ldmsd_plugattr_value: %s: %s\n", key, c);
#endif
				return c;
			}
		}
	}
	/* check overrides key */
	c = av_value(pa->overrides.avl, attr);
	if (c) {
#if DBG_LPA
		ldmsd_log(LDMSD_LDEBUG,
			"ldmsd_plugattr_value: override %s\n", c);
#endif
		return c;
	}
	/* check file defaults by plugin name */
	key = pa->overrides.key;
	lp = idx_find(pa->idx, (void *)key, strlen(key));
	if (lp) {
		c = av_value(lp->avl, attr);
		if (c) {
#if DBG_LPA
			ldmsd_log(LDMSD_LDEBUG,
				"ldmsd_plugattr_value: %s: %s\n", key, c);
#endif
			return c;
		}
	}
	return NULL;
}

static int fill_multi(struct attr_value_list *avl, const char *attr, int k, int *valc, char ***valv)
{
	if (k >= 0) {
		*valc = 1;
		*valv = malloc(2*sizeof(char *));
		if (!*valv) {
			ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_multivalue: out of mem.\n");
			return ENOMEM;
		}
		(*valv)[0] = av_value_at_idx(avl, k);
		(*valv)[1] = NULL;
		return 0;
	}
	k *= -1;
	*valc = k;
	*valv = malloc((k + 1)*sizeof(char *));
	if (!*valv) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_multivalue: out of mem.\n");
		return ENOMEM;
	}
	int i = 0;
	int j = 0;
	while (i < k && j < avl->count) {
		if (!strcmp(av_name(avl,j), attr)) {
			(*valv)[i] = av_value_at_idx(avl, i);
			i++;
		}
		j++;
	}
	(*valv)[k] = NULL;
	return 0;

}

int ldmsd_plugattr_multivalue(struct plugattr *pa, const char *attr, const char *key, int *valc, char ***valv)
{
	if (!pa || !attr || !valc || !valv) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_multivalue: bad call.\n");
		return EINVAL;
	}
	int k;
	/* check given key */
	struct list_pair *lp;
	if (key) {
		lp = idx_find(pa->idx, (void *)key, strlen(key));
		if (lp) {
			k = av_idx_of(lp->avl, attr);
			if (k != -1)
				return fill_multi(lp->avl, attr, k, valc, valv);
		}
	}
	/* check overrides key */
	k = av_idx_of(pa->overrides.avl, attr);
	if (k != -1)
		return fill_multi(pa->overrides.avl, attr, k, valc, valv);
	/* check file defaults by plugin name */
	key = pa->overrides.key;
	lp = idx_find(pa->idx, (void *)key, strlen(key));
	if (lp) {
		k = av_idx_of(lp->avl, attr);
		if (k != -1)
			return fill_multi(lp->avl, attr, k, valc, valv);
	}
	*valc = 0;
	*valv = NULL;
	return 0;
}

bool ldmsd_plugattr_kw(struct plugattr *pa, const char *kw, const char *key)
{
	if (!pa || !kw) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_kw: bad call.\n");
		return NULL;
	}
	int pos;
	/* check given key */
	struct list_pair *lp;
	if (key) {
		lp = idx_find(pa->idx, (void *)key, strlen(key));
		if (lp) {
			pos = av_idx_of(lp->kvl, kw);
			if (pos != -1)
				return true;
		}
	}
	/* check overrides key */
	pos = av_idx_of(pa->overrides.kvl, kw);
	if (pos != -1)
		return true;
	/* check file defaults by plugin name */
	key = pa->overrides.key;
	lp = idx_find(pa->idx, (void *)key, strlen(key));
	if (lp) {
		pos = av_idx_of(lp->kvl, kw);
		if (pos != -1)
			return true;
	}
	return false;
}

int ldmsd_plugattr_bool(struct plugattr *pa, const char *attr, const char *key,  bool *result)
{
	if (!pa || !attr) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_bool: bad call.\n");
		return -1;
	}
	const char *val = ldmsd_plugattr_value(pa, attr, key);
	if (val) {
		switch (val[0]) {
		case '1':
		case 't':
		case 'T':
		case 'y':
		case 'Y':
		case '\0':
			*result = true;
			break;
		case '0':
		case 'f':
		case 'F':
		case 'n':
		case 'N':
			*result = false;
			break;
		default:
			ldmsd_log(LDMSD_LERROR, "%s: non-boolean %s=%s\n",
					key, attr, val);
			return -1;
		}
		return 0;
	}
	return -2;
}

int ldmsd_plugattr_s32(struct plugattr *pa, const char *at, const char *key, int32_t *result)
{
	if (!pa || !at || !result) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_s32: bad call.\n");
		return EINVAL;
	}
	const char *val = ldmsd_plugattr_value(pa, at, key);
	if (!val)
		return ENOKEY;
	char *endp = NULL;
	errno = 0;
	int64_t j = strtoll(val, &endp, 0);
	if (endp == val || errno || *endp != '\0')
		return ENOTSUP;
	if (j > INT32_MAX || j < INT32_MIN)
		return ERANGE;
	*result = (int32_t)j;
	return 0;
}

int ldmsd_plugattr_u32(struct plugattr *pa, const char *at, const char *key, uint32_t *result)
{
	if (!pa || !at || !result) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_s32: bad call.\n");
		return EINVAL;
	}
	const char *val = ldmsd_plugattr_value(pa, at, key);
	if (!val)
		return ENOKEY;
	char *endp = NULL;
	errno = 0;
	int64_t j = strtoll(val, &endp, 0);
	if (endp == val || errno || *endp != '\0')
		return ENOTSUP;
	if (j > UINT32_MAX || j < 0)
		return ERANGE;
	*result = (uint32_t)j;
	return 0;
}

int ldmsd_plugattr_s64(struct plugattr *pa, const char *at, const char *key, int64_t *result)
{
	if (!pa || !at || !result) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_s64: bad call.\n");
		return EINVAL;
	}
	const char *val = ldmsd_plugattr_value(pa, at, key);
	if (!val)
		return ENOKEY;
	char *endp = NULL;
	errno = 0;
	int64_t j = strtoll(val, &endp, 0);
	if (endp == val || errno || *endp != '\0')
		return ENOTSUP;
	*result = j;
	return 0;
}

int ldmsd_plugattr_u64(struct plugattr *pa, const char *at, const char *key, uint64_t *result)
{
	if (!pa || !at || !result) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_u64: bad call.\n");
		return EINVAL;
	}
	const char *val = ldmsd_plugattr_value(pa, at, key);
	if (!val)
		return ENOKEY;
	char *endp = NULL;
	errno = 0;
	uint64_t j = strtoull(val, &endp, 0);
	if (endp == val || errno || *endp != '\0')
		return ENOTSUP;
	*result = j;
	return 0;
}

int ldmsd_plugattr_f64(struct plugattr *pa, const char *at, const char *key, double *result)
{
	if (!pa || !at || !result) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_f64: bad call.\n");
		return EINVAL;
	}
	const char *val = ldmsd_plugattr_value(pa, at, key);
	if (!val)
		return ENOKEY;
	char *endp = NULL;
	errno = 0;
	double j = strtod(val, &endp);
	if (endp == val || errno || *endp != '\0')
		return ENOTSUP;
	*result = j;
	return 0;
}

int ldmsd_plugattr_szt(struct plugattr *pa, const char *at, const char *key, size_t *result)
{
	if (!pa || !at || !result) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_szt: bad call.\n");
		return EINVAL;
	}
	const char *val = ldmsd_plugattr_value(pa, at, key);
	if (!val)
		return ENOKEY;
	size_t j = ovis_get_mem_size(val);
	*result = j;
	return 0;
}

struct dump_args {
	enum ldmsd_loglevel lvl;
	struct list_pair *pilp;
};

static void dump_lp(void *lp, void *a)
{
	struct list_pair *p = (struct list_pair *)lp;
	struct dump_args *da = (struct dump_args *)a;
	if (lp == da->pilp)
		return;
	char *as = av_to_string(p->avl, 0);
	char *ks = av_to_string(p->kvl, 0);
	ldmsd_log(da->lvl, "%s: [kw] %s [av] %s\n", p->key, ks, as);
	free(as);
	free(ks);
}

/* \brief dump pa (or subset indicated by key to log file at the given level. */
void ldmsd_plugattr_log(enum ldmsd_loglevel lvl, struct plugattr *pa, const char *key)
{
	if (!pa) {
		ldmsd_log(LDMSD_LERROR, "ldmsd_plugattr_log miscalled\n");
		return;
	}
	struct dump_args da;
	da.lvl = lvl;
	if (!key) {
		struct list_pair *pilp;
		pilp = idx_find(pa->idx, pa->overrides.key, strlen(pa->overrides.key));
		ldmsd_log(lvl, "plugattr: source: %s\n", pa->fname);
		ldmsd_log(lvl, "plugattr: plugin_name: %s\n", pa->overrides.key);
		char *oa = NULL, *ok = NULL;
		char *pia = NULL, *pik = NULL;
		if (pa->overrides.avl)
			oa = av_to_string(pa->overrides.avl, 0);
		if (pa->overrides.kvl)
			ok = av_to_string(pa->overrides.kvl, 0);
		if (pilp) {
			pia = av_to_string(pilp->avl, 0);
			pik = av_to_string(pilp->kvl, 0);
		}
		da.pilp = pilp;
		ldmsd_log(lvl, "plugattr %s: [kw over] %s [av over] %s :  [kw] %s [av] %s\n",
			pa->overrides.key, ok, oa, pik, pia);
		free(oa);
		free(ok);
		free(pia);
		free(pik);
		idx_traverse(pa->idx, dump_lp, (void *)&da);
	} else {
		struct list_pair *lp;
		lp = idx_find(pa->idx, (void *)key, strlen(key));
		if (!lp) {
			ldmsd_log(lvl, "plugattr %s: not-defined\n", key);
			return;
		}
		da.pilp = NULL;
		dump_lp(lp, &da);
	}	
}

int ldmsd_plugattr_config_check(const char **anames, const char **knames, struct attr_value_list *avl, struct attr_value_list *kvl, struct pa_deprecated *dep, const char *pname)
{
	int i, count;
	int unexpected = 0;

	if (dep) {
		char *alist = av_to_string(avl, 0);
		char *klist = av_to_string(kvl, 0);
		int rc = attr_deprecated(avl, kvl, dep, "ldmsd_plugattr_config_check", 
			alist, klist);
		free(alist);
		free(klist);
		if (rc) {
			unexpected++;
		}
	}
	if (avl && !anames && avl->count > 0) {
		count = 0;
		while (count < avl->count) {
			if (pname) {
				ldmsd_log(LDMSD_LWARNING,
					"%s: unexpected config attribute %s. "
					"Check spelling and man page Plugin_%s?\n",
					pname, av_name(avl, count), pname);
			}
			count++;
			unexpected++;
		}
	}
	if (anames && avl) {
		for (count = 0; count < avl->count; count++) {
			const char *k = av_name(avl, count);
			i = 0;
			int found = 0;
			while (anames[i] != NULL) {
				if (strcmp(k, anames[i]) == 0) {
					found = 1;
					break;
				}
				i++;
			}
			if (!found) {
				unexpected++;
				if (pname) {
					ldmsd_log(LDMSD_LWARNING,
						"%s: unexpected config attribute %s. Check spelling and man page Plugin_%s?\n",
						pname, k, pname);
				}
			}
		}
	}

	if (kvl && !knames && kvl->count > 0) {
		count = 0;
		while (count < kvl->count) {
			if (pname) {
				ldmsd_log(LDMSD_LWARNING,
					"%s: unexpected config keyword %s. Check spelling and man page Plugin_%s?\n",
					pname, av_name(kvl, count), pname);
			}
			count++;
			unexpected++;
		}
	}
	if (knames && kvl) {
		for (count = 0; count < kvl->count; count++) {
			const char *k = av_name(kvl, count);
			i = 0;
			int found = 0;
			while (knames[i] != NULL) {
				if (strcmp(k, knames[i]) == 0) {
					found = 1;
					break;
				}
				i++;
			}
			if (!found) {
				unexpected++;
				if (pname) {
					ldmsd_log(LDMSD_LWARNING,
						"%s: unexpected config keyword %s. Check spelling and man page Plugin_%s?\n",
						pname, k, pname);
				}
			}
		}
	}
	return unexpected;			
}


struct plugattr *ldmsd_plugattr_create(const char *filename, const char *plugin_name, struct attr_value_list *avl, struct attr_value_list *kwl, const char **avban, const char **kwban, struct pa_deprecated *dep, unsigned numkeys, ...);
#ifdef TEST_PLUGATTR
#include <libgen.h>
#include <math.h>
int main(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		struct plugattr *pa = ldmsd_plugattr_create(argv[i], "store_csv", NULL, NULL, NULL, NULL, NULL, 1, "mykey");
		printf("parsing %s returned %p\n", argv[i], pa);
		if (pa) {
			ldmsd_plugattr_log(LDMSD_LINFO, pa, NULL);
			ldmsd_plugattr_log(LDMSD_LINFO, pa, "store_csv");
		} else {
			continue;
		}
		char *bn = basename(argv[i]);
		if (!strcmp("test_plugattr.txt", bn)) {
			const char *s;
			bool b;
			int rc;
			// check expected at
			s = ldmsd_plugattr_value(pa, "d", "key1");
			if (!(s && s[0] == '3'))
				printf("missing a/v d=3 in key1\n");
			// check unexpected at
			s = ldmsd_plugattr_value(pa, "k", "key2");
			if (s)
				printf("unexpected k=%s in 'key2'\n", s);
			// check expected s32
			int32_t s32 = 0;
			rc = ldmsd_plugattr_s32(pa, "s32", "key1", &s32);
			if (rc)
				printf("missing s32 in attrs of key1 or store_csv\n");
			else
				if (s32 != -55555)
					printf("misparsed s32\n");
			// check expected s32, unexpected key to default
			s32 = 0;
			rc = ldmsd_plugattr_s32(pa, "s32", "bob", &s32);
			if (rc)
				printf("missing s32 in attrs of store_csv\n");
			else
				if (s32 != -55555)
					printf("misparsed s32\n");
			// check expected u32
			uint32_t u32 = 0;
			rc = ldmsd_plugattr_u32(pa, "u32", "key1", &u32);
			if (rc)
				printf("missing u32 in attrs of key1 or store_csv\n");
			else
				if (u32 != 123456789)
					printf("misparsed u32\n");
			// check expected s64
			int64_t s64 = 0;
			rc = ldmsd_plugattr_s64(pa, "s64", "key1", &s64);
			if (rc)
				printf("missing s64 in attrs of key1 or store_csv\n");
			else
				if (s64 != -987654321987654321)
					printf("misparsed s64\n");
			// check expected u64
			uint64_t u64 = 0;
			rc = ldmsd_plugattr_u64(pa, "u64", "key1", &u64);
			if (rc)
				printf("missing u64 in attrs of key1 or store_csv\n");
			else
				if (u64 != 987654321987654321)
					printf("misparsed u64\n");
			// check expected f64
			double f64 = 0;
			rc = ldmsd_plugattr_f64(pa, "f64", "key1", &f64);
			if (rc)
				printf("missing f64 in attrs of key1 or store_csv\n");
			else
				if (fabs(f64 - 3.14159) > 1e-5)
					printf("misparsed f64\n");
			// check expected szt
			size_t sz = 0;
			rc = ldmsd_plugattr_szt(pa, "szt", "key1", &sz);
			if (rc)
				printf("missing szt in attrs of key1 or store_csv\n");
			else
				if (sz != 32*1024*1024)
					printf("misparsed szt\n");
			// check expected kw
			b = ldmsd_plugattr_kw(pa, "h", "key1");
			if (!b)
				printf("missing h in keywords of key1\n");
			// check unexpected kw
			b = ldmsd_plugattr_kw(pa, "i", "key1");
			if (b)
				printf("unexpected i in keywords of key1\n");
			// check multiple av
			int vc = 0;
			char **vv = NULL;
			rc = ldmsd_plugattr_multivalue(pa, "c", "key1", &vc, &vv);
			if (rc)
				printf("failed alloc multivalue c.\n");
			else
				if (vc != 3)
					printf("failed multivalue c count.\n");
				else
					for (rc = 0; rc < vc; rc++)
						printf("%s ", vv[rc]);
			printf("\n");
			free(vv);
			rc = ldmsd_plugattr_multivalue(pa, "a", "key1", &vc, &vv);
			if (rc)
				printf("failed alloc multivalue a.\n");
			else
				if (vc != 1)
					printf("failed multivalue a count.\n");
				else
					for (rc = 0; rc < vc; rc++)
						printf("%s ", vv[rc]);
			printf("\n");
			free(vv);
		}
		ldmsd_plugattr_destroy(pa);
	}
	return 0;
}
#endif
