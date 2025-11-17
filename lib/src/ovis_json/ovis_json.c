#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/queue.h>
#include "ovis_json.h"
#include "ovis_json_priv.h"

#define JSON_BUF_START_LEN 255
static json_entity_t __entity_alloc(json_parser_t p)
{
	struct json_entity_entry_s *ee;
	pthread_mutex_lock(&p->entity_lock);
	ee = LIST_FIRST(&p->entity_list);
	if (ee) {
		p->entity_cache--;
		LIST_REMOVE(ee, entry);
		pthread_mutex_unlock(&p->entity_lock);
		return &ee->e;
	} else {
		pthread_mutex_unlock(&p->entity_lock);
		ee = malloc(sizeof(*ee));
	}
	return &ee->e;
}

static void __entity_free(json_entity_t e)
{
	struct json_entity_entry_s *ee = (struct json_entity_entry_s *)e;
	json_parser_t p = e->parser;
	pthread_mutex_lock(&p->entity_lock);
	p->entity_cache++;
	LIST_INSERT_HEAD(&p->entity_list, ee, entry);
	pthread_mutex_unlock(&p->entity_lock);
}

json_parser_t json_parser_new(size_t heap_size)
{
	json_parser_t p = calloc(1, sizeof *p);
	if (p) {
		int rc = regcomp(&p->f_regex,
				 "^[-+]?[0-9]*[.][0-9]+([eE][-+]?[0-9]+)?",
				 REG_EXTENDED);
		assert(0 == rc);
		LIST_INIT(&p->entity_list);
	}
	return p;
}

void json_parser_free(json_parser_t parser)
{
	struct json_entity_entry_s *ee;
	if (!parser)
		return;
	pthread_mutex_lock(&parser->entity_lock);
	while (!LIST_EMPTY(&parser->entity_list)) {
		ee = LIST_FIRST(&parser->entity_list);
		LIST_REMOVE(ee, entry);
		free(ee);
	}
	regfree(&parser->f_regex);
	pthread_mutex_unlock(&parser->entity_lock);
	free(parser);
}

int json_type_valid(int t)
{
	return (t >= JSON_INT_VALUE && t <= JSON_NULL_VALUE);
}

/* Defined in ovis_json_parser.c */
extern int ovis_json_parse(json_parser_t parser, char *input, size_t input_len, json_entity_t *entity);
int json_parse_buffer(json_parser_t p, char *buf, size_t buf_len, json_entity_t *pentity)
{
	int rc;
	json_entity_t entity = NULL;
	p->state.cur_line = 1;
	p->state.cur_pos = 0;
	p->state.cur_loc = 0;
	*pentity = NULL;
	rc = ovis_json_parse(p, buf, buf_len, &entity);
	if (!rc)
		*pentity = entity;
	return rc;
}

char *json_parse_error(json_parser_t p)
{
	char *errstr;
	int rc = asprintf(&errstr, "line: %d, col: %d, pos: %d",
			  p->state.cur_line, p->state.cur_loc, p->state.cur_pos);
	if (rc > 0)
		return errstr;
	return NULL;
}

const char *json_type_name(enum json_value_e typ)
{
	static const char *json_type_names[] = {
	    "INT", "BOOL", "FLOAT", "STRING", "ATTR", "LIST", "DICT", "NULL"};
	if (typ >= JSON_INT_VALUE && typ <= JSON_NULL_VALUE)
		return json_type_names[typ];
	return "Invalid Value Type";
}

int json_verify_string(char *s)
{
	json_entity_t e;
	json_parser_t p;
	int rc;
	p = json_parser_new(0);
	if (!p)
		return ENOMEM;
	rc = json_parse_buffer(p, s, strlen(s), &e);
	if (!rc)
		json_entity_free(e);
	json_parser_free(p);
	return (rc ? -1 : 0);
}

jbuf_t jbuf_new(void)
{
	jbuf_t jb = malloc(sizeof(*jb) + JSON_BUF_START_LEN);
	if (jb) {
		jb->buf_len = JSON_BUF_START_LEN;
		jb->cursor = 0;
	}
	return jb;
}

void jbuf_reset(jbuf_t jb)
{
	jb->cursor = 0;
}

void jbuf_free(jbuf_t jb)
{
	free(jb);
}

jbuf_t jbuf_append_va(jbuf_t jb, const char *fmt, va_list _ap)
{
	int cnt, space;
	va_list ap;
retry:
	va_copy(ap, _ap);
	space = jb->buf_len - jb->cursor;
	cnt = vsnprintf(&jb->buf[jb->cursor], space, fmt, ap);
	va_end(ap);
	if (cnt >= space) {
		jbuf_t jbnb;
		space = jb->buf_len + cnt + JSON_BUF_START_LEN + sizeof(*jb);
		jbnb = realloc(jb, space);
		if (jbnb) {
			jbnb->buf_len = space - sizeof(*jb);
			jb = jbnb;
			goto retry;
		} else {
			jbuf_free(jb);
			return NULL;
		}
	}
	jb->cursor += cnt;
	return jb;
}

jbuf_t jbuf_append_str(jbuf_t jb, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	jb = jbuf_append_va(jb, fmt, ap);
	va_end(ap);
	return jb;
}

jbuf_t jbuf_append_attr(jbuf_t jb, const char *name, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	jb = jbuf_append_str(jb, "\"%s\":", name);
	if (jb)
		jb = jbuf_append_va(jb, fmt, ap);
	va_end(ap);
	return jb;
}

json_entity_t json_attr_first(json_entity_t e)
{
	struct rbn *rbn;
	assert(e->type == JSON_DICT_VALUE);
	rbn = rbt_min(&e->value.dict_.attr_tree);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct json_entity_s, value.attr_.attr_rbn);
}

json_entity_t json_attr_next(json_entity_t e)
{
	struct rbn *rbn;
	assert(e->type == JSON_ATTR_VALUE);
	rbn = rbn_succ(&e->value.attr_.attr_rbn);
	if (rbn)
		return container_of(rbn, struct json_entity_s, value.attr_.attr_rbn);
	return NULL;
}

json_entity_t json_attr_find(json_entity_t e, const char *name)
{
	if (!e || !name) {
		errno = ENOENT;
		return NULL;
	}
	struct rbn *rbn;
	assert(e->type == JSON_DICT_VALUE);
	rbn = rbt_find(&e->value.dict_.attr_tree, name);
	if (rbn)
		return container_of(rbn, struct json_entity_s, value.attr_.attr_rbn);
	return NULL;
}

int json_attr_count(json_entity_t e)
{
	assert(e->type == JSON_DICT_VALUE);
	return rbt_card(&e->value.dict_.attr_tree);
}

static int attr_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}

#define JSON_HTBL_DEPTH 23

void json_item_add(json_entity_t a, json_entity_t e)
{
	assert(a->type == JSON_LIST_VALUE);
	a->value.list_.item_count++;
	TAILQ_INSERT_TAIL(&a->value.list_.item_list, e, item_entry);
}

json_entity_t json_item_first(json_entity_t a)
{
	json_entity_t i;
	assert(a->type == JSON_LIST_VALUE);
	i = TAILQ_FIRST(&a->value.list_.item_list);
	return i;
}

json_entity_t json_item_next(json_entity_t a)
{
	return TAILQ_NEXT(a, item_entry);
}

json_entity_t json_item_pop(json_entity_t a, int idx)
{
	int i;
	json_entity_t item;
	assert(a->type == JSON_LIST_VALUE);
	if (idx >= json_list_len(a))
		return NULL;
	for (i = 0, item = json_item_first(a); (i < idx) && item;
	     i++, item = json_item_next(item)) {
		continue;
	}
	a->value.list_.item_count--;
	TAILQ_REMOVE(&a->value.list_.item_list, item, item_entry);
	return item;
}

int json_item_rem(json_entity_t l, json_entity_t item)
{
	json_entity_t i;
	assert(l->type == JSON_LIST_VALUE);
	for (i = json_item_first(l); i; i = json_item_next(i)) {
		if (i == item)
			break;
	}
	if (i) {
		TAILQ_REMOVE(&l->value.list_.item_list, i, item_entry);
		l->value.list_.item_count--;
	} else {
		return ENOENT;
	}
	return 0;
}

json_parser_t json_entity_parser(json_entity_t e)
{
	return e->parser;
}

json_entity_t json_entity_new(json_parser_t p, enum json_value_e type, ...)
{
	uint64_t i;
	double d;
	char *s, *name;
	va_list ap;
	json_entity_t e, value;
	e = __entity_alloc(p);
	if (!e)
		return NULL;

	va_start(ap, type);

	e->parser = p;
	e->type = type;
	switch (type) {
	case JSON_STRING_VALUE:
		s = va_arg(ap, char *);
		size_t len = va_arg(ap, size_t);
		if (len < sizeof(e->value.str_.str_)) {
			e->value.str_.str = e->value.str_.str_;
		} else {
			e->value.str_.str = malloc(len+1);
			if (!e->value.str_.str) {
				__entity_free(e);
				return NULL;
			}
		}
		memcpy(e->value.str_.str, s, len);
		e->value.str_.str[len] = '\0';
		e->value.str_.str_len = len;
		break;
	case JSON_DICT_VALUE:
		rbt_init(&e->value.dict_.attr_tree, attr_cmp);
		break;
	case JSON_ATTR_VALUE:
		name = va_arg(ap, char *);
		value = va_arg(ap, json_entity_t);
		if (strlen(name) >= sizeof(e->value.attr_.name)) {
			errno = ENAMETOOLONG;
			__entity_free(e);
			return NULL;
		}
		strcpy(e->value.attr_.name, name);
		e->value.attr_.value = value;
		break;
	case JSON_LIST_VALUE:
		e->value.list_.item_count = 0;
		TAILQ_INIT(&e->value.list_.item_list);
		break;
	case JSON_INT_VALUE:
		i = va_arg(ap, uint64_t);
		e->value.int_ = i;
		break;
	case JSON_BOOL_VALUE:
		i = va_arg(ap, int);
		e->value.bool_ = i;
		break;
	case JSON_FLOAT_VALUE:
		d = va_arg(ap, double);
		e->value.double_ = d;
		break;
	case JSON_NULL_VALUE:
		e->value.int_ = 0;
		break;
	default:
		__entity_free(e);
		errno = EINVAL;
		e = NULL;
		assert(0 == "Invalid entity type");
	}
	va_end(ap);
	return e;
}

static jbuf_t __entity_dump(jbuf_t jb, json_entity_t e);
static jbuf_t __list_dump(jbuf_t jb, json_entity_t e)
{
	json_entity_t i;
	int count = 0;
	jb = jbuf_append_str(jb, "[");
	for (i = json_item_first(e); i; i = json_item_next(i)) {
		if (count)
			jb = jbuf_append_str(jb, ",");
		jb = __entity_dump(jb, i);
		count++;
	}
	jb = jbuf_append_str(jb, "]");
	return jb;
}

static jbuf_t __dict_dump(jbuf_t jb, json_entity_t e)
{
	json_entity_t a;
	int count = 0;
	jb = jbuf_append_str(jb, "{");
	for (a = json_attr_first(e); a; a = json_attr_next(a)) {
		if (count)
			jb = jbuf_append_str(jb, ",");
		jb = jbuf_append_str(jb, "\"%s\":", a->value.attr_.name);
		jb = __entity_dump(jb, a->value.attr_.value);
		count++;
	}
	jb = jbuf_append_str(jb, "}");
	return jb;
}

static jbuf_t __entity_dump(jbuf_t jb, json_entity_t e)
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
		jb = jbuf_append_str(jb, "\"%s\"", e->value.str_.str);
		break;
	case JSON_ATTR_VALUE:
		jb = jbuf_append_str(jb, "\"%s\":", e->value.attr_.name);
		jb = __entity_dump(jb, e->value.attr_.value);
		break;
	case JSON_LIST_VALUE:
		jb = __list_dump(jb, e);
		break;
	case JSON_DICT_VALUE:
		jb = __dict_dump(jb, e);
		break;
	case JSON_NULL_VALUE:
		jb = jbuf_append_str(jb, "null");
		break;
	default:
		fprintf(stderr, "%d is an invalid type.\n", e->type);
	}
	return jb;
}

jbuf_t json_entity_dump(jbuf_t jb, json_entity_t e)
{
	jbuf_t jb_ = NULL;
	if (!jb)
	{
		jb_ = jbuf_new();
		if (!jb_)
			return NULL;
		jb = jb_;
	}
	jb = __entity_dump(jb, e);
	if (!jb && jb_)
	{
		/*
		 * There was an error dumping the entity and
		 * the caller doesn't pass a jbuf,
		 * so free the allocated jbuf.
		 */
		jbuf_free(jb_);
	}

	return jb;
}

json_entity_t json_entity_copy(json_parser_t parser, json_entity_t e)
{
	int rc;
	json_entity_t new, n, v;
	enum json_value_e type = json_entity_type(e);

	switch (type) {
	case JSON_INT_VALUE:
	case JSON_BOOL_VALUE:
		new = json_entity_new(parser, type, e->value);
		break;
	case JSON_FLOAT_VALUE:
		new = json_entity_new(parser, type, e->value.double_);
		break;
	case JSON_STRING_VALUE:
		new = json_entity_new(parser, type, json_value_cstr(e),
				      json_value_strlen(e));
		break;
	case JSON_ATTR_VALUE:
		v = json_entity_copy(parser, json_attr_value(e));
		if (!v)
			return NULL;
		new = json_entity_new(parser, type, json_attr_name(e), v);
		if (!new) {
			json_entity_free(v);
			return NULL;
		}
		break;
	case JSON_LIST_VALUE:
		new = json_entity_new(parser, type);
		if (!new)
			return NULL;
		for (n = json_item_first(e); n; n = json_item_next(n)) {
			v = json_entity_copy(parser, n);
			if (!v)
			{
				json_entity_free(new);
				return NULL;
			}
			json_item_add(new, v);
		}
		break;
	case JSON_DICT_VALUE:
		new = json_entity_new(parser, type);
		if (!new)
			return NULL;
		for (n = json_attr_first(e); n; n = json_attr_next(n))
		{
			/* Copy the attribute value */
			v = json_entity_copy(parser, n);
			if (!v)
			{
				json_entity_free(new);
				return NULL;
			}
			rc = json_attr_add(new, json_attr_name(n), v);
			if (rc)
			{
				json_entity_free(v);
				json_entity_free(new);
				return NULL;
			}
		}
		break;
	case JSON_NULL_VALUE:
		new = json_entity_new(parser, JSON_NULL_VALUE);
		break;
	default:
		assert(0 == "Invalid entity type");
		break;
	}
	return new;
}

static inline void __attr_rem(json_entity_t d, json_entity_t a)
{
	rbt_del(&d->value.dict_.attr_tree, &a->value.attr_.attr_rbn);
	json_entity_free(a->value.attr_.value);
}

void __attr_add(json_entity_t d, json_entity_t a)
{
	const char *name;

	name = json_attr_name(a);
	rbn_init(&a->value.attr_.attr_rbn, (void *)name);
	rbt_ins(&d->value.dict_.attr_tree, &a->value.attr_.attr_rbn);
}

int json_attr_add(json_entity_t d, const char *name, json_entity_t v)
{
	json_entity_t a;
	assert(d->type == JSON_DICT_VALUE);
	a = json_entity_new(d->parser, JSON_ATTR_VALUE, name, v);
	__attr_add(d, a);
	return 0;
}

int json_attr_rem(json_entity_t d, char *name)
{
	json_entity_t a;
	assert(d->type == JSON_DICT_VALUE);
	a = json_attr_find(d, name);
	if (!a)
		return ENOENT;
	__attr_rem(d, a);
	return 0;
}

static void json_list_free(json_entity_t l)
{
	json_entity_t i;
	if (!l)
		return;
	assert(l->type == JSON_LIST_VALUE);
	while (!TAILQ_EMPTY(&l->value.list_.item_list)) {
		i = TAILQ_FIRST(&l->value.list_.item_list);
		TAILQ_REMOVE(&l->value.list_.item_list, i, item_entry);
		json_entity_free(i);
	}
	__entity_free(l);
}

static void json_dict_free(json_entity_t d)
{
	json_entity_t i;
	struct rbn *rbn;
	struct rbt *t;
	if (!d)
		return;
	assert(d->type == JSON_DICT_VALUE);
	t = &d->value.dict_.attr_tree;
	while (!rbt_empty(t)) {
		rbn = rbt_min(t);
		rbt_del(t, rbn);
		i = container_of(rbn, struct json_entity_s, value.attr_.attr_rbn);
		assert(i->type == JSON_ATTR_VALUE);
		json_entity_free(i);
	}
	__entity_free(d);
}

void json_entity_free(json_entity_t e)
{
	if (!e)
		return;
	switch (e->type) {
	case JSON_FLOAT_VALUE:
	case JSON_BOOL_VALUE:
	case JSON_INT_VALUE:
		__entity_free(e);
		break;
	case JSON_STRING_VALUE:
		if (e->value.str_.str_ != e->value.str_.str)
			free(e->value.str_.str);
		__entity_free(e);
		break;
	case JSON_ATTR_VALUE:
		json_entity_free(e->value.attr_.value);
		__entity_free(e);
		break;
	case JSON_LIST_VALUE:
		json_list_free(e);
		break;
	case JSON_DICT_VALUE:
		json_dict_free(e);
		break;
	case JSON_NULL_VALUE:
		free(e);
		break;
	default:
		/* Leak if we're passed garbage */
		return;
	}
}

enum json_value_e json_entity_type(json_entity_t e)
{
	return e->type;
}

int64_t json_value_int(json_entity_t value)
{
	assert(value->type == JSON_INT_VALUE);
	return value->value.int_;
}

int json_value_bool(json_entity_t value)
{
	assert(value->type == JSON_BOOL_VALUE);
	return value->value.bool_;
}

double json_value_float(json_entity_t value)
{
	assert(value->type == JSON_FLOAT_VALUE);
	return value->value.double_;
}

size_t json_value_strlen(json_entity_t e)
{
	assert(e->type == JSON_STRING_VALUE);
	return e->value.str_.str_len;
}

char *json_value_cstr(json_entity_t e)
{
	if (e->type == JSON_STRING_VALUE)
		return e->value.str_.str;
	errno = EINVAL;
	return NULL;
}

json_attr_t json_value_attr(json_entity_t e)
{
	assert(e->type == JSON_ATTR_VALUE);
	return &e->value.attr_;
}

json_list_t json_value_list(json_entity_t e)
{
	assert(e->type == JSON_LIST_VALUE);
	return &e->value.list_;
}

json_dict_t json_value_dict(json_entity_t e)
{
	assert(e->type == JSON_DICT_VALUE);
	return &e->value.dict_;
}

char *json_attr_name(json_entity_t attr)
{
	assert(attr->type == JSON_ATTR_VALUE);
	return attr->value.attr_.name;
}

json_entity_t json_attr_value(json_entity_t attr)
{
	assert(attr->type == JSON_ATTR_VALUE);
	return attr->value.attr_.value;
}

json_entity_t json_value_find(json_entity_t d, const char *name)
{
	assert(d->type == JSON_DICT_VALUE);
	json_entity_t e = json_attr_find(d, name);
	if (e)
		e = json_attr_value(e);
	return e;
}

size_t json_list_len(json_entity_t list)
{
	assert(list->type == JSON_LIST_VALUE);
	return list->value.list_.item_count;
}

static json_entity_t __dict_new(json_parser_t parser, json_entity_t d, va_list *ap);
static json_entity_t __attr_value_new(json_parser_t parser, int type, va_list *ap)
{
	json_entity_t v = NULL;
	json_entity_t item;
	switch (type) {
	case JSON_BOOL_VALUE:
		v = json_entity_new(parser, type, va_arg(*ap, int32_t));
		break;
	case JSON_FLOAT_VALUE:
		v = json_entity_new(parser, type, va_arg(*ap, double));
		break;
	case JSON_INT_VALUE:
		v = json_entity_new(parser, type, va_arg(*ap, int64_t));
		break;
	case JSON_STRING_VALUE:
		v = json_entity_new(parser, type,
				    va_arg(*ap, char *), va_arg(*ap, size_t));
		break;
	case JSON_DICT_VALUE:
		v = __dict_new(parser, NULL, ap);
		break;
	case JSON_LIST_VALUE:
		v = json_entity_new(parser, type);
		if (!v)
			return NULL;
		type = va_arg(*ap, int);
		for (type = va_arg(*ap, int); type != JSON_EOL_VALUE;
		     type = va_arg(*ap, int))
		{
			item = __attr_value_new(parser, type, ap);
			json_item_add(v, item);
			type = va_arg(*ap, int);
		}
		break;
	case JSON_NULL_VALUE:
		v = json_entity_new(parser, type);
		break;
	default:
		assert(0 == "Unexpected json value type.");
		errno = EINVAL;
		break;
	}
	return v;
}

static json_entity_t __dict_new(json_parser_t parser, json_entity_t d_, va_list *ap)
{
	json_entity_t d, v, a;
	int type;
	const char *name;

	if (d_) {
		d = d_;
	} else {
		d = json_entity_new(parser, JSON_DICT_VALUE);
		if (!d)
			return NULL;
	}

	for (name = va_arg(*ap, char *); name; name = va_arg(*ap, char *)) {
		type = va_arg(*ap, int);
		if (!json_type_valid(type)) {
			assert(0);
			errno = EINVAL;
			goto err;
		}
		switch (type) {
		case JSON_ATTR_VALUE:
			/* Note that 'name' is ignored from the argument list */
			a = va_arg(*ap, json_entity_t);
			__attr_add(d, a);
			break;
		case JSON_BOOL_VALUE:
		case JSON_DICT_VALUE:
		case JSON_FLOAT_VALUE:
		case JSON_INT_VALUE:
		case JSON_LIST_VALUE:
		case JSON_STRING_VALUE:
		case JSON_NULL_VALUE:
			/* attribute value */
			v = __attr_value_new(parser, type, ap);
			if (!v)
				goto err;
			json_attr_add(d, name, v);
			break;
		default:
			assert(0 || NULL == "unhandled type in ovis_json:__dict_new.");
		}
	}
	return d;
err:
	json_entity_free(d);
	return NULL;
}

json_entity_t json_dict_build(json_parser_t parser, json_entity_t d, ...)
{
	va_list ap;
	json_entity_t obj;
	va_start(ap, d);
	obj = __dict_new(parser, d, &ap);
	va_end(ap);
	return obj;
}
