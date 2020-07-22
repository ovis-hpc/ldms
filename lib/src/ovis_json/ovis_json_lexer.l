%{

#include <stdlib.h>
#include <inttypes.h>
#include <ovis_json.h>
#include "ovis_json_parser.h"

#define YYSTYPE json_entity_t

struct yyguts_t;
void update_yylloc(YYLTYPE *, struct yyguts_t *);
void init_yylloc(YYLTYPE *, struct yyguts_t *);

#define YY_DECL int yylex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param, json_parser_t parser, char *input, size_t input_len, yyscan_t yyscanner)
#define YY_USER_ACTION	update_yylloc(yylloc, yyg);

static inline char *str_strip_dup(const char *src)
{
	char *dst = malloc(strlen(src));
	if (dst) {
		strcpy(dst, &src[1]);
		dst[strlen(dst)-1]= '\0';
	}
	return dst;
}

static inline json_entity_t new_int_val(char *str) {
	int64_t ll = strtoll(str, NULL, 0);
	return json_entity_new(JSON_INT_VALUE, ll);
}

static inline json_entity_t new_bool_val(char *str) {
	int i;
	if (0 == strcmp(str, "true"))
		i = 1;
	else
		i = 0;
	return json_entity_new(JSON_BOOL_VALUE, i);
}

static inline json_entity_t new_float_val(char *str) {
	double d = strtold(str, NULL);
	return json_entity_new(JSON_FLOAT_VALUE, d);
}

static inline json_entity_t new_null_val(void) {
	return json_entity_new(JSON_NULL_VALUE);
}

static inline json_entity_t new_string_val(char *str) {
	char *s = str_strip_dup(str);
	json_entity_t e = json_entity_new(JSON_STRING_VALUE, s);
	free(s);
	return e;
}

%}

%option noyywrap
%option noinput nounput noyywrap 8bit nodefault
%option yylineno
%option reentrant bison-bridge bison-locations

UnicodeEscapeSequence                   u[0-9A-Fa-f]{4}
JSONEscapeCharacter                     ["\\/bfnrt]
JSONEscapeSequence                      {JSONEscapeCharacter}|{UnicodeEscapeSequence}
JSONStringCharacter                     [^"\\]|\\{JSONEscapeSequence}
JSONString                              \"{JSONStringCharacter}*\"

%%
	{
	init_yylloc(yylloc_param, yyg);
	}

<<EOF>>	{
	return 0;
	}

{JSONString}	{
	*yylval = new_string_val(yytext);
	return DQUOTED_STRING_T;
	}
\'[^\']*\'	{
	*yylval = new_string_val(yytext);
	return SQUOTED_STRING_T;
	}
"["	{ return '['; }
"]"	{ return ']'; }
"{"	{ return '{'; }
"}"	{ return '}'; }
","	{ return ','; }
":"	{ return ':'; }
\n	{ }
[ \t]	{ }
[-+]?[0-9]+	{
	*yylval = new_int_val(yytext);
	return INTEGER_T;
	}
[-+]?[0-9]*\.?[0-9]*([eE][-+]?[0-9]+)?	{
	*yylval = new_float_val(yytext);
	return FLOAT_T;
	}
true|false	{
	*yylval = new_bool_val(yytext);
	return BOOL_T;
	}
null	{ *yylval = new_null_val(); return NULL_T; }
.	{  }

%%

void update_yylloc(YYLTYPE *yyl, struct yyguts_t *yyg)
{
	yyl->first_line = yyl->last_line;
	yyl->first_column = yyl->last_column;
	if (yyl->last_line == yylineno) {
		yyl->last_column += yyleng;
	} else {
		yyl->last_line = yylineno;
		yyl->last_column = yytext + yyleng - strrchr(yytext, '\n');
	}
}

void init_yylloc(YYLTYPE *yyl, struct yyguts_t *yyg)
{
	yyl->first_line = yyl->last_line = 0;
	yyl->first_column = yyl->last_column = 0;
	yylineno = 0;
}

int json_parse_buffer(json_parser_t p, char *buf, size_t buf_len, json_entity_t *pentity)
{
	int rc;
	*pentity = NULL;
	char *nbuf = malloc(buf_len + 2);
	if (!nbuf)
		return ENOMEM;
	memcpy(nbuf, buf, buf_len);
	nbuf[buf_len] = YY_END_OF_BUFFER_CHAR;
	nbuf[buf_len+1] = YY_END_OF_BUFFER_CHAR;
	if (NULL == yy_scan_buffer(nbuf, buf_len + 2, p->scanner)) {
		rc = EINVAL;
		goto out;
	}
	rc = yyparse(p, nbuf, buf_len + 2, pentity);
out:
	free(nbuf);
	return rc;
}
