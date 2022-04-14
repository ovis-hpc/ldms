#include <assert.h>
#include <stdio.h>
#include "ldms.h"

#define SCHEMA_NAME "my_schema"
#define SET_NAME "my_set"
#define REC_TYPE_NAME "my_rec_def"
#define ARRAY_LEN 5

#define RECORD_CARD 2
#define RECORD_METRIC_0_NAME "singleton"
#define RECORD_METRIC_1_NAME "array"
#define UNIT "unit"

void verify(int exp)
{
	if (exp)
		printf("passed\n");
	else
		printf("failed\n");
}

int main(int argc, char **argv) {
	int rc, i;
	ldms_schema_t schema;
	ldms_set_t set;
	ldms_record_t rec_def;
	int rec_type_mid, list_mid;
	ldms_mval_t rec_type, rec_inst, mv, lh;
	const char *s;
	enum ldms_value_type type;
	size_t cnt;

	schema = ldms_schema_new(SCHEMA_NAME);
	assert(schema);
	rec_def = ldms_record_create(REC_TYPE_NAME);
	assert(rec_def);
	rc = ldms_record_metric_add(rec_def, RECORD_METRIC_0_NAME, UNIT, LDMS_V_U64, 1);
	assert(rc >= 0);
	rc = ldms_record_metric_add(rec_def, RECORD_METRIC_1_NAME, UNIT, LDMS_V_U64_ARRAY, 5);
	assert(rc >= 0);

	rec_type_mid = ldms_schema_record_add(schema, rec_def);
	assert(rec_type_mid >= 0);
	list_mid = ldms_schema_metric_list_add(schema, "list", "unit",
			ldms_record_heap_size_get(rec_def));
	assert(list_mid >= 0);

	ldms_init(128 * 1024L);
	set = ldms_set_new(SET_NAME, schema);
	assert(set);

	rec_type = ldms_metric_get(set, rec_type_mid);
	assert(rec_type);

	ldms_transaction_begin(set);
	rec_inst = ldms_record_alloc(set, rec_type_mid);
	assert(rec_inst);
	mv = ldms_record_metric_get(rec_inst, 0);
	mv->v_u64 = 1;
	mv = ldms_record_metric_get(rec_inst, 1);
	for (i = 0; i < ARRAY_LEN; i++) {
		mv->a_u64[i] = i + 2;
	}
	lh = ldms_metric_get(set, list_mid);
	assert(lh);
	ldms_list_append_record(set, lh, rec_inst);
	ldms_transaction_end(set);

	rec_inst = ldms_list_first(set, lh, &type, &cnt);

	printf("ldms_record_card(<rec_type>) : ");
	rc = ldms_record_card(rec_type);
	verify(2 == rc);

	printf("ldms_record_card(<rec_inst>) : ");
	rc = ldms_record_card(rec_inst);
	verify(2 == rc);

	printf("ldms_record_metric_find(<rec_type>, <name>): ");
	i = ldms_record_metric_find(rec_type, RECORD_METRIC_1_NAME);
	verify(1 == i);

	printf("ldms_record_metric_find(<rec_inst>, <name>): ");
	i = ldms_record_metric_find(rec_inst, RECORD_METRIC_1_NAME);
	verify(1 == i);

	printf("ldms_record_metric_name_get(<rec_type>, <mid>) : ");
	s = ldms_record_metric_name_get(rec_type, 0);
	verify(0 == strcmp(s, RECORD_METRIC_0_NAME));

	printf("ldms_record_metric_name_get(<rec_inst>, <mid>) : ");
	s = ldms_record_metric_name_get(rec_inst, 0);
	verify(0 == strcmp(s, RECORD_METRIC_0_NAME));

	printf("ldms_record_metric_unit_get(<rec_type>, <mid>) : ");
	s = ldms_record_metric_unit_get(rec_type, 0);
	verify(0 == strcmp(s, UNIT));

	printf("ldms_record_metric_unit_get(<rec_inst>, <mid>) : ");
	s = ldms_record_metric_unit_get(rec_inst, 0);
	verify(0 == strcmp(s, UNIT));

	printf("ldms_record_metric_type_get(<rec_type>, <mid>, &cnt) : ");
	type = ldms_record_metric_type_get(rec_type, 1, &cnt);
	verify(type == LDMS_V_U64_ARRAY);
	verify(cnt == ARRAY_LEN);

	printf("ldms_record_metric_type_get(<rec_inst>, <mid>, &cnt) : ");
	type = ldms_record_metric_type_get(rec_inst, 1, &cnt);
	verify(type == LDMS_V_U64_ARRAY);
	verify(cnt == ARRAY_LEN);

	printf("ldms_record_get(rec_type, <mid>) -- expecting to return an error : ");
	mv = ldms_record_metric_get(rec_type, 0);
	verify(NULL == mv);

	printf("ldms_record_get(rec_inst, <mid>) : ");
	mv = ldms_record_metric_get(rec_inst, 0);
	verify(1 == mv->v_u64);

	return 0;
}
