#include <stdio.h>
#include <stdlib.h>
#include <json/json_util.h>
#include "ldmsd.h"
#include "ldmsd_translator.h"

typedef struct json_translator_inst {
	struct ldmsd_plugin_inst_s base;
	uint16_t msg_no;
	char *path;
} *json_translator_inst_t;

/* ============== Common Plugin APIs ================= */

static
const char *json_translator_desc(ldmsd_plugin_inst_t pi)
{
	return "json_translator - Translate configuration files containing JSON cfgobj requests";
}

static
const char *json_translator_help(ldmsd_plugin_inst_t pi)
{
	return "json_translator does not take extra options.";
}

static
void json_translator_del(ldmsd_plugin_inst_t pi)
{
	/* do nothing */
}

static json_entity_t
json_translator_translate(ldmsd_plugin_inst_t pi, const char *path, json_entity_t req_list)
{
	int rc;
	json_entity_t req, reqs = NULL;
	json_parser_t p = NULL;
	char s[1024];
	char *data = NULL;
	size_t fsz;
	FILE *fin = NULL;
	json_translator_inst_t inst = (void *)pi;

	inst->path = strdup(path);
	if (!inst->path)
		goto oom;

	fin = fopen(path, "rt");
	if (!fin) {
		rc = errno;
		strerror_r(rc, s, 1024 - 1);
		ldmsd_log(LDMSD_LERROR, "Failed to open the config file '%s'. %s\n",
				path, s);
		goto err;
	}

	fseek(fin, 0, SEEK_END);
	fsz = ftell(fin);
	fseek(fin, 0, SEEK_SET);

	data = malloc(fsz + 1);
	if (!data)
		goto oom;
	fread(data, 1, fsz, fin);
	fclose(fin);
	fin = NULL;
	data[fsz] = 0;

	p = json_parser_new(0);
	if (!p)
		goto oom;
	rc = json_parse_buffer(p, data, fsz, &reqs);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "json_translator: failed to parse "
				"the config file %s. Error %d\n", path, rc);
		goto err;
	}
	json_parser_free(p);

	if (JSON_LIST_VALUE != json_entity_type(reqs)) {
		if (JSON_DICT_VALUE == json_entity_type(reqs)) {
			json_item_add(req_list, reqs);
			goto out;
		} else {
			ldmsd_log(LDMSD_LERROR, "json_translator: The configuration "
					"file '%s' contains wrong format. "
					"Expecting a list of request dictionaries "
					"or a request dictionary.\n", path);
			rc = EINVAL;
			goto err;
		}
	}

	while (0 < json_list_len(reqs)) {
		req = json_item_pop(reqs, 0);
		json_item_add(req_list, req);
	}
	json_entity_free(reqs);

out:
	free(data);
	return req_list;

oom:
	rc = ENOMEM;
err:
	if (fin)
		fclose(fin);
	if (data)
		free(data);
	if (reqs)
		json_entity_free(reqs);
	errno = rc;
	return NULL;
}

static int json_translator_error_report(ldmsd_plugin_inst_t pi, json_entity_t reply)
{
	json_entity_t results, result, err, msg, value, a, v;
	json_str_t cfgobj_name, key;
	int id, status;

	id = json_value_int(json_value_find(reply, "id"));
	status = json_value_int(json_value_find(reply, "status"));
	if (status) {
		msg = json_value_find(reply, "msg");
		if (msg) {
			ldmsd_log(LDMSD_LERROR, "REQ ID %d: Error %d: %s\n",
					id, status,
					json_value_str(msg)->str);
		} else {
			ldmsd_log(LDMSD_LERROR, "REQ ID %d: Error %d\n", id, status);
		}
	}
	results = json_value_find(reply, "result");
	if (!results)
		return 0;

	for (result = json_attr_first(results); result; result = json_attr_next(result)) {
		cfgobj_name = json_attr_name(result);
		err = json_attr_value(result);

		status = json_value_int(json_value_find(err, "status"));
		if (status) {
			msg = json_value_find(err, "msg");
			if (msg) {
				ldmsd_log(LDMSD_LERROR, "REQ_ID %d: Error %d: '%s': %s\n",
						id, status, cfgobj_name->str,
						json_value_str(msg)->str);
			}

			value = json_value_find(err, "value");
			if (value) {
				for (a = json_attr_first(value); a; a = json_attr_next(a)) {
					key = json_attr_name(a);
					v = json_attr_value(a);
					if (JSON_STRING_VALUE == json_entity_type(v)) {
						ldmsd_log(LDMSD_LERROR, "REQ_ID %d: Error %d: '%s': [%s] %s\n",
								id, status, cfgobj_name->str,
								key->str,  json_value_str(v)->str);
					} else {
						jbuf_t buf = json_entity_dump(NULL, v);
						if (!buf)
							return ENOMEM;
						ldmsd_log(LDMSD_LERROR, "REQ_ID %d: Error %d: '%s': [%s] %s\n",
								id, status, cfgobj_name->str,
								key->str, buf->buf);
					}
				}
			}
		}
	}
	return 0;
}

static
int json_translator_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_translator_type_t translator = (void *)pi->base;

	translator->translate = json_translator_translate;
	translator->config_file_export = NULL;
	translator->error_report = json_translator_error_report;
	return 0;
}


static struct json_translator_inst __inst = {
		.base = {
				.version = LDMSD_PLUGIN_VERSION_INITIALIZER,
				.type_name = "translator",
				.plugin_name = "json_translator",


				/* Common Plugin APIs */
				.desc   = json_translator_desc,
				.help   = json_translator_help,
				.init   = json_translator_init,
				.del    = json_translator_del,
		},
};

ldmsd_plugin_inst_t new()
{
	json_translator_inst_t inst = calloc(1, sizeof(*inst));
	if (inst) {
		*inst = __inst;
		return &inst->base;
	} else {
		return NULL;
	}
}
