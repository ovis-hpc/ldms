`store_influx` test
===================

NOTE: This is currently a manual sniff test.

Overview
--------

1 sampler daemon (see `sampler.sh` and `sampler.conf`) on localhost with
`meminfo` sampler, and 1 aggregator (see `agg.sh` and `agg.conf`) on localhost
collecting from the sampler daemon and store into `store_influx`.

The test requires:
- running influxdb with HTTP port 8086
- an influx database named `testdb`

TIPS
----

The database can be created with `influx` command:
```sh
$ influx
> create database testdb
```

The data can either be queried over HTTP:
```
http://localhost:8086/query?pretty=true&db=testdb&q=SELECT%20*%20FROM%20meminfo
```

Or, query using `influx` command:
```
$ influx
> use testdb
> select * from meminfo
```
