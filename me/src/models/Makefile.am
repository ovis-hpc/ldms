ACLOCAL_AMFLAGS = -I m4
SUBDIRS = .
pkglib_LTLIBRARIES =
CORE = ../core

AM_CFLAGS = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@
COMMON_LIBADD = $(CORE)/libme_core.la -lm

if ENABLE_MODEL_EXAMPLE
libmodel_example_la_SOURCES = model_example.c
libmodel_example_la_LIBADD = $(COMMON_LIBADD)
libmodel_example_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_example.la
endif

if ENABLE_BKDE
libmodel_bkde_la_SOURCES = model_bkde.c
libmodel_bkde_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_bkde_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_bkde.la
endif

if ENABLE_BALER_RULES
libmodel_baler_rules_la_SOURCES = model_baler_rules.c
libmodel_baler_rules_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_baler_rules_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_baler_rules.la
endif

if ENABLE_HIGH_EXACT
libmodel_high_exact_la_SOURCES = model_high_exact.c
libmodel_high_exact_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_high_exact_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_high_exact.la
endif

if ENABLE_HIGH_OUTLIER
libmodel_high_outlier_la_SOURCES = model_high_outlier.c
libmodel_high_outlier_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_high_outlier_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_high_outlier.la
endif

if ENABLE_LOW_OUTLIER
libmodel_low_outlier_la_SOURCES = model_low_outlier.c
libmodel_low_outlier_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_low_outlier_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_low_outlier.la
endif

if ENABLE_LOW_EXACT
libmodel_low_exact_la_SOURCES = model_low_exact.c
libmodel_low_exact_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_low_exact_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_low_exact.la
endif

if ENABLE_ABOVE_MEAN
libmodel_above_mean_la_SOURCES = model_above_mean.c
libmodel_above_mean_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_above_mean_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_above_mean.la
endif

if ENABLE_BELOW_MEAN
libmodel_below_mean_la_SOURCES = model_below_mean.c
libmodel_below_mean_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_below_mean_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_below_mean.la
endif

if ENABLE_HIGH_PERCENTAGE
libmodel_high_percentage_la_SOURCES = model_high_percentage.c
libmodel_high_percentage_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_high_percentage_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_high_percentage.la
endif

if ENABLE_LOW_PERCENTAGE
libmodel_low_percentage_la_SOURCES = model_low_percentage.c
libmodel_low_percentage_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_low_percentage_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_low_percentage.la
endif

if ENABLE_DETECT_INPUT_TYPE
libmodel_detect_input_type_la_SOURCES = model_detect_input_type.c
libmodel_detect_input_type_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_detect_input_type_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_detect_input_type.la
endif

if ENABLE_SEVERITY_LEVEL
libmodel_severity_level_la_SOURCES = model_severity_level.c
libmodel_severity_level_la_LIBADD = $(COMMON_LIBADD) -lm
libmodel_severity_level_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_severity_level.la
endif

if ENABLE_HIGH_EXACT_BOUND
libmodel_high_exact_bound_la_SOURCES = model_high_exact_bound.c
libmodel_high_exact_bound_la_LIBADD = $(COMMON_LIBADD)
libmodel_high_exact_bound_la_CFLGAS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_high_exact_bound.la
endif

if ENABLE_HIGH_EXACT_DETECT_NO_DATA
libmodel_high_exact_detect_no_data_la_SOURCES = model_high_exact_detect_no_data.c
libmodel_high_exact_detect_no_data_la_LIBADD = $(COMMON_LIBADD)
libmodel_high_exact_detect_no_data_la_CFLGAS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libmodel_high_exact_detect_no_data.la
endif
