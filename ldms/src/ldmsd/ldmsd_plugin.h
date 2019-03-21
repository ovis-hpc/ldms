/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __LDMSD_PLUGIN_H__
#define __LDMSD_PLUGIN_H__

#include "coll/rbt.h"

#include "ldms.h"
#include "ldmsd.h"
#include "json/json_util.h"

/**
 * \defgroup ldmsd_plugin LDMSD Plugin
 * \{
 */

#define LDMSD_PLUGIN_VERSION 0x01000000

#define LDMSD_PLUGIN_VERSION_INITIALIZER {.version = LDMSD_PLUGIN_VERSION}
#define LDMSD_PLUGIN_VERSION_INIT(v) do { \
		(v)->version = LDMSD_PLUGIN_VERSION; \
	} while(0)

#define LDMSD_PLUGIN_NAME_MAX 64

/**
 * LDMSD Plugin Version (major.minor.patch) structure.
 */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
/* ver.major is the most-significant byte */
struct ldmsd_plugin_version_s {
	union {
		struct {
			uint8_t major; /**< Major number */
			uint8_t minor; /**< Minor number */
			uint8_t patch; /**< Patch number */
			uint8_t blank; /**< Blank (no-data) */
		};
		uint32_t version;
	};
};
#else
/* ver.major is the most-significant byte */
struct ldmsd_plugin_version_s {
	union {
		struct {
			uint8_t blank; /**< Blank (no-data) */
			uint8_t patch; /**< Patch number */
			uint8_t minor; /**< Minor number */
			uint8_t major; /**< Major number */
		};
		uint32_t version;
	};
};
#endif

typedef void *(*ldmsd_plugin_new_fn_t)();

typedef struct ldmsd_plugin_type_s *ldmsd_plugin_type_t;
typedef struct ldmsd_plugin_inst_s *ldmsd_plugin_inst_t;
typedef struct ldmsd_plugin_qresult_s *ldmsd_plugin_qresult_t;

/**
 * LDMSD Plugin Type structure.
 */
struct ldmsd_plugin_type_s {
	/** Version of the plugin. */
	struct ldmsd_plugin_version_s version;

	/** Name of the plugin type. */
	const char *type_name;

	/** Pointer to instance object. */
	ldmsd_plugin_inst_t inst;

	/**
	 * Short (1-line) description of the plugin.
	 *
	 * \c i->desc(), if not NULL, overloads this function, and the
	 * application can invoke the base implementation by \c i->base.desc(i).
	 *
	 * \param i The pointer to the plugin instance.
	 *
	 * \retval str A 1-line constant string for a short description of the
	 *             plugin.
	 */
	const char *(*desc)(ldmsd_plugin_inst_t i);

	/**
	 * Help text for the plugin.
	 *
	 * \c i->help(), if not NULL, overloads this function, and the
	 * application can invoke the base implementation by
	 * \c i->base->help(i).
	 *
	 * \param i The pointer to the plugin instance.
	 *
	 * \retval str A constant string for plugin's help text.
	 */
	const char *(*help)(ldmsd_plugin_inst_t i);

	/**
	 * Plugin base initialization routine.
	 *
	 * \c i->base->init() is always called before \c i->init().
	 *
	 * \param i The pointer to the plugin instance.
	 *
	 * \retval 0     If succeeded.
	 * \retval errno If failed.
	 */
	int (*init)(ldmsd_plugin_inst_t i);

	/**
	 * Plugin delete routine.
	 *
	 * \c i->base->del() is always called after \c i->del().
	 *
	 * \param i The pointer to the plugin instance.
	 */
	void (*del)(ldmsd_plugin_inst_t i);

	/**
	 * Plugin base configuration routine.
	 *
	 * \c i->config(), if not NULL, overloads this function, and the
	 * application can invoke the base implementation by
	 * \c i->base->config(i, d, ebuf, ebufsz).
	 *
	 * \param i   The instance pointer.
	 * \param json The JSON object containing the configuration infomation
	 * \param ebuf The buffer for returning error message from the plugin.
	 *             This may be \c NULL.
	 * \param ebufsz The size of the \c ebuf. The plugin must not
	 *               use more than \c ebufsz of the \c ebuf. If \c ebuf is
	 *               \c NULL, \c ebufsz will be 0.
	 *
	 * \retval 0     If succeeded.
	 * \retval errno If failed.
	 */
	int (*config)(ldmsd_plugin_inst_t i, json_entity_t json,
					     char *ebuf, int ebufsz);

	/**
	 * Plugin query interface.
	 *
	 * The default implementation is ::ldmsd_plugin_query(). The default
	 * implementation currently handles `status` and `config` queries.
	 *
	 * Sampler plugin overrides the default impementation with
	 * ::ldmsd_sampler_query(). Sampler default query subsequently calls
	 * ::ldmsd_plugin_query(), and append sampler-specific results for
	 * `status` query.
	 *
	 * Store plugin overrides the default impementation with
	 * ::ldmsd_store_query(). Store default query subsequently calls
	 * ::ldmsd_plugin_query(), and append store-specific results for
	 * `status` query.
	 *
	 * Plugin implementation can override this function and may subsequently
	 * call existing query functions as it sees fit.
	 *
	 * The returned JSON object MUST contain the following attributes.
	 * { "rc":     <return code>,
	 *   "errmsg": "Empty string if rc is 0",
	 *   "name":   "plugin instance name",
	 *   "plugin": "plugin name",
	 *   "type":   "plugin type",
	 *   ...
	 * }
	 *
	 * If the query string is "config", the returned JSON dict MUST include
	 *
	 *  { ...
	 *    "config": [JSON list of JSON dicts. Each dict represents a config line of the instance]
	 *  }
	 *
	 * If the query string is "status", the returned JSON dict MUST include
	 *
	 * { ...
	 *    "status": {JSON dict}
	 * }
	 *
	 * For other query strings, the returned JSON object MUST include the
	 * attribute of the query string name.
	 *
	 * \param i The plugin instance.
	 * \param q The query string.
	 *
	 * \retval qr The query result.
	 */
	ldmsd_plugin_qresult_t (*query)(ldmsd_plugin_inst_t i, const char *q);
};

/** Base structure of plugin instance. */
struct ldmsd_plugin_inst_s {
	/** The version of the instance implementation. */
	struct ldmsd_plugin_version_s version;

	/** The base type name of the instance (e.g. "sampler"). */
	const char *type_name;

	/** The name of the plugin (e.g. "meminfo"). */
	const char *plugin_name;

	/**
	 * Instance name (e.g. "foo").
	 *
	 * This field is managed by \c ldmsd. The plugin can read, but must not
	 * modify nor free it.
	 */
	char *inst_name;

	/** Path to the plugin library. */
	char *libpath;

	/** [private] Reb-black tree node. */
	struct rbn rbn;

	/** [private] Reference counter. */
	int ref_count;

	/** [private] JSON object that defines the plugin instance configuration
	 * {
	 *   "config": [],
	 *   "status": {},
	 *   ...
	 * }
	 */
	json_entity_t json;

	/** A pointer to the plugin type object. */
	ldmsd_plugin_type_t base;

	/** An overloading function of \c base->desc() */
	const char *(*desc)(ldmsd_plugin_inst_t i);

	/** An overloading function of \c base->help() */
	const char *(*help)(ldmsd_plugin_inst_t i);

	/**
	 * Instance initialization.
	 *
	 * This function, if not NULL, will be called after \c i->base.init() is
	 * called by \c ldmsd. The application should not call
	 * \c i->base.init() explicitly.
	 *
	 * \param i The instance pointer.
	 *
	 * \retval 0     If succeeded.
	 * \retval errno If failed.
	 */
	int (*init)(ldmsd_plugin_inst_t i);

	/**
	 * Instance deletion.
	 *
	 * This function, if not NULL, will be called before \c i->base.del() is
	 * called by \c ldmsd. The application should not call
	 * \c i->base.del() explicitly.
	 *
	 * \param i The instance pointer.
	 */
	void (*del)(ldmsd_plugin_inst_t i);

	/**
	 * Config overloading interface.
	 *
	 * If this function is not \c NULL, it will be called instead of
	 * ldmsd_plugin_type_s::config(). The plugin implementation can call
	 * \c i->base->config() to invoke the default common config routine of
	 * the plugin.
	 *
	 * \param i   The instance pointer.
	 * \param json The JSON object containing configuration information
	 * \param ebuf The buffer for returning error message from the plugin.
	 *             This may be \c NULL.
	 * \param ebufsz The size of the \c ebuf. The plugin must not
	 *               use more than \c ebufsz of the \c ebuf. If \c ebuf is
	 *               \c NULL, \c ebufsz will be 0.
	 *
	 * \retval 0     If succeed.
	 * \retval errno If failed.
	 */
	int (*config)(ldmsd_plugin_inst_t i, json_entity_t json,
					     char *ebuf, int ebufsz);
};

/**
 * Convenient macro casting `inst` to `ldmsd_plugin_inst_t`.
 */
#define LDMSD_INST(inst) ((ldmsd_plugin_inst_t)inst)

/**
 * Load a new plugin instance.
 *
 * This function creates the instance by calling \c new() function of the plugin
 * (identified by \c plugin_name), and then calls \c inst->base->init() and \c
 * init->init() sequentially. The name will be assigned to the loaded instance.
 * If the instance of the same name exists, the function returns \c NULL, and \c
 * errno is set to \c EEXIST. \c errstr, if not \c NULL, will also be populated
 * to describe the error.
 *
 * \note Intances created by this function must be destroy with
 *       ldmsd_plugin_inst_del().
 *
 * \param inst_name   The name to assign to the new instance.
 * \param plugin_name The plugin name (e.g. meminfo).
 * \param errstr The error string buffer for the function to print the error to.
 *               This can be \c NULL.
 * \param errlen The length of the error string buffer. If \c errstr is \c NULL,
 *               this parameter is ignored.
 *
 * \retval inst The pointer to the new instance.
 * \retval NULL If load failed. \c errno is set to describe the error.
 */
ldmsd_plugin_inst_t ldmsd_plugin_inst_load(const char *inst_name,
					   const char *plugin_name,
					   char *errstr,
					   int errlen);

/**
 * Find the loaded plugin instance.
 *
 * \param inst_name The name of the instance.
 *
 * \retval inst If the instance is found.
 * \retval NULL If the instance is not found, the \c errno is set to \c ENOENT.
 *              If there is other error, the \c errno will be set to describe
 *              the error.
 */
ldmsd_plugin_inst_t ldmsd_plugin_inst_find(const char *inst_name);

/**
 * Delete the plugin instance.
 *
 * \param inst The plugin instance to delete.
 */
void ldmsd_plugin_inst_del(ldmsd_plugin_inst_t inst);

/**
 * Plugin configuration.
 *
 * \param inst The plugin instance.
 * \param avl  The list of attribute=value pairs.
 * \param kwl  The list of keyword (positional) arguments.
 * \param ebuf The buffer for returning error message from the plugin.
 *             This may be \c NULL.
 * \param ebufsz The size of the \c ebuf. The plugin must not
 *               use more than \c ebufsz of the \c ebuf. If \c ebuf is
 *               \c NULL, \c ebufsz will be 0.
 *
 * \retval 0     If succeed.
 * \retval errno If failed.
 */
int ldmsd_plugin_inst_config(ldmsd_plugin_inst_t inst,
			     json_entity_t d,
			     char *ebuf, int ebufsz);

/**
 * Plugin instance help interface.
 */
const char *ldmsd_plugin_inst_help(ldmsd_plugin_inst_t inst);

/**
 * Plugin instance short description interface.
 */
const char *ldmsd_plugin_inst_desc(ldmsd_plugin_inst_t inst);

void ldmsd_plugin_inst_get(ldmsd_plugin_inst_t inst);
void ldmsd_plugin_inst_put(ldmsd_plugin_inst_t inst);

/**
 * [Private] Get the first plugin instance.
 *
 * This function is private and shall not be called directly by plugins.
 *
 * \retval inst The first plugin instance.
 */
ldmsd_plugin_inst_t __plugin_inst_first();

/**
 * [Private] Get the next plugin instance.
 *
 * This function is private and shall not be called directly by plugins.
 *
 * \retval inst The next plugin instance.
 */
ldmsd_plugin_inst_t __plugin_inst_next(ldmsd_plugin_inst_t inst);

/**
 * [Private] Take the plugin instance tree lock.
 *
 * This function is private and shall not be called directly by plugins.
 *
 * \retval 0 This function always return 0.
 */
int __inst_rbt_lock();

/**
 * [Private] Release the plugin instance tree lock.
 *
 * This function is private and shall not be called directly by plugins.
 *
 * \retval 0 This function always return 0.
 */
int __inst_rbt_unlock();

/**
 * Plugin instance iteration macro.
 */
#define LDMSD_PLUGIN_INST_FOREACH(i) \
	for (__inst_rbt_lock(), (i) = __plugin_inst_first(); \
			(i) || (__inst_rbt_unlock()); \
			(i) = __plugin_inst_next((i)))


typedef enum {
	LDMSD_PLUGIN_QRENT_STR,  /* a string */
	LDMSD_PLUGIN_QRENT_COLL, /* collection of query result entries */
} ldmsd_plugin_qrent_type_t;

TAILQ_HEAD(ldmsd_plugin_qrent_list_s, ldmsd_plugin_qrent_s);

typedef struct ldmsd_plugin_qrent_coll_s *ldmsd_plugin_qrent_coll_t;
typedef struct ldmsd_plugin_qrent_s *ldmsd_plugin_qrent_t;

/**
 * Query result entry.
 */
struct ldmsd_plugin_qrent_s {
	struct rbn rbn;

	ldmsd_plugin_qrent_type_t type;
	char *name;
	union {
		char *str;
		ldmsd_plugin_qrent_coll_t coll;
	};
	char _priv[]; /* This contain `name` and `str` */
};

struct ldmsd_plugin_qrent_coll_s {
	struct rbt rbt;
};
typedef struct ldmsd_plugin_qrent_coll_s *ldmsd_plugin_qrent_coll_t;

/**
 * Query result (list of query result entries).
 */
struct ldmsd_plugin_qresult_s {
	int rc;
	struct ldmsd_plugin_qrent_coll_s coll;
};

/**
 * Free the query result and resources associated with it.
 */
void ldmsd_plugin_qresult_free(ldmsd_plugin_qresult_t qr);

/**
 * Create and initialize a new collection.
 */
ldmsd_plugin_qrent_coll_t ldmsd_plugin_qrent_coll_new();

/**
 * Initialize the query result entry collection.
 *
 * This function is useful for initializing the collection that is not created
 * by ::ldmsd_plugin_qrent_coll_new().
 */
void ldmsd_plugin_qrent_coll_init(ldmsd_plugin_qrent_coll_t coll);

/**
 * Cleanup (delete & free) all entries in the collection.
 */
void ldmsd_plugin_qrent_coll_cleanup(ldmsd_plugin_qrent_coll_t coll);

/**
 * Add a query result into the collection.
 *
 * If \c type is ::LDMSD_PLUGIN_QRENT_STR, \c val is a `const char *`. \c val
 * will be copied and the caller still own \c val in this case.
 *
 * If \c type is ::LDMSD_PLUGIN_QRENT_COLL, \c val is
 * \c ldmsd_plugin_qrent_coll_t, and is owned by the returned object after the
 * call. The caller should not modify or free \c val after the call in this
 * case.
 *
 * \param type The type of the new query result entry.
 * \param name The entry name.
 * \param val  The value of the entry.
 *
 * \retval 0     If succeed.
 * \retval errno If failed.
 */
int ldmsd_plugin_qrent_add(ldmsd_plugin_qrent_coll_t coll,
			   const char *name,
			   ldmsd_plugin_qrent_type_t type,
			   void *val);

struct ldmsd_plugin_qrent_bulk_s {
	const char *key;
	ldmsd_plugin_qrent_type_t type;
	void *val;
};

struct ldmsd_plugin_qjson_attrs {
	char *name;
	enum json_value_e type;
	union {
		const char *s;
		int64_t d;
	};
};

/**
 * TODO: write doc
 */
int ldmsd_plugin_qjson_attrs_add(json_entity_t result,
			struct ldmsd_plugin_qjson_attrs *bulks);

/**
 * TODO: write doc
 */
void ldmsd_plugin_qjson_err_set(json_entity_t result, int rc, char *errmsg);

/**
 * Bulk add entries into the collection.
 *
 * Iteratively add bulk entries into the collection \c coll. This function is
 * useful for adding bulk of known result entries.
 *
 * The \c val in the bulk is the same as \c val for ::ldmsd_plugin_qrent_add().
 * If the type is \c LDMSD_PLUGIN_QRENT_STR, \c val is copied and the caller
 * still own it. If the type is \c LDMSD_PLUGIN_QRENT_COLL, \c coll will own it.
 *
 * \param coll The query result entry collection.
 * \param bulk The array of `struct ldmsd_plugin_qent_bulk_s`
 *             terminating with {NULL,0,NULL}.
 *
 * \retval 0     If succeed.
 * \retval errno If failed.
 */
int ldmsd_plugin_qrent_add_bulk(ldmsd_plugin_qrent_coll_t coll,
				struct ldmsd_plugin_qrent_bulk_s *bulk);

/**
 * Add a config entry into the config collection.
 */
int ldmsd_plugin_qrent_config_add(ldmsd_plugin_qresult_t r,
					ldmsd_plugin_qrent_t coll);

/**
 * The default implementation of \c ldmsd_plugin_type_s.query().
 */
json_entity_t ldmsd_plugin_query(ldmsd_plugin_inst_t i, const char *q);

/**
 * Produces JSON string from query result.
 *
 * The caller is responsible for freeing the returned string.
 *
 * \retval json A string in JSON format.
 */
char *ldmsd_plugin_qresult_to_json(ldmsd_plugin_qresult_t qresult);

/**
 * Print collection of query result entries to buffer in JSON format.
 *
 * \param         coll The collection of query result entries.
 * \param[in,out] buff String buffer pointer. \c *buff is reallocated if the
 *                     available length is not enough.
 * \param[in,out] off  The offset from the beginning of \c *buff to start
 *                     printing. If the printing succeeded, \c *off is also
 *                     advanced by the printed length (excluding '\0').
 * \param[in,out] alen The available length of the \c *buff after \c *off. If
 *                     the printing succeeded, \c *alen is also reduced by the
 *                     printed length (excluding '\0').
 *
 * \retval 0     If succeed.
 * \retval errno If failed.
 */
int ldmsd_plugin_qrent_coll_json_print(ldmsd_plugin_qrent_coll_t coll,
				       char **buff, int *off, int *alen);

/** \} */ /* defgroup ldmsd_plugin */


/**
 * Formatted string append utility.
 *
 * \param[in,out] buff String buffer pointer. \c *buff is reallocated if the
 *                     available length is not enough.
 * \param[in,out] off  The offset from the beginning of \c *buff to start
 *                     printing. If the printing succeeded, \c *off is also
 *                     advanced by the printed length (excluding '\0').
 * \param[in,out] alen The available length of the \c *buff after \c *off. If
 *                     the printing succeeded, \c *alen is also reduced by the
 *                     printed length (excluding '\0').
 * \param         fmt  The string format (the same as \c printf).
 * \param     _va_arg_ The variable-length arguments according to the format
 *                     \c fmt (same as \c printf).
 */
__attribute__((format(printf, 4, 5)))
int sappendf(char **buff, int *off, int *alen, const char *fmt, ...);

#endif /* __LDMSD_PLUGIN_H__ */
