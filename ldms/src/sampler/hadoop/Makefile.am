lib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

#hadoop sampler core
libhadoop_sampler_la_SOURCES = sampler_hadoop.c sampler_hadoop.h
libhadoop_sampler_la_LIBADD = $(CORE)/libldms.la -lovis_util -lrt -lcoll
lib_LTLIBRARIES += libhadoop_sampler.la

COMMON_ADD = $(CORE)/libldms.la -lovis_util -lrt \
		libhadoop_sampler.la

#hadoop_namenode
libhadoop_namenode_la_SOURCES = hadoop_namenode.c
libhadoop_namenode_la_LIBADD = $(COMMON_ADD)
lib_LTLIBRARIES += libhadoop_namenode.la

#hadoop_datanode
libhadoop_datanode_la_SOURCES = hadoop_datanode.c
libhadoop_datanode_la_LIBADD = $(COMMON_ADD)
lib_LTLIBRARIES += libhadoop_datanode.la

#hadoop_jobtracker
libhadoop_jobtracker_la_SOURCES = hadoop_jobtracker.c
libhadoop_jobtracker_la_LIBADD = $(COMMON_ADD)
lib_LTLIBRARIES += libhadoop_jobtracker.la

#hadoop_tasktracker
libhadoop_tasktracker_la_SOURCES = hadoop_tasktracker.c
libhadoop_tasktracker_la_LIBADD = $(COMMON_ADD)
lib_LTLIBRARIES += libhadoop_tasktracker.la

#hadoop_maptask
libhadoop_maptask_la_SOURCES = hadoop_maptask.c
libhadoop_maptask_la_LIBADD = $(COMMON_ADD)
lib_LTLIBRARIES += libhadoop_maptask.la

#hadoop_reducetask
libhadoop_reducetask_la_SOURCES = hadoop_reducetask.c
libhadoop_reducetask_la_LIBADD = $(COMMON_ADD)
lib_LTLIBRARIES += libhadoop_reducetask.la

#hadoop_secondarynamenode
libhadoop_secondarynamenode_la_SOURCES = hadoop_secondarynamenode.c
libhadoop_secondarynamenode_la_LIBADD = $(COMMON_ADD)
lib_LTLIBRARIES += libhadoop_secondarynamenode.la

#hadoop_resourcemanager
libhadoop_resourcemanager_la_SOURCES = hadoop_resourcemanager.c
libhadoop_resourcemanager_la_LIBADD = $(COMMON_ADD)
lib_LTLIBRARIES += libhadoop_resourcemanager.la

#hadoop_nodemanager
libhadoop_nodemanager_la_SOURCES = hadoop_nodemanager.c
libhadoop_nodemanager_la_LIBADD = $(COMMON_ADD)
lib_LTLIBRARIES += libhadoop_nodemanager.la
