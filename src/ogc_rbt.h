/*
 * Copyright (c) 2008 Open Grid Computing, Inc.
 * All rights reserved.
 *
 * Confidential and Proprietary
 */
#ifndef _OGC_RBT_T
#define _OGC_RBT_T

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Red/Black tree implementation. See
 * http://www.nist.gov/dads/HTML/redblack.html for a definition of
 * this algorithm.
 */

#define OGC_RBN_RED      0
#define OGC_RBN_BLACK    1

/* Red/Black Node */
struct ogc_rbn {
    struct ogc_rbn       *left;
    struct ogc_rbn       *right;
    struct ogc_rbn       *parent;
    int                 color;
    void                *key;
};

/* Comparator callback provided for insert and search operations */
typedef int (*ogc_rbn_comparator_t)(void *, void *);

/* Processor for each node during traversal. */
typedef void (*ogc_rbn_node_fn)(struct ogc_rbn *, void *, int);

struct ogc_rbt {
    struct ogc_rbn       *root;
    ogc_rbn_comparator_t comparator;
};

void ogc_rbt_init(struct ogc_rbt *t, ogc_rbn_comparator_t c);
struct ogc_rbn *ogc_rbt_find(struct ogc_rbt *t, void *k);
struct ogc_rbn *ogc_rbt_find_min(struct ogc_rbt *t);
struct ogc_rbn *ogc_rbt_find_max(struct ogc_rbt *t);
void ogc_rbt_ins(struct ogc_rbt *t, struct ogc_rbn *n);
void ogc_rbt_del(struct ogc_rbt *t, struct ogc_rbn *n);
void ogc_rbt_traverse(struct ogc_rbt *t, ogc_rbn_node_fn f, void *fn_data);
int ogc_rbt_is_leaf(struct ogc_rbn *n);
#define ogc_offsetof(type,member) ((size_t) &((type *)0)->member)
#define ogc_container_of(ptr, type, member) ({ \
	const __typeof__(((type *)0)->member ) *__mptr = (ptr); \
	(type *)((char *)__mptr - ogc_offsetof(type,member));})

#ifdef __cplusplus
}
#endif

#endif
