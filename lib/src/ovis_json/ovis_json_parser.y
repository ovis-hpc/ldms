%{
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ovis_json.h"
#include "ovis_json_priv.h"

#define OVIS_JSON_LTYPE json_lexer_state_t
#define OVIS_JSON_STYPE json_entity_t

void yyerror(json_parser_t parser,
	     char *input, size_t input_len,
	     json_entity_t *p_entity, const char *str)
{
	json_entity_free(*p_entity);
}

#define OVIS_JSON_DEBUG		0
#define OVIS_JSON_ERROR_VERBOSE 1

int ovis_json_parse(json_parser_t parser, char *input, size_t input_len, json_entity_t *entity);
int ovis_json_lex(json_entity_t *sval, json_parser_t,
		  char *input, size_t input_len,
		  json_lexer_state_t state);
#define YYLEX ovis_json_lex
#define PARSER_SCANNER &parser->state

static inline json_entity_t new_dict_val(json_parser_t p) {
	return json_entity_new(p, JSON_DICT_VALUE);
}

static inline json_entity_t add_dict_attr(json_entity_t e, json_entity_t name, json_entity_t value)
{
	if (json_attr_add(e, json_value_cstr(name), value))
		return NULL;
	return e;
}

static inline json_entity_t new_list_val(json_parser_t p) {
	return json_entity_new(p, JSON_LIST_VALUE);
}

static inline json_entity_t add_list_item(json_entity_t e, json_entity_t v)
{
	json_item_add(e, v);
	return e;
}

%}

%define api.prefix {ovis_json_}
%define api.pure full

%lex-param { json_parser_t parser }
%lex-param { char *input }
%lex-param { size_t input_len }
%lex-param { yyscan_t PARSER_SCANNER }

%parse-param { json_parser_t parser }
%parse-param { char *input }
%parse-param { size_t input_len }
%parse-param { json_entity_t *pentity }

%token ',' ':'
%token '[' ']'
%token '{' '}'
%token '.'
%token '"'
%token OVIS_JSON_STRING_T
%token OVIS_JSON_INTEGER_T
%token OVIS_JSON_FLOAT_T
%token OVIS_JSON_BOOL_T
%token OVIS_JSON_NULL_T

%start json

%%

json :  dict { *pentity = $$ = $1; YYACCEPT; }
	| array { *pentity = $$ = $1; YYACCEPT; }
	;

dict : '{' attr_list '}' { $$ = $2; } ;

array : '[' item_list ']' { $$ = $2; } ;

value : OVIS_JSON_INTEGER_T { $$ = $1; }
	 | OVIS_JSON_FLOAT_T { $$ = $1; }
	 | OVIS_JSON_BOOL_T { $$ = $1; }
	 | OVIS_JSON_NULL_T { $$ = $1; }
	 | string { $$ = $1; }
	 | dict { $$ = $1; }
	 | array { $$ = $1; }
	 ;

string : OVIS_JSON_STRING_T { $$ = $1; }
	 ;

attr_list: /* empty */ {$$ = new_dict_val(parser); }
	 | string ':' value {
	     json_entity_t e = new_dict_val(parser);
	     $$ = add_dict_attr(e, $1, $3);
	     json_entity_free($1);
	 }
	 | attr_list ',' string ':' value {
	     $$ = add_dict_attr($1, $3, $5);
	     json_entity_free($3);
	 }
     ;

item_list: /* empty */ { $$ = new_list_val(parser); }
	| value {
		json_entity_t l = new_list_val(parser);
		$$ = add_list_item(l, $1);
	}
	| item_list ',' value {
		$$  = add_list_item($1, $3);
	}
    ;

%%

