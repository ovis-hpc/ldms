/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
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

#include <stdarg.h>
#include <stdbool.h>
#include <inttypes.h>
#include "ovis_util/util.h"
#include "ldms.h"
#include "ldmsd.h"

#ifndef LDMS_SRC_LDMSD_LDMSD_PLUGATTR_H_
#define LDMS_SRC_LDMSD_LDMSD_PLUGATTR_H_
/** The plugin attribute library provides augmenting a config (a/v, kw) with
 * parsing a/v,kw from a text file. This enables syntax for plugin loading
 * to load instance-specific configs ahead of their use without requiring:
 * - multiple calls to config function
 * - storage of pre-instantiation config arguments to be coded repeatedly in
 * multiple plugins.
 *
 * The expected command syntax uses are
 * load name=store_csv conf=customizations.store_csv.txt
 *	or
 * load name=store_csv altheader=1 conf=customizations.store_csv.txt
 * where any keys like altheader override what is stored in the text file.
 *
 * Support for routine option transformations (e.g. to parse numerics or
 * handling repeated options) is also provided.
 *
 * Currently, the library reports the log messages to the application's
 * default log.
 */

struct plugattr;

/** description of a deprecated/retired attribute.
*/
struct pa_deprecated {
	const char *name; /*< attribute name, or NULL if the last element of the input array. */
	const char *value; /*< value, if value match is required, or NULL */
	int error; /*< Is detecting name(/value) match fatal (!0) or a warning (0). */
	const char *msg; /*< specific log detail to add to generic message, or NULL. */
};
#define NULL_PA_DEP {NULL, NULL, 0, NULL}

/**
 * \brief parse the attributes from the attr_value_lists and file.
 * \param filename a file containing lines with attr=value 
 *        Each line defines an instance named as "a/b".
 *        where the values of keys named after numkeys are joined.
 * \param plugin_name the name of the plugin being configured and 
 * the set of defaults used.
 * \param plugin_name the name of the plugin being configured and 
 * the set of defaults used.
 * \param avl optional list that overrides/augments the defaults of 
 * plugin_name in filename.
 * \param kwl optional list that overrides/augments the defaults of 
 * plugin_name in filename.
 * \param dep array of deprecated items; last item in the array must
 * be NULL_PA_DEP. Multiple items of the same name handled as in
 * ldmsd_plugattr_config_check().
 * \param numkeys the number of attributes used to form the key.
 * \param ... the numkeys names of attributes used to form the key.
 *
 * e.g. ldmsd_plugattr_create("input","store_plugin", avl, kwl,
 * 		2, "container", "schema");
 * 	will parse a line including 'container=a schema=b prop=true'
 * 	and associate it with key 'a/b'.
 * A file line which contains data but none of the named keys must contain the
 * plugin_name as a keyword. A repetition of this line is an error.
 */
struct plugattr *ldmsd_plugattr_create(const char *filename, const char *plugin_name, struct attr_value_list *avl, struct attr_value_list *kwl, const char **avban, const char **kwban, struct pa_deprecated *dep, unsigned numkeys, ...);

/**
 * reclaim memory of pa.
 */
void ldmsd_plugattr_destroy(struct plugattr *pa);

/** \brief add new entry to plugattr using the key as in ldmsd_plugattr_create.
 * It is an error to add k when pa already knows of k.
 * \param pa self
 * \param avl new attributes for instance named in avl by assembled key.
 * \param kwl new keywords for instance named in avl by assembled key.
 * \param avban null terminated array of banned attribute names.
 * \param kwban null terminated array of banned keyword names.
 * \param dep array of deprecated items; last item in the array must
 * be NULL_PA_DEP. Multiple items of the same name handled as in
 * ldmsd_plugattr_config_check().
 * \param numkeys the number of attributes used to form the key.
 * \param ... the numkeys names of attributes used to form the key.
 * \return errno value.
 * As with ldmsd_plugattr_create
 * ldmsd_plugattr_add(pa, avl, kwl, 2, "container", "schema");
 */
int ldmsd_plugattr_add(struct plugattr *pa, struct attr_value_list *avl, struct attr_value_list *kwl, const char **avban, const char **kwban, struct pa_deprecated *dep, unsigned numkeys, ...);

/** \brief get the plugin_name of the plugattr. */
const char *ldmsd_plugattr_plugin(struct plugattr *pa);

/**
 * \brief lookup value of attribute if present.
 * \param pa self
 * \param attr to look up.
 * \param key  the plugin instance name.
 * \return value if present, or NULL.
 *
 * The value returned for an attribute is the first found when inspecting,
 * in order,
 *     The line matching key
 *     The content of avl provided in the _create call.
 *     The content of the line where key matches plugin_name of the _create call.
 * By convention, when used with store plugins, key is "$container/$schema".
 * By convention, when used with sampler plugins, key is "schema".
 * If key given is NULL, "the line matching key" step is skipped.
 * If key given is the same as the plugin name at create, then the
 * avl provided in _create is ignored.
 */
const char *ldmsd_plugattr_value(struct plugattr *pa, const char *attr, const char *key);

/**
 * \brief lookup all values of attribute that may be repeated.
 * \param pa self
 * \param attr to look up.
 * \param key the plugin instance name.
 * \param valc address of int to set with the length of *valv.
 * \param valv address of string array pointer to be populated.
 * \return 0 if ok, errno if a problem.
 *
 * The values returned for an attribute is the first set found when inspecting, in order,
 *     The line matching key
 *     The content of avl provided in the _create call.
 *     The content of the line where key matches plugin_name of the _create call.
 * Caller must free(valv) when done with it.
 * example:
 * int nvals; char **vals;
 * ldmsd_plugattr_multivalue(pa, "mountpoint", "store_csv fs_gw", &nvals, &vals);
 * // use vals
 * free(vals);
 */
int ldmsd_plugattr_multivalue(struct plugattr *pa, const char *attr, const char *key, int *valc, char ***valv);

/** \brief Testing for presence as a keyword (kwl).
 * Checking is done in the same order as ldmsd_plugattr_value.
 * \param pa self
 * \param kw to look up.
 * \param key the plugin instance name.
 * \return true if present and false if not.
 */
bool ldmsd_plugattr_kw(struct plugattr *pa, const char *kw, const char *key);

/** \brief convert attr to boolean value.
 * \param pa self
 * \param at attribute to look up.
 * \param key the plugin instance name.
 * \param result address to store the result at.
 * \return -1 for bad args or bad string data; -2 for nonexistence.
 *
 *  Any string value starting with the following letters yields true:
 *    1 t T y Y \0
 *  Any string value starting with the following letters yields false:
 *    0 f F n N
 * Any other string yields -1 and a logged error.
 * E.g.: int cnv = ldmsd_plugattr_bool(pa, "param", key, &result);
 * This is a wrapper on ldmsd_plugattr_value; see it for precedence explanation.
 */
int ldmsd_plugattr_bool(struct plugattr *pa, const char *at, const char *key, bool *result);

/** \brief convert attr to int.
 * Anything trailing the number in the value of at makes the conversion invalid.
 * \param pa self
 * \param at attribute to look up.
 * \param key the plugin instance name.
 * \param result address to store the result at.
 * \return 0 if converted, other errno value if not.
 * EINVAL: bad args; 
 * ENOKEY: attribute is not found.
 * ENOTSUP value is not properly numeric;
 * ERANGE result does not fit in requested type.
 * E.g.: int cnv = ldmsd_plugattr_s32(pa, "param", key, &result);
 * This is a wrapper on ldmsd_plugattr_value; see it for precedence explanation.
 */
int ldmsd_plugattr_s32(struct plugattr *pa, const char *at, const char *key, int32_t *result);

/** \brief convert attr to uint32_t.
 * Anything trailing the number in the value of at makes the conversion invalid.
 * \param pa self
 * \param at attribute to look up.
 * \param key the plugin instance name.
 * \param result address to store the result at.
 * \return 0 if converted, other errno value if not.
 * EINVAL: bad args; 
 * ENOKEY: attribute is not found.
 * ENOTSUP value is not properly numeric;
 * ERANGE result does not fit in requested type.
 *
 * This is a wrapper on ldmsd_plugattr_value; see it for precedence explanation.
 */
int ldmsd_plugattr_u32(struct plugattr *pa, const char *at, const char *key, uint32_t *result);

/** \brief convert attr to int64_t.
 * Anything trailing the number in the value of at makes the conversion invalid.
 * \param pa self
 * \param at attribute to look up.
 * \param key the plugin instance name.
 * \param result address to store the result at.
 * \return 0 if converted, other errno value if not.
 * EINVAL: bad args; 
 * ENOKEY: attribute is not found.
 * ENOTSUP value is not properly numeric;
 * ERANGE result does not fit in requested type.
 *
 * This is a wrapper on ldmsd_plugattr_value; see it for precedence explanation.
 */
int ldmsd_plugattr_s64(struct plugattr *pa, const char *at, const char *key, int64_t *result);

/** \brief convert attr to uint64_t
 * Anything trailing the number in the value of at makes the conversion invalid.
 * \param pa self
 * \param at attribute to look up.
 * \param key the plugin instance name.
 * \param result address to store the result at.
 * \return 0 if converted, other errno value if not.
 * EINVAL: bad args; 
 * ENOKEY: attribute is not found.
 * ENOTSUP value is not properly numeric;
 * ERANGE result does not fit in requested type.
 *
 * This is a wrapper on ldmsd_plugattr_value; see it for precedence explanation.
 */
int ldmsd_plugattr_u64(struct plugattr *pa, const char *at, const char *key, uint64_t *result);

/** \brief convert attr to double.
 * Anything trailing the number in the value of at makes the conversion invalid.
 * \param pa self
 * \param at attribute to look up.
 * \param key the plugin instance name.
 * \param result address to store the result at.
 * \return 0 if converted, other errno value if not.
 * EINVAL: bad args; 
 * ENOKEY: attribute is not found.
 * ENOTSUP value is not properly numeric;
 * ERANGE result does not fit in requested type.
 *
 * This is a wrapper on ldmsd_plugattr_value; see it for precedence explanation.
 */
int ldmsd_plugattr_f64(struct plugattr *pa, const char *at, const char *key, double *result);

/** \brief convert attribute string with units to size_t.
 * Unrecognized units makes the conversion invalid.
 * Recognized units are those per util.h:ovis_get_mem_size.
 * \param pa self
 * \param at attribute to look up.
 * \param key the plugin instance name.
 * \param result address to store the result at.
 * \return 0 if converted, other errno value if not.
 *
 * This is a wrapper on ldmsd_plugattr_value; see it for precedence explanation.
 */
int ldmsd_plugattr_szt(struct plugattr *pa, const char *at, const char *key, size_t *result);

/* \brief dump pa (or subset indicated by key to log file at the given level. */
void ldmsd_plugattr_log(int lvl, struct plugattr *pa, const char *key);

/** \brief Screen config lists for unexpected keywords and deprecated.
 * \param anames null terminated array of k=v parameter names allowed.
 * \param knames null terminated array of k parameter names allowed.
 * \param avl the list to check against anames
 * \param kvl the list to check against knames
 * \param dep array of deprecated items; last item in the array must
 * be NULL_PA_DEP. Multiple items with the same name and alternate values
 * should be placed consecutively to avoid redundant check messages.
 * \param plugin name, or null if no logging wanted, but logging of deprecated
 * is always performed.
 * \return the number of unexpected names found.
 *
 * examples:
 * char * attributes[] = { "altheader", "path", CSV_STORE_ATTR_COMMON, NULL };
 *
 * // logging and checking that kvl is empty and avl correct.
 * int unexpected = ldmsd_plugattr_config_check(attributes, NULL, avl, kvl, NULL, name);
 *
 * // no logging and ignoring kvl (which might hide shell scripting errors)
 * int unexpected = ldmsd_plugattr_config_check(attributes, avl, kwl, NULL, NULL);
 */
int ldmsd_plugattr_config_check(const char **anames, const char **knames, struct attr_value_list *avl, struct attr_value_list *kwl, struct pa_deprecated *dep, const char *plugin_name);


#endif /* LDMS_SRC_LDMSD_LDMSD_PLUGATTR_H_ */
