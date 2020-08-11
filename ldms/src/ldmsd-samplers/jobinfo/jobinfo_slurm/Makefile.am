pkglib_LTLIBRARIES = libjobinfo_slurm.la

libjobinfo_slurm_la_SOURCES = jobinfo_slurm.c
libjobinfo_slurm_la_LIBADD  = $(top_builddir)/lib/src/coll/libcoll.la
libjobinfo_slurm_la_CFLAGS  = @OVIS_LIB_INCDIR_FLAG@ $(SLURM_INCDIR_FLAG)
libjobinfo_slurm_la_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@ \
			      -module
