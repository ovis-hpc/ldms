#include <ovis_json/ovis_json.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <regex.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "ovis_json_parser.h"
#include "ovis_json_priv.h"

#ifndef OVIS_JSON_EOF
#define OVIS_JSON_EOF 0
#endif

/*
 * Initialize the buffer state
 */
void json_lexer_init(json_lexer_state_t state)
{
	state->cur_line = 0;
	state->cur_loc = 0;
	state->cur_pos = 0;
}

/**
 *
 */
int ovis_json_lex(json_entity_t *p_entity, json_parser_t p,
		  char *input, size_t input_len,
		  json_lexer_state_t *p_state)
{
	int rc;
	json_lexer_state_t st;
	st = &p->state;
	if (st->cur_pos >= input_len)
		return OVIS_JSON_EOF;

	/* Skip whitespace */
	for (; input[st->cur_pos] != '\0' && isspace(input[st->cur_pos]);
	     st->cur_pos++, st->cur_loc++) {
		if (input[st->cur_pos] == '\n') {
			st->cur_line++;
			st->cur_loc = 0;
		}
	}
	if (st->cur_pos >= input_len)
		return OVIS_JSON_EOF;

	char ch = input[st->cur_pos];
	size_t tok_sz = 0;
	regmatch_t match;

	switch (ch) {
	case '"':		/* Match a string */
		tok_sz = 0;
		st->cur_pos += 1;	/* Skip " */
		for (ch = input[st->cur_pos];
		     st->cur_pos < input_len;
		     ch = input[st->cur_pos]) {
			if (ch == '\\') {
				st->cur_pos++;
				st->cur_loc++;
				if (st->cur_pos >= input_len) {
					return OVIS_JSON_EOF;
				}
				st->cur_pos++;
				st->cur_loc++;
				if (st->cur_pos >= input_len) {
					return OVIS_JSON_EOF;
				}
				tok_sz += 2;
				continue;
			}
			if (ch == '"' || ch == '\n' || ch == '\0')
				break;
			tok_sz++;
			st->cur_pos++;
			st->cur_loc++;
		}
		if (ch != '"') {
			return OVIS_JSON_EOF;
		}
		*p_entity = json_entity_new(p, JSON_STRING_VALUE,
					    &input[st->cur_pos - tok_sz],
					    (size_t)tok_sz);
		st->cur_pos++;	/* Skip closing quote */
		st->cur_loc++;
		return OVIS_JSON_STRING_T;
	case ',':
	case '[':
	case ']':
	case '{':
	case '}':
	case ':':
		st->cur_pos ++;
		st->cur_loc ++;
		return ch;
	case '\0':
		return OVIS_JSON_EOF;
	default:
		if (ch == '-' || ch == '+' || isdigit(ch)) {
			int start = st->cur_pos;
			st->cur_pos++;
			st->cur_loc++;
			for (ch = input[st->cur_pos]; isdigit(ch); ch = input[st->cur_pos]) {
				st->cur_pos++;
				st->cur_loc++;
			}
			if (ch != '.' && ch != 'e' && ch != 'E') {
				// Integer
				*p_entity = json_entity_new(p, JSON_INT_VALUE, atoi(&input[start]));
				return OVIS_JSON_INTEGER_T;
			} else {
				rc = regexec(&p->f_regex,
					&input[start],
					1, &match, 0);
				if (0 == rc) {
					st->cur_pos = start + match.rm_eo;
					st->cur_loc += match.rm_eo;
					*p_entity =
						json_entity_new(p, JSON_FLOAT_VALUE,
								atof(&input[start]));
					return OVIS_JSON_FLOAT_T;
				}
			}
		}
		if (0 == strncmp(&input[st->cur_pos], "true", 4)) {
			st->cur_loc += 4;
			*p_entity = json_entity_new(p, JSON_BOOL_VALUE, 1);
			return OVIS_JSON_BOOL_T;
		}
		if (0 == strncmp(&input[st->cur_pos], "false", 5)) {
			st->cur_pos += 5;
			st->cur_loc += 5;
			*p_entity = json_entity_new(p, JSON_BOOL_VALUE, 0);
			return OVIS_JSON_BOOL_T;
		}
		if (0 == strncmp(&input[st->cur_pos], "null", 4)) {
			st->cur_pos += 4;
			st->cur_loc += 4;
			*p_entity = json_entity_new(p, JSON_NULL_VALUE);
			return OVIS_JSON_NULL_T;
		}
		return OVIS_JSON_EOF;
	}
	return OVIS_JSON_EOF;
}
