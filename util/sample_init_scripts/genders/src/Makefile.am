bin_PROGRAMS = ldmsctl_args

AM_CFLAGS = \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

# AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
# AM_CPPFLAGS = -DPLUGINDIR='"$(pkglibdir)"'

if ENABLE_LIBGENDERS
ldmsctl_args_SOURCES = ldmsctl_args.cxx
ldmsctl_args_LDADD = -lgendersplusplus -lboost_program_options
ldmsctl_args_LDFLAGS = $(AM_LDFLAGS) @LIBGENDERS_LIBDIR_FLAG@ @LIBGENDERS_LIB64DIR_FLAG@
ldmsctl_args_CPPFLAGS = $(AM_CFLAGS) @LIBGENDERS_INCDIR_FLAG@
endif

