cityhash-c
==========

gcc city.c city-test.c -o city-test -msse4.2

To omit seeded and 128/256 function versions, compile with -DCITYSLIM
