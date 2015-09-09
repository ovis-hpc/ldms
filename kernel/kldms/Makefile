
EXTRA_CFLAGS += -I$(SUBDIRS)/../../ldms/src/core -Wall
EXTRA_CFLAGS += -I$(SUBDIRS) -DCONFIG_CRAY_ARIES

CONFIG_LDMS=m
CONFIG_LDMS_VMSTAT=m
CONFIG_LDMS_GPCDR=m

obj-$(CONFIG_LDMS)		+= kldms.o
obj-$(CONFIG_LDMS_VMSTAT)	+= kldms_vmstat.o
 obj-$(CONFIG_LDMS_GPCDR)	+= kldms_gpcdr.o

kldms_gpcdr-objs		:= kgpcdr.o gpcdr.o ghal.o
