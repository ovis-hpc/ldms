/* -*- c-basic-offset: 8 -*-
 * See COPYING at the top of the source tree for the license
*/

#include "ldmsd.h"

/*
 * The row cache implements a "2-D" cache of rows. The 1st is the group.
 * A good example is the netdev metric set. In this set, there is a list
 * of records, each record contains metrics for a network interface. In
 * this case the group is looked-up by the component-id (node), and
 * device-name (interface). It can be configured in the configuraiton file
 * to be any combination of metric values, but for this example, the json
 * is:
 *
 *   "group" : { ..., "index" : [ "component_id", "name" ], ... }
 *
 * Each group is itself a tree of rows. This tree is ordered by the "order"
 * key in the "group" dictionary. This tree is maintained with a maximum
 * number of entries that is limited by the "limit" keyword. In this
 * example the json is:
 *
 *   "group" : { ..., "limit" : 2, "order" : [ "timestamp" ] ... }
 *
 * When the limit is reached, the row with the min key in the tree is
 * removed and the new row added.
 *
 * Putting all together, the json is as follows:
 *
 *   "group" : { "index" : [ "component_id", "name" ],
 *               "order" : [ "timstamp" ], "limit" : 2 }
 */

/*
 * The same comparator can be used for both groups and rows
*/
static int tree_comparator(void *a, const void *b)
{
	int i;
	ldmsd_row_cache_idx_t key_a = (ldmsd_row_cache_idx_t)a;
	ldmsd_row_cache_idx_t key_b = (ldmsd_row_cache_idx_t)b;
	ldmsd_row_cache_key_t rowk_a, rowk_b;
	for (i = 0; i < key_a->key_count; i++) {
		rowk_a = &key_a->keys[i];
		rowk_b = &key_b->keys[i];
		switch (rowk_a->type) {
		case LDMS_V_TIMESTAMP:
			if (rowk_a->val.v_ts.sec < rowk_b->val.v_ts.sec)
				return -1;
			if (rowk_a->val.v_ts.sec > rowk_b->val.v_ts.sec)
				return 1;
			if (rowk_a->val.v_ts.usec < rowk_b->val.v_ts.usec)
				return -1;
			if (rowk_a->val.v_ts.usec > rowk_b->val.v_ts.usec)
				return 1;
			return 0;
		case LDMS_V_CHAR_ARRAY:
			return strncmp(rowk_a->val.a_char, rowk_b->val.a_char,
				       rowk_a->count);
		case LDMS_V_CHAR:
			if (rowk_a->val.v_char == rowk_b->val.v_char)
				continue;
			if (rowk_a->val.v_char > rowk_b->val.v_char)
				return 1;
			return -1;
		case LDMS_V_U8:
			if (rowk_a->val.v_u8 == rowk_b->val.v_u8)
				continue;
			if (rowk_a->val.v_u8 > rowk_b->val.v_u8)
				return 1;
			return -1;
		case LDMS_V_S8:
			if (rowk_a->val.v_s8 == rowk_b->val.v_s8)
				continue;
			if (rowk_a->val.v_s8 > rowk_b->val.v_s8)
				return 1;
			return -1;
		case LDMS_V_U16:
			if (rowk_a->val.v_u16 == rowk_b->val.v_u16)
				continue;
			if (rowk_a->val.v_u16 > rowk_b->val.v_u16)
				return 1;
			return -1;
		case LDMS_V_S16:
			if (rowk_a->val.v_s16 == rowk_b->val.v_s16)
				continue;
			if (rowk_a->val.v_s16 > rowk_b->val.v_s16)
				return 1;
			return -1;
		case LDMS_V_U32:
			if (rowk_a->val.v_u32 == rowk_b->val.v_u32)
				continue;
			if (rowk_a->val.v_u32 > rowk_b->val.v_u32)
				return 1;
			return -1;
		case LDMS_V_S32:
			if (rowk_a->val.v_s32 == rowk_b->val.v_s32)
				continue;
			if (rowk_a->val.v_s32 > rowk_b->val.v_s32)
				return 1;
			return -1;
		case LDMS_V_U64:
			if (rowk_a->val.v_u64 == rowk_b->val.v_u64)
				continue;
			if (rowk_a->val.v_u64 > rowk_b->val.v_u64)
				return 1;
			return -1;
		case LDMS_V_S64:
			if (rowk_a->val.v_s64 == rowk_b->val.v_s64)
				continue;
			if (rowk_a->val.v_s64 > rowk_b->val.v_s64)
				return 1;
			return -1;
		case LDMS_V_F32:
			if (rowk_a->val.v_f == rowk_b->val.v_f)
				continue;
			if (rowk_a->val.v_f > rowk_b->val.v_f)
				return 1;
			return -1;
		case LDMS_V_D64:
			if (rowk_a->val.v_d == rowk_b->val.v_d)
				continue;
			if (rowk_a->val.v_d > rowk_b->val.v_d)
				return 1;
			return -1;
		default:
			assert(0);
		}
	}

	return 0;
}

/**
 * @brief Cache a row to a group
 *
 * @param strgp - The owning storage policy
 * @param row_limit - The limit of rows to cache in each group
 * @return ldmsd_row_cache_t
 */
ldmsd_row_cache_t ldmsd_row_cache_create(ldmsd_strgp_t strgp, int row_limit)
{
	ldmsd_row_cache_t rcache = calloc(1, sizeof(*rcache));
	if (!rcache)
		return NULL;

	rcache->strgp = strgp;
	rcache->row_limit = row_limit;
	rbt_init(&rcache->group_tree, tree_comparator);
	pthread_mutex_init(&rcache->lock, NULL);

	return rcache;
}
/**
 * \brief ldmsd_row_idx_create
 *
 * A row index is an ordered collection of ldms_mval_t that are
 * compared one after the other. This used to order rows in the cache
 * such that operators such as 'diff' make sense, e.g. ordered by
 * 'job_id', 'component_id', and 'timestamp'.
 */
ldmsd_row_cache_idx_t ldmsd_row_cache_idx_create(int key_count, ldmsd_row_cache_key_t keys)
{
	ldmsd_row_cache_idx_t row_idx =	calloc(1, sizeof(*row_idx));
	if (!row_idx)
		goto out;
	row_idx->key_count = key_count;
	row_idx->keys = keys;
 out:
	return row_idx;
}

int ldmsd_row_cache(ldmsd_row_cache_t rcache,
		ldmsd_row_cache_idx_t group_key,
		ldmsd_row_cache_idx_t row_key,
		ldmsd_row_t row)
{
	ldmsd_row_group_t group;
	struct rbn *group_rbn;

	/* Insert the row_list into the tree using rcache->row_key */
	ldmsd_row_cache_entry_t entry = calloc(1, sizeof(*entry));
	if (!entry)
		return ENOMEM;

	pthread_mutex_lock(&rcache->lock);

	/* Look up the group */
	group_rbn = rbt_find(&rcache->group_tree, group_key);
	if (!group_rbn) {
		/* Create a new group and add it to the tree */
		group = calloc(1, sizeof(*group));
		rbt_init(&group->row_tree, tree_comparator);
		rbn_init(&group->rbn, group_key);
		rbt_ins(&rcache->group_tree, &group->rbn);
		group_rbn = &group->rbn;
	}
	group = container_of(group_rbn, struct ldmsd_row_group_s, rbn);

	if (rbt_card(&group->row_tree) == rcache->row_limit) {
		ldmsd_row_cache_entry_t cent;
		struct rbn *rbn;
		rbn = rbt_min(&group->row_tree);
		assert(rbn);
		cent = container_of(rbn, struct ldmsd_row_cache_entry_s, rbn);
		assert(rbn);
		free(cent->idx->keys);
		free(cent->idx);
		free(cent->row);
		rbt_del(&group->row_tree, rbn);
		free(cent);
	}

	rbn_init(&entry->rbn, row_key);
	entry->row = row;
	entry->idx = row_key;
	rbt_ins(&group->row_tree, &entry->rbn);
	pthread_mutex_unlock(&rcache->lock);

	return 0;
}

/**
 * @brief Return a list containing the most recent \c count rows from the cache
 *
 * Adds the newest rows from the row cache into the list. The rows are not
 * removed from the cache
 *
 * @param row_list - The row list into which rows will be inserted
 * @param row_count - The number of rows to insert into the row list
 * @param cache - The row cache handle
 * @param group_key - The group from which the rows will be taken
 * @returns The number of rows inserted
 */
int ldmsd_row_cache_make_list(ldmsd_row_list_t row_list, int row_count,
				ldmsd_row_cache_t cache,
				ldmsd_row_cache_idx_t group_key
				)
{
	int count = 0;
	struct rbn *rbn;
	ldmsd_row_group_t group;
	ldmsd_row_cache_entry_t entry;
	TAILQ_INIT(row_list);

	pthread_mutex_lock(&cache->lock);

	rbn = rbt_find(&cache->group_tree, group_key);
	if (!rbn)
		goto out;
	group = container_of(rbn, struct ldmsd_row_group_s, rbn);
	rbn = rbt_max(&group->row_tree);
	if (!rbn)
		goto out;
	entry = container_of(rbn, struct ldmsd_row_cache_entry_s, rbn);
	while (count < row_count) {
		TAILQ_INSERT_TAIL(row_list, entry->row, entry);
		count += 1;
		rbn = rbn_pred(rbn);
		if (!rbn)
			break;
		entry = container_of(rbn, struct ldmsd_row_cache_entry_s, rbn);
	}
out:
	pthread_mutex_unlock(&cache->lock);
	return count;
}
