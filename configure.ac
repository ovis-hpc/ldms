dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(ovis, 3.4.11, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([m4/Ovis-top.m4])
AC_CONFIG_AUX_DIR(config)
AX_PREFIX_CONFIG_H_G(ovis-all-config.h)
AC_CONFIG_SUBDIRS([lib])
AC_PROG_CC
AM_INIT_AUTOMAKE([foreign tar-pax])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_CONFIG_HEADER(config.h)
AM_PROG_LIBTOOL
AC_LIB_RPATH

dnl Checks for programs
AC_PROG_CXX
AC_CHECK_SIZEOF(long)

AC_C_CONST

OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])

# note: ocm, me, and komondor are not distributed products.
# DIST automake rules must not include them and
# enable-<one of them> is not supported on packaged distributions.

OPTION_DEFAULT_DISABLE([gpcdlocal], [ENABLE_GPCDLOCAL], [  cray only])
if test "$enable_gpcdlocal" = "yes"; then
	AC_CONFIG_SUBDIRS([gpcd-support])
	if ! test -d $srcdir/gpcd-support/gpcd; then
		AC_MSG_ERROR([You need the gpcd-support submodule to enable it.])
	fi
fi

OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS], [  requires lib])
if test "$enable_sos" = "yes"; then
	AC_CONFIG_SUBDIRS([sos])
	if ! test -d $srcdir/sos/sos; then
		AC_MSG_ERROR([You need the sos submodule to enable it.])
	fi
fi

OPTION_DEFAULT_DISABLE([ocm], [ENABLE_OCM], [  requires lib])
if test "$enable_ocm" = "yes"; then
	AC_CONFIG_SUBDIRS([ocm])
fi

# ldms is not optional for now.
OPTION_DEFAULT_ENABLE([ldms], [ENABLE_LDMS],[  requires lib])
AC_CONFIG_SUBDIRS([ldms])

OPTION_DEFAULT_DISABLE([baler], [ENABLE_BALER], [  requires sos])

AC_CONFIG_SUBDIRS([baler])

OPTION_DEFAULT_DISABLE([me], [ENABLE_ME], [  requires lib])
if test "$enable_me" = "yes"; then
	AC_CONFIG_SUBDIRS([me])
fi
OPTION_DEFAULT_DISABLE([komondor], [ENABLE_KOMONDOR], [  requires lib])
if test "$enable_komondor" = "yes"; then
	AC_CONFIG_SUBDIRS([komondor])
fi

dnl Check for libevent
OPTION_WITH([libevent], [LIBEVENT], [/usr])
dnl extra setup
OPTION_WITH_EVENT

SUBST_MAYBE([gpcdlocal])
SUBST_MAYBE([sos])
SUBST_MAYBE([ocm])
SUBST_MAYBE([ldms])
SUBST_MAYBE([baler])
SUBST_MAYBE([me])
SUBST_MAYBE([komondor])

OPTION_DEFAULT_DISABLE([libgenders], [ENABLE_LIBGENDERS],[  requires C++,boost])
OPTION_DEFAULT_DISABLE([genderssystemd], [ENABLE_TOSS_SYSTEMD],[  toss systemd init scripts])

if test -z "$ENABLE_LIBGENDERS_TRUE"
then
	OPTION_WITH([libgenders], [LIBGENDERS],[/usr])
	AC_LANG_PUSH([C++])
	BOOST_REQUIRE( , AC_MSG_ERROR([Genders enabled but boost not found.]))
	BOOST_STRING_ALGO
	save_LDFLAGS=$LDFLAGS
	LDFLAGS=$LIBGENDERS_LIBDIR_FLAG
	AX_CXX_CHECK_LIB(gendersplusplus,[Gendersplusplus::GendersException], [],
		AC_MSG_ERROR([libgendersplusplus not found. ]))
	LDFLAGS=$save_LDFLAGS
	AC_LANG_POP([C++])
	LIBS=""
fi

OVIS_PKGLIBDIR
OPTION_GITINFO
OPTION_HOSTINFO

# define substitutions for configvars and other sed-generated files.
# note carefully the escapes.
OVIS_DO_SUBST([OVIS_SUBST_RULE], ["sed \
-e 's,[[@]]mandir[[@]],\$(mandir),g' \
-e 's,[[@]]localedir[[@]],\$(localedir),g' \
-e 's,[[@]]libdir[[@]],\$(libdir),g' \
-e 's,[[@]]psdir[[@]],\$(psdir),g' \
-e 's,[[@]]pdfdir[[@]],\$(pdfdir),g' \
-e 's,[[@]]dvidir[[@]],\$(dvidir),g' \
-e 's,[[@]]htmldir[[@]],\$(htmldir),g' \
-e 's,[[@]]infodir[[@]],\$(infodir),g' \
-e 's,[[@]]docdir[[@]],\$(docdir),g' \
-e 's,[[@]]includedir[[@]],\$(includedir),g' \
-e 's,[[@]]localstatedir[[@]],\$(localstatedir),g' \
-e 's,[[@]]sharedstatedir[[@]],\$(sharedstatedir),g' \
-e 's,[[@]]sysconfdir[[@]],\$(sysconfdir),g' \
-e 's,[[@]]datadir[[@]],\$(datadir),g' \
-e 's,[[@]]datarootdir[[@]],\$(datarootdir),g' \
-e 's,[[@]]libexecdir[[@]],\$(libexecdir),g' \
-e 's,[[@]]sbindir[[@]],\$(sbindir),g' \
-e 's,[[@]]bindir[[@]],\$(bindir),g' \
-e 's,[[@]]prefix[[@]],\$(prefix),g' \
-e 's,[[@]]exec_prefix[[@]],\$(exec_prefix),g' \
-e 's,[[@]]pkglibdir[[@]],\$(pkglibdir),g' \
-e 's,[[@]]pythondir[[@]],\$(pythondir),g' \
"
])

AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile
util/Makefile
util/relocation/Makefile
util/sample_init_scripts/Makefile
util/sample_init_scripts/genders/Makefile
util/sample_init_scripts/genders/man/Makefile
util/sample_init_scripts/genders/src/Makefile
util/sample_init_scripts/genders/systemd/Makefile
util/sample_init_scripts/genders/systemd/etc/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/ClusterGenders/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/ClusterSecrets/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/plugins-conf/Makefile
util/sample_init_scripts/genders/systemd/services/Makefile
util/sample_init_scripts/genders/sysv/Makefile
util/sample_init_scripts/genders/sysv/etc/Makefile
util/sample_init_scripts/genders/sysv/etc/init.d/Makefile
util/sample_init_scripts/genders/examples/Makefile
])

OVIS_EXEC_SCRIPTS(
	util/relocation/bld_manifest.sh
	util/relocation/relocate-paths.sh
	util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/ldmsd
	util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/ldms-functions
	util/sample_init_scripts/genders/systemd/ldmsd-pre-systemd
	util/sample_init_scripts/genders/systemd/ldmsd-wrapper.sh
	util/sample_init_scripts/genders/sysv/etc/init.d/ldmsd
	util/sample_init_scripts/genders/sysv/etc/init.d/ldmsd.template
	util/sample_init_scripts/genders/sysv/ldmsd-pre-sysvinit
)

AC_CONFIG_COMMANDS([scripts],[ echo fixing script perms.
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
