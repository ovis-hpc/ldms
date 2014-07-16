KDIR := /lib/modules/`uname -r`/build
PWD := $(shell pwd)

EXTRA_CFLAGS = -I./ -I${PWD}/../../ldms/src/core -g -O0

default: modules

modules:
	${MAKE} -C ${KDIR} M=${PWD} NOSTDINC_FLAGS="$(EXTRA_CFLAGS)"

clean:
	${MAKE) -C ${KDIR} M=${PWD} clean
