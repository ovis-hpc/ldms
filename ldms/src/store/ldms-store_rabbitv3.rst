.. _store_rabbitv3:

=====================
store_rabbitv3
=====================

--------------------------------------------
Man page for the LDMS store_rabbitv3 plugin
--------------------------------------------

:Date:   03 Dec 2016
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| load name=store_rabbitv3
| config name=store_rabbitv3 [ <attr> = <value> ]
| strgp_add name=store_rabbitv3 [ <attr> = <value> ]

DESCRIPTION
===========

The store_rabbitv3 plugin is a rabbitmq producer. Actual storage of data
must be arranged separately by configuring some other amqp client.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The configuration parameters root, host, port, exchange, vhost, user,
and pwfile are shared across all metric sets.

**config**
   | name=<plugin_name> root=<root> host=<host> port=<port>
     exchange=<exch> vhost=<vhost> user=<user> pwfile=<auth>
     extraprops=<y/n> metainterval=<mint>
   | These parameters are:

   name=<plugin_name>
      |
      | This MUST be store_rabbitv3.

   root=<root>
      |
      | The routing key prefix shared by all metric sets will be <root>.

   host=<host>
      |
      | The rabbitmq server host. The default is localhost.

   port=<port number>
      |
      | The server port on the nearest rabbitmq host. The default is
        5672.

   exchange=<exch>
      |
      | The amqp exchange to publish with is <exch>. The default is
        amq.topic.

   vhost=<vhost>
      |
      | The virtual host to be used is <vhost>. The default is "/".

   user=<user>
      |
      | The amqp username is <user>. The default is "guest".

   pwfile=<auth>
      |
      | The file <auth> contains the amqp user password in the format
        'secretword=password. The default password "guest" is assumed if
        no file is specified.

   extraprops=<y/n>
      |
      | Turn on (y) or off (n) the use of extra properties with all
        messages.

   mint
      |
      | The number of seconds between emission of time and host
        invariant (meta) metrics.

STORE ATTRIBUTE SYNTAX
======================

**store**
   | name=<plugin_name> schema=<schema_name> container=<container>

   name=<plugin_name>
      |
      | This MUST be store_rabbitv3.

   schema=<schema_name>
      |
      | The name of the metric group, independent of the host name.

   container=<container>
      |
      | The container will be used in the routing key. The current
        routing key patterns is:
        <root>.<container>.<schema>.<metric_name_amqp>.<producer_name>

   Use a unique container parameter for different metric sets coming
   from different sampler (e.g., do not use the same container for
   procstat and meminfo); however, use the same container for the same
   metric set coming from all hosts (e.g., for all meminfo).

AMQ event contents
==================

This store generates rabbitmq events. The message in each event is just
the metric value in string form. The message properties of each event
encode everything else.

The properties follow the AMQP standard, with LDMS specific
interpretations:

   timestamp
      |
      | The sample collection time in MICROSECONDS UTC. Divide by
        1,000,000 to get seconds UTC.

   type
      |
      | The ldms metric data type.

   app_id
      |
      | The app_id is the integer component_id, if it has been defined
        by the sampler.

Optional AMQ event contents
===========================

These fields and headers are present if extraprops=y is configured.

content_type
   |
   | <"text/plain"> for all.

reply_to
   |
   | The producer name.

metric
   |
   | The label registered by the sampler plugin, which might be
     anything.

metric_name_amqp
   |
   | The label modified to work as a routing key, not necessarily easily
     read.

metric_name_least
   |
   | The label modified to work as a programming variable name, possibly
     shortened and including a hash suffix. Not expected to be fully
     human-readable in all cases. It will be the same across runs for
     metric sets whose content labels do not vary across runs.

container
   |
   | The container configuration name.

schema
   |
   | The schema configuration name.

PAYLOAD FORMAT
==============

Payloads are ASCII formatted.

Scalar values are formatted in obvious C ways to ensure full precision
is retained. Each is a doublet: type,value

Array values are formatted as comma separated lists:
type,array-length,value[,value]*.

Char array values omit the commas in the value list, giving the
appearance of a string. Note however that there may be embedded nul
characters.

NOTES
=====

The semantics of LDMS messages are not an extremely close match to
network mail and news messages. The interpretations on message
properties used here may be subject to change in major releases of LDMS.

The authentication to AMQP server uses the SASL plaintext method. In HPC
environments this is normally secure. Additional options enabling
encryption are likely to appear in future work at a cost in CPU.
Normally, an amqp server federation member should be hosted on or very
near the LDMS aggregator host.

BUGS
====

The periodic emission of meta metrics should be per (producer,metric)
pair, but the store API is not yet sufficient to make this a scalable
and efficient operation. In the meanwhile, meta metrics are emitted on
first definition and assumed to be identical for a metric set across all
producers. The special case of component_id (if present) is handled
correctly when extraprops=y is configured.

EXAMPLES
========

See the LDMS test script ldms_local_amqptest.sh.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, rabbitmq-:ref:`server(1) <server>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`
