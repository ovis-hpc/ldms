/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
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

#include "ldms.h"
#include "ldms_private.h"
#include "ldms_grp.h"

struct ldms_set_hdr *__set_alloc(const char *instance_name,
				 ldms_schema_t schema,
				 uid_t uid, gid_t gid, mode_t perm);

int __child_idx(ldms_grp_node_t node, uint16_t child_id_le);

struct ldms_set *
__record_set(const char *instance_name,
	     struct ldms_set_hdr *sh, struct ldms_data_hdr *dh, int flags);

static void __grp_init(ldms_grp_t grp, uint16_t m_max)
{
	ldms_grp_priv_t priv = (void*)ldms_metric_get(&grp->set, 0);
	ldms_grp_rec_t rec;
	ldms_grp_node_t node;
	int i;
	/* init priv */
	priv = (void*)ldms_metric_get(&grp->set, 0);
	priv->zero[0] = priv->zero[1] = 0;
	priv->node_root = 0; /* empty b-tree */
	priv->rec_list = 0;   /* empty member list */
	priv->rec_free_list = htole16(1); /* refers to grp[1] */
	priv->node_free_list = htole16(1); /* refers to priv->node[1] */
	priv->m_max = htole16(m_max);
	/* priv->node[0] is a dummy. priv->node[1 .. m_max] are usable. */
	/* init rec and node */
	for (i = 1; i <= m_max; i++) {
		rec = (void*)ldms_metric_get(&grp->set, i);
		rec->id = htole16(i);
		rec->name[0] = 0;
		rec->prev = htole16(i - 1);
		rec->next = (i < m_max)?htole16(i + 1):(0);
		node = &priv->node[i];
		bzero(node, sizeof(*node));
		node->id = htole16(i);
		node->next = (i < m_max)?htole16(i + 1):(0);
	}
}

ldms_grp_t ldms_grp_new_with_auth(const char *name, int m_max,
				  uid_t uid, gid_t gid, mode_t perm)
{
	ldms_schema_t schema;
	int i, ret;
	char mname[8];
	size_t priv_sz;
	ldms_grp_t grp = NULL;
	struct ldms_set_hdr *meta;
	if (m_max > 65535) {
		errno = EOVERFLOW;
		goto err_0;
	}
	errno = ENOSYS;
	schema = ldms_schema_new(LDMS_GRP_SCHEMA);
	priv_sz = sizeof(struct ldms_grp_priv_s) +
		  ((m_max+1) * sizeof(struct ldms_grp_node_s));
	ret = ldms_schema_meta_array_add(schema, "priv", LDMS_V_CHAR_ARRAY,
					 "", priv_sz);
	if (ret < 0) {
		errno = -ret;
		goto err_1;
	}
	for (i = 0; i < m_max; i++) {
		snprintf(mname, sizeof(mname), "%u", i);
		ret = ldms_schema_meta_array_add(schema, mname,
			LDMS_V_CHAR_ARRAY, "", sizeof(struct ldms_grp_rec_s));
		if (ret < 0) {
			errno = -ret;
			goto err_1;
		}
	}
	meta = __set_alloc(name, schema, uid, gid, perm);
	if (!meta)
		goto err_1;
	__ldms_set_tree_lock();
	grp = (void*)__record_set(name, meta, (void*)meta + meta->meta_sz,
				  LDMS_SET_F_GROUP|LDMS_SET_F_LOCAL);
	__ldms_set_tree_unlock();
	if (!grp)
		goto err_1;
	grp->set.flags |= LDMS_SET_F_GROUP;
	__grp_init(grp, m_max);
	return grp;

 err_1:
	ldms_schema_delete(schema);
 err_0:
	return NULL;
}

ldms_grp_t ldms_grp_new(const char *name, int m_max)
{
	uid_t uid = geteuid();
	gid_t gid = getegid();
	return ldms_grp_new_with_auth(name, m_max, uid, gid, 0644);
}

void ldms_grp_delete(ldms_grp_t grp)
{
	ldms_set_delete(&grp->set);
}

static ldms_grp_rec_t __grp_find(ldms_grp_t grp, const char *key,
		      ldms_grp_node_t *node, int *idx)
{
	int cmp;
	int i = -1;
	ldms_grp_priv_t priv = LDMS_GRP_PRIV(grp);
	ldms_grp_node_t last_n = NULL;
	ldms_grp_node_t n = LDMS_GRP_NODE(grp, le16toh(priv->node_root));
	ldms_grp_rec_t rec;
	while (n) {
		for (i = 0; i < le16toh(n->n); i++) {
			rec = LDMS_GRP_REC(grp, le16toh(n->ent[i].rec_id));
			cmp = strcmp(key, rec->name);
			if (cmp == 0) { /* found */
				last_n = n;
				goto out;
			}
			if (cmp < 0) {
				/* go to the left child */
				goto next;
			}
		}
		/* i == n, go to the right child */
	next:
		last_n = n;
		n = LDMS_GRP_NODE(grp, le16toh(n->ent[i].child));
	}
	/* not found */
	rec = NULL;
 out:
	/* also record the node and idx for ins */
	if (node)
		*node = last_n;
	if (idx)
		*idx = i;
	return rec;
}

static ldms_grp_rec_t __rec_alloc(ldms_grp_t grp)
{
	ldms_grp_priv_t priv = LDMS_GRP_PRIV(grp);
	ldms_grp_rec_t rec = LDMS_GRP_REC(grp, le16toh(priv->rec_free_list));
	ldms_grp_rec_t next_rec;
	if (rec) {
		/* rec_free_list: remove first */
		priv->rec_free_list = rec->next; /* already a little-endian */
		next_rec = LDMS_GRP_REC(grp, le16toh(rec->next));
		if (next_rec)
			next_rec->prev = 0;
	} else {
		errno = ENOMEM;
	}
	return rec;
}

static void __rec_free(ldms_grp_t grp, ldms_grp_rec_t rec)
{
	ldms_grp_priv_t priv = LDMS_GRP_PRIV(grp);
	ldms_grp_rec_t _rec = LDMS_GRP_REC(grp, le16toh(priv->rec_free_list));
	rec->name[0] = 0;
	rec->prev = 0;
	rec->next = priv->rec_free_list; /* already a little-endian */
	if (_rec)
		_rec->prev = rec->id;    /* already a little-endian */
	priv->rec_free_list = rec->id;   /* already a little-endian */
}

static ldms_grp_node_t __node_alloc(ldms_grp_t grp)
{
	ldms_grp_priv_t priv = LDMS_GRP_PRIV(grp);
	ldms_grp_node_t node = LDMS_GRP_NODE(grp, le16toh(priv->node_free_list));
	if (node) {
		/* rec_free_list: remove first */
		priv->node_free_list = node->next; /* already a little-endian */
		node->n = 0;
		node->is_leaf = 0;
		node->parent = 0;
	} else {
		errno = ENOMEM;
	}
	return node;
}

static void __node_free(ldms_grp_t grp, ldms_grp_node_t node)
{
	ldms_grp_priv_t priv = LDMS_GRP_PRIV(grp);
	node->next = priv->node_free_list; /* already a little-endian */
	priv->node_free_list = node->id;   /* already a little-endian */
}

/* returns rec_id in little-endian format */
uint16_t __node_ent_pred(ldms_grp_t grp, ldms_grp_node_t node, int i,
		    ldms_grp_node_t *ret_node, int *idx)
{
	ldms_grp_node_t p, n;
	int ci;
	n = LDMS_GRP_NODE(grp, le16toh(node->ent[i].child));
	if (n) {
		/* find right most entry in the right most leaf of the subtree */
		while (!n->is_leaf) {
			n = LDMS_GRP_NODE(grp, le16toh(n->ent[n->n].child));
		}
		if (ret_node)
			*ret_node = n;
		if (idx)
			*idx = n->n-1;
		return n->ent[n->n-1].rec_id;
	}
	/* no child .. go to sibling entry in the node */
	if (i) {
		if (ret_node)
			*ret_node = node;
		if (idx)
			*idx = i-1;
		return node->ent[i-1].rec_id;
	}
	/* otherwise, go to parent */
	n = node;
	p = LDMS_GRP_NODE(grp, le16toh(n->parent));
	while (p) {
		ci = __child_idx(p, n->id);
		assert(ci >= 0);
		if (ci) {
			if (ret_node)
				*ret_node = p;
			if (idx)
				*idx = ci-1;
			return p->ent[ci-1].rec_id;
		}
		n = p;
		p = LDMS_GRP_NODE(grp, le16toh(n->parent));
	}
	return 0;
}

/* returns rec_id in little-endian format */
uint16_t __node_ent_succ(ldms_grp_t grp, ldms_grp_node_t node, int i,
		    ldms_grp_node_t *ret_node, int *idx)
{
	ldms_grp_node_t p, n;
	int ci;
	n = LDMS_GRP_NODE(grp, le16toh(node->ent[i+1].child));
	if (n) {
		/* find left most entry in the left most leaf of the subtree */
		while (!n->is_leaf) {
			n = LDMS_GRP_NODE(grp, le16toh(n->ent[0].child));
		}
		if (ret_node)
			*ret_node = n;
		if (idx)
			*idx = 0;
		return n->ent[0].rec_id;
	}
	/* no child .. go to sibling entry in the node */
	if (i+1 < node->n) {
		if (ret_node)
			*ret_node = node;
		if (idx)
			*idx = i+1;
		return node->ent[i+1].rec_id;
	}
	/* otherwise, go to parent */
	n = node;
	p = LDMS_GRP_NODE(grp, le16toh(n->parent));
	while (p) {
		ci = __child_idx(p, n->id);
		assert(ci >= 0);
		if (ci + 1 < p->n) {
			if (ret_node)
				*ret_node = p;
			if (idx)
				*idx = ci+1;
			return p->ent[ci+1].rec_id;
		}
		n = p;
		p = LDMS_GRP_NODE(grp, le16toh(n->parent));
	}
	return 0;
}

int __child_idx(ldms_grp_node_t node, uint16_t child_id_le)
{
	int i, n;
	n = le16toh(node->n);
	for (i = 0; i < n+1; i++) {
		if (node->ent[i].child == child_id_le)
			return i;
	}
	return -1;
}

int ldms_grp_ins(ldms_grp_t grp, const char *member)
{
	int i, idx, rc;
	ldms_grp_priv_t priv = LDMS_GRP_PRIV(grp);
	ldms_grp_node_t child, node, node2, left, right;
	ldms_grp_rec_t rec, rec_prev, rec_next;
	const static uint16_t half = LDMS_GRP_NCHILD/2;

	if (grp->set.flags & LDMS_SET_F_REMOTE)
		return EINVAL; /* remote peer created and owned the group */

	if (strlen(member) > LDMS_GRP_NAME_MAX)
		return ENAMETOOLONG;
	rec = __grp_find(grp, member, &node, &idx);
	if (rec) {
		rc = EEXIST;
		goto err_0;
	}

	rec = __rec_alloc(grp);
	if (!rec) {
		rc = ENOMEM;
		goto err_0;
	}
	snprintf(rec->name, sizeof(rec->name), "%s", member);
	assert(!node || node->is_leaf);
	left = NULL;
	right = NULL;
	/* Insert rec chain first */
	if (!node) {
		/* we're the first */
		rec_prev = NULL;
		rec_next = NULL;
	} else if (idx) {
		/* idx > 0, prev rec [idx-1] is guaranteed to be available */
		rec_prev = LDMS_GRP_REC(grp, le16toh(node->ent[idx-1].rec_id));
		assert(rec_prev);
		rec_next = LDMS_GRP_REC(grp, le16toh(rec_prev->next));
	} else {
		/* will insert at idx==0, ent[0] is our next rec */
		rec_next = LDMS_GRP_REC(grp, le16toh(node->ent[0].rec_id));
		assert(rec_next);
		rec_prev = LDMS_GRP_REC(grp, le16toh(rec_next->prev));
	}
	if (rec_prev) {
		rec->prev = rec_prev->id; /* LE */
		rec_prev->next = rec->id; /* LE */
	} else {
		/* we're the first in the list */
		rec->prev = 0;
		priv->rec_list = rec->id; /* LE */
	}
	if (rec_next) {
		rec->next = rec_next->id; /* LE */
		rec_next->prev = rec->id; /* LE */
	} else {
		rec->next = 0;
	}
	/* Then rebalance the tree */
 ins:
	/* inserting rec into node at idx with left and right child of rec */
	/* each node holds NCHILD-1 entries */
	if (!node) {
		/* need to create a new root -- also ignore idx */
		node = __node_alloc(grp);
		if (!node) {
			rc = ENOMEM;
			assert(0);
			goto err_1;
		}
		idx = 0;
		node->parent = 0;
		node->n = 1; /* 1-byte */
		node->ent[0].rec_id = rec->id; /* LE */
		if (left) {
			assert(right);
			node->is_leaf = 0;
			left->parent = node->id;    /* LE */
			right->parent = node->id;   /* LE */
			node->ent[0].child = left->id;   /* LE */
			node->ent[1].child = right->id; /* LE */
			node->ent[1].rec_id = 0;
		} else {
			assert(!right);
			node->is_leaf = 1;
			node->ent[0].child = 0;   /* LE */
			node->ent[1].child = 0;   /* LE */
			node->ent[1].rec_id = 0;
		}
		priv->node_root = node->id; /* LE */
		goto out;
	}
	assert(node->n >= half || node->id == priv->node_root);
	if (node->n + 1 < LDMS_GRP_NCHILD) {
		/* vacant */
		memmove(&node->ent[idx+1], &node->ent[idx],
			sizeof(node->ent[0])*(node->n+1-idx));
		node->ent[idx].rec_id = rec->id; /* LE */
		node->ent[idx].child = left?left->id:0; /* LE */
		node->ent[idx+1].child = right?right->id:0; /* LE */
		node->n += 1; /* char is both LE and BE */
		goto out;
	}
	/* else, need node splitting */
	node2 = __node_alloc(grp);
	if (!node2) {
		rc = ENOMEM;
		assert(0); /* this must not happen as len(nodes) == len(recs) */
		goto err_1;
	}
	node2->is_leaf = node->is_leaf;
	node2->parent = node->parent;
	node2->n = node->n = half;
	if (idx == half) {
		/* rec will be inserted into the parent */
		memcpy(node2->ent, &node->ent[half], (half+1)*sizeof(node->ent[0]));
		node->ent[half].rec_id = 0;
		if (node->is_leaf) {
			assert(!left && !right);
			node->ent[half+1].child = 0;
			node2->ent[0].child = 0;
		} else {
			assert(left && right);
			node->ent[half+1].child = left->id;
			left->parent = node->id;
			node2->ent[0].child = right->id;
			right->parent = node2->id;
		}
	} else if (idx < half) {
		/* rec insert in node */
		memcpy(node2->ent, &node->ent[half], (half+1)*sizeof(node->ent[0]));
		memmove(&node->ent[idx+1], &node->ent[idx], (half-idx)*sizeof(node->ent[0]));
		node->ent[idx].rec_id = rec->id;
		if (node->is_leaf) {
			assert(!left && !right);
			node->ent[idx].child = 0;
			node->ent[idx+1].child = 0;
		} else {
			assert(left && right);
			node->ent[idx].child = left->id;
			left->parent = node->id;
			node->ent[idx+1].child = right->id;
			right->parent = node->id;
		}
		/* update rec to insert into parent */
		rec = LDMS_GRP_REC(grp, le16toh(node->ent[half].rec_id));
		node->ent[half].rec_id = 0;
	} else {
		/* rec insert in node2 */
		/* node->ent[half].rec is the new rec to insert to parent */
		idx -= half + 1;
		if (idx)
			memcpy(node2->ent, &node->ent[half+1], (idx)*sizeof(node->ent[0]));
		node2->ent[idx].rec_id = rec->id;
		memcpy(&node2->ent[idx+1], &node->ent[idx+half+1], (half-idx)*sizeof(node->ent[0]));
		if (node2->is_leaf) {
			assert(!right);
			node2->ent[idx].child = 0;
			node2->ent[idx+1].child = 0;
		} else {
			assert(left && right);
			node2->ent[idx].child = left->id;
			left->parent = node2->id;
			node2->ent[idx+1].child = right->id;
			right->parent = node2->id;
		}
		/* update rec to insert into parent */
		rec = LDMS_GRP_REC(grp, le16toh(node->ent[half].rec_id));
		node->ent[half].rec_id = 0;
	}
	/* node2 is a new node, need to update all children */
	for (i = 0; i <= node2->n; i++) {
		child = LDMS_GRP_NODE(grp, le16toh(node2->ent[i].child));
		if (child)
			child->parent = node2->id;
	}
	left = node;
	right = node2;
	node = LDMS_GRP_NODE(grp, le16toh(node->parent));
	if (node) {
		idx = __child_idx(node, left->id);
		assert(idx >= 0);
	} else {
		idx = -1;
	}
	goto ins;

 out:
	ldms_metric_modify(&grp->set, 0); /* increment meta_gn */
	return 0;
 err_1:
	__rec_free(grp, rec);
 err_0:
	return rc;
}

/* merge parent->ent[ci+1].child into parent->ent[ci].child */
static inline
void __merge_child(ldms_grp_t grp, ldms_grp_node_t parent, int ci)
{
	ldms_grp_node_t child;
	int i;
	ldms_grp_node_t node = LDMS_GRP_NODE(grp, parent->ent[ci].child);
	ldms_grp_node_t right = LDMS_GRP_NODE(grp, parent->ent[ci+1].child);
	node->ent[node->n].rec_id = parent->ent[ci].rec_id;
	/* all children of the right sibling become children of node */
	if (!node->is_leaf) {
		for (i = 0; i <= right->n; i++) {
			child = LDMS_GRP_NODE(grp, le16toh(right->ent[i].child));
			child->parent = node->id; /* LE */
		}
	}
	memcpy(&node->ent[node->n+1], &right->ent[0], (right->n+1)*sizeof(node->ent[0]));
	node->n += 1 + right->n;
	memmove(&parent->ent[ci], &parent->ent[ci+1], sizeof(parent->ent[0])*(parent->n - ci));
	parent->ent[ci].child = node->id;
	parent->n--;
	__node_free(grp, right);
	/* parent may become empty. In such case, parent->ent[0].child is he
	 * merged child. */
}

int ldms_grp_rm(ldms_grp_t grp, const char *member)
{
	int idx, rc, leaf_idx, ci;
	uint16_t rec_id_le;
	ldms_grp_priv_t priv = LDMS_GRP_PRIV(grp);
	ldms_grp_node_t child, node, parent, left, right, leaf;
	ldms_grp_rec_t rec, rec_prev, rec_next;
	const static uint16_t half = LDMS_GRP_NCHILD/2;

	if (grp->set.flags & LDMS_SET_F_REMOTE)
		return EINVAL; /* remote peer created and owned the group */

	if (strlen(member) > LDMS_GRP_NAME_MAX)
		return ENAMETOOLONG;
	rec = __grp_find(grp, member, &node, &idx);
	if (!rec) {
		rc = ENOENT;
		goto err_0;
	}
	/* take care of rec list first */
	rec_prev = LDMS_GRP_REC(grp, le16toh(rec->prev));
	rec_next = LDMS_GRP_REC(grp, le16toh(rec->next));
	if (rec_prev)
		rec_prev->next = rec->next;
	else /* rec is the first element */
		priv->rec_list = rec->next;
	if (rec_next)
		rec_next->prev = rec->prev;
	__rec_free(grp, rec);
	rec = NULL;
	if (node->is_leaf)
		goto remove;
	/* internal node -- need to swap a spot with a leaf */
	rec_id_le = __node_ent_succ(grp, node, idx, &leaf, &leaf_idx);
	assert(rec_id_le);
	assert(leaf->is_leaf);
	node->ent[idx].rec_id = rec_id_le;
	leaf->ent[leaf_idx].rec_id = 0; /* mark as empty */
	node = leaf;
	idx = leaf_idx;

 remove: /* remove leaf->ent[idx] */
	memmove(&node->ent[idx], &node->ent[idx+1], sizeof(node->ent[0])*(node->n - idx));
	node->n--;

 rebalance: /* rebalance from the node upward */
	if (!node->parent) {
		/* root */
		if (!node->n) {
			/* root empty -- root->ent[0].child is the new root */
			priv->node_root = node->ent[0].child;
			__node_free(grp, node);
			if (priv->node_root)
				LDMS_GRP_NODE(grp, le16toh(priv->node_root))->parent = 0;
		}
		goto out;
	}
	if (node->n >= half) /* good */
		goto out;
	parent = LDMS_GRP_NODE(grp, le16toh(node->parent));
	ci = __child_idx(parent, node->id);
	assert(ci >= 0);
	/* need to borrow an entry from a sibling or merge */
	left = (ci>0)?LDMS_GRP_NODE(grp, parent->ent[ci-1].child):NULL;
	if (left && left->n > half) {
		/* borrow from the left sibling */
		memmove(&node->ent[1], &node->ent[0], sizeof(node->ent[0])*(node->n+1));
		node->ent[0].rec_id = parent->ent[ci-1].rec_id;
		parent->ent[ci-1].rec_id = left->ent[left->n-1].rec_id;
		left->ent[left->n-1].rec_id = 0; /* mark empty */
		node->ent[0].child = left->ent[left->n].child;
		left->ent[left->n].child = 0; /* mark empty */
		left->n--;
		node->n++;
		/* update the child that changes parent */
		child = LDMS_GRP_NODE(grp, le16toh(node->ent[0].child));
		if (!node->is_leaf)
			child->parent = node->id;
		goto out;
	}
	right = (ci<parent->n)?LDMS_GRP_NODE(grp, parent->ent[ci+1].child):NULL;
	if (right && right->n > half) {
		/* borrow from the right sibling */
		node->ent[node->n].rec_id = parent->ent[ci].rec_id;
		parent->ent[ci].rec_id = right->ent[0].rec_id;
		node->ent[node->n+1].child = right->ent[0].child;
		memmove(&right->ent[0], &right->ent[1], sizeof(node->ent[0])*right->n);
		right->ent[right->n].child = 0; /* mark empty */
		right->n--;
		node->n++;
		/* update the child that changes parent */
		child = LDMS_GRP_NODE(grp, le16toh(node->ent[node->n].child));
		if (!node->is_leaf)
			child->parent = node->id;
		goto out;
	}
	/* cannot borrow, need a merge */
	if (right) {
		/* merge the right into node */
		__merge_child(grp, parent, ci);
		node = parent;
		goto rebalance;
	}
	if (left) {
		/* merge node into the left */
		__merge_child(grp, parent, ci-1);
		node = parent;
		goto rebalance;
	}
	/* must not reach here as non-root nodes must have a sibling. */
	assert(0 == "non-root w/o siblings");

 out:
	ldms_metric_modify(&grp->set, 0); /* increment meta_gn */
	return 0;

 err_0:
	return rc;
}

static void __grp_rm_nodes(ldms_grp_t grp, ldms_grp_node_t node)
{
	ldms_grp_node_t child;
	int i;
	if (!node->is_leaf) {
		for (i = 0; i <= node->n; i++) {
			child = LDMS_GRP_NODE(grp, le16toh(node->ent[i].child));
			__grp_rm_nodes(grp, child);
			node->ent[i].child = 0;
			node->ent[i].rec_id = 0;
		}
	}
	__node_free(grp, node);
}

void ldms_grp_rm_all(ldms_grp_t grp)
{
	ldms_grp_priv_t priv = (void*)ldms_metric_get(&grp->set, 0);
	ldms_grp_rec_t rec;
	ldms_grp_node_t node;
	/* remove all rec */
	while ((rec = LDMS_GRP_REC(grp, le16toh(priv->rec_list)))) {
		priv->rec_list = rec->next; /* little-endian */
		__rec_free(grp, rec);
	}
	/* remove all nodes */
	node = LDMS_GRP_NODE(grp, le16toh(priv->node_root));
	if (node) {
		__grp_rm_nodes(grp, node);
		priv->node_root = 0;
	}
}

int ldms_is_grp(ldms_set_t set)
{
	return 0 != (set->flags & LDMS_SET_F_GROUP);
}

ldms_grp_rec_t ldms_grp_first(ldms_grp_t grp)
{
	if (!ldms_set_is_consistent(&grp->set)) {
		errno = EBUSY;
		return NULL;
	}
	ldms_grp_priv_t priv = (void*)ldms_metric_get(&grp->set, 0);
	return LDMS_GRP_REC(grp, le16toh(priv->rec_list));
}

ldms_grp_rec_t ldms_grp_next(ldms_grp_t grp, ldms_grp_rec_t rec)
{
	if (!ldms_set_is_consistent(&grp->set)) {
		errno = EBUSY;
		return NULL;
	}
	return LDMS_GRP_REC(grp, le16toh(rec->next));
}

typedef void (*__traverse_cb_fn)(ldms_grp_t grp, ldms_grp_rec_t rec, void *_ctxt);

void __traverse(ldms_grp_t grp, ldms_grp_node_t node,
		__traverse_cb_fn cb, void *ctxt)
{
	int i;
	ldms_grp_node_t child;
	ldms_grp_rec_t rec;
	assert(node->n);
	for (i = 0; i < node->n; i++) {
		child = LDMS_GRP_NODE(grp, le16toh(node->ent[i].child));
		if (child)
			__traverse(grp, child, cb, ctxt);
		rec = LDMS_GRP_REC(grp, le16toh(node->ent[i].rec_id));
		assert(rec);
		cb(grp, rec, ctxt);
	}
	/* the right-most child */
	child = LDMS_GRP_NODE(grp, le16toh(node->ent[node->n].child));
	if (child)
		__traverse(grp, child, cb, ctxt);
}

struct __subtree_stat_s {
	int height;
	int depth; /* for printing */
	ldms_grp_rec_t min_rec;
	ldms_grp_rec_t max_rec;
};
typedef struct __subtree_stat_s *__subtree_stat_t;

int __verify_error(int err, const char *func, int line)
{
	/* wrap this in a function so that the error is easily caught in gdb */
	printf("ERROR: %s() line %d: %s(%d)\n", func, line, ovis_errno_abbvr(err), err);
	return err;
}

#define LDMS_GRP_DEBUG
#ifdef LDMS_GRP_DEBUG
#define VERIFY_ERROR(E) __verify_error(E, __func__, __LINE__)
#else
#define VERIFY_ERROR(E) (E)
#endif

#define DASHES "-----------------------------------------------------"

int __verify_leaf(ldms_grp_t grp, ldms_grp_node_t node, __subtree_stat_t ret)
{
	int i;
	ldms_grp_rec_t rec;
	ldms_grp_rec_t prev_rec;
	prev_rec = NULL;
	printf("%.*s %s %d (parent: %d)\n", ret->depth + 1, DASHES, "BEGIN",
					    node->id, node->parent);
	for (i = 0; i < node->n; i++) {
		if (node->ent[i].child)
			return VERIFY_ERROR(EINVAL);
		rec = LDMS_GRP_REC(grp, le16toh(node->ent[i].rec_id));
		printf("%.*s %s\n", ret->depth + 1, DASHES, rec->name);
		if (i == 0)
			ret->min_rec = rec;
		else if (strcmp(prev_rec->name, rec->name) >= 0)
			return VERIFY_ERROR(EINVAL);
		prev_rec = rec;
	}
	printf("%.*s %s %d\n", ret->depth + 1, DASHES, "END", node->id);
	ret->max_rec = rec;
	ret->height = 0;
	return 0;
}

int __verify_node(ldms_grp_t grp, ldms_grp_node_t node, __subtree_stat_t ret)
{
	int rc, i, height;
	struct __subtree_stat_s s = {.depth = ret->depth+1};
	ldms_grp_node_t child;
	ldms_grp_rec_t rec;
	ldms_grp_rec_t prev_rec;

	if (node->n > LDMS_GRP_NCHILD - 1)
		return VERIFY_ERROR(EINVAL);
	if (node->parent && node->n < LDMS_GRP_NCHILD/2)
		return VERIFY_ERROR(EINVAL);
	if (!node->n) /* empty node */
		return VERIFY_ERROR(EINVAL);
	if (node->is_leaf)
		return __verify_leaf(grp, node, ret);
	prev_rec = NULL;
	printf("%.*s %s %d (parent: %d)\n", ret->depth + 1, DASHES, "BEGIN",
					    node->id, node->parent);
	for (i = 0; i <= node->n; i++) {
		child = LDMS_GRP_NODE(grp, le16toh(node->ent[i].child));
		rec = LDMS_GRP_REC(grp, le16toh(node->ent[i].rec_id));
		/* node->ent[node->n] has no record */
		if (i < node->n && !rec)
			return VERIFY_ERROR(EINVAL);
		if (i == node->n && rec)
			return VERIFY_ERROR(EINVAL);
		if (!child)
			return VERIFY_ERROR(EINVAL);
		if (child->parent != node->id)
			return VERIFY_ERROR(EINVAL);
		rc = __verify_node(grp, child, &s);
		printf("%.*s %s\n", ret->depth+1, DASHES, rec->name);
		if (rc)
			return rc;
		if (rec && strcmp(s.max_rec->name, rec->name) >= 0)
			return VERIFY_ERROR(EINVAL);
		if (prev_rec && strcmp(prev_rec->name, s.min_rec->name) >= 0)
			return VERIFY_ERROR(EINVAL);
		if (i == 0) {
			height = s.height;
			ret->height = s.height;
			ret->min_rec = s.min_rec;
		}
		if (i == node->n) {
			ret->max_rec = s.max_rec;
		}
		if (s.height != height)
			return VERIFY_ERROR(EINVAL);
	}
	printf("%.*s %s %d\n", ret->depth + 1, DASHES, "END", node->id);
	return 0; /* verified */
}

struct __verify_ctxt_s {
	int rc;
	ldms_grp_rec_t rec; /* expected record */
};

void __verify_rec_cb(ldms_grp_t grp, ldms_grp_rec_t rec, void *_ctxt)
{
	struct __verify_ctxt_s *ctxt = _ctxt;
	if (ctxt->rc)
		return;
	if (rec != ctxt->rec) {
		ctxt->rc = VERIFY_ERROR(EINVAL);
		return;
	}
	ctxt->rec = LDMS_GRP_REC(grp, le16toh(ctxt->rec->next));
}

int ldms_grp_verify(ldms_grp_t grp)
{
	ldms_grp_priv_t priv = LDMS_GRP_PRIV(grp);
	ldms_grp_node_t root = LDMS_GRP_NODE(grp, le16toh(priv->node_root));
	struct __verify_ctxt_s ctxt;
	struct __subtree_stat_s s = {.depth = 0};
	int rc;
	if (root) {
		rc = __verify_node(grp, root, &s);
		if (rc)
			return rc;
	}
	ctxt.rc = 0;
	ctxt.rec = ldms_grp_first(grp);
	if (root)
		__traverse(grp, root, __verify_rec_cb, &ctxt);
	return ctxt.rc;
}

void __grp_update_completed(void *arg)
{
	ldms_grp_t grp = arg;
	struct ldms_grp_ev_s gev = {
		.type = LDMS_GRP_EV_FINALIZE,
		.ctxt = grp->cb_arg,
	};
	grp->grp_cb(grp, &gev);
	__sync_bool_compare_and_swap(&grp->busy, 1, 0);
}

static void __grp_update_cb(ldms_t t, ldms_set_t s, int flags, void *arg)
{
	ldms_grp_t grp = arg;
	if (grp == (void*)s) {
		ref_put(&grp->upd_ref, "ldms_grp_update");
		return;
	}
	struct ldms_grp_ev_s gev = {
		.type = LDMS_GRP_EV_UPDATED,
		.ctxt = grp->cb_arg,
		.entupd = {
			.flags = flags,
			.set = s,
			.xprt = t,
		},
	};
	grp->grp_cb(grp, &gev);
	if ((flags & LDMS_UPD_F_MORE) == 0)
		ref_put(&grp->upd_ref, ldms_set_name_get(s));
}

int ldms_grp_update(ldms_grp_t grp, ldms_grp_cb_t cb, void *arg)
{
	ldms_grp_rec_t rec;
	ldms_set_t _set;
	int rc;
	struct ldms_grp_ev_s gev = {.ctxt = arg};
	if (!__sync_bool_compare_and_swap(&grp->busy, 0, 1))
		return EBUSY;
	ref_init(&grp->upd_ref, "ldms_grp_update", __grp_update_completed, grp);
	grp->grp_cb = cb;
	grp->cb_arg = arg;
	if (ldms_set_is_consistent(&grp->set)) {
		/* If the group is not consistent the data may be
		 * incomplete and not iterable. */

		/* Update the members (with same callback and arg) */
		LDMS_GRP_FOREACH(rec, grp) {
			_set = ldms_set_by_name(rec->name);
			if (!_set) {
				/* member not yet existed. call the app callback
				 * to let the application decide what to do with
				 * it. */
				gev.type = LDMS_GRP_EV_NOENT;
				gev.noent.name = rec->name;
				grp->grp_cb(grp, &gev);
				continue;
			}
			ref_get(&grp->upd_ref, ldms_set_name_get(_set));
			rc = ldms_xprt_update(_set, __grp_update_cb, grp);
			if (rc)
				ref_put(&grp->upd_ref, ldms_set_name_get(_set));
		}
	}
	/* then, update the group itself last */
	if (grp->set.flags & LDMS_SET_F_REMOTE) {
		/* the ref will be put down in the callback */
		rc = __ldms_remote_update(grp->set.xprt, &grp->set,
					  __grp_update_cb, grp);
		if (rc)
			ref_put(&grp->upd_ref, "ldms_grp_update");
	} else {
		/* no need to update for local grp */
		ref_put(&grp->upd_ref, "ldms_grp_update");
	}
	return 0;
}
