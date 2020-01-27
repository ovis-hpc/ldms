[#]: # (man)

NAME
====

ldmsd-sampler-dev - LDMSD sampler plugin development guide


SYNOPSIS
========

`#include "ldmsd_plugin.h"`<br>
`#include "ldmsd_sampler.h"`


DESCRIPTION
===========

`ldmsd` (LDMS Daemon) uses sampler plugin instances to read data from the
sources and put them into LDMS sets. A sampler plugin instance is a C extended
structure of `ldmsd_plugin_inst_s`. Each sampler instance has a copy of
`ldmsd_sampler_type_s` structure (an extension of `ldmsd_plugin_type_s`)
associated with it. `ldmsd_plugin_inst_s.base` points to `ldmsd_sampler_type_s`,
and `ldmsd_sampler_type_s.base.inst` points back to the instance as depicted
below:

```txt

    .-----------------------.
    | inst extension        |
    |-----------------------|        .-----------------------.
    | .-------------------. |        | ldmsd_sampler_type_s  |
    | |ldmsd_plugin_inst_s| |        |-----------------------|
    | |-------------------| |        | .-------------------. |
    | | base  ------------+-+------->| |ldmsd_plugin_type_s| |
    | |                   | |        | |-------------------| |
    | | INSTANCE APIs     | |<-------+-+-inst              | |
    | '-------------------' |        | |                   | |
    |                       |        | | PLUGIN APIs       | |
    | EXTENSION DATA        |        | '-------------------' |
    '-----------------------'        |                       |
                                     | SAMPLER APIs          |
                                     '-----------------------'

```

`ldmsd` uses APIs defined in `ldmsd_plugin_inst_s`, `ldmsd_plugin_type_s` and
`ldmsd_sampler_type_s` (the sampler type extension) to communicate with the
plugin sampler instance. The inst extension part usually holds
plugin-instance-specific information such as states and file descriptors. The
extended instance structure serves an instnace object to the `ldmsd` (it will
see the structure as `ldmsd_plugin_inst_s`). The plugin implementation can see
the extended instance structure as `self` in Python or `this` in C++.

The interactions between `ldmsd` and a sampler plugin instance can be summarized
as follows:

<dl>

<dt>instance creation</dt>
<dd>
`ldmsd` create a plugin instance upon a `load` user request (see [Creating an
Instance](#creating-an-instance)) by calling `new()` function in the plugin
library. The plugin then allocate the memory to hold the extended instance
structure and setup INSTANCE APIs. At this point, the plugin instance does not
have `ldmsd_sampler_type_s` associated with it yet. `ldmsd` will inspect the
`ldmsd_plugin_inst_s.type` (must be `LDMSD_SAMPLER_TYPENAME` in this case) and
create a copy of `ldmsd_sampler_type_s` after `new()` has returned.
</dd>

<dt>sampler instance initialization</dt>
<dd>
After `new()` has returned, and a copy of `ldmsd_sampler_type_s` has been
created and linked with the instance by `ldmsd`, `ldmsd_plugin_inst_s.init()` is
called to let the plugin further setup the SAMPLER APIs. Additional plugin
resources could be initialized during the init call as well.
</dd>

<dt>configuration</dt>
<dd>
`ldmsd` configure the plugin instance when the user gave `config` command. The
configuration attributes from the command are passed along to the plugin config
function (see [Config](#config) subsection). After a successful config, the
plugin became configured and is ready to be sampled by `smplr` sampler policy
(see [ldmsd-sampler][ldmsd-sampler](7)). Some plugin may need multiple `config`
call to be *configured*.
</dd>

<dt>sampling</dt>
<dd>
After the plugin instance became *configured*, and after `smplr` started,
`sample()` API will be periodically called (see [Sample](#sample)). The periodic
calls will keep coming until the associated `smplr` is stopped.
</dd>

<dt>deletion</dt>
<dd>
After the associated `smplr` stopped, and a user `term` request for the
instance, the sampler instance will be deleted (see [Deletion of Sampler Plugin
Instance](#deletion-of-sampler-plugin-instance)).
</dd>

</dl>

We will implement a `thermal` sampler in the this guide to illustrate how to
implement an LDMSD sampler plugin in the following subsections:

[Preparation](#preparation)<br>
[Conventions](#conventions)<br>
[Extending Plugin Instance](#extending-plugin-instance)<br>
[Creating an Instance](#creating-an-instance)<br>
[Help and Description](#help-and-description)<br>
[Initialization](#initialization)<br>
[Sampler APIs](#sampler-apis)<br>
[Config](#config)<br>
[Createing a Schema and a Set](#creating-a-schema-and-a-set)<br>
[Sample](#sample)<br>


Preparation
-----------
<span id="preparation"></span>

This step is for developing a sampler plugin in ovis development tree
(recommended). If you are developing a plugin outside of ovis development tree,
you can skip this step, and have to make sure that the plugin links with
`libsampler.so`.

Please follow these steps to prepare a workspace for developing `thermal`
sampler:

**1)** Create a sub directory `thermal` in `ldms/src/ldmsd-samplers`.


**2)** Create a `Makefile.am` and a (or several) C source file for the plugin
(`thermal.c` for this example). The `Makefile.am` should look like the
following:

```make
# file: ldms/src/ldmsd-samplers/thermal/Makefile.am
include ../common.am

pkglib_LTLIBRARIES = libthermal.la

libthermal_la_SOURCES = thermal.c
libthermal_la_CFLAGS  = $(SAMPLER_CFLAGS)
libthermal_la_LIBADD  = $(SAMPLER_LIBADD)
libthermal_la_LDFLAGS = $(SAMPLER_LDFLAGS)
```

The `../common.am` defines `SAMPLER_CFLAGS`, `SAMPLER_LIBADD` and
`SAMPLER_LDFLAGS` variables containing necessary artifacts to build the plugin.


**3)** Edit `ldms/configure.ac`, add an option for the sampler, and add the
`Makefile` in "2)" in the `AC_CONFIG_FILES` list as follows:

```autoconf
dnl Options for sampler
...

OPTION_DEFAULT_DISABLE([thermal], [ENABLE_THERMAL])

...

AC_CONFIG_FILES([Makefile src/Makefile src/core/Makefile
                 ...
                 src/thermal/Makefile
                 ...
])
...

```

Please note that the paths in `ldms/configure.ac` is relative to `ldms/`
directory (where the autoconf script resides).

`OPTION_DEFAULT_DISABLE` macro is used so that the plugin won't be build unless
the user specify `--enable-thermal` configure option. In the case that the
plugin does not depend on extra libraries other than those required by `ldmsd`,
we may use `OPTION_DEFAULT_ENABLE` macro to have it build by default (unless the
user specify `--disable-thermal` configure option).

In the case of extra dependencies, please also add a library / header checking
logic in the `ldms/configure.ac`.


**4)** Edit `ldms/src/ldmsd-samplers/Makefile.am` and add `thermal` as a
conditional build subdirectory:

```make
# file: ldms/src/ldmsd-samplers/Makefile.am
...
if ENABLE_THERMAL
SUBDIRS += thermal
endif
...
```

**5)** (Optional) For convenience, the template
`ldms/templates/sampler_template.c` can be copied to
`ldms/src/ldmsd-samplers/thermal/thermal.c` as a starting point of development.


Conventions
-----------
<span id="conventions"></span>

**Functions** of the plugin are `static`, except for `new()` function. The name
of the plugin functions are preferably prefixed with plugin name for
readability (e.g. `static int thermal_init(ldmsd_plugin_inst_t pi)`).

The name of the **extended instance** structure is preferably `struct
<PLUGIN_NAME>_inst_s` (e.g. `struct thermal_inst_s`).


Extending Plugin Instance
-------------------------
<span id="extending-plugin-instance"></span>

To extend a sample plugin instance, simply define a new structure with `struct
ldmsd_plugin_inst_s` as the first member. It is conventional to name the element
`base`. The following is an example for `thermal`:

```c
/* file: ldms/src/ldmsd-samplers/thermal/thermal.c */
...

typedef struct thermal_inst_s *thermal_inst_t;
struct thermal_inst_s {
    struct ldmsd_plugin_inst_s base;
    int zone; /* zone to monitor */
    int fd; /* file descriptor to the thermal temperature file */
};
...
```


Creating an Instance
--------------------
<span id="creating-an-instance"></span>

The sampler plugin implementation must implement `ldmsd_plugin_inst_t new()`
function. This function is called by `ldmsd` when it needs to create an instance
of the plugin (when `load` ldmsd config command is processed).

`new()` function must allocate memory for the newly created instance and
setup the instance APIs. The following is an example for `thermal` sampler:

```c
/* file: ldms/src/ldmsd-samplers/thermal/thermal.c */
...

/* global structure for setting up instance APIs and some default values */
struct thermal_inst_s __inst = {
	.base = {
		/* must get version from this macro */
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		/* type must be LDMSD_SAMPLER_TYPENAME */
		.type_name   = LDMSD_SAMPLER_TYPENAME,
		.plugin_name = "thermal",

                /* Common Plugin APIs, these functions are to be implemented */
		.desc   = thermal_desc,
		.help   = thermal_help,
		.init   = thermal_init,
		.del    = thermal_del,
		.config = thermal_config,
	},

	.zone =  0,
	.fd   = -1,
};

ldmsd_plugin_inst_t new()
{
	thermal_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst; /* copy __inst value above */
	return &inst->base;
}
...
```

At this point, the corresponding `ldmsd_sampler_type_s` has not been created and
paired with this instance yet. `ldmsd ` will handle the creation and pairing
after `new()` returned.


Help and Description
--------------------
<span id="help-and-description"></span>

The `ldmsd_plugin_inst_s.desc()` interface is for a short description of the
plugin, and `ldmsd_plugin_inst_s.help()` interface is for a long help text
describing how to use/configure the plugin instance. The `thermal` description
and help implemntation is as the following:

```c
/* file: ldms/src/ldmsd-samplers/thermal/thermal.c */
...
static const char *thermal_desc(ldmsd_plugin_inst_t pi)
{
	return "thermal - /sys/class/thermal sampler";
}

static const char *thermal_help(ldmsd_plugin_inst_t pi)
{
	return	"thermal config synopsis:\n"
		"    config name=INST [COMMON OPTIONS] zone=INT\n"
		"\n"
		"Descriptions:\n"
		"    `zone` option is an integer specifying the zone\n"
		"    in /sys/class/thermal/ to monitor\n";
}
...
```


Initialization
--------------
<span id="initialization"></span>

After the plugin instance is created by `new()`, and `ldmsd` successfully link
it to a copy of `ldmsd_sampler_type_s`, `ldmsd_plugin_inst_s.init()` is called
to initialize the plugin instance. The plugin is expected to initialize its
resources during this call. Some plugin that relies on static files that
requires no configuration call may decide to open those files during init.

The sampler plugin must implement `ldmsd_sampler_type_s.update_schema()` to
define plugin-specific metrics, and `ldmsd_sampler_type_s.update_set()` to
update values of plugin-specific metrics in the set. These two functions need to
be set in `ldmsd_plugin_inst_s.init()`. The following is an example of init
function of `thermal` sampler:

```c
static int thermal_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(pi);

	samp->update_schema = thermal_update_schema;
	samp->update_set = thermal_update_set;

	/* ((thermal_inst_t)pi)->fd has already been initialized in `new()` */
	return 0;
}
```


Sampler APIs
------------
<span id="sampler-apis"></span>

The APIs in `ldmsd_sampler_type_s` are for `ldmsd` to communicate to the sampler
plugin instance (e.g. tell plugin instance to populate data set), and for
sampler plugin to make a request to `ldmsd` (e.g. creating a set). If the plugin
implementation decides to override an API, it should do so in
`ldmsd_plugin_inst_s.init()`. The plugin implementation can access the
associated `ldmsd_sampler_type_s` by using a convenient `LDMSD_SAMPLER()` macro.
You will see the usage of these APIs in the upcoming subsections.

The following is a list of sampler APIs and their descriptions.

<dl>

<dt>ldmsd_sampler_type_s.create_schema()</dt>
<dd>
This API is meant to be called by the sampler plugin implementation when it
needs to create and construct an LDMS schema (e.g. before creating a set). This
function will create a new LDMS schema object and add common metrics (e.g.
<code>component_id</code>, <code>job_id</code>) to the schema. Then, this
function calls <code>ldmsd_sampler_type_s.update_schema()</code> which is
expected to be overridden by sampler plugin implementation to add
plugin-specific metrics into the schema.
<br><br>
The plugin implementation must not override this function.
</dd>

<dt>ldmsd_sampler_type_s.update_schema()</dt>
<dd>
The plugin implementation should override this function to add metrics to the
supplied schema. This function will be called as a subsequent call of
<code>ldmsd_sampler_type_s.create_schema()</code>.
</dd>

<dt>ldmsd_sampler_type_s.create_set()</dt>
<dd>
This API is meant to be called by the sampler plugin, and not to be overridden.
The sampler plugin call this API to create an LDMS set (with schema created from
<code>ldmsd_sampler_type_s.create_schema()</code>). A sampler plugin may create
multiple sets. However, please mind that the set name must be unique in the
<code>ldmsd</code>.
<br><br>
If the set has been created successfully, it is added into the
<code>ldmsd_sampler_type_s.set_list</code> with <code>ldmsd_set_entry_s</code>
wrapping the <code>ldms_set_t</code>.
</dd>

<dt>ldmsd_sampler_type_s.sample()</dt>
<dd>
This is an API that is called by <code>ldmsd</code> to populate data in the sets
created by the sampler plugin instance. The default implementation is to go
through sets in <code>ldmsd_sampler_type_s.set_list</code>. For each set, begin
LDMS transaction then call <code>ldmsd_sampler_type_s.base_update_set()</code>
to update the common metrics (e.g.  <code>job_id</code>). Next, call
<code>ldmsd_sampler_type_s.update_set()</code> to update sample-specific
metrics, and finally end the LDMS transaction.
<br><br>
The sampler plugin implementation can override this function.
</dd>

<dt>ldmsd_sampler_type_s.base_update_set()</dt>
<dd>
The sampler plugin must not override this function.
<br><br>
This function updates the common metrics portion of the set created by
<code>ldmsd_sampler_type_s.create_set()</code>, which is called by the default
implementation of <code>ldmsd_sampler_type_s.sample()</code>. If the sampler
plugin implementation overrides <code>ldmsd_sampler_type_s.sample()</code>, it
should call this function to update the common metrics part of the set.
</dd>

<dt>ldmsd_sampler_type_s.update_set()</dt>
<dd>
The plugin implementation is expected to override this API to populate
plugin-specific metrics. This function is a subsequence call from the default
implementation of <code>ldmsd_sampler_type_s.sample()</code>.
</dd>

<dt>ldmsd_sampler_type_s.delete_set()</dt>
<dd>
The sampler plugin must not override this function.
<br><br>
This is an API for the sampler plugin implementation to call to delete an LDMS
set created from <code>ldmsd_sampler_type_s.create_set()</code>.
<br><br>
*NOTE:* The sets created by <code>ldmsd_sampler_type_s.create_set()</code> will
also be automatically deleted when the plugin instance is deleted.
</dd>

</dl>

The following is `thermal` implementation of sampler API:

```c
...

static int
thermal_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t sch)
{
	int rc;
	rc = ldms_schema_metric_add(sch, "temp", LDMS_V_U64, "mC");
	/* NOTE: ldms_schema_metric_add() returns metric index (>=0) on
	 *       success, or `-errno` on error. */
	if (rc < 0)
		return -rc; /* rc == -errno */
	return 0; /* succeeded, return no error */
}

static int
thermal_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	thermal_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(pi);
	char buff[64];
	int rc;
	uint64_t val;
	rc = lseek(inst->fd, 0, SEEK_SET);
	if (rc)
		return errno; /* seek failed */
	rc = read(inst->fd, buff, sizeof(buff));
	if (rc < 0)
		return errno; /* read failed */

	val = strtoul(buff, NULL, 10);
	/* `samp->first_idx` is the index of the first plugin-specific
	 * metric we added in thermal_update_schema(). This is
	 * maintained by `ldmsd`. */
	ldms_metric_set_u64(set, samp->first_idx, val);
	return 0;
}
...
```


Config
------
<span id="config"></span>

After `new()` and `ldmsd_plugin_inst_s.init()` (corresponding to the user's
`load` command), `ldmsd_plugin_inst_s.config()` is called when the user gave
`config` command. For example:

```conf
load name=t plugin=thermal
config name=t zone=0
```

The configuration attributes are passed along to `ldmsd_plugin_inst_s.config()`
API by `ldmsd` via `json` parameter (see [json_util.h][json_util.h]). If
`ldmsd_plugin_inst_s.config()` is not implemented (`NULL`), the base
`ldmsd_plugin_type_s.config()` is called instead.

`ldmsd_plugin_inst_s.config()` implementation should also call
`ldmsd_plugin_type_s.config()` to process the common configuration attributes
(e.g. `producer` and `component_id`). It is quite common among samplers to
create LDMS sets in config function.

The following is the config implementaion of `thermal`:

```c
static int
thermal_config(ldmsd_plugin_inst_t pi, json_entity_t json,
		char *ebuf, int ebufsz)
{
	thermal_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(pi);
	ldms_schema_t sch;
	ldms_set_t set;
	const char *val;
	char path[128];
	int rc;

	if (inst->fd >= 0) {
		/* reconfigure detected */
		snprintf(ebuf, ebufsz, "Reconfiguration is not supported.");
		return EINVAL;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc; /* base config should have `ebuf` filled */
	val = json_attr_find_str(json, "zone");
	if (val) {
		/* `zone` is given */
		inst->zone = atoi(val);
	}

	/* open temperature file */
	snprintf(path, sizeof(path),
		"/sys/class/thermal/thermal_zone%d/temp", inst->zone);
	inst->fd = open(path, O_RDONLY);
	if (inst->fd < 0) {
		snprintf(ebuf, ebufsz, "Error %d opening file: %s",
			 errno, path);
		return errno;
	}

	/* create schema and set */
	sch = samp->create_schema(pi);
	if (!sch) {
		snprintf(ebuf, ebufsz, "Schema creation error: %d", errno);
		return errno;
	}
	set = samp->create_set(pi, samp->set_inst_name, sch, NULL);
	/* NOTE: samp->set_inst_name is the set instance name obtained from
	 * the `instance` config option (by samp->base.config()).
	 */
	ldms_schema_delete(sch); /* 1-time use */
	if (!set) {
		snprintf(ebuf, ebufsz, "Set creation failed: %d", errno);
		return errno;
	}
	return 0;
}
```


Createing a Schema and a Set
----------------------------
<span id="creating-a-schema-and-a-set"></span>

An LDMS set of the sampler plugin should be created by
`ldmsd_sampler_type_s.create_set()` API, with a schema created from
`ldmsd_sampler_type_s.create_schema()` as the following snippet:

```c
sch = samp->create_schema(pi);
set = samp->create_set(pi, SET_NAME, sch, CTXT);
```

The set usually get created in config function (as seen in [Config](#config)
subsection). However, the plugin implementation can create sets anytime.

`ldmsd_sampler_type_s.create_schema()` subsequently calls
`ldmsd_sampler_type_s.update_schema()` to add plugin-specific metrics to the
schema. The following is the `update_schema()` implementation of `thermal`
sampler:

```c
/* part of `thermal.c` */

static int
thermal_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t sch)
{
	int rc;
	rc = ldms_schema_metric_add(sch, "temp", LDMS_V_U64, "mC");
	/* NOTE: ldms_schema_metric_add() returns metric index (>=0) on
	 *       success, or `-errno` on error. */
	if (rc < 0)
		return -rc; /* rc == -errno */
	return 0; /* succeeded, return no error */
}
```

Sample
------
<span id="sample"></span>

If a sampler policy (see `smplr` in [ldmsd-sampler][ldmsd-sampler]) is setup and
associates with the sampler plugin instance, the `ldmsd_sampler_type_s.sample()`
is called periodically by `ldmsd`. The default implementation of the sample
function is to iterate through the `ldmsd_sampler_type_s.set_list`, and update
the data for each set by calling `ldmsd_sampler_type_s.base_update_set()` and
`ldmsd_sampler_type_s.update_set()` for common metrics and plugin-specific
metrics respectively. The following is the `update_set()` implementation of
`thermal` sampler:

```c
/* part of `thermal.c` */

static int
thermal_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	thermal_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(pi);
	char buff[64];
	int rc;
	uint64_t val;
	rc = lseek(inst->fd, 0, SEEK_SET);
	if (rc)
		return errno; /* seek failed */
	rc = read(inst->fd, buff, sizeof(buff));
	if (rc < 0)
		return errno; /* read failed */

	val = strtoul(buff, NULL, 10);
	/* `samp->first_idx` is the index of the first plugin-specific
	 * metric we added in thermal_update_schema(). This is
	 * maintained by `ldmsd`. */
	ldms_metric_set_u64(set, samp->first_idx, val);
	return 0;
}

```

Deletion of Sampler Plugin Instance
-----------------------------------
<span id="delete"></span>

A plugin instance can be deleted upon user request (`term` command). The
`ldmsd_plugin_inst_s.del()` is called to let the plugin implementation clear
instnace-specific resources. The plugin implementation must **NOT** free the
instance itself. `ldmsd` will free the instance afterward.

The following is `del()` implementation of `thermal` sampler:

```c
/* part of `thermal.c` */
static void
thermal_del(ldmsd_plugin_inst_t pi)
{
	thermal_inst_t inst = (void*)pi;
	if (inst->fd >= 0)
		close(inst->fd);
}
```


EXAMPLE
=======

This section contain the complete code that we have been building through the
guide.

Source Files
------------

This is the full content of `thermal.c`:

```c

/* file: ldms/src/ldmsd-samplers/thermal/thermal.c */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"


#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct thermal_inst_s *thermal_inst_t;
struct thermal_inst_s {
	struct ldmsd_plugin_inst_s base;

	int zone; /* zone to monitor */
	int fd; /* file descriptor to the thermal temperature file */
};

/* ============== Sampler Plugin APIs ================= */

static int
thermal_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t sch)
{
	int rc;
	rc = ldms_schema_metric_add(sch, "temp", LDMS_V_U64, "mC");
	/* NOTE: ldms_schema_metric_add() returns metric index (>=0) on
	 *       success, or `-errno` on error. */
	if (rc < 0)
		return -rc; /* rc == -errno */
	return 0; /* succeeded, return no error */
}

static int
thermal_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	thermal_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(pi);
	char buff[64];
	int rc;
	uint64_t val;
	rc = lseek(inst->fd, 0, SEEK_SET);
	if (rc)
		return errno; /* seek failed */
	rc = read(inst->fd, buff, sizeof(buff));
	if (rc < 0)
		return errno; /* read failed */

	val = strtoul(buff, NULL, 10);
	/* `samp->first_idx` is the index of the first plugin-specific
	 * metric we added in thermal_update_schema(). This is
	 * maintained by `ldmsd`. */
	ldms_metric_set_u64(set, samp->first_idx, val);
	return 0;
}


/* ============== Common Plugin APIs ================= */

static const char *
thermal_desc(ldmsd_plugin_inst_t pi)
{
	return "thermal - /sys/class/thermal sampler";
}

static const char *
thermal_help(ldmsd_plugin_inst_t pi)
{
	return	"thermal config synopsis:\n"
		"    config name=INST [COMMON OPTIONS] zone=INT\n"
		"\n"
		"Descriptions:\n"
		"    `zone` option is an integer specifying the zone\n"
		"    in /sys/class/thermal/ to monitor\n";
}

static int
thermal_config(ldmsd_plugin_inst_t pi, json_entity_t json,
		char *ebuf, int ebufsz)
{
	thermal_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(pi);
	ldms_schema_t sch;
	ldms_set_t set;
	const char *val;
	char path[128];
	int rc;

	if (inst->fd >= 0) {
		/* reconfigure detected */
		snprintf(ebuf, ebufsz, "Reconfiguration is not supported.");
		return EINVAL;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc; /* base config should have `ebuf` filled */
	val = json_attr_find_str(json, "zone");
	if (val) {
		/* `zone` is given */
		inst->zone = atoi(val);
	}

	/* open temperature file */
	snprintf(path, sizeof(path),
		"/sys/class/thermal/thermal_zone%d/temp", inst->zone);
	inst->fd = open(path, O_RDONLY);
	if (inst->fd < 0) {
		snprintf(ebuf, ebufsz, "Error %d opening file: %s",
			 errno, path);
		return errno;
	}

	/* create schema and set */
	sch = samp->create_schema(pi);
	if (!sch) {
		snprintf(ebuf, ebufsz, "Schema creation error: %d", errno);
		return errno;
	}
	set = samp->create_set(pi, samp->set_inst_name, sch, NULL);
	/* NOTE: samp->set_inst_name is the set instance name obtained from
	 * the `instance` config option (by samp->base.config()).
	 */
	ldms_schema_delete(sch); /* 1-time use */
	if (!set) {
		snprintf(ebuf, ebufsz, "Set creation failed: %d", errno);
		return errno;
	}
	return 0;
}

static void
thermal_del(ldmsd_plugin_inst_t pi)
{
	thermal_inst_t inst = (void*)pi;
	if (inst->fd >= 0)
		close(inst->fd);
}

static int
thermal_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(pi);

	samp->update_schema = thermal_update_schema;
	samp->update_set = thermal_update_set;

	/* ((thermal_inst_t)pi)->fd has already been initialized in `new()` */
	return 0;
}

/* global structure for setting up instance APIs and some default values */
struct thermal_inst_s __inst = {
	.base = {
		/* must get version from this macro */
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		/* type must be LDMSD_SAMPLER_TYPENAME */
		.type_name   = LDMSD_SAMPLER_TYPENAME,
		.plugin_name = "thermal",

                /* Common Plugin APIs, these functions are to be implemented */
		.desc   = thermal_desc,
		.help   = thermal_help,
		.init   = thermal_init,
		.del    = thermal_del,
		.config = thermal_config,
	},

	.zone =  0,
	.fd   = -1,
};

ldmsd_plugin_inst_t new()
{
	thermal_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst; /* copy __inst value above */
	return &inst->base;
}

/* EOF */
```


This is the Makefile for building `thermal` sampler:

```make
# file: ldms/src/ldmsd-samplers/thermal/Makefile.am

include ../common.am

pkglib_LTLIBRARIES = libthermal.la

libthermal_la_SOURCES = thermal.c
libthermal_la_CFLAGS  = $(SAMPLER_CFLAGS)
libthermal_la_LIBADD  = $(SAMPLER_LIBADD)
libthermal_la_LDFLAGS = $(SAMPLER_LDFLAGS)
```


Configure and Makefile Modification
-----------------------------------

The following diff shows the modification to configure and Makefile relating to
the build of `thermal` sampler:

```diff
--- a/ldms/configure.ac
+++ b/ldms/configure.ac
@@ -143,6 +143,8 @@ OPTION_DEFAULT_DISABLE([ibm_occ], [ENABLE_IBM_OCC_SAMPLER])
 OPTION_DEFAULT_DISABLE([appinfo], [ENABLE_APPINFO])
 OPTION_DEFAULT_DISABLE([test_sampler], [ENABLE_TEST_SAMPLER])

+OPTION_DEFAULT_DISABLE([thermal], [ENABLE_THERMAL])
+
 dnl test_sampler will also build with --enable-ldms-test
 AM_CONDITIONAL([ENABLE_TEST_SAMPLER_LDMS_TEST], [test "x$ENABLE_LDMS_TEST_FALSE" = "
 OPTION_DEFAULT_DISABLE([grptest], [ENABLE_GRPTEST])
@@ -517,6 +519,7 @@ AC_CONFIG_FILES([Makefile src/Makefile src/core/Makefile
                 src/ldmsd-samplers/shm/shm_util/Makefile
                 src/ldmsd-samplers/shm/mpi_profiler/Makefile
                 src/ldmsd-samplers/filesingle/Makefile
+                src/ldmsd-samplers/thermal/Makefile
                 src/ldmsd-stores/Makefile
                 src/ldmsd-stores/store_sos/Makefile
                 src/ldmsd-stores/store_csv/Makefile

--- a/ldms/src/ldmsd-samplers/Makefile.am
+++ b/ldms/src/ldmsd-samplers/Makefile.am
@@ -129,3 +129,7 @@ endif
 if ENABLE_FILESINGLE
 SUBDIRS += filesingle
 endif
+
+if ENABLE_THERMAL
+SUBDIRS += thermal
+endif

```

Building
--------

The following steps will build ldms with `thermal` sampler.

```sh
$ cd ldms/
$ ./autogen.sh
$ mkdir build
$ cd build/
$ ../configure --prefix=/opt/ldms --with-ovis-lib=/opt/ovis-lib \
               --enable-thermal
$ make && make install
```

Running the Sampler
-------------------

The following is the config file for the `ldmsd` sampler with one `thermal`
sampler monitoring `zone2`:

```conf
# file: samp.conf

load name=zone2 plugin=thermal
config name=zone2 zone=2

smplr_add name=smplr instance=zone2
smplr_start name=smplr interval=1000000 offset=0
```

Then, we can test-run the sampler daemon in foreground mode as follows:

```sh
$ ldmsd -F -c samp.conf -x sock:10001 -v INFO
```


Verifying
---------

`ldms_ls` to the daemon to verify the sampler result:

```sh
$ ldms_ls -x sock -p 10001 -l
bla:10001/zone2: consistent, last update: Thu May 16 15:47:20 2019 -0500 [82us]
D u64        component_id                               0
D u64        job_id                                     0
D u64        app_id                                     0
D u64        temp                                       39000 mC

```

FILES
=====

This is a list of files in the source tree related to this tutorial.

<dl>

<dt>ldms/templates/sampler_template.c</dt>
<dd>The template to implement a sampler plugin.</dd>

<dt>ldms/configure.ac</dt>
<dd>
The <code>autoconf</code> file of ldms project that needs to be modified to
include new option and Makefile for the new plugin.
</dd>

<dt>ldms/src/ldmsd-samplers/</dt>
<dd>The directory hosting ldmsd sampler plugin subdirectories.</dd>

<dt>ldms/src/ldmsd-samplers/Makefile.am</dt>
<dd>
The automake file containing conditional <code>SUBDIRS</code> list to build
sampler plugins.
</dd>

<dt>ldms/src/ldmsd-samplers/common.am</dt>
<dd>
The automake file to be <code>include</code>, containing useful variables for
building ldmsd sampler plugins.
</dd>

</dl>


SEE ALSO
========

[ldmsd-sampler][ldmsd-sampler](7)
[ldmsd-store-dev][ldmsd-store-dev](7)


[ldmsd-sampler]: ldmsd-sampler.md
[ldmsd-store-dev]: ldmsd-store-dev.md
[json_util.h]: ../../../lib/src/json/json_util.h
