#################################
### Individual storage subdir ###
#################################

SUBDIRS =

if ENABLE_SOS
SUBDIRS += store_sos
SUBDIRS += store_papi
endif

if ENABLE_CSV
SUBDIRS += store_csv
endif

if ENABLE_AMQP
SUBDIRS += store_amqp
endif

if ENABLE_STORE_TEST
SUBDIRS += store_test
endif

if ENABLE_INFLUX
SUBDIRS += store_influx
endif
