dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.69)
AC_INIT(my_plugin, 1.0.0, foo@bar.com)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([m4/my_plugin_top.m4])
AC_CONFIG_AUX_DIR(config)
AX_PREFIX_CONFIG_H_G(my_plugin-config.h)
AC_PROG_CC
AM_INIT_AUTOMAKE([foreign tar-pax])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_CONFIG_HEADER(config.h)
AM_PROG_LIBTOOL
AC_LIB_RPATH

# the following 3 detect and set the installed ovis environment
OVIS_PKGLIBDIR
LDMS_PLUGIN_VARS
LIB_PLUGIN_VARS

dnl Checks for programs
AC_CHECK_SIZEOF(long)

AC_C_CONST


OPTION_DEFAULT_ENABLE([my_plugin], [ENABLE_MY_PLUGIN])

AC_SUBST(ac_configure_args)

OVIS_EXEC_SCRIPTS(
	scripts/check_banned.sh
        scripts/rm_banned.sh
)

AC_CONFIG_FILES([
src/Makefile
Makefile
scripts/Makefile
test/Makefile
])

AC_CONFIG_COMMANDS([scripts],[ echo fixing test script perms.
for i in $ovis_exec_scripts; do
	if test -f $ovis_exec_scripts_objdir/$i; then
		chmod a+x $ovis_exec_scripts_objdir/$i;
	else
		echo failing chmod on missing $ovis_exec_scripts_objdir/$i;
	fi
done
],[
	ovis_exec_scripts="$ovis_exec_scripts"
	ovis_extra_dist="$ovis_extra_dist"
	ovis_exec_scripts_objdir="$ac_pwd"
])


AC_OUTPUT

if test "x$enable_rpath" != "xyes"; then
echo "TURNING libtool rpath into no-ops"
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
fi
