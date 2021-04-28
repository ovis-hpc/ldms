#include "comp_id_helper.h"

int comp_id_helper_schema_add(ldms_schema_t schema, comp_id_t cid)
{
	int rc;
	if (!cid || ! schema)
		return EINVAL;
	if (cid->defined) {
		rc = ldms_schema_meta_add(schema, LDMSD_COMPID, LDMS_V_U64);
		if (rc < 0) {
			return -rc;
		}
		cid->comp_id_index = rc;
	}
	return 0;
}

int comp_id_helper_metric_update(ldms_set_t set, comp_id_t cid)
{
	if (!set || !cid)
		return EINVAL;
	if (cid->defined)
		ldms_metric_set_u64(set, cid->comp_id_index, cid->component_id);
	return 0;
}

int comp_id_helper_config_optional(struct attr_value_list *avl, comp_id_t cid)
{
	if (!cid || !avl)
		return EINVAL;
	char *tmp;
	char *endp = NULL;
	errno = 0;
	cid->defined = false;
	tmp = av_value(avl, LDMSD_COMPID);
	if (!tmp)
		return 0;
	unsigned long long int j = strtoull(tmp, &endp, 0);
	if (endp == tmp || errno) {
		return EINVAL;
	}
	cid->component_id = j;
	cid->defined = true;
	return 0;
}

int comp_id_helper_config(struct attr_value_list *avl, comp_id_t cid)
{
	int rc = comp_id_helper_config_optional(avl, cid);
	if (rc)
		return rc;
	if (!cid->defined) {
		cid->defined = true;
		cid->component_id = 0;
	}
	return 0;
}
