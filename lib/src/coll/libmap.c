#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <getopt.h>
#include "map.h"
#include "map_priv.h"

/* return list of map schemas */
char **map_list_get(const char *path)
{
	int i=0;
	char *map_name, *p;
	char **map_list;
	sos_t sos;
	sos_schema_t schema;

	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos) {
		printf("Container %s could not be opened", path);
		goto err0;
	}
	for (schema = sos_schema_first(sos); schema;
	     schema = sos_schema_next(schema)) {
		if (strstr(sos_schema_name(schema), "__map__") != NULL) {
			i++;
		}
	}
	if (i == 0)
		goto err1;
	printf("map count: %d\n", i);
	map_list = calloc(i+1, sizeof(char*));
	i=0;
	for (schema = sos_schema_first(sos); schema;
	     schema = sos_schema_next(schema)) {
		if (strstr(sos_schema_name(schema), "__map__") != NULL) {
			map_name = strdup(sos_schema_name(schema));
			p = strstr(map_name, "__map__");
			*p = '\0';
			map_list[i] = map_name;
			i++;
		}
	}
	sos_container_close(sos, SOS_COMMIT_ASYNC);
	return map_list;
err1:
	printf("No matching maps found in container %s", path);
	sos_container_close(sos, SOS_COMMIT_ASYNC);
err0:
	perror("map_list_get");
	return NULL;
}

void map_list_free(char *map_list[])
{
	if (!map_list)
		return;
	int i;
	for (i=0;map_list[i]; i++) {
		free(map_list[i]);
	}
	free(map_list);
}

int map_container_new(const char *path)
{
	int rc;
	sos_t c_cont;
	sos_part_t c_part;

	rc = sos_container_new(path, 0660);
	c_cont = sos_container_open(path, SOS_PERM_RW);
	if (!c_cont) {
		rc = errno;
		goto err0;
	}
	rc = sos_part_create(c_cont, "ROOT", NULL);
	if (rc) {
		rc = errno;
		goto err1;
	}
	c_part = sos_part_find(c_cont, "ROOT");
	rc = sos_part_state_set(c_part, SOS_PART_STATE_PRIMARY);
	if (rc) {
		rc = errno;
		goto err2;
	}
	return rc;
err2:
	sos_part_put(c_part);
err1:
	sos_container_close(c_cont, SOS_COMMIT_ASYNC);
err0:
	return rc;
}

/*! Add new schema map to container */
int map_new(const char *path, const char *name)
{
	int rc;
	char schema_name[64];
	sos_t sos;
	sos_schema_t schema;

	sprintf(schema_name, "%s__map__", name);
	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos) {
		rc = errno;
		goto err0;
	}
	schema = sos_schema_new(schema_name);
	if (!schema) {
		rc = errno;
		goto err1;
	}
	rc = sos_schema_attr_add(schema, "source", SOS_TYPE_UINT64);
	if (rc)
		goto err1;
	rc = sos_schema_index_add(schema, "source");
	if (rc)
		goto err1;
	rc = sos_schema_attr_add(schema, "target", SOS_TYPE_UINT64);
	if (rc)
		goto err1;
	rc = sos_schema_index_add(schema, "target");
	if (rc)
		goto err1;
	rc = sos_schema_add(sos, schema);
	if (rc)
		goto err2;
	printf("schema creation successful\n");
	return 0;
err2:
	sos_schema_delete(sos, name);
err1:
	sos_container_close(sos, SOS_COMMIT_ASYNC);
err0:
	printf("Could not open container %s\n", path);
	return rc;
}

map_t map_open(char *path, char *map_name)
{
	map_t map_s;
	char schema_name[64];

	sprintf(schema_name, "%s__map__", map_name);
	map_s = calloc(1,sizeof(*map_s));
	if (!map_s)
		goto err0;
	map_s->sos = sos_container_open(path, SOS_PERM_RW);
	if (!map_s->sos)
		goto err1;
	map_s->schema = sos_schema_by_name(map_s->sos, schema_name);
	if (!map_s->schema)
		goto err2;
	map_s->src_attr = sos_schema_attr_by_name(map_s->schema, "source");
	if (!map_s->src_attr)
		goto err2;
	map_s->tgt_attr = sos_schema_attr_by_name(map_s->schema, "target");
	if (!map_s->tgt_attr)
		goto err2;
	map_s->src_iter = sos_attr_iter_new(map_s->src_attr);
	map_s->tgt_iter = sos_attr_iter_new(map_s->tgt_attr);
	return map_s;
err2:
	sos_container_close(map_s->sos, SOS_COMMIT_ASYNC);
	printf("Schema name does not exist in Container\n");
err1:
	free(map_s);
	printf("Container %s could not be opened", path);
err0:
	return NULL;
}

int map_close(map_t map_s)
{
	sos_container_close(map_s->sos, SOS_COMMIT_ASYNC);
	free(map_s);
	return 0;
}

int map_entry_new(map_t map_s, char *obj_cols)
{
	int i=0;
	uint64_t x, y;
	char *token;
	sos_value_data_t t_data, i_data;
	sos_obj_t obj;

	while ((token = strtok_r(obj_cols,",",&obj_cols))) {
		if (i==0) {
			x = strtoul(token, NULL, 0);
			if (!x)
				goto err0;
		} else {
			y = strtoul(token, NULL, 0);
			if (!y)
				goto err0;
		}
		i++;
	}
	obj = sos_obj_new(map_s->schema);
	if (!obj)
		goto err0;
	t_data = sos_obj_attr_data(obj, map_s->src_attr, NULL);
	if (!t_data)
		goto err1;
	t_data->prim.uint64_ = x;
	i_data = sos_obj_attr_data(obj, map_s->tgt_attr, NULL);
	if (!i_data)
		goto err1;
	i_data->prim.uint64_ = y;
	sos_obj_index(obj);
	sos_obj_put(obj);
	return 0;
err1:
	sos_obj_put(obj);
err0:
	printf("value %s not type uint64_t\n", token);
	return errno;
}

int map_transform(map_t map_s, uint64_t match_val, uint64_t *transformed_val)
{
	int rc;
	sos_value_data_t data;
	sos_obj_t match_obj;
	SOS_KEY(trans_key);

	if (!sos_key_for_attr(trans_key, map_s->src_attr, match_val))
		return errno;
	rc = sos_iter_find(map_s->src_iter, trans_key);
	if (rc)
		return rc;
	match_obj = sos_iter_obj(map_s->src_iter);
	data = sos_obj_attr_data(match_obj, map_s->tgt_attr, NULL);
	*transformed_val = data->prim.uint64_;
	sos_obj_put(match_obj);
	return rc;
}

int map_inverse(map_t map_s, uint64_t match_val, uint64_t *inversed_val)
{
	int rc;
	sos_value_data_t data;
	sos_obj_t match_obj;
	SOS_KEY(inv_key);

	if (!sos_key_for_attr(inv_key, map_s->tgt_attr, match_val))
		return errno;
	rc = sos_iter_find(map_s->tgt_iter, inv_key);
	if (rc)
		return rc;
	match_obj = sos_iter_obj(map_s->tgt_iter);
	data = sos_obj_attr_data(match_obj, map_s->src_attr, NULL);
	*inversed_val = data->prim.uint64_;
	sos_obj_put(match_obj);
	return rc;
}

static
int __attr_find(uint64_t *ret, sos_attr_t attr, sos_obj_t (*find)(sos_index_t, sos_key_t *))
{
	sos_value_data_t data;
	sos_obj_t obj;
	sos_index_t idx = sos_attr_index(attr);
	if (!idx)
		return errno;
	obj = find(idx, NULL);
	if (!obj)
		return ENOENT;
	data = sos_obj_attr_data(obj, attr, NULL);
	*ret = data->prim.uint64_;
	sos_obj_put(obj);
	return 0;
}

int map_transform_min(map_t map, uint64_t *ret)
{
	return __attr_find(ret, map->tgt_attr, sos_index_find_min);
}

int map_transform_max(map_t map, uint64_t *ret)
{
	return __attr_find(ret, map->tgt_attr, sos_index_find_max);
}

int map_inverse_min(map_t map, uint64_t *ret)
{
	return __attr_find(ret, map->src_attr, sos_index_find_min);
}

int map_inverse_max(map_t map, uint64_t *ret)
{
	return __attr_find(ret, map->src_attr, sos_index_find_max);
}

static
int __xform(sos_attr_t from_attr, uint64_t input,
		sos_attr_t to_attr, uint64_t *output,
		int is_ge)
{
	sos_obj_t obj;
	sos_value_data_t data;
	SOS_KEY(key);
	sos_index_t idx = sos_attr_index(from_attr);

	sos_key_set(key, &input, sizeof(input));
	obj = is_ge?sos_index_find_sup(idx, key):sos_index_find_inf(idx, key);
	if (!obj)
		return ENOENT;
	data = sos_obj_attr_data(obj, to_attr, NULL);
	*output = data->prim.uint64_;
	sos_obj_put(obj);
	return 0;
}

int map_transform_ge(map_t map, uint64_t src, uint64_t *dst)
{
	return __xform(map->src_attr, src, map->tgt_attr, dst, 1);
}

int map_transform_le(map_t map, uint64_t src, uint64_t *dst)
{
	return __xform(map->src_attr, src, map->tgt_attr, dst, 0);
}

int map_inverse_ge(map_t map, uint64_t dst, uint64_t *src)
{
	return __xform(map->tgt_attr, dst, map->src_attr, src, 1);
}

int map_inverse_le(map_t map, uint64_t dst, uint64_t *src)
{
	return __xform(map->tgt_attr, dst, map->src_attr, src, 0);
}
