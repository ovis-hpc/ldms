#ifndef _JSON_PARSER_H_
#define _JSON_PARSER_H_
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <coll/rbt.h>		/* container_of() */
#include <sys/queue.h>

typedef struct json_attr_s *json_attr_t;
typedef struct json_array_s *json_array_t;
typedef struct json_dict_s *json_dict_t;
typedef struct json_entity_s *json_entity_t;

enum json_value_e {
	JSON_INT_VALUE,
	JSON_BOOL_VALUE,
	JSON_FLOAT_VALUE,
	JSON_STRING_VALUE,
	JSON_ARRAY_VALUE,
	JSON_DICT_VALUE,
	JSON_NULL_VALUE
};

static const char *json_type_names[] = {
	"INT", "BOOL", "FLOAT", "STRING", "ARRAY", "DICT", "NULL"
};

typedef struct json_str_s {
	char str_digest[SHA256_DIGEST_LENGTH];
	char *str;
	size_t str_len;
} *json_str_t;

struct json_entity_s {
	enum json_value_e type;
	union {
		int bool_;
		int64_t int_;
		double double_;
		json_str_t str_;
		json_array_t array_;
		json_dict_t dict_;
	} value;
};

typedef struct json_item_s {
	json_entity_t item;
	TAILQ_ENTRY(json_item_s) item_entry;
} *json_item_t;

struct json_array_s {
	struct json_entity_s base;
	int item_count;
	TAILQ_HEAD(json_item_list, json_item_s) item_list;
};

struct json_attr_s {
	json_str_t name;
	json_entity_t value;
	TAILQ_ENTRY(json_attr_s) attr_entry;
};

struct json_dict_s {
	struct json_entity_s base;
	TAILQ_HEAD(json_attr_list, json_attr_s) attr_list;
};

static inline json_str_t str_strip_dup(const char *src)
{
	json_str_t str;
	char *dst;
	size_t len;

	str = malloc(sizeof *str);
	if (str) {
		dst = strdup(src + 1);
		if (dst) {
			len = strlen(dst);
			dst[--len] = '\0';
			str->str = dst;
			str->str_len = len;
			SHA256(dst, len, str->str_digest);
		} else {
			free(str);
			str = NULL;
		}
	}
	return str;
}

static inline json_entity_t new_entity(enum json_value_e type) {
	json_entity_t e = malloc(sizeof *e);
	if (e)
		e->type = type;
	return e;
}

static inline json_entity_t new_int_val(char *str) {
	json_entity_t e = new_entity(JSON_INT_VALUE);
	if (e)
		e->value.int_ = strtoll(str, NULL, 0);
	return e;
}

static inline json_entity_t new_bool_val(char *str) {
	json_entity_t e = new_entity(JSON_BOOL_VALUE);
	if (e) {
		if (0 == strcmp(str, "true"))
			e->value.bool_ = 1;
		else
			e->value.bool_ = 0;
	}
	return e;
}

static inline json_entity_t new_float_val(char *str) {
	json_entity_t e = new_entity(JSON_FLOAT_VALUE);
	if (e)
		e->value.double_ = strtold(str, NULL);
	return e;
}

static inline json_entity_t new_null_val(void) {
	json_entity_t e = new_entity(JSON_NULL_VALUE);
	return e;
}

static inline json_entity_t new_string_val(char *str) {
	json_entity_t e = new_entity(JSON_STRING_VALUE);
	if (e)
		e->value.str_ = str_strip_dup(str);
	return e;
}

static inline json_entity_t new_dict_val(void) {
	json_dict_t d = malloc(sizeof *d);
	if (!d)
		return NULL;

	d->base.type = JSON_DICT_VALUE;
	d->base.value.dict_ = d;
	TAILQ_INIT(&d->attr_list);
	return &d->base;
}

static inline json_entity_t add_dict_attr(json_entity_t e, json_entity_t name, json_entity_t v)
{
	json_dict_t d = container_of(e, struct json_dict_s, base);
	json_attr_t a = malloc(sizeof(*a));
	if (a) {
		a->name = name->value.str_;
		free(name);
		a->value = v;
		TAILQ_INSERT_TAIL(&d->attr_list, a, attr_entry);
	}
	return e;
}

static inline json_entity_t new_array_val(void) {
	json_array_t a = malloc(sizeof *a);
	if (!a)
		return NULL;

	a->base.type = JSON_ARRAY_VALUE;
	a->base.value.array_ = a;
	a->item_count = 0;
	TAILQ_INIT(&a->item_list);
	return &a->base;
}

static inline json_entity_t add_array_item(json_entity_t e, json_entity_t v)
{
	json_array_t a = container_of(e, struct json_array_s, base);
	json_item_t i = malloc(sizeof(*i));
	if (i) {
		a->item_count++;
		TAILQ_INSERT_TAIL(&a->item_list, i, item_entry);
		i->item = v;
	}
	return e;
}

typedef struct json_parser_s {
	SHA256_CTX sha_ctxt;
	int lnum;		/* line number */
	int cpos;		/* character position */
	struct yy_buffer_state *buffer_state;
} *json_parser_t;

static inline json_parser_t json_parser_new(size_t user_data) {
	json_parser_t p = calloc(1, sizeof *p + user_data);
	if (p) {
		SHA256_Init(&p->sha_ctxt);
	}
	return p;
}

static inline void json_parser_free(json_parser_t parser)
{
	free(parser);
}

extern void json_entity_free(json_entity_t entity);
extern int json_parse_buffer(json_parser_t p, char *buf, size_t buf_len, json_entity_t *entity);

#define YYSTYPE json_entity_t
#endif
