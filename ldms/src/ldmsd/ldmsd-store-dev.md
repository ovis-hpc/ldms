[#]: # (man)

NAME
====

ldmsd-store-dev - LDMSD Store Development Guide


SYNOPSIS
========

`#include "ldmsd_plugin.h"`<br>
`#include "ldmsd_store.h"`


DESCRIPTION
===========

An LDMSD storage plugin is an LDMSD plugin implementing the store interface
(`ldmsd_store_type_s`). A storage plugin instance is an entity created from the
storage plugin to handle the storage of updated LDMS sets of the same schema
(see [ldmsd-aggregator][agg](7) for an example usage). Each storage plugin
instance has a copy of `ldmsd_store_type_s` structure associated with it, which
can be conveniently accessed by the macro `LDMSD_STORE(inst)`. The association
of the store plugin instance and a copy of `ldmsd_store_type_s` structure can be
depicted as follows:

```txt

    .-----------------------.
    | inst extension        |
    |-----------------------|        .-----------------------.
    | .-------------------. |        | ldmsd_store_type_s    |
    | |ldmsd_plugin_inst_s| |        |-----------------------|
    | |-------------------| |        | .-------------------. |
    | | base  ------------+-+------->| |ldmsd_plugin_type_s| |
    | |                   | |        | |-------------------| |
    | | INSTANCE APIs     | |<-------+-+-inst              | |
    | '-------------------' |        | |                   | |
    |                       |        | | PLUGIN APIs       | |
    | EXTENSION DATA        |        | '-------------------' |
    '-----------------------'        |                       |
                                     | STORE APIs            |
                                     '-----------------------'

```

`ldmsd` uses the STORE APIs in `ldmsd_plugin_type_s` to communicate with the the
plugin instance about storage business (e.g. open, close, store). The PLUGIN
APIs in `ldmsd_plugin_type_s` and INSTANCE APIs in `ldmsd_plugin_ints_s` are for
instance management and configuration. The storage plugin must extend the
`ldmsd_plugin_inst_s` structure to handle its plugin instance. The
`ldmsd_store_type_s` structure is an extended strucutre of `ldmsd_plugin_type_s`
that handlesa storage and common plugin APIs. It is created and managed by
`ldmsd`. The storage plugin does not have an authority over this structure other
than override some APIs.

The interaction between `ldmsd` and the storage plugin / storage plugin instance
can be summarized as the following:

<dl>

<dt>instance creation</dt>
<dd>
`ldmsd` create a plugin instance upon a `load` user request (see [Creating an
Instance](#creating-an-instance)) by calling `new()` function in the plugin
library. The plugin then allocate the memory to hold the extended instance
structure and setup INSTANCE APIs. At this point, the plugin instance does not
have `ldmsd_store_type_s` associated with it yet. `ldmsd` will inspect the
`ldmsd_plugin_inst_s.type` (must be `LDMSD_STORE_TYPENAME` in this case) and
create a copy of `ldmsd_store_type_s` after `new()` has returned.
</dd>

<dt>store instance initialization</dt>
<dd>
After `new()` has returned, and a copy of `ldmsd_store_type_s` has been created
and linked with the instance by `ldmsd`, `ldmsd_plugin_inst_s.init()` is called
to let the plugin further setup the STORE APIs. Additional plugin resources
could be initialized during the init call as well.
</dd>

<dt>configuration</dt>
<dd>
`ldmsd` configure the plugin instance when the user gave `config` command. The
configuration attributes from the command are passed along to the plugin config
function (see [Config](#config) subsection). After a successful config, the
plugin became configured and is ready to be used by `strgp` (storage policy, see
[ldmsd-aggregator][agg](7)).
</dd>

<dt>store open</dt>
<dd>
`strgp` will call `ldmsd_store_type_s.open()` to perform an *open* operation on
the storage. This is analogous to opening a file before writing data into it.
</dd>

<dt>store</dt>
<dd>
After a successful open, when a `strgp`-matching set get updated,
`ldmsd_store_type_s.store()` is called to notify the storage instance to store
the updated set. This is analogous to file write.
</dd>

<dt>store close</dt>
<dd>
After a successful open, when the `strgp` is stopped by the user the API
`ldmsd_store_type_s.close()` is called to notify the plugin instance to clean up
the resources from open. This is analogous to file close.
</dd>

<dt>instance deletion</dt>
<dd>
After `strgp` stopped and deleted, the store instance could also be deleted by
the user. In such case, `ldmsd_plugin_inst_s.delete()` is called to let the
plugin instance clean up its resources.
</dd>

</dl>


We will use a `plaintext` storage plugin as an example to guide through how to
implement a storage plugin for LDMS in the following subsections.

[Preparation](#preparation)<br>
[Conventions](#conventions)<br>
[Extending Plugin Instance](#extending-plugin-instance)<br>
[Creating an Instance](#creating-an-instance)<br>
[Help and Description](#help-and-description)<br>
[Initialization](#initialization)<br>
[Store APIs](#store-apis)<br>
[Config](#config)<br>
[Createing a Schema and a Set](#creating-a-schema-and-a-set)<br>
[Sample](#sample)<br>


Preparation
------------
<div id="preparation"></div>

This step is for developing a storage plugin in ovis development tree
(recommended). If you are developing a plugin outside of ovis development tree,
you can skip this step, and have to make sure that the plugin links with
`libstore.so`.

Please follow these steps to prepare a workspace for developing `plaintext`
store:

**1)** Create a sub directory `plaintext` in `ldms/src/ldmsd-stores`.


**2)** Create a `Makefile.am` and a (or several) C source file for the plugin
(`plaintext.c` for this example). The `Makefile.am` should look like the
following:

```make
# file: ldms/src/ldmsd-stores/plaintext/Makefile.am
include ../common.am

pkglib_LTLIBRARIES = libplaintext.la

libplaintext_la_SOURCES = plaintext.c
libplaintext_la_CFLAGS  = $(STORE_CFLAGS)
libplaintext_la_LIBADD  = $(STORE_LIBADD)
libplaintext_la_LDFLAGS = $(STORE_LDFLAGS)
```

The `../common.am` defines `STORE_CFLAGS`, `STORE_LIBADD` and `STORE_LDFLAGS`
variables containing necessary artifacts to build the storage plugin.


**3)** Edit `ldms/configure.ac`, add an option for the sampler, and add the
`Makefile` in "2)" in the `AC_CONFIG_FILES` list as follows:

```autoconf
dnl Options for sampler
...

OPTION_DEFAULT_DISABLE([plaintext], [ENABLE_PLAINTEXT])

...

AC_CONFIG_FILES([Makefile src/Makefile src/core/Makefile
                 ...
                 src/plaintext/Makefile
                 ...
])
...

```

Please note that the paths in `ldms/configure.ac` is relative to `ldms/`
directory (where the autoconf script resides).

`OPTION_DEFAULT_DISABLE` macro is used so that the plugin won't be build unless
the user specifies `--enable-plaintext` configure option. In the case that the
plugin does not depend on extra libraries other than those required by `ldmsd`,
we may use `OPTION_DEFAULT_ENABLE` macro to have it build by default (unless the
user specify `--disable-thermal` configure option).

In the case of extra dependencies, please also add a library / header checking
logic in the `ldms/configure.ac`.


**4)** Edit `ldms/src/ldmsd-stores/Makefile.am` and add `plaintext` as a
conditional build subdirectory:

```make
# file: ldms/src/ldmsd-stores/Makefile.am
...
if ENABLE_PLAINTEXT
SUBDIRS += plaintext
endif
...
```

**5)** (Optional) For convenience, the template
`ldms/templates/store_template.c` can be copied to
`ldms/src/ldmsd-stores/plaintext/plaintext.c` as a starting point of
development.


Conventions
-----------
<div id="conventions"></div>

**Functions** of the plugin are `static`, except for `new()` function. The name
of the plugin functions are preferably prefixed with plugin name for
readability (e.g. `static int plaintext_init(ldmsd_plugin_inst_t pi)`).

The name of the **extended instance** structure is preferably `struct
<PLUGIN_NAME>_inst_s` (e.g. `struct plaintext_inst_s`).


Extending Plugin Instance
-------------------------
<div id="extending-plugin-instance"></div>

To extend a sample plugin instance, simply define a new structure with `struct
ldmsd_plugin_inst_s` as the first member. It is conventional to name the element
`base`. The following is an example for `plaintext`:

```c
/* file: ldms/src/ldmsd-samplers/plaintext/plaintext.c */
...

typedef struct plaintext_inst_s *plaintext_inst_t;
struct plaintext_inst_s {
    struct ldmsd_plugin_inst_s base;
    char *path;
    FILE *f; /* file pointer to the output file */
};
...
```


Creating an Instance
--------------------
<div id="creating-an-instance"></div>

The storage plugin implementation must implement `ldmsd_plugin_inst_t new()`
function. This function is called by `ldmsd` when it needs to create an instance
of the plugin (when `load` ldmsd config command is processed).

`new()` function must allocate memory for the newly created instance and
setup the instance APIs. The following is an example for `plaintext` sampler:

```c
/* file: ldms/src/ldmsd-stores/plaintext/plaintext.c */
...

/* global structure for setting up instance APIs and some default values */
struct plaintext_inst_s __inst = {
	.base = {
		/* must get version from this macro */
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		/* type must be LDMSD_STORE_TYPENAME */
		.type_name   = LDMSD_STORE_TYPENAME,
		.plugin_name = "plaintext",

                /* Common Plugin APIs, these functions are to be implemented */
		.desc   = plaintext_desc,
		.help   = plaintext_help,
		.init   = plaintext_init,
		.del    = plaintext_del,
		.config = plaintext_config,
	},
};

ldmsd_plugin_inst_t new()
{
	plaintext_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst; /* copy __inst value above */
	return &inst->base;
}
...
```

At this point, the corresponding `ldmsd_store_type_s` has not been created and
paired with this instance yet. `ldmsd` will handle the creation and pairing
after `new()` returned.


Help and Description
--------------------
<div id="help-and-description"></div>

The `ldmsd_plugin_inst_s.desc()` interface is for a short description of the
plugin, and `ldmsd_plugin_inst_s.help()` interface is for a long help text
describing how to use/configure the plugin instance. The `plaintext` description
and help implemntation is as the following:

```c
/* file: ldms/src/ldmsd-stores/plaintext/plaintext.c */
...
static const char *plaintext_desc(ldmsd_plugin_inst_t pi)
{
	return "plaintext - write data to plain text file";
}

static const char *plaintext_help(ldmsd_plugin_inst_t pi)
{
	return	"plaintext config synopsis:\n"
		"    config name=INST [COMMON OPTIONS] path=PATH_TO_FILE\n"
		"\n"
		"Descriptions:\n"
		"    plaintext writes plain text data to the file specified\n"
		"    by `path` parameter. If the file does not exist,\n"
		"    it will be created. If the file existed, it will be\n"
		"    appended to.\n";
}
...
```


Initialization
--------------
<div id="initialization"></div>

After the plugin instance is created by `new()`, and `ldmsd` successfully link
it to a copy of `ldmsd_store_type_s`, `ldmsd_plugin_inst_s.init()` is called to
initialize the plugin instance and setup (or overries) STORE APIs in the linked
`ldmsd_store_type_s`. The plugin is expected to initialize its resources during
this call. Some plugin that relies on static files that requires no
configuration call may decide to open those files during init.

The storage plugin must implement `ldmsd_store_type_s.open()`,
`ldmsd_store_type_s.close()`, `ldmsd_store_type_s.flush()`, and
`ldmsd_store_type_s.store()` store operations. These interface must be setup
during `ldmsd_plugin_inst_s.init()`. The following is an example of init
function of `plaintext` sampler:

```c
static int plaintext_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_store_type_t store = LDMSD_STORE(pi);

	store->open  = plaintext_open;
	store->close = plaintext_close;
	store->store = plaintext_store;
	store->flush = plaintext_flush;

	return 0;
}
```

Config
------
<div id="config"></div>

After `new()` and `ldmsd_plugin_inst_s.init()` (corresponding to the user's
`load` command), `ldmsd_plugin_inst_s.config()` is called when the user gave
`config` command. For example:

```conf
load name=x plugin=plaintext
config name=x path=/tmp/plain
```

The configuration attributes are passed along to `ldmsd_plugin_inst_s.config()`
API by `ldmsd` via `json` parameter (see [json_util.h][json_util.h]). If
`ldmsd_plugin_inst_s.config()` is not implemented (`NULL`), the base
`ldmsd_plugin_type_s.config()` is called instead.
`ldmsd_plugin_inst_s.config()` implementation should also call
`ldmsd_plugin_type_s.config()` to process the common configuration attributes.
On configuration error, the plugin instance can `snprintf()` to `ebuf` to
explain the error and return the error number.

The following is an implementation of `config()` for `plaintext`.

```c
...
static int
plaintext_config(ldmsd_plugin_inst_t pi, json_entity_t json,
                 char *ebuf, int ebufsz)
{
	ldmsd_store_type_t store = LDMSD_STORE(pi);
	plaintext_inst_t inst = (void*)pi;
	int rc;
	const char *val;

	rc = store->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	val = json_attr_find_str(json, "path");
	if (!val) {
		snprintf(ebuf, ebufsz, "missing `path` attribute.\n");
		return EINVAL;
	}
	inst->path = strdup(val);
	if (!inst->path) {
		snprintf(ebuf, ebufsz, "Out of memory.\n");
		return errno;
	}
	return 0;
}
...
```


Store APIs
----------
<div id="store-apis"></div>

`ldmsd` handle data storage business with a storage plugin instance through the
storage APIs as the following:

<dl>

<dt><tt>ldmsd_store_type_s.open()</tt></dt>
<dd>
After a successful config, <code>ldmsd</code> assures to call
<code>ldmsd_store_type_s.open()</code> before storing the data to let the
storage instance prepare the underlying resources. This is analogous to
<code>open()</code> of a file.
<br><br>
<code>ldmsd_store_type_s.open()</code> also receive <code>strgp</code> (storage
policy) which contain schema information needed for storage initialization.
<code>strgp.schema</code> is a string containing schema name of the sets being
fed to the storage instance. <code>strgp.metric_count</code> is the number of
metrics to be stored. <code>strgp.metric_list</code> is a list of
<code>ldmsd_strgp_metric</code> containing information for each metric to be
stored. The plugin instance can iterate through the list using
<code>ldmsd_strgp_metric_first()</code> and
<code>ldmsd_strgp_metric_next()</code> functions. For each
<code>ldmsd_strgp_metric</code>, the name, type, and metric index are
<code>.name</code>, <code>.type</code>, and <code>.idx</code> respectively.
</dd>

<dt><tt>ldmsd_store_type_s.store()</tt></dt>
<dd>
<code>ldmsd</code> calls <code>ldmsd_store_type_s.store()</code>, supplying
<code>set</code> and <code>strgp</code> to the plugin instance, to notify the
storage plugin instance about the set that has just been updated. The plugin
instance then access <code>set</code> data using LDMS API (e.g.
<code>ldms_metric_get_u64()</code> -- see more in <code>ldms.h</code>) and store
the data accordingly. <code>strgp.metric_list</code> tells the storage plugin
instance which data to store. The plugin instance can iterate through the list
using <code>ldmsd_strgp_metric_first()</code> and
<code>ldmsd_strgp_metric_next()</code> functions. For each
<code>ldmsd_strgp_metric</code>, the name, type, and metric index are
<code>.name</code>, <code>.type</code>, and <code>.idx</code> respectively.
</dd>

<dt><tt>ldmsd_store_type_s.flush()</tt></dt>
<dd>
<code>ldmsd</code> occastionally calls <code>.flush()</code> to notify the store
to flush out cached data. If the store does not support flush operation, it
still need to implement a flush function that does nothing and returns 0.
</dd>

<dt><tt>ldmsd_store_type_s.close()</tt></dt>
<dd>
When the user issued <code>strgp_stop</code> command to <code>ldmsd</code>, the
<code>ldmsd_store_type_s.close()</code> is called to close down the (opened)
store. The storage plugin instance shall close the underlying store and clean up
resources allocated in <code>ldmsd_store_type_s.open()</code>.
</dd>

</dl>

The following is the `plaintext` implementation of store APIs:

```c
...
static int
plaintext_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
        plaintext_inst_t inst = (void*)pi;
        inst->f = fopen(inst->path, "a");
        if (!inst->f)
                return errno;
        return 0;
}

static int
plaintext_close(ldmsd_plugin_inst_t pi)
{
        plaintext_inst_t inst = (void*)pi;
        fclose(inst->f);
        inst->f = NULL;
}

static int
plaintext_flush(ldmsd_plugin_inst_t pi)
{
        plaintext_inst_t inst = (void*)pi;
        fflush(inst->f);
}

static int
plaintext_store(ldmsd_plugin_inst_t pi, ldms_set_t set, ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	plaintext_inst_t inst = (void*)pi;
	ldmsd_strgp_metric_t m;
	const char *setname = ldms_set_instance_name_get(set);

	for (m = ldmsd_strgp_metric_first(strgp);
			m;
			m = ldmsd_strgp_metric_next(m)) {
		fprintf(inst->f, "%s:%s:%s:", setname, m->name,
					      ldms_metric_type_to_str(m->type));
		fprint_metric_val(inst->f, set, m->idx);
                /*
                 * fprint_metric_val() implementation is in the EXAMPLE
                 * section below
                 */
		fprintf(inst->f, "\n");
	}

	return 0;
}
...
```


Deletion of Sampler Plugin Instance
-----------------------------------
<div id="delete"></div>

A plugin instance can be deleted upon user request (`term` command, after the
associated `strgp` has been stopped and removed). The
`ldmsd_plugin_inst_s.del()` is called to let the plugin implementation clear
instnace-specific resources. The plugin implementation must **NOT** free the
instance itself. `ldmsd` will free the instance afterward.

The following is `del()` implementation of `plaintext` storage plugin:

```c
/* part of `plaintext.c` */
static void
plaintext_del(ldmsd_plugin_inst_t pi)
{
	plaintext_inst_t inst = (void*)pi;
	if (inst->path)
		free(inst->path);
	if (inst->f)
		fclose(inst->f);
}
```


EXAMPLE
=======

This section contain the complete code that we have been building through the
guide.


Source files
------------

This is the full content of `plaintext.c`:

```c

/* file: ldms/src/ldmsd-stores/plaintext/plaintext.c */

#include "ldmsd.h"
#include "ldmsd_store.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct plaintext_inst_s *plaintext_inst_t;
struct plaintext_inst_s {
	struct ldmsd_plugin_inst_s base;
	char *path;
	FILE *f;
};


/* ============== Store Plugin APIs ================= */

static int
plaintext_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
	/* Perform `open` operation */
	plaintext_inst_t inst = (void*)pi;
	inst->f = fopen(inst->path, "a");
	if (!inst->f)
		return errno;
	return 0;
}

static int
plaintext_close(ldmsd_plugin_inst_t pi)
{
	/* Perform `close` operation */
	plaintext_inst_t inst = (void*)pi;
	fclose(inst->f);
	inst->f = NULL;
	return 0;
}

static int
plaintext_flush(ldmsd_plugin_inst_t pi)
{
	/* Perform `flush` operation */
	plaintext_inst_t inst = (void*)pi;
	fflush(inst->f);
	return 0;
}

static void
fprint_metric_array_val(FILE *f, ldms_set_t set, int i)
{
	enum ldms_value_type type = ldms_metric_type_get(set, i);
	int j, n;
	n = ldms_metric_array_get_len(set, i);
	for (j = 0; j < n; j++) {
		if (j)
			fprintf(f, ",");
		switch (type) {
		case LDMS_V_U8_ARRAY:
			fprintf(f, "%hhu", ldms_metric_array_get_u8(set, i, j));
			break;
		case LDMS_V_S8_ARRAY:
			fprintf(f, "%hhd", ldms_metric_array_get_s8(set, i, j));
			break;
		case LDMS_V_U16_ARRAY:
			fprintf(f, "%hu", ldms_metric_array_get_u16(set, i, j));
			break;
		case LDMS_V_S16_ARRAY:
			fprintf(f, "%hd", ldms_metric_array_get_s16(set, i, j));
			break;
		case LDMS_V_U32_ARRAY:
			fprintf(f, "%u", ldms_metric_array_get_u32(set, i, j));
			break;
		case LDMS_V_S32_ARRAY:
			fprintf(f, "%d", ldms_metric_array_get_s32(set, i, j));
			break;
		case LDMS_V_U64_ARRAY:
			fprintf(f, "%lu", ldms_metric_array_get_u64(set, i, j));
			break;
		case LDMS_V_S64_ARRAY:
			fprintf(f, "%ld", ldms_metric_array_get_s64(set, i, j));
			break;
		case LDMS_V_F32_ARRAY:
			fprintf(f, "%f", ldms_metric_array_get_float(set, i, j));
			break;
		case LDMS_V_D64_ARRAY:
			fprintf(f, "%lf", ldms_metric_array_get_double(set, i, j));
			break;
		}
	}
}

static void
fprint_metric_val(FILE *f, ldms_set_t set, int i)
{
	enum ldms_value_type type = ldms_metric_type_get(set, i);
	if (type == LDMS_V_CHAR_ARRAY) {
		fprintf(f, "%s", ldms_metric_array_get_str(set, i));
		return;
	}
	if (ldms_type_is_array(type)) {
		fprint_metric_val(f, set, i);
		return ;
	}
	switch (type) {
	case LDMS_V_CHAR:
		fprintf(f, "%c", ldms_metric_get_char(set, i));
		break;
	case LDMS_V_U8:
		fprintf(f, "%hhu", ldms_metric_get_u8(set, i));
		break;
	case LDMS_V_S8:
		fprintf(f, "%hhd", ldms_metric_get_s8(set, i));
		break;
	case LDMS_V_U16:
		fprintf(f, "%hu", ldms_metric_get_u16(set, i));
		break;
	case LDMS_V_S16:
		fprintf(f, "%hd", ldms_metric_get_s16(set, i));
		break;
	case LDMS_V_U32:
		fprintf(f, "%u", ldms_metric_get_u32(set, i));
		break;
	case LDMS_V_S32:
		fprintf(f, "%d", ldms_metric_get_s32(set, i));
		break;
	case LDMS_V_U64:
		fprintf(f, "%lu", ldms_metric_get_u64(set, i));
		break;
	case LDMS_V_S64:
		fprintf(f, "%ld", ldms_metric_get_s64(set, i));
		break;
	case LDMS_V_F32:
		fprintf(f, "%f", ldms_metric_get_float(set, i));
		break;
	case LDMS_V_D64:
		fprintf(f, "%lf", ldms_metric_get_double(set, i));
		break;

	}
}

static int
plaintext_store(ldmsd_plugin_inst_t pi, ldms_set_t set, ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	plaintext_inst_t inst = (void*)pi;
	ldmsd_strgp_metric_t m;
	const char *setname = ldms_set_instance_name_get(set);

	for (m = ldmsd_strgp_metric_first(strgp);
			m;
			m = ldmsd_strgp_metric_next(m)) {
		fprintf(inst->f, "%s:%s:%s:", setname, m->name,
					      ldms_metric_type_to_str(m->type));
		fprint_metric_val(inst->f, set, m->idx);
		fprintf(inst->f, "\n");
	}

	return 0;
}

/* ============== Common Plugin APIs ================= */

static const char *
plaintext_desc(ldmsd_plugin_inst_t pi)
{
	return "plaintext - write data to plain text file";
}

static const char *
plaintext_help(ldmsd_plugin_inst_t pi)
{
	return	"plaintext config synopsis:\n"
		"    config name=INST [COMMON OPTIONS] path=PATH_TO_FILE\n"
		"\n"
		"Descriptions:\n"
		"    plaintext writes plain text data to the file specified\n"
		"    by `path` parameter. If the file does not exist,\n"
		"    it will be created. If the file existed, it will be\n"
		"    appended to.\n";
}

static int
plaintext_config(ldmsd_plugin_inst_t pi, json_entity_t json,
		 char *ebuf, int ebufsz)
{
	ldmsd_store_type_t store = LDMSD_STORE(pi);
	plaintext_inst_t inst = (void*)pi;
	int rc;
	const char *val;

	rc = store->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	val = json_attr_find_str(json, "path");
	if (!val) {
		snprintf(ebuf, ebufsz, "missing `path` attribute.\n");
		return EINVAL;
	}
	inst->path = strdup(val);
	if (!inst->path) {
		snprintf(ebuf, ebufsz, "Out of memory.\n");
		return errno;
	}
	return 0;
}

static void
plaintext_del(ldmsd_plugin_inst_t pi)
{
	plaintext_inst_t inst = (void*)pi;
	if (inst->path)
		free(inst->path);
	if (inst->f)
		fclose(inst->f);
}

static int
plaintext_init(ldmsd_plugin_inst_t pi)
{
	plaintext_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;

	/* override store operations */
	store->open = plaintext_open;
	store->close = plaintext_close;
	store->flush = plaintext_flush;
	store->store = plaintext_store;

	return 0;
}

static struct plaintext_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = LDMSD_STORE_TYPENAME,
		.plugin_name = "plaintext",

                /* Common Plugin APIs */
		.desc   = plaintext_desc,
		.help   = plaintext_help,
		.init   = plaintext_init,
		.del    = plaintext_del,
		.config = plaintext_config,

	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t
new()
{
	plaintext_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
/* -----------------  EOF --------------------- */
```


This is the Makefile for building `plaintext` store:
```make
# file: ldms/src/ldmsd-stores/plaintext/Makefile.am

include ../common.am

libplaintext_la_SOURCES = plaintext.c
libplaintext_la_CFLAGS = $(STORE_CFLAGS)
libplaintext_la_LIBADD = $(STORE_LIBADD)
libplaintext_la_LDFLAGS = $(STORE_LDFLAGS)
pkglib_LTLIBRARIES = libplaintext.la
```

Configure and Makefile Modification
-----------------------------------

The following diff shows the modification to configure and Makefile relating to
the build of `plaintext` store:

```diff
--- a/ldms/configure.ac
+++ b/ldms/configure.ac
@@ -68,6 +68,7 @@ OPTION_DEFAULT_ENABLE([flatfile], [ENABLE_FLATFILE])
 OPTION_DEFAULT_ENABLE([csv], [ENABLE_CSV])
 OPTION_DEFAULT_DISABLE([rabbitkw], [ENABLE_RABBITKW])
 OPTION_DEFAULT_DISABLE([rabbitv3], [ENABLE_RABBITV3])
+OPTION_DEFAULT_DISABLE([plaintext], [ENABLE_PLAINTEXT])

 dnl AMQP
 OPTION_DEFAULT_DISABLE([amqp], [ENABLE_AMQP])
@@ -524,6 +525,7 @@ AC_CONFIG_FILES([Makefile src/Makefile src/core/Makefile
                 src/ldmsd-stores/store_sos/Makefile
                 src/ldmsd-stores/store_csv/Makefile
                 src/ldmsd-stores/store_amqp/Makefile
+                src/ldmsd-stores/plaintext/Makefile
                 scripts/Makefile
                 src/test/Makefile
                 etc/Makefile

--- a/ldms/src/ldmsd-stores/Makefile.am
+++ b/ldms/src/ldmsd-stores/Makefile.am
@@ -15,3 +15,7 @@ endif
 if ENABLE_AMQP
 SUBDIRS += store_amqp
 endif
+
+if ENABLE_PLAINTEXT
+SUBDIRS += plaintext
+endif
```


Building
--------

The following steps will build ldms with `plaintext` store.

```sh
$ cd ldms/
$ ./autogen.sh
$ mkdir build
$ cd build/
$ ../configure --prefix=/opt/ldms --with-ovis-lib=/opt/ovis-lib \
               --enable-plaintext
$ make && make install
```


Running ldmsds to test the store
--------------------------------

We need a sampler `ldmsd` (see [ldmsd-sampler][samp](7)) as a data source to
feed to an aggregator `ldmsd` (see [ldmsd-aggregator][agg](7)) that use
`plaintext` to store data. We will setup a sampler `ldmsd` on localhost:10001
with `meminfo` plugin, and an aggregator `ldmsd` on localhost:9001 with
`plaintext` store. Running them in foreground mode `-F` also helps in debugging.

```sh
# on one terminal
$ ldmsd -F -x sock:10001 -c samp.conf

# on another terminal
$ ldmsd -F -x sock:9001 -c agg.conf
```


The following is the content of `samp.conf`:

```
# samp.conf
load name=mem plugin=meminfo
config name=mem

smplr_add name=smplr instance=mem component_id=20
smplr_start name=smplr interval=1000000 offset=0
```


And, the following is the content of `agg.conf`:

```
# agg.conf
#### prdcr ####
prdcr_add name=prdcr host=localhost port=10001 xprt=sock \
          interval=1000000 type=active
prdcr_start name=prdcr interval=1000000

#### store ####
load name=pt plugin=plaintext
config name=pt path=meminfo.txt buffer=0

strgp_add name=sp container=pt schema=meminfo
strgp_prdcr_add name=sp regex=.*
strgp_start name=sp

#### updtr ####
updtr_add name=updtr interval=1000000 offset=500000
updtr_prdcr_add name=updtr regex=.*
updtr_start name=updtr

```


Verifying
---------

Tailing the output file `meminfo.txt` to see the data written out:

```sh
$ tail -f meminfo.txt
```

The output might look paused sometimes due to file buffering.


SEE ALSO
========

[ldmsd-sampler-dev][samp-dev](7),
[ldmsd-aggregator][agg](7)
[ldmsd-sampler][samp](7)


[agg]: ldms/src/ldmsd/ldmsd-aggregator.md
[samp]: ldms/src/ldmsd/ldmsd-sampler.md
[samp-dev]: ldms/src/ldmsd/ldmsd-sampler-dev.md
[json_util.h]: lib/src/json/json_util.h
