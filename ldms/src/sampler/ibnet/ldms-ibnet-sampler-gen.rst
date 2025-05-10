.. _ldms-ibnet-sampler-gen:

======================
ldms-ibnet-sampler-gen
======================

--------------------------------------------------
Man page for the LDMS ibnet plugin support utility
--------------------------------------------------

:Date:   4 June 2020
:Manual section: 1
:Manual group: LDMS sampler


ldms-get-opa-network.sh - man page for the LDMS ibnet plugin support
utility

SYNOPSIS
========

ldms-ibnet-sampler-gen --samplers <hostfile> --out <output prefix>
[options]

DESCRIPTION
===========

The ldms-ibnet-sampler-gen script produces files splitting the ports in
the netfile among the hosts listed in the samplers file. The input is
expected to be the network dump of an approximately three-level FAT
tree.

OPTIONS
=======

::

     -h, --help            show the help message and exit
     --out OUTPREFIX       prefix of output files
     --net IBNDPFILE       file name of output collected from 'ibnetdiscover -p'
     --opa OPAFILE         file name of output collected from 'ldms-get-opa-network.sh'
     --samplers HOSTFILE   file listing samplers as named in the node name map, one per line.
     --lidnames            dump lid,name map to stdout and exit.
     --annotate            annotate out sampler assignment files with node-name-map strings.
                           and lists of unassigned switch ports.
     --sharp               port to exclude in topology calculations (for sharp)
     --tier0               generate tier0-1 graphs
     --tier1               generate tier1-2 graphs
     --tier2               generate tier2-3 graphs
     --circo-tiers CIRCO_PREFIX
                           dump circo tier plots to files starting with prefix
                           given CIRCO_PREFIX.
     --sfdp-tiers SFDP_PREFIX
                           dump circo tier plots to files starting with prefix
                           given SFDP_PREFIX.
     --info                print key intermediate results
     --debug               print miscellaneous debug messages
     --dump_sw             print switches parsed
     --dump_ca             print HCA list parsed
     --dump_links          print links parsed
     --dump_tiers          print tiers discovered
     --dump_parse          print parser debugging

EXAMPLES
========

::

   cat <<EOF >cluster-samplers
   admin1 qib0
   admin2 qib0
   admin3 qib0
   EOF

   ibnetdiscover -p > cluster-p-netdiscover

   # check lids for being parsed right
   ldms-ibnet-sampler-gen --lidnames --net cluster-p-netdiscover --samplers x --out x |
       sort -k2 -t, > lid.host.txt

   ldms-ibnet-sampler-gen --net cluster-p-netdiscover --samplers clustre-samplers --sharp 37 --annotate --out sbx

::

   cat <<EOF >cluster-samplers
   admin1 hfi1_0
   admin2 hfi1_0
   admin3 hfi1_0
   EOF

   ldms-get-opa-network.sh > cluster-opa-map

   # check lids for being parsed right
   ldms-ibnet-sampler-gen --lidnames --opa cluster-opa-map --samplers cluster-samplers  --out x |sort -k2 -t, > lid.host.txt

   ldms-ibnet-sampler-gen --opa cluster-opa-map --samplers cluster-samplers --out swx

NOTES
=====

A Mellanox SHARP port appears as an HCA in a switch. Connections on the
sharp port should be ignored for topology decomposition and sampler load
balancing purposes, as they usually make the topology flat if included.

This program does not directly invoke infiniband or omnipath utilities.
It does invoke (and require) graphviz utilities if the tier, circo, or
sfdp options are applied.

Applying the --node-name-map option to ibnetdiscover when generating the
net file makes the results more readable.

SEE ALSO
========

:ref:`ibnet(7) <ibnet>`, circo, dot, ldms-get-opa-network, ibnetdiscover
