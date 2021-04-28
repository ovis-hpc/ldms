#ifndef __COMPID_HELPER_H
#define __COMPID_HELPER_H

#include "ldms.h"
#include "ldmsd.h"
#include "stdbool.h"

/* default initialization of this structure should be  0,false (or calloc) */
typedef struct comp_id_data {
	uint64_t component_id; /*< if given correctly in config options, this will be set to the value */
	int comp_id_index;  /*< set index of comp_id, if in use. */
	bool defined; /*< if given correctly in config options, this will be true. */
} *comp_id_t;

/* avl is checked for a component_id and cid is filled accordingly.
 * If component_id is not given, 0 is assumed and cid->defined becomes true.
 * @return if component_id is given but invalid, returns EINVAL, else 0.
 */
int comp_id_helper_config(struct attr_value_list *avl, comp_id_t cid);

/* avl is checked for a component_id and cid is filled accordingly.
 * @return if component_id is given but invalid, returns EINVAL, else 0.
 */
int comp_id_helper_config_optional(struct attr_value_list *avl, comp_id_t cid);

/*
 * Add component_id metadata to schema if cid->defined is true.
 * Sets comp_id_index to result of metric addition if successful.
 * @return errno value if requested add fails.
 */
int comp_id_helper_schema_add(ldms_schema_t schema, comp_id_t cid);

/*
 * Update component_id metric schema if cid->defined is true.
 * Normally needed only once, and caller is expected to handle
 * ldms_transaction_begin/end if needed.
 * @return errno value if request fails.
 */
int comp_id_helper_metric_update(ldms_set_t set, comp_id_t cid);

#endif /* __COMPID_HELPER_H */
