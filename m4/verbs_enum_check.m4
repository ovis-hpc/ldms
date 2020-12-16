AC_DEFUN([IBVERBS_ENUM_CHECK],
[# Check for enum elements IBV_WC_T*
AC_LANG_PUSH([C])dnl
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include "stdlib.h"
#include "infiniband/verbs.h"
int check_enum(int i) {
	switch(i) {
	case IBV_WC_TSO:
	case IBV_WC_TM_ADD:
 	case IBV_WC_TM_DEL:
 	case IBV_WC_TM_SYNC:
 	case IBV_WC_TM_RECV:
 	case IBV_WC_TM_NO_TAG:
		break;
	default:
		break;
	}
	return 0;
}
]])], [$1=1], [$1=0])
AC_LANG_POP([C])dnl
])
