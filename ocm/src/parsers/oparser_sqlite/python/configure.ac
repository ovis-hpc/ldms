AC_PREREQ(2.57)
AC_INIT([oconf_sqlite], 1.0.0, nichamon@ogc.us)
AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_MACRO_DIR([m4])

dnl Checks for programs
AC_PATH_PROG(TAR, [tar], [no])

dnl Check for python interpreter
AM_PATH_PYTHON([2.6])

OPTION_DEFAULT_DISABLE([ocsqlite_tests], [ENABLE_OCSQLITE_TESTS])

AC_CONFIG_FILES([Makefile
		oconf_sqlite/Makefile
		tests/Makefile])
AC_OUTPUT
