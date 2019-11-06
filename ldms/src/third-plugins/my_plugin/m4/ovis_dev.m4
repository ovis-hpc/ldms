dnl define the --with-ldms-libdir configure option and validate it's input.
dnl then load the values from ovis-ldms-configvars.sh and reexport them.
dnl The config vars can be made to appear in a makefile by adding
dnl @LDMS_MAKE_VARS@ to the Makefile.am
AC_DEFUN([LDMS_PLUGIN_VARS],[
withval=""
AC_ARG_WITH(
	[ldms-libdir],
	[AS_HELP_STRING(
		[--with-ldms-libdir@<:@=path@:>@],
		[Specify ldms installed libdir path @<:@default=/usr/lib64/ovis-ldms@:>@])]
	,
	[WITH_LDMS_LIBDIR=$withval],
	[WITH_LDMS_LIBDIR=/usr/lib64/ovis-ldms
	if test -n "$WITH_LIB_LIBDIR"; then
		WITH_LDMS_LIBDIR=$WITH_LIB_LIBDIR
	fi
]
)

if ! test -f "$WITH_LDMS_LIBDIR/ovis-ldms-configvars.sh"; then
	AC_MSG_ERROR([Did not find $WITH_LDMS_LIBDIR/ovis-ldms-configvars.sh])
fi
. $WITH_LDMS_LIBDIR/ovis-ldms-configvars.sh
dnl   include(`foreach.m4')
m4_foreach_w([x], [ovis_ldms_plugins ovis_ldms_version ovis_ldms_mandir ovis_ldms_localedir ovis_ldms_libdir ovis_ldms_psdir ovis_ldms_pdfdir ovis_ldms_dvidir ovis_ldms_htmldir ovis_ldms_infodir ovis_ldms_docdir ovis_ldms_includedir ovis_ldms_localstatedir ovis_ldms_sharedstatedir ovis_ldms_sysconfdir ovis_ldms_datadir ovis_ldms_datarootdir ovis_ldms_libexecdir ovis_ldms_sbindir ovis_ldms_bindir ovis_ldms_prefix ovis_ldms_exec_prefix ovis_ldms_pkglibdir ovis_ldms_pythondir], [
AC_SUBST(x)
])dnl

AC_SUBST([LDMS_MAKE_VARS],["
ovis_ldms_version=$ovis_ldms_version
ovis_ldms_mandir=$ovis_ldms_mandir
ovis_ldms_localedir=$ovis_ldms_localedir
ovis_ldms_libdir=$ovis_ldms_libdir
ovis_ldms_psdir=$ovis_ldms_psdir
ovis_ldms_pdfdir=$ovis_ldms_pdfdir
ovis_ldms_dvidir=$ovis_ldms_dvidir
ovis_ldms_htmldir=$ovis_ldms_htmldir
ovis_ldms_infodir=$ovis_ldms_infodir
ovis_ldms_docdir=$ovis_ldms_docdir
ovis_ldms_includedir=$ovis_ldms_includedir
ovis_ldms_localstatedir=$ovis_ldms_localstatedir
ovis_ldms_sharedstatedir=$ovis_ldms_sharedstatedir
ovis_ldms_sysconfdir=$ovis_ldms_sysconfdir
ovis_ldms_datadir=$ovis_ldms_datadir
ovis_ldms_datarootdir=$ovis_ldms_datarootdir
ovis_ldms_libexecdir=$ovis_ldms_libexecdir
ovis_ldms_sbindir=$ovis_ldms_sbindir
ovis_ldms_bindir=$ovis_ldms_bindir
ovis_ldms_prefix=$ovis_ldms_prefix
ovis_ldms_exec_prefix=$ovis_ldms_exec_prefix
ovis_ldms_pkglibdir=$ovis_ldms_pkglibdir
ovis_ldms_pythondir=$ovis_ldms_pythondir
"
])
])

dnl define the --with-lib-libdir configure option and validate it's input.
dnl then load the values from ovis-lib-configvars.sh and reexport them.
dnl The config vars can be made to appear in a makefile by adding
dnl @LIB_MAKE_VARS@ to the Makefile.am
AC_DEFUN([LIB_PLUGIN_VARS],[
withval=""
AC_ARG_WITH(
	[lib-libdir],
	[AS_HELP_STRING(
		[--with-lib-libdir@<:@=path@:>@],
		[Specify ovis lib installed libdir path @<:@default=/usr/lib64/ovis-lib@:>@])]
	,
	[WITH_LIB_LIBDIR=$withval],
	[WITH_LIB_LIBDIR=/usr/lib64/ovis-lib
	if test -n "$WITH_LDMS_LIBDIR"; then
		WITH_LIB_LIBDIR=$WITH_LDMS_LIBDIR
	fi
]
)

if ! test -f "$WITH_LIB_LIBDIR/ovis-lib-configvars.sh"; then
	AC_MSG_ERROR([Did not find $WITH_LIB_LIBDIR/ovis-lib-configvars.sh])
fi
. $WITH_LIB_LIBDIR/ovis-lib-configvars.sh
m4_foreach_w([x], [ovis_lib_plugins ovis_lib_mandir ovis_lib_localedir ovis_lib_libdir ovis_lib_psdir ovis_lib_pdfdir ovis_lib_dvidir ovis_lib_htmldir ovis_lib_infodir ovis_lib_docdir ovis_lib_includedir ovis_lib_localstatedir ovis_lib_sharedstatedir ovis_lib_sysconfdir ovis_lib_datadir ovis_lib_datarootdir ovis_lib_libexecdir ovis_lib_sbindir ovis_lib_bindir ovis_lib_prefix ovis_lib_exec_prefix ovis_lib_pkglibdir ovis_lib_pythondir], [
AC_SUBST(x)
])dnl

AC_SUBST([LIB_MAKE_VARS],["ovis_lib_mandir=$ovis_lib_mandir
ovis_lib_localedir=$ovis_lib_localedir
ovis_lib_libdir=$ovis_lib_libdir
ovis_lib_psdir=$ovis_lib_psdir
ovis_lib_pdfdir=$ovis_lib_pdfdir
ovis_lib_dvidir=$ovis_lib_dvidir
ovis_lib_htmldir=$ovis_lib_htmldir
ovis_lib_infodir=$ovis_lib_infodir
ovis_lib_docdir=$ovis_lib_docdir
ovis_lib_includedir=$ovis_lib_includedir
ovis_lib_localstatedir=$ovis_lib_localstatedir
ovis_lib_sharedstatedir=$ovis_lib_sharedstatedir
ovis_lib_sysconfdir=$ovis_lib_sysconfdir
ovis_lib_datadir=$ovis_lib_datadir
ovis_lib_datarootdir=$ovis_lib_datarootdir
ovis_lib_libexecdir=$ovis_lib_libexecdir
ovis_lib_sbindir=$ovis_lib_sbindir
ovis_lib_bindir=$ovis_lib_bindir
ovis_lib_prefix=$ovis_lib_prefix
ovis_lib_exec_prefix=$ovis_lib_exec_prefix
ovis_lib_pkglibdir=$ovis_lib_pkglibdir
ovis_lib_pythondir=$ovis_lib_pythondir
"
])
])

dnl take a list of script files and define the .in suffixed 
dnl version of the list for packaging via EXTRADIST
AC_DEFUN([OVIS_EXEC_SCRIPTS], [
	ovis_exec_scripts=""
	for i in "$*"; do
		x="$i ";
	done
	for i in $x; do
		ovis_exec_scripts="$ovis_exec_scripts $i"
		x=`basename $i`
		if ! test "x$x" = "x"; then
			ovis_extra_dist="$ovis_extra_dist ${x}.in"
		else
			echo cannot parse $i
		fi
		AC_CONFIG_FILES([$i],[chmod a+x $i])
	done
	AC_SUBST([OVIS_EXTRA_DIST],[$ovis_extra_dist])
])


dnl SYNOPSIS: OVIS_PKGLIBDIR
dnl defines automake pkglibdir value for configure output
dnl and enables gcc color
AC_DEFUN([OVIS_PKGLIBDIR], [
AC_ARG_WITH(pkglibdir,
[AS_HELP_STRING([--with-pkglibdir],
        [specify subdirectory of libdir where plugin libraries shall be.])],
        [AC_SUBST([pkglibdir],['${libdir}'/$withval])
         AC_MSG_NOTICE([Using plugin directory $withval])
        ],
        [AC_SUBST([pkglibdir],['${libdir}'/$PACKAGE])
         AC_MSG_NOTICE([Using plugin directory $PACKAGE])
        ])
AX_CHECK_COMPILE_FLAG([-fdiagnostics-color=auto], [
CFLAGS="$CFLAGS -fdiagnostics-color=auto"
])
])
