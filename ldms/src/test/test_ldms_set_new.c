#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "ldms.h"

#define UID 1000
#define GID 1000
#define PERM 777
#define HEAP_SZ (512 x 1024L)
#define SET_NAME "my_set"
#define SCHEMA_NAME "my_schema"
#define REC_DEF_NAME "my_record"

void verify(int expr)
{
	if (expr)
		printf(" passed\n");
	else
		printf(" failed\n");
}

void record_set(ldms_mval_t rec)
{
	int i;
	size_t card;
	ldms_mval_t mv;

	card = ldms_record_card(rec);
	for (i = 0; i < card; i++) {
		mv = ldms_record_metric_get(rec, i);
		mv->v_u64 = 1;
	}
}

int main(int argc, char **argv) {
	ldms_schema_t schema;
	ldms_set_t set;
	ldms_record_t rec_def;
	size_t heap_sz = LDMS_HEAP_MIN_SIZE * 2;

	ldms_init(1024);

	schema = ldms_schema_new(SCHEMA_NAME);
	assert(schema);

	rec_def = ldms_record_create(REC_DEF_NAME);
	assert(rec_def);

	ldms_record_metric_add(rec_def, "e1", "unit", LDMS_V_U64, 1);
	ldms_schema_record_add(schema, rec_def);
	ldms_schema_metric_list_add(schema, "list", "unit", heap_sz);

	set = ldms_set_new(SET_NAME, schema);
	assert(set);
	printf("ldms_set_new -- correct heap size: ");
	verify(ldms_set_heap_size_get(set) == heap_sz);
	ldms_set_delete(set);

	set = ldms_set_new_with_heap(SET_NAME, schema, heap_sz * 2);
	assert(set);
	printf("ldms_set_new_with_heap -- correct heap size > 0: ");
	verify(ldms_set_heap_size_get(set) == heap_sz * 2);
	ldms_set_delete(set);

	set = ldms_set_new_with_heap(SET_NAME, schema, 0);
	assert(set);
	printf("ldms_set_new_with_heap -- correct heap size of 0: ");
	verify(ldms_set_heap_size_get(set) == 0);
	ldms_set_delete(set);

	set = ldms_set_new_with_auth(SET_NAME, schema, UID, GID, PERM);
	assert(set);
	printf("ldms_set_new_with_auth -- correct uid: ");
	verify(ldms_set_uid_get(set) == UID);
	printf("ldms_set_new_with_auth -- correct gid: ");
	verify(ldms_set_gid_get(set) == GID);
	printf("ldms_set_new_with_auth -- correct perm: ");
	verify(ldms_set_perm_get(set) == PERM);
	printf("ldms_set_new_with_auth -- correct heap size: ");
	verify(ldms_set_heap_size_get(set) == heap_sz);
	ldms_set_delete(set);

	set = ldms_set_create(SET_NAME, schema, UID, GID, PERM, heap_sz *2);
	assert(set);
	printf("ldms_set_new_custom -- correct uid: ");
	verify(ldms_set_uid_get(set) == UID);
	printf("ldms_set_new_custom -- correct gid: ");
	verify(ldms_set_gid_get(set) == GID);
	printf("ldms_set_new_custom -- correct perm: ");
	verify(ldms_set_perm_get(set) == PERM);
	printf("ldms_set_new_custom -- correct heap size: ");
	verify(ldms_set_heap_size_get(set) == heap_sz * 2);
	ldms_set_delete(set);

	printf("DONE\n");
	return 0;
}
