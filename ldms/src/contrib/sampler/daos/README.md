DAOS LDMS Sampler Plugin
========================

This directory contains the source, build scripts, and unit tests for the DAOS
LDMS Sampler Plugin. When enabled, this sampler reads the telemetry exposed by
each local [DAOS Storage Engine](https://github.com/daos-stack/daos).

# Building

Follow the [LDMS Build Guide](https://github.com/ovis-hpc/ovis#building-the-ovis--ldms-source-code),
making sure that you specify --with-daos=/path/to/daos/install in order to pick up the headers and
built libraries.

After making changes while developing, you can just run `make -C ldms/src/contrib/sampler/daos install`
from the top of the ovis clone.

# Running

The following steps assume that you have a running daos server with at least one engine configured. The
default config assumes two -- setting engine_count=1 in the sampler config will override this. If no target_count value is set, the default of 8 is used.

First, create a simple sampler config to enable DAOS sampling, e.g.
```
load name=daos_sampler
config name=daos_sampler producer=${HOSTNAME} system=${DAOS_SYSTEM} target_count=${DAOS_NUM_TARGET}
start name=daos_sampler interval=${SAMPLE_INTERVAL}
```

Next, start ldmsd in foreground mode with debug logging enabled: `SAMPLE_INTERVAL=5000000 ldmsd -m1MB -x sock:10444 -v DEBUG -F -c /path/to/sampler.conf`

NOTE: The default memory size (512KB) is too small for the number of metrics collected. Larger sizes may be specified for a large number of pools.

Finally, run ldms_ls in order to see the collected metrics (updated every 5s with the sample interval shown above):
```
$ ldms_ls -h localhost -x sock -p 10444 -l
daos_server/0/0: consistent, last update: Wed Aug 25 18:40:25 2021 +0000 [653335us]
M char[]     system                                     "daos_server"
M u32        rank                                       0
M u32        target                                     0
D u64        io/latency/update/256B                     0
D u64        io/latency/update/256B/min                 0
D u64        io/latency/update/256B/max                 0
D u64        io/latency/update/256B/samples             0
D d64        io/latency/update/256B/mean                0.000000
D d64        io/latency/update/256B/stddev              0.000000
...
D u64        io/latency/update/32KB                     611
D u64        io/latency/update/32KB/min                 611
D u64        io/latency/update/32KB/max                 611
D u64        io/latency/update/32KB/samples             1
D d64        io/latency/update/32KB/mean                611.000000
D d64        io/latency/update/32KB/stddev              0.000000
D u64        io/latency/update/64KB                     0
D u64        io/latency/update/64KB/min                 0
D u64        io/latency/update/64KB/max                 0
D u64        io/latency/update/64KB/samples             0
D d64        io/latency/update/64KB/mean                0.000000
D d64        io/latency/update/64KB/stddev              0.000000
D u64        io/latency/update/128KB                    1018
D u64        io/latency/update/128KB/min                567
D u64        io/latency/update/128KB/max                1214
D u64        io/latency/update/128KB/samples            8
D d64        io/latency/update/128KB/mean               828.000000
D d64        io/latency/update/128KB/stddev             238.011404
```

# TODO
 * Configurable filtering of metrics
 * Dynamic schema generation based on runtime telemetry tree