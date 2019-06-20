include ../common.am

pkglib_LTLIBRARIES = libstore_amqp.la

libstore_amqp_la_SOURCES = store_amqp.c
libstore_amqp_la_CFLAGS = $(STORE_CFLAGS)
libstore_amqp_la_LIBADD = $(STORE_LIBADD)
libstore_amqp_la_LDFLAGS = $(STORE_LDFLAGS) -lrabbitmq
