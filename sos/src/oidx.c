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
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/fcntl.h>

#include "ovis-test/test.h"
#include "oidx.h"
#include "oidx_priv.h"

static size_t oidx_entries;

/**
 * Compare two keys: \a k1 and \a k2.
 * \return -1 if \a k1 < \a k2, 0 if \a k1 == \a k2 and 1 if \a k1 > \a k2.
 */
inline
int oidx_key_cmp(unsigned char *k1, unsigned char *k2, int keylen)
{
	int i;
	for (i=0; i < keylen; i++) {
		if (k1[i] < k2[i])
			return -1;
		if (k1[i] > k2[i])
			return 1;
	}
	return 0;
}

static uint64_t new_oidx_layer(oidx_t t, size_t sz)
{
	struct oidx_layer_s *l;

	l = ods_alloc(t->ods, sizeof(*l) + sz);
	if (!l) {
		/* Attempt to extend index */
		if (ods_extend(t->ods, 128*sz))
			goto err0;

		/* Fix-up any cached ptrs */
		t->udata = ods_get_user_data(t->ods, &t->udata_sz);

		l = ods_alloc(t->ods, sizeof(*l) + sz);
		if (!l)
			goto err0;
	}
	memset((void *)l, 0, sz + sizeof(*l));
	return ods_obj_ptr_to_offset(t->ods, l);

 err0:
	return 0;
}

void oidx_flush(oidx_t oidx)
{
	ods_flush(oidx->ods);
}

void oidx_close(oidx_t oidx)
{
	oidx_flush(oidx);
	ods_close(oidx->ods);
	free(oidx);
}

oidx_t oidx_open(const char *path, int o_flag, ...)
{
	va_list argp;
	int o_mode;
	oidx_t t = malloc(sizeof *t);
	if (!t)
		goto err0;

	if (o_flag & O_CREAT) {
		va_start(argp, o_flag);
		o_mode = va_arg(argp, int);
	} else
		o_mode = 0;

	t->ods = ods_open(path, o_flag, o_mode);
	if (!t->ods)
		goto err1;

	t->udata = ods_get_user_data(t->ods, &t->udata_sz);
	if (memcmp(t->udata->signature, OIDX_SIGNATURE,
		   sizeof(t->udata->signature))) {
		t->udata->radix = 256;
		t->udata->layer_sz =
			t->udata->radix * sizeof(struct oidx_entry_s);
		memcpy(t->udata->signature, OIDX_SIGNATURE,
		       sizeof(t->udata->signature));

		t->udata->top = new_oidx_layer(t, t->udata->layer_sz);
	}
	if (!t->udata->top)
		goto err1;

	return t;

 err1:
	free(t);
 err0:
	return NULL;
}

static void free_layer(oidx_t t, struct oidx_layer_s *l)
{
	int i;

	if (!l)
		return;

	for (i = 0; i < t->udata->radix; i++)
		free_layer(t,
			   ods_obj_offset_to_ptr(t->ods, l->entries[i].next));

	ods_free(t->ods, l);
}

/**
 * \brief Free a prefix tree.
 *
 * \param d Pointer to the prefix tree
 */
void oidx_destroy(oidx_t t)
{
	free_layer(t, ods_obj_offset_to_ptr(t->ods, t->udata->top));
	free(t);
}

/**
 * \brief Find the data object associated with a prefix key
 */
uint64_t oidx_find(oidx_t t, oidx_key_t key, size_t keylen)
{
	unsigned char *pkey = key;
	struct oidx_layer_s *pl;

	if (!keylen || !key || keylen > OIDX_KEY_MAX_LEN)
		return 0;

	keylen--;
	for (pl = ods_obj_offset_to_ptr(t->ods, t->udata->top);
	     pl && keylen; keylen--)
		pl = ods_obj_offset_to_ptr(t->ods, pl->entries[*pkey++].next);

	/* If we ran out of key before we ran out of layers --> not found */
	if (keylen)
		return 0;

	/* ... or we ran out on the last layer */
	if (!pl)
		return 0;

	return pl->entries[*pkey].obj;
}

/**
 * \brief Find the right most element of the sub tree \a pl
 * \param t The OIDX structure pointer
 * \param pl The head of the subtree
 * \param okey The output key to get to the right most element from pl
 * \return OIDX ODS offset to ::oidx_objref_head_s.
 */
uint64_t oidx_find_right_most(oidx_t t, struct oidx_layer_s *pl,
			      unsigned char* opkey)
{
	int tmp_k;
	struct oidx_layer_s *tmp_pl;
	uint64_t ref;
loop:
	for (tmp_k = t->udata->radix; tmp_k >= 0; tmp_k--) {
		if (ref = pl->entries[tmp_k].obj) {
			*opkey++ = tmp_k;
			return ref;
		}
		if (tmp_pl = ods_obj_offset_to_ptr(t->ods,
					pl->entries[tmp_k].next)) {
			*opkey++ = tmp_k;
			pl = tmp_pl;
			goto loop;
		}
	}
	return 0;
}

/**
 * \brief Find the left most element of the sub tree \a pl.
 *
 * \param t The OIDX structure pointer
 * \param pl The head of the subtree
 * \param okey The output key to get to the left most element from pl
 * \return OIDX ODS offset to ::oidx_objref_head_s.
 */
uint64_t oidx_find_left_most(oidx_t t, struct oidx_layer_s *pl,
			     unsigned char* opkey)
{
	int tmp_k;
	struct oidx_layer_s *tmp_pl;
	uint64_t ref;
loop:
	for (tmp_k = 0; tmp_k < t->udata->radix; tmp_k++) {
		if (ref = pl->entries[tmp_k].obj) {
			*(opkey++) = tmp_k;
			return ref;
		}
		if (tmp_pl = ods_obj_offset_to_ptr(t->ods,
					pl->entries[tmp_k].next)) {
			*(opkey++) = tmp_k;
			pl = tmp_pl;
			goto loop;
		}
	}
	return 0;
}

/**
 * \brief Find the data object that is an approximated supremum to the given
 * \a key. The key of the approximated supremum object will be set to the
 * output parameter \a okey.
 *
 * \param t The oidx structure pointer
 * \param key The input key
 * \param okey The output key which is the approximated supremum key to the
 *   input key
 * \param keylen The length (bytes) of the key
 */
uint64_t oidx_find_approx_sup(oidx_t t, oidx_key_t key,
	oidx_key_t okey, size_t keylen)
{
	unsigned char *pkey = key;
	unsigned char *pokey = okey;
	struct oidx_layer_s *pl, *tmp_pl;
	int tmp_k;

	if (!keylen || !key || keylen > OIDX_KEY_MAX_LEN)
		return 0;

	tmp_pl = 0;
	keylen--;

	for (pl = ods_obj_offset_to_ptr(t->ods, t->udata->top);
	     pl && keylen; keylen--){
		tmp_pl = ods_obj_offset_to_ptr(t->ods, pl->entries[*pkey].next);
		if (!tmp_pl) {
			break;
		}
		pl = tmp_pl;
		*pokey = *pkey;
		pkey++;
		pokey++;
	}

	keylen++;

	if (!tmp_pl) {
		/* cannot find exact key .. look for the supremum */

		/* for first continued layer ...
		 * look to the right ... */
		tmp_k = (*pkey);
		do {
			tmp_k++;
			tmp_pl = ods_obj_offset_to_ptr(t->ods,
					pl->entries[tmp_k].next);
		} while (!tmp_pl && tmp_k < 256);

		if (tmp_pl) {
			pl = tmp_pl;
			*pokey = tmp_k;
			pokey++;
			keylen--;
			return oidx_find_left_most(t, pl, pokey);
		} else {
			return oidx_find_right_most(t, pl, pokey);
		}
	} else {
		// can find exact key up to the last layer
		uint64_t obj = pl->entries[*pkey].obj;
		if (obj) {
			*pokey = *pkey;
			return obj;
		} else {
			// not found at last layer

			// Look to the right
			tmp_k = (*pkey);
			do {
				obj = pl->entries[++tmp_k].obj;
			} while (!obj && tmp_k < 255);

			if (obj) {
				*pokey = tmp_k;
				return obj;
			}

			// Look to the left
			tmp_k = (*pkey);
			do {
				obj = pl->entries[--tmp_k].obj;
			} while (!obj && tmp_k > 0);

			if (obj) {
				*pokey = tmp_k;
				return obj;
			}

		}
	}

	return 0; /* Just to suppress compiler warning. */
}

/**
 * \brief Find the data object that is an approximated infimum to the given
 * \a key. The key of the approximated infimum object will be set to the
 * output parameter \a okey.
 *
 * \param t The oidx structure pointer
 * \param key The input key
 * \param okey The output key which is the approximated infimum key to the
 *   input key
 * \param keylen The length (bytes) of the key
 */
uint64_t oidx_find_approx_inf(oidx_t t, oidx_key_t key,
	oidx_key_t okey, size_t keylen)
{
	unsigned char *pkey = key;
	unsigned char *pokey = okey;
	struct oidx_layer_s *pl, *tmp_pl;
	int tmp_k;

	if (!keylen || !key || keylen > OIDX_KEY_MAX_LEN)
		return 0;

	tmp_pl = 0;
	keylen--;

	for (pl = ods_obj_offset_to_ptr(t->ods, t->udata->top);
	     pl && keylen; keylen--){
		tmp_pl = ods_obj_offset_to_ptr(t->ods, pl->entries[*pkey].next);
		if (!tmp_pl) {
			break;
		}
		pl = tmp_pl;
		*pokey = *pkey;
		pkey++;
		pokey++;
	}

	keylen++;

	if (!tmp_pl) {
		// cannot find exact key .. look for the infimum

		// for first continued layer ...
		tmp_k = (*pkey);
		do {
			tmp_k--;
			tmp_pl = ods_obj_offset_to_ptr(t->ods,
						pl->entries[tmp_k].next);
		} while (!tmp_pl && tmp_k > 0);
		if (tmp_pl) {
			pl = tmp_pl;
			*pokey = tmp_k;
			pokey++;
			keylen--;
			return oidx_find_right_most(t, pl, pokey);
		} else {
			return oidx_find_left_most(t, pl, pokey);
		}
	} else {
		// can find exact key up to the last layer
		uint64_t obj = pl->entries[*pkey].obj;
		if (obj) {
			*pokey = *pkey;
			return obj;
		} else {
			// not found at last layer
			//
			// Look to the left
			tmp_k = (*pkey);
			do {
				obj = pl->entries[--tmp_k].obj;
			} while (!obj && tmp_k > 0);

			if (obj) {
				*pokey = tmp_k;
				return obj;
			}

			// Look to the right
			tmp_k = (*pkey);
			do {
				obj = pl->entries[++tmp_k].obj;
			} while (!obj && tmp_k < 255);

			if (obj) {
				*pokey = tmp_k;
				return obj;
			}
		}
	}

	/* Should not reach here, but put this return statement to suppress
	 * compiler warning.*/
	return 0;
}

/**
 * \brief Add a prefix key and associated data object to the tree.
 *
 * \note \a obj must be an offset to object in ODS.
 *
 * ODS of the oidx will store prefix tree (represented by layers), and multiple
 * lists of references of objects that have the same key (as shown in picture
 * below).
 * <pre>
 * layer[ ... ]
 * 	 |
 * 	 V
 * 	layer[ ... ]
 * 	  	|
 * 	  	V
 * 	  	[begin, end]
 * 	  	  |      |
 * 	  	  |      v
 * 	  	  |     [prev|obj_ref|next]
 * 	  	  |      |      ^      |
 * 	  	  v      v      |      =
 * 	  	  [prev|obj_ref|next]
 * 	  	    |
 * 	  	    =
 * </pre>
 *
 * \param t The pointer to the opened ::oidx_s.
 * \param key Pointer to the key.
 * \param keylen The length (in byte) of \a key.
 * \param obj The object reference (usually an offset of obj in Object ODS).
 */
int oidx_add(oidx_t t, oidx_key_t key, size_t keylen, uint64_t obj)
{
	struct oidx_layer_s *pl;
	unsigned char *poidx = key;

	/* We don't allow adding a NULL pointer as an object */
	if (!obj)
		return EINVAL;
#if 0
	if (oidx_find(t, key, keylen))
		return EEXIST;
#endif

	oidx_entries++;
	keylen--;
	for (pl = ods_obj_offset_to_ptr(t->ods, t->udata->top);
	     keylen;
	     keylen--, pl = ods_obj_offset_to_ptr(t->ods,
						  pl->entries[*poidx++].next)) {
		/* If there is no layer at this prefix, add one */
		if (!pl->entries[*poidx].next) {
			/* Since allocation may make my ptr no longer
			 * valid, cache it's offset so I can
			 * regenerate it after allocation */
			uint64_t pl_off = ods_obj_ptr_to_offset(t->ods, pl);
			uint64_t n_layer =
				new_oidx_layer(t, t->udata->layer_sz);
			if (!n_layer)
				goto err0;
			pl = ods_obj_offset_to_ptr(t->ods, pl_off);
			pl->entries[*poidx].next = n_layer;
			pl->count++; /* Add layer reference */
		}
	}

	/* Now, get the list of object refs of this key */
	oidx_objref_head_t h = ods_obj_offset_to_ptr(t->ods,
						pl->entries[*poidx].obj);
	if (!h) {
		h = ods_alloc(t->ods, sizeof(*h));
		if (!h)
			goto err0;
		h->begin = h->end = 0;
		pl->entries[*poidx].obj = ods_obj_ptr_to_offset(t->ods, h);
	}

	/* allocate + set up new list entry */
	oidx_objref_entry_t e = ods_alloc(t->ods, sizeof(*e));
	if (!e)
		goto err0;
	e->next = 0;
	e->prev = h->end;
	e->objref = obj;

	/* insert the obj ref into the list */
	if (h->begin) {
		/* list not empty */
		oidx_objref_entry_t prev = ods_obj_offset_to_ptr(t->ods, h->end);
		prev->next = ods_obj_ptr_to_offset(t->ods, e);
		h->end = prev->next;
	} else {
		/* list empty */
		h->begin = h->end = ods_obj_ptr_to_offset(t->ods, e);
	}
	pl->count++;	/* Add object reference */
	return 0;
err0:
	return ENOMEM;
}

/**
 * \brief Delete an entry from a prefix tree based on it's key.
 *
 * \param d Pointer to the prefix tree
 * \param poidx Pointer to the prefix key
 * \returns The object associated with the prefix key
 */
static int _delete_from_layer(struct oidx_s *oidx, oidx_layer_t pl,
			      oidx_key_t key, size_t keylen, int key_idx,
			      uint64_t *p_obj)
{
	int count;
	unsigned char idx = ((unsigned char *)key)[key_idx];
	if (key_idx < keylen-1) {
		if (pl->entries[idx].next) {
			oidx_layer_t nl =
				ods_obj_offset_to_ptr(oidx->ods,
						      pl->entries[idx].next);
			count = _delete_from_layer(oidx, nl,
						   key, keylen, key_idx + 1,
						   p_obj);
			/*
			 * Doing this will invalidate/corrupt
			 * outstanding iterators
			 */
			if (!count) {
				/*
				 * When the layer below us is no
				 * longer used, free it
				 */
				ods_free(oidx->ods, nl);
				pl->entries[idx].next = 0;
				pl->count--; /* remove layer reference */
			}
		}
	} else if (key_idx == keylen - 1) {
		*p_obj = pl->entries[idx].obj;
		/* Don't decrement the count if there was never an object here */
		if (pl->entries[idx].obj) {
			pl->entries[idx].obj = 0;
			pl->count--; /* remove object reference */
		}
	}
	return pl->count;
}

/**
 * Delete the object list from the prefix tree (by \a key).
 * \note The object list and objects in the list are not deleted. The caller is
 * responsible for clearing the object reference list.
 * \param t The oidx handle.
 * \param key The pointer to the key.
 * \param keylen The length of the key.
 * \returns The reference of object list.
 */
uint64_t oidx_delete(oidx_t t, oidx_key_t key, size_t keylen)
{
	struct oidx_s *oidx = t;
	uint64_t obj = 0;
	oidx_layer_t top = ods_obj_offset_to_ptr(oidx->ods, oidx->udata->top);
	(void)_delete_from_layer(oidx, top, key, keylen, 0, &obj);
	return obj;
}

int oidx_obj_remove(oidx_t t, oidx_key_t key, size_t keylen, uint64_t obj)
{
	uint64_t hoff = oidx_find(t, key, keylen);
	if (!hoff)
		return ENOENT;
	oidx_objref_head_t h = ods_obj_offset_to_ptr(t->ods, hoff);
	uint64_t eoff = h->begin;
	oidx_objref_entry_t e, en, ep;
	while (eoff) {
		e = ods_obj_offset_to_ptr(t->ods, eoff);
		if (e->objref == obj)
			break;
		eoff = e->next;
	}

	if (!eoff)
		return ENOENT;
	if (e->next) {
		if (e->prev) {
			/* Middle of the list */
			en = ods_obj_offset_to_ptr(t->ods, e->next);
			ep = ods_obj_offset_to_ptr(t->ods, e->prev);
			en->prev = e->prev;
			ep->next = e->next;
		} else {
			/* First entry */
			h->begin = e->next;
			en = ods_obj_offset_to_ptr(t->ods, e->next);
			en->prev = 0;
		}
	} else {
		if (e->prev) {
			/* Last entry */
			h->end = e->prev;
			ep = ods_obj_offset_to_ptr(t->ods, e->prev);
			ep->next = 0;

		} else {
			/* e is the only entry, delete the key from the layer */
			uint64_t list = oidx_delete(t, key, keylen);
			if (list)
				ods_free(t->ods, ods_obj_offset_to_ptr(t->ods,
								list));

		}
	}
	ods_free(t->ods, e);
	return 0;
}

static void _walk(oidx_t oidx, oidx_layer_t pl,
		  oidx_walk_fn walk_fn, oidx_key_t key, size_t keylen, void *context)
{
	int col;
	for (col = 0; col < oidx->udata->radix; col++) {
		((uint8_t*)key)[keylen] = (uint8_t)col;
		if (!pl->entries[col].obj && !pl->entries[col].next)
			continue;

		if (pl->entries[col].obj)
			walk_fn(oidx, key, keylen+1, pl->entries[col].obj, context);

		if (!pl->entries[col].next)
			continue;

		_walk(oidx,
		      ods_obj_offset_to_ptr(oidx->ods, pl->entries[col].next),
		      walk_fn, key, keylen + 1, context);
	}
}

void oidx_walk(oidx_t oidx, oidx_walk_fn walk_fn, void *context)
{
	char _key[256];
	oidx_key_t key = _key;
	oidx_layer_t top = ods_obj_offset_to_ptr(oidx->ods, oidx->udata->top);
	_walk(oidx, top, walk_fn, key, 0, context);
}

oidx_iter_t oidx_iter_new(oidx_t oidx)
{
	oidx_iter_t iter = calloc(1, sizeof *iter);
	if (!iter)
		return NULL;
	iter->oidx = oidx;
	iter->layer[0] = ods_obj_offset_to_ptr(oidx->ods, oidx->udata->top);
	return iter;
}

void oidx_iter_free(oidx_iter_t iter)
{
	free(iter);
}

void reset_cur_list(oidx_iter_t iter)
{
	iter->cur_list_head = 0;
	iter->cur_list_entry = 0;
}

/**
 * (Private) next object in the current list.
 * \return Reference to the next object.
 * \return 0 if the list ends.
 */
uint64_t oidx_iter_next_in_list(oidx_iter_t iter)
{
	if (!iter->cur_list_head)
		return 0;
	oidx_objref_head_t h = ods_obj_offset_to_ptr(iter->oidx->ods,
							iter->cur_list_head);
	oidx_objref_entry_t ent;
	if (!iter->cur_list_entry) {
		if (h->begin)
			ent = ods_obj_offset_to_ptr(iter->oidx->ods, h->begin);
		else {
			reset_cur_list(iter);
			return 0;
		}
	} else {
		ent = ods_obj_offset_to_ptr(iter->oidx->ods,
							iter->cur_list_entry);
	}

	uint64_t obj = ent->objref;
	if (ent->next)
		iter->cur_list_entry = ent->next;
	else
		reset_cur_list(iter);

	return obj;
}

/**
 * (Private) previous object in the current list.
 * \return Reference to the previous object.
 * \return 0 if the list ends.
 */
uint64_t oidx_iter_prev_in_list(oidx_iter_t iter)
{
	if (!iter->cur_list_head)
		return 0;
	oidx_objref_head_t h = ods_obj_offset_to_ptr(iter->oidx->ods,
							iter->cur_list_head);
	oidx_objref_entry_t ent;
	if (!iter->cur_list_entry) {
		if (h->end)
			ent = ods_obj_offset_to_ptr(iter->oidx->ods, h->end);
		else {
			reset_cur_list(iter);
			return 0;
		}
	} else {
		ent = ods_obj_offset_to_ptr(iter->oidx->ods,
							iter->cur_list_entry);
	}

	uint64_t obj = ent->objref;
	if (ent->prev)
		iter->cur_list_entry = ent->prev;
	else
		reset_cur_list(iter);

	return obj;
}

uint64_t oidx_iter_current_list(oidx_iter_t iter)
{
	return iter->cur_list_head;
}

uint64_t oidx_iter_current_obj(oidx_iter_t iter)
{
	if (!iter->cur_list_entry)
		return 0;
	oidx_objref_entry_t e = ods_obj_offset_to_ptr(iter->oidx->ods,
							iter->cur_list_entry);
	return e->objref;
}

uint64_t oidx_iter_next_obj(oidx_iter_t iter)
{
	/* Look into the current list first. */
	uint64_t obj = oidx_iter_next_in_list(iter);
	if (obj)
		return obj;
	/* If not found, get the next list and objects from the next list. */
	obj = oidx_iter_next_list(iter);
	if (obj) /* Iterate to next list (next key) success. */
		return oidx_iter_next_in_list(iter);
	return 0;
}

uint64_t oidx_iter_prev_obj(oidx_iter_t iter)
{
	/* Look into the current list first. */
	uint64_t obj = oidx_iter_prev_in_list(iter);
	if (obj)
		return obj;
	/* If not found, get the prev list and objects from the prev list. */
	obj = oidx_iter_prev_list(iter);
	if (obj) /* Iterate to prev list (prev key) success. */
		return oidx_iter_prev_in_list(iter);
	return 0;
}

uint64_t oidx_iter_next_list(oidx_iter_t iter)
{
	oidx_layer_t l;
	uint64_t obj = 0;
	uint64_t next_off = 0;
	int icl; /* iter->cur_layer */
	int ili; /* iter->layer_idx[icl] */

	icl = iter->cur_layer; /* always synchronized with iter->cur_layer
       				* --> no need to re-initialize */
	if (icl < 0)
		goto obj_not_found;

layer_work:
	l = iter->layer[icl];
next_index:
	ili = ++(iter->layer_idx[icl]);
	if (ili >= iter->oidx->udata->radix) {
		/* level end, reset and go up one level. */
		iter->layer[icl] = NULL;
		iter->key[icl] = 0;
		iter->layer_idx[icl] = -1;
		icl = --(iter->cur_layer);
		if (icl < 0)
			goto obj_not_found;
		goto layer_work;
	}
	iter->key[icl] = ili;
	obj = l->entries[ili].obj;
	if (obj)
		goto obj_found;
	next_off = l->entries[ili].next;
	if (next_off) {
		/* go deeper */
		icl = ++(iter->cur_layer);
		iter->layer[icl] = ods_obj_offset_to_ptr(iter->oidx->ods,
								next_off);
		iter->layer_idx[icl] = -1;
		goto layer_work;
	}
	goto next_index;

obj_found:
	/* obj is now refering to the head of the list */
	iter->cur_list_head = obj;
	iter->cur_list_entry = 0;
	return obj;

obj_not_found:
	return 0;
}

uint64_t oidx_iter_prev_list(oidx_iter_t iter)
{
	oidx_layer_t l;
	uint64_t obj = 0;
	uint64_t next_off = 0;
	int icl; /* iter->cur_layer */
	int ili; /* iter->layer_idx[icl] */

	icl = iter->cur_layer; /* always synchronized with iter->cur_layer
       				* --> no need to re-initialize */
	if (icl < 0)
		goto obj_not_found;

layer_work:
	l = iter->layer[icl];
next_index:
	ili = --(iter->layer_idx[icl]);
	if (ili < 0) {
		/* level end, reset and go up one level. */
		iter->layer[icl] = NULL;
		iter->key[icl] = 0;
		iter->layer_idx[icl] = iter->oidx->udata->radix;
		icl = --(iter->cur_layer);
		if (icl < 0)
			goto obj_not_found;
		goto layer_work;
	}
	iter->key[icl] = ili;
	obj = l->entries[ili].obj;
	if (obj)
		goto obj_found;
	next_off = l->entries[ili].next;
	if (next_off) {
		/* go deeper */
		icl = ++(iter->cur_layer);
		iter->layer[icl] = ods_obj_offset_to_ptr(iter->oidx->ods,
								next_off);
		iter->layer_idx[icl] = iter->oidx->udata->radix;
		goto layer_work;
	}
	goto next_index;

obj_found:
	/* obj is now refering to the head of the list */
	iter->cur_list_head = obj;
	iter->cur_list_entry = 0;
	return obj;

obj_not_found:
	return 0;
}

/**
 * \brief (Internal) This function synchronizes the layers inside the \c iter.
 *
 * This will also synchronize other iterator's attributes related to object
 * iteration.
 *
 * \param iter The iterator.
 * \returns The object reference at the last layer.
 */
uint64_t oidx_iter_sync_key_layers(oidx_iter_t iter)
{
	struct oidx_layer_s *pl, *next_pl;
	unsigned char *ckey = iter->key;
	oidx_t oidx = iter->oidx;
	pl = iter->layer[0] = ods_obj_offset_to_ptr(oidx->ods,
						    oidx->udata->top);
	int count = 0;
	uint64_t obj = 0;
loop:
	next_pl = ods_obj_offset_to_ptr(oidx->ods, pl->entries[*ckey].next);
	if (next_pl) {
		iter->layer_idx[count] = *ckey++;
		iter->layer[++count] = next_pl;
		pl = next_pl;
		goto loop;
	}
	if (obj = pl->entries[*ckey].obj) {
		iter->layer_idx[count] = *ckey++;
	}
	iter->cur_layer = count;
	return obj;
}

void oidx_iter_seek_start(oidx_iter_t iter)
{
	oidx_t oidx = iter->oidx;
	memset(iter, 0, sizeof *iter);
	iter->oidx = oidx;
	struct oidx_layer_s *pl = ods_obj_offset_to_ptr(oidx->ods,
							oidx->udata->top);
	oidx_find_left_most(oidx, pl, iter->key);
	uint64_t obj = oidx_iter_sync_key_layers(iter);
	if (obj)
		iter->cur_list_head = obj;
}

void oidx_iter_seek_end(oidx_iter_t iter)
{
	oidx_t oidx = iter->oidx;
	memset(iter, 0, sizeof *iter);
	iter->oidx = oidx;
	struct oidx_layer_s *pl = ods_obj_offset_to_ptr(oidx->ods,
							oidx->udata->top);
	oidx_find_right_most(oidx, pl, iter->key);
	uint64_t obj = oidx_iter_sync_key_layers(iter);
	if (obj)
		iter->cur_list_head = obj;
}

uint64_t oidx_iter_seek(oidx_iter_t iter, oidx_key_t key, size_t keylen)
{
	uint64_t obj, nxt_off;
	int i;
	unsigned char *c_key = key;

	obj = oidx_find(iter->oidx, key, keylen);
	if (!obj)
		goto out;
	memcpy(iter->key, key, keylen);
	iter->keylen = keylen;
	iter->layer[0] = ods_obj_offset_to_ptr(iter->oidx->ods,
					       iter->oidx->udata->top);
	for (i = 0; i < keylen; i++) {
		nxt_off = iter->layer[i]->entries[c_key[i]].next;
		iter->layer_idx[i] = (int)c_key[i];
		if (i < keylen-1)
			iter->layer[i+1] =
				ods_obj_offset_to_ptr(iter->oidx->ods,
						      nxt_off);
	}
	iter->cur_layer = keylen-1;
	iter->cur_list_entry = 0;
	iter->cur_list_head = obj;
 out:
	return obj;
}

/**
 * Seek the iterator \a iter to the supremum (minimum upperbound) of the given
 * \a key.
 *
 * \param iter The pointer to oidx iterator.
 * \param key The pointer to the key.
 * \param keylen The length of the key.
 * \returns Pointer to object if the supremum is found.
 * \returns 0 if the supremum is not found.
 */
uint64_t oidx_iter_seek_sup(oidx_iter_t iter, oidx_key_t key, size_t keylen)
{
	uint64_t obj, nxt_off;
	int i;
	unsigned char c_key[keylen];

	obj = oidx_find_approx_sup(iter->oidx, key, c_key, keylen);
	if (!obj)
		goto out;
	memcpy(iter->key, c_key, keylen);
	iter->keylen = keylen;
	iter->layer[0] = ods_obj_offset_to_ptr(iter->oidx->ods,
					       iter->oidx->udata->top);
	for (i = 0; i < keylen; i++) {
		nxt_off = iter->layer[i]->entries[c_key[i]].next;
		iter->layer_idx[i] = (int)c_key[i];
		if (i < keylen-1)
			iter->layer[i+1] =
				ods_obj_offset_to_ptr(iter->oidx->ods,
						      nxt_off);
	}
	iter->cur_layer = keylen-1;
	// Now the iterator point approximately at the supremum, we'd just to make
	// sure that it actually point to the supremum.
	while (obj && oidx_key_cmp(iter->key, key, keylen) < 0) {
		obj = oidx_iter_next_list(iter);
	}
	iter->cur_list_head = obj;
	iter->cur_list_entry = 0;
	// It's OK to return 0, indicating that there are no infimum to the
	// given key.
 out:
	return obj;
}

uint64_t oidx_iter_seek_inf(oidx_iter_t iter, oidx_key_t key, size_t keylen)
{
	uint64_t obj, nxt_off;
	int i;
	unsigned char c_key[keylen];

	obj = oidx_find_approx_inf(iter->oidx, key, c_key, keylen);
	if (!obj)
		goto out;
	memcpy(iter->key, c_key, keylen);
	iter->keylen = keylen;
	iter->layer[0] = ods_obj_offset_to_ptr(iter->oidx->ods,
					       iter->oidx->udata->top);
	for (i = 0; i < keylen; i++) {
		nxt_off = iter->layer[i]->entries[c_key[i]].next;
		iter->layer_idx[i] = (int)c_key[i];
		if (i < keylen-1)
			iter->layer[i+1] =
				ods_obj_offset_to_ptr(iter->oidx->ods,
						      nxt_off);
	}
	iter->cur_layer = keylen-1;
	// Now the iterator point approximately at the infimum, we'd just to make
	// sure that it actually point to the infimum.
	while (obj && oidx_key_cmp(iter->key, key, keylen) > 0) {
		obj = oidx_iter_prev_list(iter);
	}
	iter->cur_list_head = obj;
	iter->cur_list_entry = 0;
	// It's OK to return 0, indicating that there are no infimum to the
	// given key.
 out:
	return obj;
}

#ifdef OIDX_MAIN
#include <stdio.h>
#include "oidx_priv.h"

struct walk_s {
	FILE *fp;
};

int obj_id_as_str(oidx_t oidx,
		  oidx_key_t key, size_t keylen,
		  uint64_t obj, void *context)
{
	static char str_buf[32];
	struct walk_s *w = context;
	unsigned char *cp = (unsigned char *)&obj;
	fprintf(w->fp,
		"%02x:%02x:%02x:%02x "
		"\"%02x %02x %02x %02x\"\n",
		((uint8_t*)key)[0],
		((uint8_t*)key)[1],
		((uint8_t*)key)[2],
		((uint8_t*)key)[3],
		cp[3], cp[2], cp[1], cp[0]);
	return 0;
}

int main(int argc, char *argv[]) {
	char buf[256];
	char *s;
	static char comp_name[12];
	int keylen;
	ssize_t cnt = 0;
	oidx_t oidx;
	static char comp_type[12];
	static char metric_name[12];
	uint32_t comp_id;
	uint64_t metric_value, secs, msecs;

	oidx = oidx_open(argv[1], O_RDWR | O_CREAT, 0660);
	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		char *o;
		sscanf(buf, "%[^,],%[^,],%d,%ld,%ld,%ld",
		       comp_type, metric_name,
		       &comp_id, &metric_value, &secs, &msecs);
		sprintf(comp_name, "%08x", comp_id);
		uint64_t obj = comp_id;
		comp_id = ntohl(comp_id);
		(void)oidx_add(oidx, (oidx_key_t)&comp_id, 4, obj);
	}
	struct walk_s walk;
	walk.fp = stdout;
	oidx_walk(oidx, obj_id_as_str, &walk);
	printf("%zuM\n", oidx_entries / 1024 / 1024);

	/* iterator test */
	oidx_iter_t iter = oidx_iter_new(oidx);
	uint64_t obj_off;
	printf("Iterator test.\n");
	while (obj_off = oidx_iter_next_obj(iter)) {
		char *c_id = (char *)&obj_off;
		fprintf(stdout, "%02hhx:%02hhx:%02hhx:%02hhx\n",
			c_id[3], c_id[2], c_id[1], c_id[0]);
	}

	/* seek test */
	printf("Seek to %08x and continue from there.\n", ntohl(comp_id));
	obj_off = oidx_iter_seek(iter, &comp_id, 4);

	TEST_ASSERT(obj_off, "Seek to known object.\n");
	while (obj_off = oidx_iter_next_obj(iter)) {
		char *c_id = (char *)&obj_off;
		fprintf(stdout, "%02hhx:%02hhx:%02hhx:%02hhx\n",
			c_id[3], c_id[2], c_id[1], c_id[0]);
	}

	/* reset start test */
	oidx_iter_seek_start(iter);
	printf("Reset the iterator at the start and iterate forward.\n");
	while (obj_off = oidx_iter_next_obj(iter)) {
		char *c_id = (char *)&obj_off;
		fprintf(stdout, "%02hhx:%02hhx:%02hhx:%02hhx\n",
			c_id[3], c_id[2], c_id[1], c_id[0]);
	}

	oidx_iter_seek_end(iter);
	printf("Reset the iterator at the end and iterate backward.\n");
	while (obj_off = oidx_iter_prev_obj(iter)) {
		char *c_id = (char *)&obj_off;
		fprintf(stdout, "%02hhx:%02hhx:%02hhx:%02hhx\n",
			c_id[3], c_id[2], c_id[1], c_id[0]);
	}

	/* Dump ODS before delete */
	printf("=== ODS BEFORE DELETE ===\n");
	ods_dump(oidx->ods, stdout);
	printf("=== END ODS BEFORE DELETE ===\n");

	/* Delete test  */
	oidx_iter_seek_start(iter);
	printf("Delete everything.\n");
	while (obj_off = oidx_iter_next_obj(iter)) {
		char *c_id = (char *)&obj_off;
		comp_id = htonl(obj_off);
		fprintf(stdout, "%02hhx:%02hhx:%02hhx:%02hhx\n",
			c_id[3], c_id[2], c_id[1], c_id[0]);
		uint64_t l = oidx_delete(oidx, &comp_id, 4);
		/* now delete the list */
		oidx_objref_head_t h = ods_obj_offset_to_ptr(oidx->ods, l);
		oidx_objref_entry_t e;
		while (e = OIDX_LIST_FIRST(oidx, h)) {
			OIDX_LIST_REMOVE(oidx, h, e);
			ods_free(oidx->ods, e);
		}
		ods_free(oidx->ods, h);
		/* after deleting an object, the iterator becomes invalid */
		oidx_iter_seek_start(iter);
	}

	printf("=== ODS AFTER DELTE ===\n");
	ods_dump(oidx->ods, stdout);
	printf("=== END ODS AFTER DELETE ===\n");
	/* reset start test ... there should be nothing left */
	oidx_iter_seek_start(iter);
	printf("Reset the iterator at the start make sure there's nothing left.\n");
	int c = 0;
	while (obj_off = oidx_iter_next_obj(iter)) {
		char *c_id = (char *)&obj_off;
		fprintf(stdout, "FAIL! %02hhx:%02hhx:%02hhx:%02hhx still in index\n",
			c_id[3], c_id[2], c_id[1], c_id[0]);
		c++;
	}
	TEST_ASSERT(c == 0, "number of object == 0\n");

	return 0;
}

#endif
