SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
CORE = ../../core

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

#CFLAGS := $(filter-out -Werror, ${CFLAGS})

COMMON_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la \
		$(top_builddir)/lib/src/ovis_log/libovis_log.la

liblinux_proc_sampler_files_store_la_SOURCES = linux_proc_sampler_files_store.c
liblinux_proc_sampler_files_store_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS) -g -O0
liblinux_proc_sampler_files_store_la_LIBADD = $(COMMON_LIBADD) \
	@SOS_LIB64DIR_FLAG@ @SOS_LIBDIR_FLAG@ -lsos -lc
pkglib_LTLIBRARIES += liblinux_proc_sampler_files_store.la

liblinux_proc_sampler_env_store_la_SOURCES = linux_proc_sampler_env_store.c
liblinux_proc_sampler_env_store_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS) -g -O0
liblinux_proc_sampler_env_store_la_LIBADD = $(COMMON_LIBADD) \
	@SOS_LIB64DIR_FLAG@ @SOS_LIBDIR_FLAG@ -lsos -lc
pkglib_LTLIBRARIES += liblinux_proc_sampler_env_store.la

liblinux_proc_sampler_argv_store_la_SOURCES = linux_proc_sampler_argv_store.c
liblinux_proc_sampler_argv_store_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS) -g -O0
liblinux_proc_sampler_argv_store_la_LIBADD = $(COMMON_LIBADD) \
	@SOS_LIB64DIR_FLAG@ @SOS_LIBDIR_FLAG@ -lsos -lc
pkglib_LTLIBRARIES += liblinux_proc_sampler_argv_store.la
