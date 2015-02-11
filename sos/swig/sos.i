/* File: sos.i */
%module sos
%include "cpointer.i"
%{
#define SWIG_FILE_WITH_INIT
#include "sos.h"
%}

%include "sos.h"

/* These typedef will make swig knows standard integers */
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

%pointer_class(struct sos_key_s, sos_key);
