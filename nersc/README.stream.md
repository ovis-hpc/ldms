See Cary's notes at gert01:/global/gscratch1/sd/whitney/ldms/README.ldms

# Example 1.  Publishing data directly to the node running the jsondump store

In this example, we'll run an ldmsd instance on nid001001 and publish a json
document from login35.

## Create an ldmsd configuration file to run the jsondump store

ldmsd.jsondump.direct.conf:
```
load name=store_jsondump
config name=store_jsondump path=/tmp/test-jsondump container=test-container stream=test-stream buffer=0 host=logstash port=0
```

The `port=0` option stops the jsondump store from connecting to the remote
host.

## Start the ldmsd instance running the jsondump store

The -F argument runs ldmsd in foreground mode.  It will remain attached
to the terminal and produce output to stdout/stderr.

```
ldmsd -v DEBUG -x sock:8000:nid001001-nmn -F -c ldmsd.jsondump.direct.conf
```

## Write a test json document to the test stream

```
login35$ echo '"hello world!"' | ldmsd_stream_publish -x sock -h nid001001-nmn -p 8000 -s test-stream -t json
```

This command will not produce any output.

## Check the aggregator logs

The aggregator will produce the following output:
```
Tue Mar 28 00:19:19 2023: ERROR     : store_jsondump: DEBUG: About to enter stream processing for "hello world!"
.
Tue Mar 28 00:19:19 2023: ERROR     : store_jsondump: DEBUG: In stream processing
```

## Check that the data was written to the store
```
# cat /tmp/test-jsondump/test-container/test-stream
"hello world!"
```

# Example 2.  Publishing via a compute node ldmsd

## Start an ldmsd on a compute node

ldmsd.compute.conf:
```
# No plugins to load.  this ldmsd only listens for stream events
```

```
ldmsd -v DEBUG -x sock:8000:nid001064-nmn -F -c ldmsd.compute.conf
```

## Start an ldmsd on an aggregator node

In this example login02 is our aggregator node.  The aggregator subscribes to
the streams exported by the compute nodes with the `prdcr_subscribe` directive.
Connections to sampler nodes are defined and established with the `prdcr_add`
and `prdcr_start` directives.

Our aggregator is configured as follows in
ldmsd.jsondump.aggregator.conf
```
prdcr_add name=nid001064 host=nid001064-nmn type=active xprt=sock port=8000 interval=60000000

prdcr_subscribe stream=test-stream regex=.*
prdcr_start_regex regex=.*

load name=store_jsondump
config name=store_jsondump path=/tmp/test-jsondump container=test-container stream=test-stream buffer=0 host=logstash port=0
```

NOTE:  /tmp/test-jsondump must exist before starting the ldmsd otherwise the jsondump
plugin aborts.

Start the ldmsd on the aggregator node
```
ldmsd -v DEBUG -x sock:8001:login02-nmn -F -c ldmsd.jsondump.aggregator.conf
```

## Publish a json document to the stream on the compute node

On the compute node run:
```
echo '"hello world!"' | ldmsd_stream_publish -x sock -h nid001064-nmn -p 8000 -s test-stream -t json
```

## Test to see if the document was written on the aggregator node

On the aggregator (login02-nmn):
```
cat /tmp/test-jsondump/test-container/test-stream
"hello world!"
```
