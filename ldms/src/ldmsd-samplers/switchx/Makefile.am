include ../common.am

pkglib_LTLIBRARIES = libswitchx.la

libswitchx_la_SOURCES = switchx.c
libswitchx_la_CFLAGS = $(SAMPLER_CFLAGS) \
		       -I$(SWITCHX_LIBDIR)/../usr/include
libswitchx_la_LIBADD = $(SAMPLER_LIBADD) \
		       $(SWITCHX_LIBDIR)/libsxapi.a \
		       $(SWITCHX_LIBDIR)/libsxutils.a \
		       $(SWITCHX_LIBDIR)/libsxdemadparser.a \
		       $(SWITCHX_LIBDIR)/libsxcomp.a \
		       $(SWITCHX_LIBDIR)/libsxdev.a \
		       $(SWITCHX_LIBDIR)/libcommon.a \
		       $(SWITCHX_LIBDIR)/libsxlog.a \
		       $(SWITCHX_LIBDIR)/libibmad.a \
		       $(SWITCHX_LIBDIR)/libibumad.a \
		       $(SWITCHX_LIBDIR)/libosmcomp.a
libswitchx_la_LDFLAGS = $(SAMPLER_LDFLAGS)
