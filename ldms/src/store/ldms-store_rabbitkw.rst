.. _store_rabbitkw:

=====================
store_rabbitkw
=====================

--------------------------------------------
Man page for the LDMS store_rabbitkw plugin
--------------------------------------------

:Date:   10 Jun 2018
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller or in a configuration file
| load name=store_rabbitkw
| config name=store_rabbitkw [ <attr> = <value> ]
| strgp_add name=store_rabbitkw [ <attr> = <value> ]

DESCRIPTION
===========

The store_rabbitkw plugin is a rabbitmq producer. Actual storage of data
must be arranged separately by configuring some other amqp client.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The configuration parameters routing_key, host, port, exchange, vhost,
user, and pwfile are shared across all metric sets.

**config**
   | name=<plugin_name> exchange=<exch> routing_key=<route> host=<host>
     port=<port> vhost=<vhost> user=<user> pwfile=<auth>
     [extraprops=<y/n> logmsg=<y/n> useserver=[y/n> heartbeat=<sec>
     timeout=<msec> retry=<sec>]
   | These parameters are:

   name=<plugin_name>
      |
      | This MUST be store_rabbitkw.

   routing_key<route>
      |
      | The routing key shared by all metric sets is <route>.

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
        amq.topic. This must preexist; the plugin will no cause its
        creation.

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

   retry=<sec>
      |
      | If amqp connection fails due to network or server issue, retry
        every <sec> seconds. Default is 60.

   heartbeat=<sec>
      |
      | Heartbeat interval used to detect failed connections.

   timeout=<millisec>
      |
      | Timeout to use for connections, in milliseconds. Default is
        1000.

   extraprops=<y/n>
      |
      | Turn on (y) or off (n) the use of extra properties with all
        messages. If AMQP-based filtering is not planned, 'n' will
        reduce message sizes slightly.

   logmsg=<y/n>
      |
      | Enable (y) or disable (n, the default) logging all message
        metric content at the DEBUG level. This is a debugging option.

   useserver=<y/n>
      |
      | Enable (y, the default) or disable (n) calls to the amqp server;
        this is a debugging option.

STORE ATTRIBUTE SYNTAX
======================

**store**
   | name=<plugin_name> schema=<schema_name> container=<container>

   name=<plugin_name>
      |
      | This MUST be store_rabbitkw.

   schema=<schema_name>
      |
      | The name of the metric group, independent of the host name. The
        schema will be used as a header in messages if extraprops is y.

   container=<container>
      |
      | The container will be used as a header in messages if extraprops
        is y.

AMQ event contents
==================

This store generates rabbitmq events containing the data from LDMS set
instances. All events are on the single queue that is configured.

The properties follow the AMQP standard, with LDMS specific
interpretations:

   timestamp
      |
      | The sample collection time in MICROSECONDS UTC. Divide by
        1,000,000 to get seconds UTC.

   app_id
      |
      | The app_id is LDMS.

Optional AMQ event contents
===========================

These fields and headers are present if extraprops=y is configured.

content_type
   |
   | <"text/plain"> for all.

reply_to
   |
   | The metric set instance name.

container
   |
   | The container configuration name.

schema
   |
   | The schema configuration name.

PAYLOAD FORMAT
==============

Payloads are ASCII formatted, tab separated "label=val" lists.

Scalar metric values are formatted in obvious C ways to ensure full
precision is retained. Each is a tab-separated triplet 'metric=$name
type=$scalar_type value=$value'. Before the metric values on each line
are the keys and values: timestamp_us, producer, container, schema.

Array values are formatted as semicolon separated lists: Each metric
appears as a tab-separated quartet 'metric=$name type=$scalar_type
length=$array_length value=$value'.

CHAR_ARRAY values are formatted as strings. Note these are terminated at
the first nul character.

NOTES
=====

The semantics of LDMS messages are not an extremely close match to
network mail and news messages targeted by AMQP. The interpretations on
message properties used here may be subject to change in future
releases.

The authentication to AMQP server uses the SASL plaintext method. In HPC
environments this is normally secure. Additional options enabling
encryption are likely to appear in future work at a cost in CPU.
Normally, an amqp server federation member should be hosted on or very
near the LDMS aggregator host.

Presently each payload contains a single line (with tab separators).
Future versions may capture multiple set instances per message, where
each set is separated by newlines from the others.

The behavior of this AMQP client when faced with AMQP server
disappearance is to retry connection later and to ignore any metric data
seen while disconnected.

BUGS
====

String data containing tab characters are not compatible with this data
encoding. This may be fixed when a satisfactory alternate representation
is agreed for these special characters.

EXAMPLES
========

See the LDMS test script rabbitkw

ADMIN HINTS
===========

On Linux, this requires an amqp service (typically
rabbitmq-server.service) running in the network. That service may
require epmd.service.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, rabbitmq-:ref:`server(1) <server>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`store_rabbitv3(7) <store_rabbitv3>`
