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

void yyerror(json_doc_t doc, json_lexer_state_t state,
	     char *input, size_t input_len,
	     const char *str)
{
	;
}

#define OVIS_JSON_DEBUG		0
#define OVIS_JSON_ERROR_VERBOSE 0

int ovis_json_parse(json_doc_t doc, struct json_lexer_state_s *state,
		    char *input, size_t input_len);
int ovis_json_lex(json_entity_t *sval, json_doc_t doc,
		  char *input, size_t input_len,
		  json_lexer_state_t state);
#define YYLEX ovis_json_lex
#define PARSER_SCANNER state

static json_entity_t new_dict_val(json_doc_t doc)
{
	return json_dict_new(doc);
}

static json_entity_t add_dict_attr(json_entity_t e,
				   json_entity_t name,
				   json_entity_t value,
				   json_lexer_state_t state)
{
	if (json_attr_add(e, json_value_cstr(name), value))
		return NULL;
	return e;
}

static json_entity_t new_list_val(json_doc_t doc)
{
	return json_list_new(doc);
}

static json_entity_t add_list_item(json_entity_t e, json_entity_t v,
				   json_lexer_state_t state)
{
	json_item_add(e, v);
	return e;
}

%}

%define api.prefix {ovis_json_}
%define api.pure full

%lex-param { json_doc_t doc }
%lex-param { char *input }
%lex-param { size_t input_len }
%lex-param { yyscan_t PARSER_SCANNER }

%parse-param { json_doc_t doc }
%parse-param { struct json_lexer_state_s *state }
%parse-param { char *input }
%parse-param { size_t input_len }

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

json :  dict { doc->root = $$ = $1; YYACCEPT; }
	| array { doc->root = $$ = $1; YYACCEPT; }
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

attr_list: /* empty */ { $$ = new_dict_val(doc); }
	 | string ':' value {
	     json_entity_t e = new_dict_val(doc);
	     $$ = add_dict_attr(e, $1, $3, state);
	     json_entity_free($1);
	 }
	 | attr_list ',' string ':' value {
	     $$ = add_dict_attr($1, $3, $5, state);
	     json_entity_free($3);
	 }
     ;

item_list: /* empty */ { $$ = new_list_val(doc); }
	| value {
		json_entity_t l = new_list_val(doc);
		$$ = add_list_item(l, $1, state);
	}
	| item_list ',' value {
		$$  = add_list_item($1, $3, state);
	}
    ;

%%
