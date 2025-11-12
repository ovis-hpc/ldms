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
	return jb;
}

static jbuf_t print_dict(jbuf_t jb, json_entity_t e)
{
	json_entity_t a;
	int count = 0;
	jb = jbuf_append_str(jb, "{");
	for (a = json_attr_first(e); a; a = json_attr_next(a)) {
		if (count)
			jb = jbuf_append_str(jb, ",");
		jb = jbuf_append_str(jb, "\"%s\":", json_attr_name(a));
		print_entity(jb, json_attr_value(a));
		count++;
	}
	jb = jbuf_append_str(jb, "}");
	return jb;
}

static jbuf_t print_entity(jbuf_t jb, json_entity_t e)
{
	switch (json_entity_type(e)) {
	case JSON_INT_VALUE:
		jb = jbuf_append_str(jb, "%ld", json_value_int(e));
		break;
	case JSON_BOOL_VALUE:
		if (json_value_bool(e))
			jb = jbuf_append_str(jb, "true");
		else
			jb = jbuf_append_str(jb, "false");
		break;
	case JSON_FLOAT_VALUE:
		jb = jbuf_append_str(jb, "%f", json_value_float(e));
		break;
	case JSON_STRING_VALUE:
		jb = jbuf_append_str(jb, "\"%s\"", json_value_cstr(e));
		break;
	case JSON_LIST_VALUE:
		print_list(jb, e);
		break;
	case JSON_DICT_VALUE:
		jb = print_dict(jb, e);
		break;
	case JSON_NULL_VALUE:
		jb = jbuf_append_str(jb, "null");
		break;
	default:
		fprintf(stderr, "%d is an invalid entity type", json_entity_type(e));
	}
	return jb;
}

char buffer[1024*1024];
int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("%s: need input file name\n", argv[0]);
		return 1;
	}
	FILE *fp = fopen(argv[1], "r");
	if (!fp) {
		printf("%s: unable to open %s\n" , argv[0], argv[1]);
		return 1;
	}
	json_doc_t doc;
	int rc = fread(buffer, 1, sizeof(buffer), fp);
	rc = json_parse_buffer(buffer, rc, &doc);
	jbuf_t jb;
	jb = jbuf_new();
	if (rc == 0) {
		jb = print_entity(jb, json_doc_root(doc));
		printf("%s\n", jb->buf);
		jbuf_free(jb);
	}
	json_doc_free(doc);
	return 0;
}



