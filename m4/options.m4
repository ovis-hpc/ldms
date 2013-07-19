dnl SYNOPSIS: OPTION_DEFAULT_ENABLE([name], [enable_flag_var])
dnl EXAMPLE: OPTION_DEFAULT_ENABLE([mysql], [ENABLE_MYSQL])
AC_DEFUN([OPTION_DEFAULT_ENABLE], [
AC_ARG_ENABLE($1, [  --disable-$1     Disable the $1 module],
        [       if test x$enableval = xno ; then
                        disable_$1=yes
			echo $1 module is disabled
                fi
        ])
AM_CONDITIONAL([$2], [test "$disable_$1" != "yes"])
])

dnl SYNOPSIS: OPTION_DEFAULT_DISABLE([name], [enable_flag_var])
dnl EXAMPLE: OPTION_DEFAULT_DISABLE([mysql], [ENABLE_MYSQL])
AC_DEFUN([OPTION_DEFAULT_DISABLE], [
AC_ARG_ENABLE($1, [  --enable-$1     Enable the $1 module],
        [       if test x$enableval = xyes ; then
                        enable_$1=yes
			echo $1 module is enabled
                fi
        ])
AM_CONDITIONAL([$2], [test "$enable_$1" == "yes"])
])

dnl SYNOPSIS: OPTION_WITH([name], [VAR_BASE_NAME])
dnl EXAMPLE: OPTION_WITH([sos], [SOS])
dnl NOTE: With VAR_BASE_NAEM being SOS, this macro will set SOS_INCIDR and
dnl 	SOS_LIBDIR to the include path and library path respectively.
AC_DEFUN([OPTION_WITH], [
AC_ARG_WITH(
	$1,
	AS_HELP_STRING(
		[--with-$1@<:@=path@:>@],
		[Specify $1 path @<:@default=/usr/local@:>@]
	),
	[WITH_$2=$withval
	 AM_CONDITIONAL([ENABLE_$2], [true])
	],
	[WITH_$2=/usr/local]
)

$2_LIBDIR=$WITH_$2/lib
$2_INCDIR=$WITH_$2/include
AC_SUBST([$2_LIBDIR], [$$2_LIBDIR])
AC_SUBST([$2_INCDIR], [$$2_INCDIR])
])

dnl Similar to OPTION_WITH, but a specific case for MYSQL
AC_DEFUN([OPTION_WITH_MYSQL], [
AC_ARG_WITH(
	[mysql],
	AS_HELP_STRING(
		[--with-mysql@<:@=path@:>@],
		[Specify mysql path @<:@default=/usr/local@:>@]
	),
	[	dnl $withval is given.
		WITH_MYSQL=$withval
		MYSQL_LIBDIR=$WITH_MYSQL/lib
		MYSQL_INCDIR=$WITH_MYSQL/include
	],
	[	dnl $withval is not given.
		mysql_config=`which mysql_config`
		if test $mysql_config
		then
			MYSQL_LIBDIR=`mysql_config --libs |
				sed 's/ /\n/g' | grep -- '-L' | sed 's/-L//'`
			MYSQL_INCDIR=`mysql_config --cflags |
				sed 's/ /\n/g' | grep -- '-I' | sed 's/-I//'`
		else
			AC_MSG_ERROR([Cannot find mysql, please specify
					--with-mysql option.])
		fi
	]
)

AC_SUBST([MYSQL_LIBDIR])
AC_SUBST([MYSQL_INCDIR])
])
