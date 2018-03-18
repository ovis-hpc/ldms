%{

#include <stdlib.h>
#include <inttypes.h>
#include <json.h>
#include "json_parser.h"

#define YY_DECL int yylex(YYSTYPE *lvalp, json_parser_t parser, char *input, size_t input_len)
#define YY_USER_ACTION parser->cpos += yyleng;

%}

UnicodeEscapeSequence                   u[0-9A-Fa-f]{4}
JSONEscapeCharacter                     ["\\/bfnrt]
JSONEscapeSequence                      {JSONEscapeCharacter}|{UnicodeEscapeSequence}
JSONStringCharacter                     [^"\\]|\\{JSONEscapeSequence}
JSONString                              \"{JSONStringCharacter}*\"

%%
	if (!parser->buffer_state) {
		parser->buffer_state = yy_scan_string(input);
		parser->lnum = parser->cpos = 0;
	}

<<EOF>>	{
	yy_delete_buffer(parser->buffer_state);
	parser->buffer_state = NULL;
	return 0;
	}

{JSONString}	{
	*lvalp = new_string_val(yytext);
	return DQUOTED_STRING_T;
	}
\'[^\']*\'	{
	*lvalp = new_string_val(yytext);
	return SQUOTED_STRING_T;
	}
"["	{ return L_SQUARE_T; }
"]"	{ return R_SQUARE_T; }
"{"	{ return L_CURLY_T; }
"}"	{ return R_CURLY_T; }
","	{ return COMMA_T; }
":"	{ return COLON_T; }
\n	{ parser->cpos = 0; parser->lnum += 1; }
[ \t]	{ }
[-+]?[0-9]+	{
	*lvalp = new_int_val(yytext);
	return INTEGER_T;
	}
[-+]?[0-9]*\.?[0-9]*([eE][-+]?[0-9]+)?	{
	*lvalp = new_float_val(yytext);
	return FLOAT_T;
	}
true|false	{
	*lvalp = new_bool_val(yytext);
	return BOOL_T;
	}
null	{ *lvalp = new_null_val(); return NULL_T; }
.	{  }

%%

