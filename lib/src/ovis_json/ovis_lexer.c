#include <ovis_json/ovis_json.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <regex.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "ovis_json_priv.h"
#include "ovis_json_parser.h"

#ifndef OVIS_JSON_EOF
#define OVIS_JSON_EOF 0
#endif

static int
big__string(json_doc_t doc, json_entity_t *p_entity,
	    char *input, size_t input_len, json_lexer_state_t st)
{
	char ch;
	size_t tok_sz, rem;
	char *src = &input[st->buf_pos+1];
	rem = input_len - st->buf_pos - 1;
	tok_sz = 0;
	for (ch = *src; tok_sz < rem; ch = *src) {
		if (ch == '"' || ch == '\n' || ch == '\0')
			break;
		tok_sz++;
		src++;
		if (ch != '\\')
			continue;
		tok_sz++;
		src++;
		/* Handle escaped '"' */
		if (tok_sz >= rem)
			goto eof;
	}
	if (ch != '"') {
		/* This is the case where the string is not terminated */
		goto eof;
	}
	*p_entity = json_string_new(doc, &input[st->buf_pos+1], tok_sz);
	st->buf_pos += tok_sz + 2;
	st->line_loc += tok_sz + 2;
	st->stats.overflow_count ++;
	return OVIS_JSON_STRING_T;
 eof:
	return OVIS_JSON_EOF;
}

static int
parse_string(json_doc_t doc, json_entity_t *p_entity,
	     char *input, size_t input_len, json_lexer_state_t st)
{
	char ch;
	size_t tok_sz, rem;
	json_entity_t str = json_string_new(doc, "", 0);
	char *s = str->value.str_.str;
	char *src;

	/* Attempt to copy the string into the entity char by char and
	 * avoid processing the string twice; once to determine it's
	 * length, and a second time to copy it into the destination
	 * entity. The special case is if the input string is bigger
	 * than JSON_ATTR_NAME_MAX, which requires a memory allocation
	 * and restart of the processing.
	 */
	rem = input_len - st->buf_pos - 1;
	src = &input[st->buf_pos+1];
	tok_sz = 0;
	for (ch = *src; tok_sz < rem; ch = *src) {
		if (ch == '"' || ch == '\n' || ch == '\0')
			break;

		tok_sz++;
		if (tok_sz >= JSON_ATTR_NAME_MAX) {
			json_entity_free(str);
			return big__string(doc, p_entity, input, input_len, st);
		}

		*s++ = *src++;
		str->value.str_.str_len ++;
		if (ch == '\\') {
			/* Handle escaped '"' */
			tok_sz ++;
			*s++ = *src++;
			str->value.str_.str_len ++;
		}
	}
	if (ch != '"') {
		/* This is the case where the string is not terminated
		 * or too big to fit in the allocated size */
		goto eof;
	}
	*s = '\0';
	*p_entity = str;
	st->buf_pos += tok_sz + 2;
	st->line_loc += tok_sz + 2;
	st->stats.fit_count ++;
	return OVIS_JSON_STRING_T;
 eof:
	json_entity_free(str);
	return OVIS_JSON_EOF;
}

/**
 *
 */
int ovis_json_lex(json_entity_t *p_entity, json_doc_t doc,
		  char *input, size_t input_len,
		  json_lexer_state_t st)
{
	if (st->buf_pos >= input_len)
		return OVIS_JSON_EOF;

	/* Skip whitespace */
	for (; input[st->buf_pos] != '\0' && isspace(input[st->buf_pos]);
	     st->buf_pos++, st->line_loc++) {
		if (input[st->buf_pos] == '\n') {
			st->line_no++;
			st->line_loc = 0;
		}
	}
	if (st->buf_pos >= input_len)
		return OVIS_JSON_EOF;

	char ch = input[st->buf_pos];
	long jint;
	double jfloat;
	int start = st->buf_pos;

	/* Delimeter */
	switch (ch) {
	case ',':
	case '[':
	case ']':
	case '{':
	case '}':
	case ':':
		st->buf_pos ++;
		st->line_loc ++;
		return ch;
	case '\0':
		return OVIS_JSON_EOF;
	}

	/* Quoted string */
	if (ch == '"')
		return parse_string(doc, p_entity, input, input_len, st);

	/* Number */
	long sign = 1;
	if (ch == '-') {
		sign = -1;
		st->buf_pos += 1;
		ch = input[st->buf_pos];
	} else if (ch == '+') {
		st->buf_pos += 1;
		ch = input[st->buf_pos];
	} else if (!isdigit(ch)) {
		goto keyword;
	}
	jint = 0;
	while (isdigit(ch)) {
		jint = (jint * 10) + (long)(ch - '0');
		ch = input[++st->buf_pos];
	}
	if (ch != '.' && ch != 'e' && ch != 'E') {
		/* Integer */
		jint *= sign;
		*p_entity = json_int_new(doc, jint);
		st->stats.int_count++;
		st->line_loc += st->buf_pos - start;
		return OVIS_JSON_INTEGER_T;
	} else {
		/* Floating point. */
		double divisor = 10.0;
		sign = 1;
		jfloat = (double)jint;
		if (ch == '.') {
			st->buf_pos++;
			ch = input[st->buf_pos];
			while (isdigit(ch)) {
				jfloat += (double)(ch - '0') / divisor;
				divisor *= 10.0;
				st->buf_pos ++;
				ch = input[st->buf_pos];
			}
		}
		double exp;
		if (ch == 'e' || ch == 'E') {
			st->buf_pos++;
			ch = input[st->buf_pos];
			if (ch == '-') {
				st->buf_pos++;
				ch = input[st->buf_pos];
				sign = -1;
			} else if (ch == '+') {
				st->buf_pos++;
				ch = input[st->buf_pos];
			}
			divisor = 10.0;
			exp = 0.0;
			if (sign == 1) {
				while (isdigit(ch)) {
					exp += (double)(ch - '0') * divisor;
					divisor *= 10.0;
					st->buf_pos ++;
					ch = input[st->buf_pos];
				}
			} else {
				while (isdigit(ch)) {
					exp += (double)(ch - '0') / divisor;
					divisor *= 10.0;
					st->buf_pos ++;
					ch = input[st->buf_pos];
				}
			}
			jfloat *= exp;
		}
		*p_entity = json_float_new(doc, jfloat);
		st->stats.float_count ++;
		st->line_loc += st->buf_pos - start;
		return OVIS_JSON_FLOAT_T;
	}
	/* Keyword */
keyword:
	switch (ch) {
	case 't':
		if (0 == strncmp(&input[st->buf_pos], "true", 4)) {
			st->buf_pos += 4;
			st->line_loc += 4;
			*p_entity = json_entity_new(doc, JSON_BOOL_VALUE, 1);
			st->stats.bool_count++;
			return OVIS_JSON_BOOL_T;
		}
		break;
	case 'f':
		if (0 == strncmp(&input[st->buf_pos], "false", 5)) {
			st->buf_pos += 5;
			st->line_loc += 5;
			*p_entity = json_entity_new(doc, JSON_BOOL_VALUE, 0);
			st->stats.bool_count++;
			return OVIS_JSON_BOOL_T;
		}
		break;
	case 'n':
		if (0 == strncmp(&input[st->buf_pos], "null", 4)) {
			st->buf_pos += 4;
			st->line_loc += 4;
			*p_entity = json_entity_new(doc, JSON_NULL_VALUE);
			st->stats.null_count++;
			return OVIS_JSON_NULL_T;
		}
		break;
	default:
		assert(0);
		break;
	}
	return OVIS_JSON_EOF;
}
