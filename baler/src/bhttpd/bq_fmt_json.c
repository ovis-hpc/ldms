/**
 * \file bq_fmt_json.c
 * \brief Baler Query JSON formatter.
 * \note This code is compiled as a part of bhttpd.
 */
#include "query/bquery.h"
#include "baler/btypes.h"
#include "baler/butils.h"

#include <time.h>

struct bqfmt_json {
	struct bq_formatter base;
	int first_tkn;
	int ptn_label;
	int ptn_id;
	uint64_t msg_ref;
};

void bqfmt_json_set_label(struct bq_formatter *fmt, int label)
{
	((struct bqfmt_json*)fmt)->ptn_label = label;
}

void bqfmt_json_set_ptn_id(struct bq_formatter *fmt, int ptn_id)
{
	((struct bqfmt_json*)fmt)->ptn_id = ptn_id;
}

void bqfmt_json_set_msg_ref(struct bq_formatter *fmt, uint64_t msg_ref)
{
	((struct bqfmt_json*)fmt)->msg_ref = msg_ref;
}

int __bqfmt_json_ptn_prefix(struct bq_formatter *fmt, struct bdstr *bdstr, uint32_t ptn_id)
{
	int rc = 0;
	char buff[32];
	struct bqfmt_json *fj = (void*)fmt;
	return bdstr_append_printf(bdstr, "{ \"type\": \"PTN\", \"ptn_id\": %d"
					, ptn_id);
}

int __bqfmt_json_ptn_suffix(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	return bdstr_append(bdstr, "}");
}

int __bqfmt_json_msg_prefix(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	return bdstr_append_printf(bdstr, "{ \"type\": \"MSG\", "
			"\"ref\": %lu", ((struct bqfmt_json*)fmt)->msg_ref);
}

int __bqfmt_json_msg_suffix(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	return bdstr_append(bdstr, "}");
}

int __bqfmt_json_tkn_begin(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	((struct bqfmt_json*)fmt)->first_tkn = 1;
	return bdstr_append(bdstr, ", \"msg\": [");
}

int __bqfmt_json_tkn_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
		const struct bstr *bstr, struct btkn_attr *attr,
		uint32_t tkn_id)
{
	int rc;
	char buff[128];
	const int lim = sizeof(buff) - 2;
	int i, j;

	if (((struct bqfmt_json*)fmt)->first_tkn) {
		((struct bqfmt_json*)fmt)->first_tkn = 0;
	} else {
		rc = bdstr_append(bdstr, ", ");
		if (rc)
			return rc;
	}
	rc = bdstr_append_printf(bdstr, "{\"tok_type\": \"%s\", "
			"\"text\": \"", btkn_attr_type_str(attr->type));
	if (rc)
		return rc;
	i = 0;
loop:
	j = 0;
	while (j < lim && i < bstr->blen) {
		switch (bstr->cstr[i]) {
		case '\\':
		case '"':
		case '/':
		case '\b':
		case '\f':
		case '\n':
		case '\r':
		case '\t':
			/* These are the characters that need to be escaped. */
			buff[j++] = '\\';
		default:
			buff[j++] = bstr->cstr[i++];
		}
	}
	rc = bdstr_append_printf(bdstr, "%.*s", j, buff);
	if (rc)
		return rc;
	if (i < bstr->blen)
		goto loop;
	rc = bdstr_append_printf(bdstr, "\"}");
	return rc;
}

int __bqfmt_json_tkn_end(struct bq_formatter *fmt, struct bdstr *bdstr)
{
	return bdstr_append(bdstr, "]");
}

int __bqfmt_json_date_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
		time_t ts)
{
	struct tm tm;
	char tmp[64];
	localtime_r(&ts, &tm);
	strftime(tmp, sizeof(tmp), ", \"ts\": \"%F %T\"", &tm);
	return bdstr_append(bdstr, tmp);
}

int __bqfmt_json_host_fmt(struct bq_formatter *fmt, struct bdstr *bdstr,
		const struct bstr *bstr)
{
	int rc;
	rc = bdstr_append(bdstr, ", \"host\": \"");
	if (rc)
		return ENOMEM;
	rc = bdstr_append_bstr(bdstr, bstr);
	if (rc)
		return ENOMEM;
	rc = bdstr_append(bdstr, "\"");
	return rc;
}

struct bq_formatter *bqfmt_json_new()
{
	struct bqfmt_json *fmt = calloc(1, sizeof(*fmt));
	if (!fmt)
		return NULL;
	fmt->base.ptn_prefix = __bqfmt_json_ptn_prefix;
	fmt->base.ptn_suffix = __bqfmt_json_ptn_suffix;
	fmt->base.msg_prefix = __bqfmt_json_msg_prefix;
	fmt->base.msg_suffix = __bqfmt_json_msg_suffix;
	fmt->base.tkn_begin = __bqfmt_json_tkn_begin;
	fmt->base.tkn_fmt = __bqfmt_json_tkn_fmt;
	fmt->base.tkn_end = __bqfmt_json_tkn_end;
	fmt->base.date_fmt = __bqfmt_json_date_fmt;
	fmt->base.host_fmt = __bqfmt_json_host_fmt;
	return &fmt->base;
}

void bqfmt_json_free(struct bq_formatter *fmt)
{
	free(fmt);
}
