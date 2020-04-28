`zap_test_shared_man`
=====================

The test program is located at `lib/src/zap/test/zap_test_shared_map.c`. The
configure option `--enable-zaptest` must be given to build the test program.
The following is the configure options Narate used to build the test program.

```sh
../configure \
        --prefix=${PREFIX} \
        --enable-etc \
        --enable-python \
        --enable-test_sampler \
        --enable-ldms-test \
        --enable-zaptest \
	--enable-rdma \
        PYTHON='/usr/bin/python3' \
        CFLAGS='-ggdb3 -O0 -Wall -Werror'
```

Test Scenario
-------------

- We will have a server (`zap_test_shared_map -m server`) that listens to
  multiple transport and a single memory region to share with all clients
  connecting to it.  Initially, the memory block is initialized with zeros.

- A "writer" client (`zap_test_shared_map -m writer`) then connects to the
  server and write a pre-defined data to the memory block in the server.

- A "reader" client (`zap_test_shared_map -m reader`) connects to the server to
  read the memory block and verifies that the data is as expected.


It is recommended that the server run on one node, and the clients run on
another node. On cygnus cluster, rdma transport to self does not work.

The following is an example of sequence of execution:

```sh
# Listen on sock 20000 and rdma 20001
root@cygnus-08 $ zap_test_shared_map -m server -x sock:*:20000 -x rdma:*:20001

# On another node, run the writer using rdma
root@cygnus-07 $ zap_test_shared_map -m writer -x rdma:cygnus-08-iw:20001

# Then verify with sock
root@cygnus-07 $ zap_test_shared_map -m reader -x sock:cygnus-08:20000
Info: data verified

# It is OK to verify multiple times with any transport
root@cygnus-07 $ zap_test_shared_map -m reader -x rdma:cygnus-08-iw:20001

# If we want to test writing using sock, the server need to be restarted as the
# data has already been modified.
```
