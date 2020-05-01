[#]: # (man)

NAME
====

ldmsd_store_app - LDMSD store_app storage plugin


SYNOPSIS
========
<b>load</b> <b>name</b>=<i>INST_NAME</i> <b>plugin</b>=<b>store_app</b>

<b>config</b> <b>name</b>=<i>INST_NAME</i> <b>path</b>=<i>CONTAINER_PATH</i>
  \[<b>perm</b>=<i>OCTAL_PERM</i>\]


DESCRIPTION
===========

`store_app` is an LDMSD storage plugin for storing data from the sets from
`app_sampler` LDMSD sampler plugin. `store_app` uses `SOS` as its database
back-end. The `path` option points to the `SOS` container. If the container does
not exist, it will be created with permission given by `perm` option (default:
0660). The
container contains multiple schemas, each of which assoicates with a metric from
the sets from `app_sampler` (e.g. `stat_utime`). Schemas in the container
have the following attributes:

- `timestamp` : the data sampling timestamp.
- `component_id`: the component ID producing the data.
- `job_id`: the Slurm job ID.
- `app_id`: the application ID.
- `rank`: the Slurm task rank.
- ***METRIC_NAME***: the metric value (the name of this attribute is the metric
  name of the metric).
- `comp_time`: (indexed) the join of `component_id` and `timestamp`.
- `time_job`: (indexed) the join of `timestamp` and `job_id`.
- `job_rank_time`: (indexed) the join of `job_id`, `rank`, and `timestamp`.
- `job_time_rank`: (indexed) the join of `job_id`, `timestamp`, and `rank`.

Please also see [ldmsd-aggregator](../../ldmsd/ldmsd-aggregator.md)(7) *STORE*
section for more information regarding how storage plugin works in `ldmsd`.


CONFIG OPTIONS
==============
<dl>
<dt>name</dt>
<dd>The name of the plugin instance to configure.</dd>
<dt>path</dt>
<dd>The path to the SOS container.</dd>
<dt>perm</dt>
<dd>The octal mode (e.g. 0777) that is used in SOS container creation. The
default is <b>0660</b>.</dd>
</dl>

EXAMPLES
========
```
# in ldmsd config file
load name=mystore plugin=store_app
config name=mystore path=/sos/app perm=0600
strgp_add name=app_strgp container=mystore schema=app_sampler
# NOTE: the schema in strgp is LDMS set schema, not to confuse with the one
# schema per metric in our SOS store.
strgp_prdcr_add name=app_strgp regex=.*
strgp_start name=app_strgp
```

The following is an example on how to retrieve the data using Python:
```python
from sosdb import Sos
cont = Sos.Container()
cont.open('/sos/app')
sch = cont.schema_by_name('status_vmsize')
attr = sch.attr_by_name('time_job') # attr to iterate over must be indexed
itr = attr.attr_iter()
b = itr.begin()
while b == True:
  obj = itr.item()
  print(obj['status_vmsize']) # object attribute access by name
  print(obj[5]) # equivalent to above
  print(obj[:]) # get everything at once
  b = itr.next()
```


SEE ALSO
========
[ldmsd-aggregator](../../ldmsd/ldmsd-aggregator.md)(7)
