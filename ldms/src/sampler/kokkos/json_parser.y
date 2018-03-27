%{
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "json.h"

void yyerror(json_parser_t parser, char *input, size_t input_len,
	     json_entity_t *pentity, const char *str)
{
	int pos;
	fprintf(stderr, "line number   : %d\n", parser->lnum);
	fprintf(stderr, "column number : %d\n", parser->cpos);
	/*
	for (pos = 0; pos < parser->cpos; pos++)
		fprintf(stderr, " ");
	fprintf(stderr, "^\n");
	fprintf(stderr, "%s\n", str);
	*/
	if (*pentity) {
		json_entity_free(*pentity);
		*pentity = NULL;
	}
}

extern size_t yyleng;

#define YYDEBUG		1
#define YYERROR_VERBOSE 1

int yyparse(json_parser_t parser, char *input, size_t input_len, json_entity_t *entity);
void yy_delete_buffer(struct yy_buffer_state *);
int yylex(void *, json_parser_t, char *input, size_t input_len);

%}

%define api.pure full
%lex-param { json_parser_t parser }
%lex-param { char *input }
%lex-param { size_t input_len }
%parse-param { json_parser_t parser }
%parse-param { char *input }
%parse-param { size_t input_len }
%parse-param { json_entity_t *pentity }

%token COMMA_T COLON_T
%token L_SQUARE_T R_SQUARE_T
%token L_CURLY_T R_CURLY_T
%token DQUOTED_STRING_T SQUOTED_STRING_T
%token INTEGER_T FLOAT_T
%token BOOL_T
%token NULL_T

%start json

%%

json : value	{
	 $$ = *pentity = $1;
	 YYACCEPT;
	 }
	 ;

dict : L_CURLY_T attr_list R_CURLY_T { $$ = $2; } ;

array : L_SQUARE_T item_list R_SQUARE_T { $$ = $2; } ;

value : INTEGER_T { $$ = $1; }
	 | FLOAT_T { $$ = $1; }
	 | BOOL_T { $$ = $1; }
	 | NULL_T { $$ = $1; }
	 | string { $$ = $1; }
	 | dict { $$ = $1; }
	 | array { $$ = $1; }
	 ;

string : DQUOTED_STRING_T { $$ = $1; }
	 | SQUOTED_STRING_T { $$ = $1; }
	 ;

attr_list: /* empty */ { $$ = new_dict_val(); }
	 | string COLON_T value {
	     json_entity_t e = new_dict_val();
	     add_dict_attr(e, $1, $3);
	     $$ = e;
	 }
	 | attr_list COMMA_T string COLON_T value {
	     $$ = add_dict_attr($1, $3, $5);
	 }
     ;

item_list: /* empty */ { $$ = new_array_val(); }
     | value {
	 json_entity_t a = new_array_val();
	 $$ = add_array_item(a, $1);
     }
     | item_list COMMA_T value {
	 $$  = add_array_item($1, $3);
     }
     ;

%%
int json_parse_buffer(json_parser_t p, char *buf, size_t buf_len, json_entity_t *pentity)
{
	 int rc;

	 *pentity = NULL;
	 if (p->buffer_state) {
		 /* The previous call did not reset the lexer state */
		 yy_delete_buffer(p->buffer_state);
		 p->lnum = p->cpos = 0;
		 p->buffer_state = NULL;
	 }

	 rc = yyparse(p, buf, buf_len, pentity);
	 if (rc)
		 return EINVAL;

	 return 0;
}

static void json_array_free(json_entity_t e)
{
	json_item_t i;
	while (!TAILQ_EMPTY(&e->value.array_->item_list)) {
		i = TAILQ_FIRST(&e->value.array_->item_list);
		TAILQ_REMOVE(&e->value.array_->item_list, i, item_entry);
		json_entity_free(i->item);
		free(i);
	}
}

static void json_dict_free(json_entity_t e)
{
	json_attr_t i;
	while (!TAILQ_EMPTY(&e->value.dict_->attr_list)) {
		i = TAILQ_FIRST(&e->value.dict_->attr_list);
		TAILQ_REMOVE(&e->value.dict_->attr_list, i, attr_entry);
		json_entity_free(i->value);
		free(i);
	}
}

void json_entity_free(json_entity_t e)
{
	switch (e->type) {
	case JSON_INT_VALUE:
		free(e);
		break;
	case JSON_BOOL_VALUE:
		free(e);
		break;
	case JSON_FLOAT_VALUE:
		free(e);
		break;
	case JSON_STRING_VALUE:
		free(e->value.str_);
		free(e);
		break;
	case JSON_ARRAY_VALUE:
		json_array_free(e);
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
