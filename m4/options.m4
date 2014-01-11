dnl SYNOPSIS: OPTION_DEFAULT_ENABLE([name], [enable_flag_var])
dnl EXAMPLE: OPTION_DEFAULT_ENABLE([mysql], [ENABLE_MYSQL])
dnl note: supports hyphenated feature names now.
AC_DEFUN([OPTION_DEFAULT_ENABLE], [
AC_ARG_ENABLE($1, [  --disable-$1     Disable the $1 module],
        [       if test "x$enableval" = "xno" ; then
                        disable_]m4_translit([$1], [-+.], [___])[=yes
                        enable_]m4_translit([$1], [-+.], [___])[=no
			AC_MSG_NOTICE([Disable $1 module requested ])
                fi
        ], [ AC_MSG_NOTICE([Disable $1 module NOT requested]) ])
AM_CONDITIONAL([$2], [test "$disable_]m4_translit([$1], [-+.], [___])[" != "yes"])
])

dnl SYNOPSIS: OPTION_DEFAULT_DISABLE([name], [enable_flag_var])
dnl EXAMPLE: OPTION_DEFAULT_DISABLE([mysql], [ENABLE_MYSQL])
dnl note: supports hyphenated feature names now.
AC_DEFUN([OPTION_DEFAULT_DISABLE], [
AC_ARG_ENABLE($1, [  --enable-$1     Enable the $1 module],
        [       if test "x$enableval" = "xyes" ; then
                        enable_]m4_translit([$1], [-+.], [___])[=yes
                        disable_]m4_translit([$1], [-+.], [___])[=no
			AC_MSG_NOTICE([Enable $1 module requested])
                fi
        ], [ AC_MSG_NOTICE([Enable $1 module NOT requested]) ])
AM_CONDITIONAL([$2], [test "$enable_]m4_translit([$1], [-+.], [___])[" == "yes"])
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

if test -d $WITH_$2/lib; then
	$2_LIBDIR=$WITH_$2/lib
	$2_LIBDIR_FLAG=-L$WITH_$2/lib
fi
if test "x$$2_LIBDIR" = "x"; then
	$2_LIBDIR=$WITH_$2/lib64
	$2_LIBDIR_FLAG=-L$WITH_$2/lib64
fi
if test -d $WITH_$2/lib64; then
	$2_LIB64DIR=$WITH_$2/lib64
	$2_LIB64DIR_FLAG=-L$WITH_$2/lib64
fi
if test -d $WITH_$2/include; then
	$2_INCDIR=$WITH_$2/include
	$2_INCDIR_FLAG=-I$WITH_$2/include
fi
AC_SUBST([$2_LIBDIR], [$$2_LIBDIR])
AC_SUBST([$2_LIB64DIR], [$$2_LIB64DIR])
AC_SUBST([$2_INCDIR], [$$2_INCDIR])
AC_SUBST([$2_LIBDIR_FLAG], [$$2_LIBDIR_FLAG])
AC_SUBST([$2_LIB64DIR_FLAG], [$$2_LIB64DIR_FLAG])
AC_SUBST([$2_INCDIR_FLAG], [$$2_INCDIR_FLAG])
])

dnl SYNOPSIS: OPTION_WITH_SOS()
dnl EXAMPLE: OPTION_WITH([sos], [SOS])
dnl NOTE: With VAR_BASE_NAEM being SOS, this macro will set SOS_INCIDR and
dnl 	SOS_LIBDIR to the include path and library path respectively.
AC_DEFUN([OPTION_WITH_SOS], [
AC_ARG_WITH(
	sos,
	AS_HELP_STRING(
		[--with-sos@<:@=path@:>@],
		[Specify sos path @<:@default=in build tree@:>@]
	),
	[WITH_SOS=$withval
	 AM_CONDITIONAL([ENABLE_SOS], [true])
	],
	[WITH_SOS=build]
)

if test "x$WITH_SOS" != "xbuild"; then
	if test -d $WITH_SOS/lib; then
		SOS_LIBDIR=$WITH_SOS/lib
		SOS_LIBDIR_FLAG=-L$WITH_SOS/lib
	fi
	if test "x$SOS_LIBDIR" = "x"; then
		SOS_LIBDIR=$WITH_SOS/lib64
		SOS_LIBDIR_FLAG=-L$WITH_SOS/lib64
	fi
	if test -d $WITH_SOS/lib64; then
		SOS_LIB64DIR=$WITH_SOS/lib64
		SOS_LIB64DIR_FLAG=-L$WITH_SOS/lib64
	fi
	if test -d $WITH_SOS/include; then
		SOS_INCDIR=$WITH_SOS/include
		SOS_INCDIR_FLAG=-I$WITH_SOS/include
	fi
else
	# sosbuilddir should exist by ldms configure time

	sossrcdir=`(cd $srcdir/../sos/src && pwd)`
	sosbuilddir=`(cd ../sos/src && pwd)`
	SOS_LIBDIR=$sosbuilddir
	SOS_LIBDIR_FLAG=-L$sosbuilddir
	SOS_INCDIR=$sossrcdir
fi
AC_SUBST([SOS_LIBDIR], [$SOS_LIBDIR])
AC_SUBST([SOS_LIB64DIR], [$SOS_LIB64DIR])
AC_SUBST([SOS_INCDIR], [$SOS_INCDIR])
AC_SUBST([SOS_LIBDIR_FLAG], [$SOS_LIBDIR_FLAG])
AC_SUBST([SOS_LIB64DIR_FLAG], [$SOS_LIB64DIR_FLAG])
AC_SUBST([SOS_INCDIR_FLAG], [$SOS_INCDIR_FLAG])
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
		mysql_config=$WITH_MYSQL/bin/mysql_config
	],
	[	dnl $withval is not given.
		mysql_config=`which mysql_config`
	]
)

if test $mysql_config
then
	MYSQL_LIBS=`$mysql_config --libs`
	MYSQL_INCLUDE=`$mysql_config --include`
else
	AC_MSG_ERROR([Cannot find mysql_config, please specify
			--with-mysql option.])
fi
AC_SUBST([MYSQL_LIBS])
AC_SUBST([MYSQL_INCLUDE])
])
dnl this could probably be generalized for handling lib64,lib python-binding issues
AC_DEFUN([OPTION_WITH_EVENT],[
  AC_ARG_WITH([libevent],
  [  --with-libevent=DIR      use libevent in DIR],
  [ case "$withval" in
    yes|no)
      AC_MSG_RESULT(no)
      ;;
    *)
     CPPFLAGS="-I$withval/include"
     EVENTLIBS="-L$withval/lib -L$withval/lib64"
      ;;
    esac ])
  option_old_libs=$LIBS
  AC_CHECK_LIB(event, event_base_new, [EVENTLIBS="$EVENTLIBS -levent"],
      AC_MSG_ERROR([event_base_new() not found. sock requires libevent.]),[$EVENTLIBS])
  AC_SUBST(EVENTLIBS)
  LIBS=$option_old_libs
])

dnl SYNOPSIS: OPTION_WITH_MAGIC([name])
dnl EXAMPLE: OPTION_WITH_MAGIC([XYZ],[411],[desc])
dnl sets default value of magic number XYZ for make and headers, 
dnl using second argument as default if not given by user
dnl and description.
dnl Good for getting default sizes and ports at config time
AC_DEFUN([OPTION_WITH_MAGIC], [
AC_ARG_WITH(
        $1,
        AS_HELP_STRING(
                [--with-$1@<:@=NNN@:>@],
                [Specify $1 $3 @<:@configure default=$2@:>@]
        ),
        [$1=$withval],
        [$1=$2; withval=$2]
)
$1=$withval
if printf "%d" "$withval" >/dev/null 2>&1; then
        :
else
        AC_MSG_ERROR([--with-$1 given non-integer input $withval])
fi
AC_DEFINE_UNQUOTED([$1],[$withval],[$3])
AC_SUBST([$1],[$$1])
])

