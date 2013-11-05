SUBDIRS = test

AM_CFLAGS = -I@OVIS_LIB_INCDIR@ -I@SOS_INCDIR@
AM_LDFLAGS = -L@OVIS_LIB_LIB64DIR@ -L@OVIS_LIB_LIBDIR@ \
	     -L@SOS_LIBDIR@ -L@SOS_LIB64DIR@

sbin_PROGRAMS = komondor

komondor_SOURCES = komondor.c
komondor_LDADD = -lcoll -lzap -lpthread -lsos

if ENABLE_OCM
komondor_CFLAGS = $(AM_CFLAGS) -DENABLE_OCM -I@OCM_INCDIR@
komondor_LDADD += -locm
komondor_LDFLAGS = $(AM_LDFLAGS) -L@OCM_LIBDIR@ -L@OCM_LIB64DIR@
endif

