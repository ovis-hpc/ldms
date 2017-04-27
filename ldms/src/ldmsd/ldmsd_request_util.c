/*
 * ldmsd_request_util.c
 *
 *  Created on: Apr 28, 2017
 *      Author: nichamon
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "ldmsd_request.h"

struct req_str_id {
	const char *str;
	uint32_t id;
};

const struct req_str_id req_str_id_table[] = {
	/* This table need to be sorted by keyword for bsearch() */
	{  "config",             LDMSD_PLUGN_CONFIG_REQ   },
	{  "daemon",             LDMSD_DAEMON_STATUS_REQ   },
	{  "env",                LDMSD_ENV_REQ  },
	{  "exit",               LDMSD_EXIT_DAEMON_REQ  },
	{  "include",            LDMSD_INCLUDE_REQ  },
	{  "load",               LDMSD_PLUGN_LOAD_REQ   },
	{  "loglevel",           LDMSD_VERBOSE_REQ  },
	{  "logrotate",          LDMSD_LOGROTATE_REQ  },
	{  "oneshot",            LDMSD_ONESHOT_REQ  },
	{  "prdcr_add",          LDMSD_PRDCR_ADD_REQ  },
	{  "prdcr_del",          LDMSD_PRDCR_DEL_REQ  },
	{  "prdcr_start",        LDMSD_PRDCR_START_REQ  },
	{  "prdcr_start_regex",  LDMSD_PRDCR_START_REGEX_REQ  },
	{  "prdcr_stop",         LDMSD_PRDCR_STOP_REQ  },
	{  "prdcr_stop_regex",   LDMSD_PRDCR_STOP_REGEX_REQ  },
	{  "start",              LDMSD_PLUGN_START_REQ   },
	{  "stop",               LDMSD_PLUGN_STOP_REQ   },
	{  "strgp_add",          LDMSD_STRGP_ADD_REQ  },
	{  "strgp_del",          LDMSD_STRGP_DEL_REQ  },
	{  "strgp_metric_add",   LDMSD_STRGP_METRIC_ADD_REQ  },
	{  "strgp_metric_del",   LDMSD_STRGP_METRIC_DEL_REQ  },
	{  "strgp_prdcr_add",    LDMSD_STRGP_PRDCR_ADD_REQ  },
	{  "strgp_prdcr_del",    LDMSD_STRGP_PRDCR_DEL_REQ  },
	{  "strgp_start",        LDMSD_STRGP_START_REQ  },
	{  "strgp_stop",         LDMSD_STRGP_STOP_REQ  },
	{  "term",               LDMSD_PLUGN_TERM_REQ   },
	{  "udata",              LDMSD_SET_UDATA_REQ  },
	{  "udata_regex",        LDMSD_SET_UDATA_REGEX_REQ  },
	{  "updtr_add",          LDMSD_UPDTR_ADD_REQ  },
	{  "updtr_del",          LDMSD_UPDTR_DEL_REQ  },
	{  "updtr_match_add",    LDMSD_UPDTR_MATCH_ADD_REQ  },
	{  "updtr_match_del",    LDMSD_UPDTR_MATCH_DEL_REQ  },
	{  "updtr_prdcr_add",    LDMSD_UPDTR_PRDCR_ADD_REQ  },
	{  "updtr_prdcr_del",    LDMSD_UPDTR_PRDCR_DEL_REQ  },
	{  "updtr_start",        LDMSD_UPDTR_START_REQ  },
	{  "updtr_stop",         LDMSD_UPDTR_STOP_REQ  },
	{  "usage",              LDMSD_PLUGN_LIST_REQ  },
	{  "version",            LDMSD_VERSION_REQ  },
};

/* This table need to be sorted by keyword for bsearch() */
const struct req_str_id attr_str_id_table[] = {
	{  "base",              LDMSD_ATTR_BASE   },
	{  "container",		LDMSD_ATTR_CONTAINER   },
	{  "host",		LDMSD_ATTR_HOST   },
	{  "incr",              LDMSD_ATTR_INCREMENT   },
	{  "instance",		LDMSD_ATTR_INSTANCE   },
	{  "interval",		LDMSD_ATTR_INTERVAL   },
	{  "level",             LDMSD_ATTR_LEVEL   },
	{  "match",		LDMSD_ATTR_MATCH   },
	{  "metric",		LDMSD_ATTR_METRIC   },
	{  "name",		LDMSD_ATTR_NAME   },
	{  "offset",		LDMSD_ATTR_OFFSET   },
	{  "path",              LDMSD_ATTR_PATH   },
	{  "plugin",		LDMSD_ATTR_PLUGIN   },
	{  "port",		LDMSD_ATTR_PORT   },
	{  "producer",		LDMSD_ATTR_PRODUCER   },
	{  "regex",		LDMSD_ATTR_REGEX   },
	{  "schema",		LDMSD_ATTR_SCHEMA   },
	{  "time",              LDMSD_ATTR_TIME   },
	{  "type",		LDMSD_ATTR_TYPE   },
	{  "udata",             LDMSD_ATTR_UDATA   },
	{  "xprt",		LDMSD_ATTR_XPRT   },
};

uint32_t req_str_id_cmp(const struct req_str_id *a, const struct req_str_id *b)
{
	return strcmp(a->str, b->str);
}

uint32_t ldmsd_req_str2id(const char *verb)
{
	struct req_str_id key = {verb, -1};
	struct req_str_id *x = bsearch(&key, req_str_id_table,
			sizeof(req_str_id_table)/sizeof(*req_str_id_table),
			sizeof(*req_str_id_table), (void *)req_str_id_cmp);
	if (!x)
		return LDMSD_NOTSUPPORT_REQ;
	return x->id;
}

uint32_t ldmsd_req_attr_str2id(const char *name)
{
	struct req_str_id key = {name, -1};
	struct req_str_id *x = bsearch(&key, attr_str_id_table,
			sizeof(attr_str_id_table)/sizeof(*attr_str_id_table),
			sizeof(*attr_str_id_table), (void *)req_str_id_cmp);
	if (!x)
		return -1;
	return x->id;
}

/*
 * If both \c name and \c value are NULL, the end attribute is added to req_buf.
 * If \c name is NULL but \c value is not NULL, the attribute of type ATTR_STRING
 * is added to req_buf.
 * If \c name and \c value are not NULL, the attribute of the corresponding type
 * is added to req_buf.
 * Otherwise, EINVAL is returned.
 */
static int add_attr_from_attr_str(char *name, char *value,
		char **reqbuf, size_t *reqbuf_offset, size_t *reqbuf_len)
{
	struct ldmsd_req_attr_s attr;
	char *buf = *reqbuf;
	size_t offset = *reqbuf_offset;
	size_t len = *reqbuf_len;

	if (!name && !value) {
		attr.discrim = 0;
		attr.attr_len = 0;
	} else if (name && !value) {
		/* The attribute value must be provided */
		return EINVAL;
	} else {
		attr.discrim = 1;
		/* Assume that the string av is NULL-terminated */
		attr.attr_len = strlen(value) + 1; /* +1 to include \0 */
		if (!name) {
			/* Caller wants the attribute id of ATTR_STRING */
			attr.attr_id = LDMSD_ATTR_STRING;
		} else {
			attr.attr_id = ldmsd_req_attr_str2id(name);
			if (attr.attr_id < 0) {
				return EINVAL;
			}
		}
	}

	if (len - offset <
			sizeof(struct ldmsd_req_attr_s) + attr.attr_len) {
		buf = realloc(buf, len * 2);
		if (!buf) {
			return ENOMEM;
		}
		len = len * 2;
	}

	memcpy(&buf[offset], &attr, sizeof(attr));
	offset += sizeof(attr);

	if (attr.attr_len) {
		memcpy(&buf[offset], value, attr.attr_len);
		offset += attr.attr_len;
	}

	*reqbuf = buf;
	*reqbuf_len = len;
	*reqbuf_offset = offset;
	return 0;
}

void __get_attr_name_value(char *av, char **name, char **value)
{
	*name = av;
	*value = strchr(av, '=');
	**value = '\0';
	(*value)++;
}

int ldmsd_process_cfg_str(ldmsd_req_hdr_t request, const char *cfg,
			char **buf, size_t *bufoffset, size_t *buflen)
{
	char *av, *verb, *tmp, *ptr, *name, *value, *dummy;
	int rc;

	dummy = strdup(cfg);
	if (!dummy)
		return ENOMEM;
	verb = dummy;
	/* Get the request id */
	av = strchr(dummy, ' ');
	if (av) {
		*av = '\0';
		av++;
	}

	request->marker = LDMSD_RECORD_MARKER;
	request->code = ldmsd_req_str2id(verb);
	if ((request->code < 0) || (request->code == LDMSD_NOTSUPPORT_REQ)) {
		rc = ENOSYS;
		goto err;
	}

	if (!av)
		goto last_attr;

	if (request->code == LDMSD_PLUGN_CONFIG_REQ) {
		size_t len = strlen(av);
		size_t cnt = 0;
		tmp = malloc(len);
		if (!tmp) {
			rc = ENOMEM;
			goto err;
		}
		av = strtok_r(av, " ", &ptr);
		while (av) {
			__get_attr_name_value(av, &name, &value);

			if (0 == strncmp(name, "name", 4)) {
				/* Find the name attribute */
				rc = add_attr_from_attr_str(name, value, buf,
						bufoffset, buflen);
				if (rc) {
					free(tmp);
					goto err;
				}

			} else {
				/* Construct the other attribute into a ATTR_STRING */
				cnt += snprintf(&tmp[cnt], len - cnt, "%s=%s ",
							name, value);
			}
			av = strtok_r(NULL, " ", &ptr);
		}
		tmp[cnt-1] = '\0'; /* Replace the last ' ' with '\0' */
		rc = add_attr_from_attr_str(NULL, tmp, buf, bufoffset, buflen);
		free(tmp);
		if (rc)
			goto err;
	} else {
		av = strtok_r(av, " ", &ptr);
		while (av) {
			__get_attr_name_value(av, &name, &value);
			rc = add_attr_from_attr_str(name, value,
					buf, bufoffset, buflen);
			if (rc)
				goto err;
			av = strtok_r(NULL, " ", &ptr);
		}
	}
last_attr:
	/* Add the end attribute */
	rc = add_attr_from_attr_str(NULL, NULL, buf, bufoffset, buflen);
	request->rec_len = *bufoffset + sizeof(*request);
	free(dummy);
	return 0;
err:
	free(dummy);
	return rc;
}
