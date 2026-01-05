For perfevent2 example
======================

1) Generate ``perfdb.json`` using the following command:

        ldms-perfdb-gen -o perfdb.json


2) Run ldmsd (listening on port 4411) by:

        ldmsd -c samp.conf

   If you get an error about "Cannot resolve event ...", it is likely that the
   event specified in the file "perfconf.json" is not available on the machine.
   Simply remove the event, or add other events from the following command:

        perf list hw cache pmu


3) Fetch 2 samples and show the counter diff:

        ldms_ls.py
