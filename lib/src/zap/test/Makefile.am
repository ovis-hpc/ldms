ACLOCAL_AMFLAGS = -I m4
sbin_PROGRAMS = zap_test
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

zap_test_SOURCES = zap_test.c
zap_test_LDADD = -lzap -lpthread -ldl

sbin_PROGRAMS += zap_test_big
zap_test_big_SOURCES = zap_test_big.c
zap_test_big_LDADD = -lzap -lpthread -ldl

sbin_PROGRAMS += zap_test_reconnect
zap_test_reconnect_SOURCES = zap_test_reconnect.c
zap_test_reconnect_LDADD = -lzap -lpthread -ldl

sbin_PROGRAMS += zap_test_disconnect
zap_test_disconnect_SOURCES = zap_test_disconnect.c
zap_test_disconnect_LDADD = -lzap -lpthread -ldl

sbin_PROGRAMS += zap_test_reconnect_many
zap_test_reconnect_many_SOURCES = zap_test_reconnect_many.c
zap_test_reconnect_many_LDADD = -lzap -lpthread -ldl

sbin_PROGRAMS += zap_test_shared_map
zap_test_shared_map_SOURCES = zap_test_shared_map.c
zap_test_shared_map_LDADD = -lzap -lpthread -ldl

sbin_PROGRAMS += zap_test_many_read
zap_test_many_read_SOURCES = zap_test_many_read.c
zap_test_many_read_LDADD = -lzap -lpthread -ldl
