
AM_CFLAGS = \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

if ENABLE_LIBGENDERS
bin_PROGRAMS = ldmsctl_args3
ldmsctl_args3_SOURCES = ldmsctl_args3.cxx
ldmsctl_args3_LDADD = -lgendersplusplus -lboost_program_options -lboost_regex
ldmsctl_args3_LDFLAGS = $(AM_LDFLAGS) @LIBGENDERS_LIBDIR_FLAG@ @LIBGENDERS_LIB64DIR_FLAG@
ldmsctl_args3_CPPFLAGS = $(AM_CFLAGS) @LIBGENDERS_INCDIR_FLAG@
endif

