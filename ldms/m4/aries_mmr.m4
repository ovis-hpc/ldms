dnl SYNOPSIS: CHECK_ARIES_MMR
dnl Checks --with-ariesgpcd=LIBDIR,INCDIR input
dnl if aries_mmr has been enabled.
AC_DEFUN([OPTION_ARIES_MMR], [
withval=""
AC_ARG_WITH([aries-libgpcd],
	[AS_HELP_STRING([--with-aries-libgpcd=LIBDIR,INCDIR],
	[Locations of gpcd library and headers for aries_mmr sampler.
E.g. --with-aries-libgpcd=/special/libs,/private/headerdir
/private/headerdir contains gpcd_pub.h])],
	[:], [with_aries_libgpcd=no]
)

if test "$enable_aries_mmr" = "yes"; then

if test -z "$ENABLE_GPCDLOCAL_TRUE"; then
	if test "x$WITH_GPCDLOCAL" != "xbuild"; then
		OCFLAGS=$CFLAGS
		CFLAGS=-I$GPCDLOCAL_INCDIR
		AC_CHECK_HEADERS(gpcd_pub.h,
			[],
			AC_MSG_FAILURE([gpcd_pub.h not found])
		)
		CFLAGS=$OCFLAGS
	else
		AC_MSG_NOTICE([Using staged gpcdlocal $GPCDLOCAL_LIB64DIR_FLAG $GPCDLOCAL_LIBDIR_FLAG])
	fi
	AC_MSG_NOTICE([updating with-aries-libgpcd=$with_aries_libgpcd])
	with_aries_libgpcd="$GPCDLOCAL_BUILD_LIBDIR,$GPCDLOCAL_BUILD_INCDIR"
	AC_MSG_NOTICE([to $with_aries_libgpcd])
fi

case $with_aries_libgpcd in
no|yes|,|"")
	AC_MSG_ERROR([enable-aries-mmr needs good --with-aries-libgpcd arguments])
	;;
,*)
	AC_MSG_ERROR([--with-aries-libgpcd got only INCDIR: $with_aries_libgpcd])
	;;
*,)
	AC_MSG_ERROR([--with-aries-libgpcd got only LIBDIR: $with_aries_libgpcd])
	;;
*,?*)
	ARIES_LIBGPCD_LIBDIR=`echo $with_aries_libgpcd | sed 's/,.*$//'`
	ARIES_LIBGPCD_INCDIR=`echo $with_aries_libgpcd | sed 's/^.*,//'`
	;;
*)
	AC_MSG_ERROR([Need --with-aries-libgpcd=LIBDIR,INCDIR for aries-mmr])
	;;
esac
save_LDFLAGS="$LDFLAGS"
LDFLAGS="-L$ARIES_LIBGPCD_LIBDIR $LDFLAGS"
if test -z "$GPCDLOCAL_BUILD_LIBDIR"; then
AC_CHECK_LIB([gpcd], [gpcd_create_context],
	[AC_SUBST([ARIES_LIBGPCD_LIBDIR], [$ARIES_LIBGPCD_LIBDIR])
         AC_DEFINE([HAVE_ARIES_LIBGPCD], [1],
		 [Define if have gpcd for aries mmr sampler])
	],
      [AC_MSG_ERROR([gpcd test failed for $ARIES_LIBGPCD_LIBDIR])]
)
else
AC_MSG_NOTICE([Assuming lib gpcdlocal will be built.])
fi
LDFLAGS="$save_LDFLAGS"

save_CFLAGS="$CFLAGS"
CFLAGS="-I$ARIES_LIBGPCD_INCDIR $CFLAGS"
AC_CHECK_HEADER([gpcd_lib.h],[
	AC_SUBST([ARIES_LIBGPCD_INCDIR], [$ARIES_LIBGPCD_INCDIR])
	],
	[AC_MSG_ERROR([Found no gpcd_lib.h])]
)
CFLAGS="$save_CFLAGS"

fi

])
