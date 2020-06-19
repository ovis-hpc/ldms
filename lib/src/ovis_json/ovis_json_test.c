#include <coll/rbt.h>
#include "ovis_json.h"
#include "stdio.h"

static jbuf_t print_entity(jbuf_t jb, json_entity_t e);

static jbuf_t print_list(jbuf_t jb, json_entity_t e)
{
	json_entity_t i;
	int count = 0;
	jb = jbuf_append_str(jb, "[");
	for (i = json_item_first(e); i; i = json_item_next(i)) {
		if (count)
			jb = jbuf_append_str(jb, ",");
		print_entity(jb, i);
		count++;
	}
	jb = jbuf_append_str(jb, "]");
}

static jbuf_t print_dict(jbuf_t jb, json_entity_t e)
{
	json_entity_t a;
	hent_t ent;
	int count = 0;
	jb = jbuf_append_str(jb, "{");
	for (a = json_attr_first(e); a; a = json_attr_next(a)) {
		if (count)
			jb = jbuf_append_str(jb, ",");
		jb = jbuf_append_str(jb, "\"%s\":", a->value.attr_->name->value.str_->str);
		print_entity(jb, a->value.attr_->value);
		count++;
	}
	jb = jbuf_append_str(jb, "}");
	return jb;
}

static jbuf_t print_entity(jbuf_t jb, json_entity_t e)
{
	switch (e->type) {
	case JSON_INT_VALUE:
		jb = jbuf_append_str(jb, "%ld", e->value.int_);
		break;
	case JSON_BOOL_VALUE:
		if (e->value.bool_)
			jb = jbuf_append_str(jb, "true");
		else
			jb = jbuf_append_str(jb, "false");
		break;
	case JSON_FLOAT_VALUE:
		jb = jbuf_append_str(jb, "%f", e->value.double_);
		break;
	case JSON_STRING_VALUE:
		jb = jbuf_append_str(jb, "\"%s\"", e->value.str_->str);
		break;
	case JSON_LIST_VALUE:
		print_list(jb, e);
		break;
	case JSON_DICT_VALUE:
		print_dict(jb, e);
		break;
	case JSON_NULL_VALUE:
		jb = jbuf_append_str(jb, "null");
		break;
	default:
		fprintf(stderr, "%d is an invalid entity type", e->type);
	}
	return jb;
}

char buffer[1024*1024];
int main(int argc, char *argv[])
{
	FILE *fp = fopen(argv[1], "r");
	jbuf_t jb = jbuf_new();
	json_entity_t entity;
	json_parser_t parser = json_parser_new(0);
	int rc = fread(buffer, 1, sizeof(buffer), fp);
	rc = json_parse_buffer(parser, buffer, rc, &entity);
#if 0
	if (rc == 0)
		jb = print_entity(jb, entity);
	printf("%s\n", jb->buf);
	jbuf_free(jb);
	jb = jbuf_new();
	json_entity_t kernel_data = json_attr_find(entity, "kokkos-kernel-data");
	if (kernel_data) {
		print_entity(jb, kernel_data);
		json_entity_t mpi_rank = json_attr_find(kernel_data, "mpi-rank");
		if (mpi_rank)
			print_entity(jb, mpi_rank);
	}
	jbuf_free(jb);
#endif
	json_entity_free(entity);
	json_parser_free(parser);
	return 0;
}



