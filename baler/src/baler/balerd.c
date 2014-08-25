/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
/**
 * \file balerd.c
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 19, 2013
 *
 * \defgroup balerd Baler Daemon
 * \{
 * \brief Baler daemon (core).
 *
 * The implementation of Baler daemon core goes in here.
 */
#include <stdio.h>
#include <pthread.h>
#include <limits.h>
#include <ctype.h>
#include <dlfcn.h>
#include <unistd.h>
#include <signal.h>

#ifdef ENABLE_OCM
#include "ocm/ocm.h"
ocm_t ocm; /**< ocm handle */
char ocm_key[512]; /**< $HOSTNAME/balerd */
uint16_t ocm_port = OCM_DEFAULT_PORT;
int ocm_cb(struct ocm_event *e);
#endif

#include "bcommon.h"
#include "btypes.h"
#include "butils.h"
#include "bmapper.h"
#include "binput_private.h"
#include "boutput.h"
#include "btkn.h"
#include "bptn.h"
#include "bwqueue.h"

/***** Command line arguments ****/
#ifdef ENABLE_OCM
const char *optstring = "hFC:l:s:z:";
#else
const char *optstring = "hFC:l:s:";
#endif
const char *config_path = NULL;
const char *log_path = NULL;

void display_help_msg()
{
	char *help_msg =
"Usage: balerd [options]\n"
"\n"
"Options: \n"
"	-l <path>	Log file (log to stdout by default)\n"
"	-s <path>	Store path (default: ./store)\n"
"	-C <path>	Configuration (Baler commands) file\n"
"	-F		Foreground mode (default: daemon mode)\n"
"	-h		Show help message\n"
"\n";
	printf("%s\n", help_msg);
}

/********** Configuration Variables **********/
/**
 * \defgroup bconf_vars Configuration Variables
 * \{
 */
int binqwkrN = 1; /**< Input Worker Thread Number */
int boutqwkrN = 1; /**< Output Worker Thread Number */
int is_foreground = 0; /**< Run as foreground? */
/**\}*/

/********** Configuration Commands **********/
char* str_to_lower(char *s)
{
	while (s) {
		*s = tolower(*s);
		s++;
	}
	return s;
}

/* define cfg commands here.
 * an extra '_' infront of LIST is to prevent name collision with possible
 * command 'list' */
#define BCFG_CMD__LIST(PREFIX, CMD) \
	CMD(PREFIX, PLUGIN), \
	CMD(PREFIX, TOKENS), \
	CMD(PREFIX, HOSTS)

/* automatically generated enum */
enum BCFG_CMD {
	BCFG_CMD__LIST(BCFG_CMD_, BENUM),
	BCFG_CMD_LAST
};

/* automatically generated command strings */
#define BCFG_CMD_STR(X) #X,
const char *bcfg_str[] = {
	BCFG_CMD__LIST(, BENUM_STR)
};

enum BCFG_CMD bcfg_cmd_str2enum(const char *s)
{
	return bget_str_idx(bcfg_str, BCFG_CMD_LAST, s);
}

/********** Global Variable Section **********/
extern uint64_t *metric_ids;

/**
 * Input queue workers
 */
pthread_t *binqwkr;

/**
 * Head of the ::bconfig_list.
 */
LIST_HEAD(bconfig_head, bconfig_list);

/**
 * List of plugin configurations.
 */
struct bconfig_list {
	char *command; /**< Configuration command */
	struct bpair_str_head arg_head_s; /**< Argument list head */
	LIST_ENTRY(bconfig_list) link; /**< The link to next/prev item */
};

/**
 * The input plugin configuration list head.
 */
struct bconfig_head bipconf_head_s = {NULL};

/**
 * The output plugin configuration list head.
 */
struct bconfig_head bopconf_head_s = {NULL};

/**
 * Plugin instance list head.
 */
LIST_HEAD(bplugin_head, bplugin);

/**
 * Input plugin instance head.
 */
struct bplugin_head bip_head_s = {NULL};

/**
 * Output plugin instance head.
 */
struct bplugin_head bop_head_s = {NULL};

/**
 * A pointer to Baler Output Queue.
 */
struct bwq *boutq;

/**
 * Output queue workers
 */
pthread_t *boutqwkr;

/**
 * Baler Input Worker's Context.
 * Data in this structure will be reused repeatedly by the worker thread
 * that owns it. Repeatedly allocate and deallocate is much slower than
 * using stack or global variables. I do not put this in stack because
 * this is relatively huge.
 */
struct bin_wkr_ctxt {
	int worker_id; /**< Worker ID */
	union {
		/**
		 * Space for ptn_str.
		 */
		char _ptn_str[sizeof(struct bstr)+sizeof(uint32_t)*4096];
		/**
		 * Pattern string.
		 */
		struct bstr ptn_str;
	};
	union {
		/**
		 * Space for message structure.
		 */
		char _msg[sizeof(struct bmsg)+sizeof(uint32_t)*4096];
		/**
		 * Message.
		 */
		struct bmsg msg;
	};
};

/**
 * Context for Output Worker.
 */
struct bout_wkr_ctxt {
	int worker_id; /**< Worker ID */
};

struct btkn_store *token_store; /**< Token store */
struct bptn_store *pattern_store; /**< Pattern store */

struct btkn_store *comp_store; /**< Token store for comp_id */

/*********************************************/

void* binqwkr_routine(void *arg);
void* boutqwkr_routine(void *arg);

void bconfig_list_free(struct bconfig_list *bl) {
	struct bpair_str *bp;
	while ((bp = LIST_FIRST(&bl->arg_head_s))) {
		LIST_REMOVE(bp, link);
		bpair_str_free(bp);
	}
	free(bl);
}
/**
 * Baler Daemon Initialization.
 */
void initialize_daemon()
{
	/* Daemonize? */
	if (!is_foreground) {
		binfo("Daemonizing...");
		if (daemon(1, 1) == -1) {
			berror("daemon");
			exit(-1);
		}
		binfo("Daemonized");
	}
	umask(0);
	/* Input/Output Work Queue */
	binq = bwq_alloci(1024);
	if (!binq) {
		berror("(binq) bwq_alloci");
		exit(-1);
	}

	boutq = bwq_alloci(1024);
	if (!boutq) {
		berror("(boutq) bwq_alloci");
		exit(-1);
	}

	/* Token/Pattern stores */
	binfo("Opening Token Store.");
	char tmp[PATH_MAX];
	const char *store_path = bget_store_path();
	sprintf(tmp, "%s/tkn_store", store_path);
	token_store = btkn_store_open(tmp);
	if (!token_store) {
		berror("btkn_store_open");
		berr("Cannot open token store: %s", tmp);
		exit(-1);
	}
	/* First add the spaces into the store */
	btkn_store_char_insert(token_store, " \t\r\n", BTKN_TYPE_SPC);
	btkn_store_char_insert(token_store, BTKN_SYMBOL_STR, BTKN_TYPE_SYM);

	sprintf(tmp, "%s/ptn_store", store_path);
	pattern_store = bptn_store_open(tmp);
	if (!pattern_store) {
		berror("bptn_store_open");
		berr("Cannot open pattern store: %s", tmp);
		exit(-1);
	}

	/* Comp store for comp<->comp_id */
	binfo("Opening Component Store.");
	sprintf(tmp, "%s/comp_store", store_path);
	comp_store = btkn_store_open(tmp);
	if (!comp_store) {
		berror("btkn_store_open");
		berr("Cannot open token store: %s", tmp);
		exit(-1);
	}

	/* Input worker threads */
	binqwkr = malloc(sizeof(*binqwkr)*binqwkrN);
	if (!binqwkr) {
		berror("malloc for binqwkr");
		exit(-1);
	}
	int i, rc;
	struct bin_wkr_ctxt *ictxt;
	for (i=0; i<binqwkrN; i++) {
		ictxt = calloc(1, sizeof(*ictxt));
		if (!ictxt) {
			berror("calloc for ictxt");
			exit(-1);
		}
		ictxt->worker_id = i;
		if ((rc = pthread_create(binqwkr+i, NULL,binqwkr_routine,
						ictxt)) != 0) {
			berr("pthread_create error code: %d\n", rc);
			exit(-1);
		}
	}

	/* Output worker threads */
	boutqwkr = malloc(sizeof(*boutqwkr)*boutqwkrN);
	if (!boutqwkr) {
		berror("malloc for boutqwkr");
		exit(-1);
	}
	struct bout_wkr_ctxt *octxt;
	for (i=0; i<boutqwkrN; i++) {
		octxt = calloc(1, sizeof(*octxt));
		if (!octxt) {
			berror("calloc for octxt");
			exit(-1);
		}
		octxt->worker_id = i;
		if ((rc = pthread_create(boutqwkr+i, NULL, boutqwkr_routine,
						octxt)) != 0) {
			berr("pthread_create error, code: %d\n", rc);
			exit(-1);
		}
	}

	metric_ids = calloc(1024*1024, sizeof(*metric_ids));
	if (!metric_ids) {
		berr("cannot allocate metric_ids.\n");
		exit(-1);
	}

	/* OCM */
#ifdef ENABLE_OCM
	ocm = ocm_create("sock", ocm_port, ocm_cb, __blog);
	if (!ocm) {
		berr("cannot create ocm: error %d\n", errno);
		exit(-1);
	}
	rc = gethostname(ocm_key, 512);
	if (rc) {
		berr("cannot get hostname, error %d: %m\n", errno);
		exit(-1);
	}
	sprintf(ocm_key + strlen(ocm_key), "/%s", "balerd");
	ocm_register(ocm, ocm_key, ocm_cb);
	ocm_enable(ocm);
#endif
	binfo("Baler Initialization Complete.");
}

/**
 * Load a single plugin and put it into the given plugin instance list.
 * \param pcl The load plugin configuration.
 * \param inst_head The head of the plugin instance list.
 * \return 0 on success.
 * \return Error code on error.
 */
int load_plugin(struct bconfig_list *pcl, struct bplugin_head *inst_head)
{
	int rc = 0;
	char plibso[PATH_MAX];
	struct bpair_str *bname = bpair_str_search(&pcl->arg_head_s,
			"name", NULL);
	if (!bname) {
		berr("Cannot load plugin without argument 'name', "
				"command: %s", pcl->command);
		rc = EINVAL;
		goto out;
	}
	sprintf(plibso, "lib%s.so", bname->s1);
	void *h = dlopen(plibso, RTLD_NOW);
	if (!h) {
		rc = ELIBACC;
		berr("dlopen: %s\n", dlerror());
		goto out;
	}
	struct bplugin* (*pcreate)();
	dlerror(); /* Clear current dlerror */
	*(void**) (&pcreate) = dlsym(h, "create_plugin_instance");
	char *err = dlerror();
	if (err) {
		rc = ELIBBAD;
		berr("dlsym error: %s\n", err);
		goto out;
	}
	struct bplugin *p = pcreate();
	if (!p) {
		rc = errno;
		berr("Cannot create plugin %s\n", bname->s1);
		goto out;
	}
	LIST_INSERT_HEAD(inst_head, p, link);

	/* Configure the plugin. */
	if (rc = p->config(p, &pcl->arg_head_s)) {
		berr("Config error, code: %d\n", rc);
		goto out;
	}

	/* And start the plugin. Plugin should not block this though. */
	if (rc = p->start(p)) {
		berr("Plugin %s start error, code: %d\n", bname->s1,
				rc);
		goto out;
	}
out:
	return rc;
}

int load_plugins(struct bconfig_head *conf_head,
		struct bplugin_head *inst_head)
{
	struct bconfig_list *pcl;
	char plibso[PATH_MAX];
	int rc = 0;
	LIST_FOREACH(pcl, conf_head, link) {
		rc = load_plugin(pcl, inst_head);
		if (rc)
			goto out;
	}
out:
	return rc;
}

/**
 * A simple macro to skip the delimiters in \c d.
 * \param x A pointer to the string.
 * \param d A pointer to the string containing delimiters.
 */
#define SKIP_DELIMS(x, d) while (*(x) && strchr((d), *(x))) {(x)++;}

#define WHITE_SPACES " \t"

/**
 * Get the first token from \c *s. The returned string is newly created, and \c
 * *s will point to the next token. On error, \c *s will not be changed and the
 * function returns NULL.
 * \param[in,out] s \c *s is the string.
 * \param delims The delimiters.
 * \return NULL on error.
 * \return A pointer to the extracted token. Caller is responsible for freeing
 * 	it.
 */
char* get_token(const char **s, char* delims)
{
	const char *str = *s;
	while (*str && !strchr(delims, *str)) {
		str++;
	}
	int len = str - *s;
	char *tok = malloc(len + 1);
	if (!tok)
		return NULL;
	memcpy(tok, *s, len);
	tok[len] = 0;
	*s = str;
	return tok;
}

/**
 * Parse the configuration string \c cstr.
 * \param cstr The configuration string (e.g. "rsyslog port=9999").
 * \return NULL on error.
 * \return The pointer to ::bconfig_list, containing the parsed
 * 	information.
 */
struct bconfig_list* parse_config_str(const char *cstr)
{
	struct bconfig_list *l = calloc(1, sizeof(*l));
	if (!l)
		goto err0;
	const char *s = cstr;
	SKIP_DELIMS(s, WHITE_SPACES);

	/* The first token is the name of the plugin. */
	l->command = get_token(&s, WHITE_SPACES);
	if (!l->command) {
		errno = EINVAL;
		goto err1;
	}
	SKIP_DELIMS(s, WHITE_SPACES);

	/* The rest are <key>=<value> configuration arguments. */
	char *value;
	char *key;
	struct bpair_str *pstr;
	struct bpair_str tail; /* dummy tail */
	LIST_INSERT_HEAD(&l->arg_head_s, &tail, link);
	while (*s && (key = get_token(&s, "="WHITE_SPACES))) {
		SKIP_DELIMS(s, "="WHITE_SPACES);
		value = get_token(&s, WHITE_SPACES);
		SKIP_DELIMS(s, WHITE_SPACES);
		if (!value) {
			errno = EINVAL;
			goto err2;
		}
		pstr = malloc(sizeof(*pstr));
		if (!pstr)
			goto err3;
		pstr->s0 = key;
		pstr->s1 = value;
		LIST_INSERT_BEFORE(&tail, pstr, link);
	}
	LIST_REMOVE(&tail, link);

	/* Parse done, return the config list node. */
	return l;
err3:
	free(value);
err2:
	free(key);
	/* Reuse pstr */
	while (pstr = LIST_FIRST(&l->arg_head_s)) {
		LIST_REMOVE(pstr, link);
		bpair_str_free(pstr);
	}
err1:
	free(l);
err0:
	return NULL;
}

int process_cmd_plugin(struct bconfig_list *cfg)
{
	struct bpair_str *bp = bpair_str_search(&cfg->arg_head_s, "name", NULL);
	if (!bp)
		return EINVAL;
	if (strncmp(bp->s1, "bin", 3) == 0)
		return load_plugin(cfg, &bip_head_s);
	else if (strncmp(bp->s1, "bout", 4) == 0)
		return load_plugin(cfg, &bop_head_s);
	return EINVAL;
}

/**
 * \returns 0 on success.
 * \returns Error code on error.
 */
int process_cmd_tokens(struct bconfig_list *cfg)
{
	struct btkn_store *store;
	struct bpair_str *bp_path = bpair_str_search(&cfg->arg_head_s, "path",
									NULL);
	struct bpair_str *bp_type = bpair_str_search(&cfg->arg_head_s, "type",
									NULL);
	if (!bp_path || !bp_type)
		return EINVAL;

	const char *path = bp_path->s1;
	btkn_type_t tkn_type = btkn_type(bp_type->s1);
	switch (tkn_type) {
	case BTKN_TYPE_HOST:
		store = comp_store;
		break;
	case BTKN_TYPE_ENG:
		store = token_store;
		break;
	default:
		berr("Unknown token type: %s\n", bp_type->s1);
		return EINVAL;
	}
	FILE *fi = fopen(path, "rt");
	if (!fi)
		return errno;
	char buff[1024 + sizeof(struct bstr)];
	struct bstr *bstr = (void*)buff;
	char *c;
	uint32_t tkn_id;
	while (fgets(bstr->cstr, 1024, fi)) {
		c = bstr->cstr + strlen(bstr->cstr) - 1;
		while (isspace(*c))
			*c-- = '\0';
		bstr->blen = strlen(bstr->cstr);
		if (!bstr->blen)
			continue; /* skip empty line */
		tkn_id = btkn_store_insert(store, bstr);
		if (tkn_id == BMAP_ID_ERR) {
			berr("cannot insert '%s' into token store", bstr->cstr);
			return errno;
		}
		struct btkn_attr attr = {tkn_type};
		btkn_store_set_attr(token_store, tkn_id, attr);
	}
	return 0;
}

int process_cmd_hosts(struct bconfig_list *bl)
{
	/*
	 * NOTE 1: comp_store should be filled at this point.
	 *         Each hostname should be able to resolved into an internal
	 *         component id.
	 *
	 * NOTE 2: attribute-value list of 'hosts' command is
	 * hostname1:metric_id1, hostnam2:metric_id2, ...
	 */
	struct btkn_store *store = comp_store;
	struct bpair_str *av;
	char buff[512];
	struct bstr *bs = (void*)buff;
	LIST_FOREACH(av, &bl->arg_head_s, link) {
		uint64_t mid = strtoull(av->s1, NULL, 0);
		bstr_set_cstr(bs, av->s0, 0);
		uint32_t cid = bmap_get_id(store->map, bs);
		if (cid < BMAP_ID_BEGIN) {
			cid = btkn_store_insert(comp_store, bs);
			if (cid < BMAP_ID_BEGIN) {
				berr("cannot insert host: %s\n", av->s0);
				return EINVAL;
			}
		}
		if (cid >= 1024*1024) {
			berr("component_id for '%s' out of range: %d\n", av->s0,
					cid);
			return EINVAL;
		}
		cid -= (BMAP_ID_BEGIN - 1);
		metric_ids[cid] = mid;
	}
	return 0;
}

/**
 * Process the given command \c cmd.
 * \param cmd The command to process.
 * \return 0 on success.
 * \return Error code on error.
 */
int process_command(const char *cmd)
{
	struct bconfig_list *cfg = parse_config_str(cmd);
	int rc = 0;

	switch (bcfg_cmd_str2enum(cfg->command)) {
	case BCFG_CMD_PLUGIN:
		rc = process_cmd_plugin(cfg);
		break;
	case BCFG_CMD_TOKENS:
		rc = process_cmd_tokens(cfg);
		break;
	case BCFG_CMD_HOSTS:
		rc = process_cmd_hosts(cfg);
		break;
	default:
		/* Unknown command */
		berr("Unknown command: %s", cfg->command);
		rc = EINVAL;
	}

	bconfig_list_free(cfg);

	return rc;
}

/**
 * Configuration file handling function.
 */
void config_file_handling(const char *path)
{
	FILE *fin = fopen(path, "rt");
	if (!fin) {
		perror("fopen");
		exit(-1);
	}
	char buff[1024];
	int lno = 0;
	int rc = 0;
	char *s;
	while (fgets(buff, 1024, fin)) {
		lno++;
		/* eliminate trailing spaces */
		s = buff + strlen(buff) - 1;
		while (isspace(*s))
			*s = '\0';
		/* eliminate preceding white spaces */
		s = buff;
		while (isspace(*s))
			s++;
		if (*s == '#')
			continue; /* comment line */
		if ((rc = process_command(s))) {
			berr("process_command error %d: %s at %s:%d\n",
					rc, strerror(rc), path, lno);
			exit(rc);
		}
	}
}

#ifdef ENABLE_OCM
/* This is an interface to the existing baler configuration */
void process_ocm_cmd(ocm_cfg_cmd_t cmd)
{
	struct bconfig_list *bl;
	struct bpair_str *bp;
	struct ocm_av_iter iter;
	const char *key;
	const struct ocm_value *v;

	bl = calloc(1, sizeof(*bl));
	if (!bl) {
		berr("%m at %s:%d in %s\n", __FILE__, __LINE__, __func__);
		return;
	}

	/* Create baler command from ocm command */
	ocm_av_iter_init(&iter, cmd);
	bl->command = (char*)ocm_cfg_cmd_verb(cmd);
	while (ocm_av_iter_next(&iter, &key, &v) == 0) {
		bp = calloc(1, sizeof(*bp));
		bp->s0 = (char*)key;
		bp->s1 = (char*)v->s.str;
		LIST_INSERT_HEAD(&bl->arg_head_s, bp, link);
	}
	/* end create baler command */

	/* Process the command */
	switch (bcfg_cmd_str2enum(bl->command)) {
	case BCFG_CMD_PLUGIN:
		process_cmd_plugin(bl);
		break;
	case BCFG_CMD_TOKENS:
		process_cmd_tokens(bl);
		break;
	case BCFG_CMD_HOSTS:
		process_cmd_hosts(bl);
		break;
	default:
		/* Unknown command */
		berr("Unknown command: %s", bl->command);
	}

cleanup:
	while ((bp = LIST_FIRST(&bl->arg_head_s))) {
		LIST_REMOVE(bp, link);
		free(bp);
	}
	free(bl);
}

void process_ocm_cfg(ocm_cfg_t cfg)
{
	ocm_cfg_cmd_t cmd;
	struct ocm_cfg_cmd_iter iter;
	ocm_cfg_cmd_iter_init(&iter, cfg);
	while (ocm_cfg_cmd_iter_next(&iter, &cmd) == 0) {
		process_ocm_cmd(cmd);
	}
}

int ocm_cb(struct ocm_event *e)
{
	switch (e->type) {
	case OCM_EVENT_CFG_REQUESTED:
		ocm_event_resp_err(e, ENOSYS, ocm_cfg_req_key(e->req),
							"Not implemented.");
		break;
	case OCM_EVENT_CFG_RECEIVED:
		process_ocm_cfg(e->cfg);
		break;
	case OCM_EVENT_ERROR:
		berr("ocm event error, key: %s, code: %d, msg: %s",
				ocm_err_key(e->err),
				ocm_err_code(e->err),
				ocm_err_msg(e->err));
		break;
	}
	return 0;
}
#endif

void handle_set_log_file(const char *path)
{
	log_path = strdup(path);
	if (!log_path) {
		perror(path);
		exit(-1);
	}

	int rc = blog_open_file(log_path);
	if (rc) {
		fprintf(stderr, "Failed to open the log file '%s'\n", path);
		exit(rc);
	}
}

/**
 * Configuration handling.
 * Currently, just do stuffs from the command line. This function needs to be
 * changed later to receive the real configuration from the configuration
 * manager.
 */
void args_handling(int argc, char **argv)
{
	bset_store_path("./store");
	int c;

next_arg:
	c = getopt(argc, argv, optstring);
	switch (c) {
	case 'l':
		handle_set_log_file(optarg);
		break;
	case 's':
		bset_store_path(optarg);
		break;
	case 'F':
		is_foreground = 1;
		break;
	case 'C':
		config_path = strdup(optarg);
		break;
#ifdef ENABLE_OCM
	case 'z':
		ocm_port = atoi(optarg);
		break;
#endif
	case 'h':
		display_help_msg();
		exit(0);
	case -1:
		goto arg_done;
	}
	goto next_arg;

arg_done:
	binfo("Baler Daemon Started.\n");
}

void binq_data_print(struct binq_data *d)
{
	printf("binq: %.*s %ld (%d): ", d->hostname->blen, d->hostname->cstr,
			d->tv.tv_sec, d->tok_count);
	struct bstr_list_entry *e;
	LIST_FOREACH(e, d->tokens, link) {
		printf(" '%.*s'", e->str.blen, e->str.cstr);
	}
	printf("\n");
}

/**
 * Core processing of an input entry.
 * \param ent Input entry.
 * \param ctxt The context of the worker.
 * \return 0 on success.
 * \return errno on error.
 */
int process_input_entry(struct bwq_entry *ent, struct bin_wkr_ctxt *ctxt)
{
	int rc = 0;
	struct binq_data *in_data = &ent->data.in;
	uint32_t comp_id = bmap_get_id(comp_store->map, in_data->hostname);
	if (comp_id < BMAP_ID_BEGIN) {
		/* Error, cannot find the comp_id */
		errno = ENOENT;
		return -1;
	}
	comp_id -= (BMAP_ID_BEGIN - 1);
	struct bstr_list_entry *str_ent;
	/* NOTE: ptn and msg is meant to be a uint32_t string */
	struct bstr *str = &ctxt->ptn_str;
	/* msg->agrv will hold the pattern arguments. */
	struct bmsg *msg = &ctxt->msg;
	uint32_t attr_count = 0;
	uint32_t tkn_idx = 0;
	LIST_FOREACH(str_ent, in_data->tokens, link) {
		int tid = btkn_store_insert(token_store, &str_ent->str);
		if (tid == BMAP_ID_ERR)
			return errno;
		struct btkn_attr attr = btkn_store_get_attr(token_store, tid);
		/* REMARK: The default type of a token is '*' */
		if (attr.type == BTKN_TYPE_STAR) {
			/* This will be marked as '*' and put into arg list. */
			str->u32str[tkn_idx] = BMAP_ID_STAR;
			msg->argv[attr_count++] = tid;
		} else {
			/* otherwise, just put it into the string. */
			str->u32str[tkn_idx] = tid;
		}
		tkn_idx++;
	}
	msg->argc = attr_count;
	str->blen = tkn_idx * sizeof(uint32_t);
	/* Now str is the pattern string, with arguments in msg->argv */
	/* pid stands for pattern id */
	uint32_t pid = bptn_store_addptn(pattern_store, str);
	if (pid == BMAP_ID_ERR)
		return errno;
	msg->ptn_id = pid;
	rc = bptn_store_addmsg(pattern_store, msg);
	if (rc)
		return rc;
	/* Copy msg to omsg for future usage in output queue. */
	struct bmsg *omsg = bmsg_alloc(msg->argc);
	if (!omsg)
		return ENOMEM;
	memcpy(omsg, msg, BMSG_SZ(msg));
	/* Prepare output queue entry. */
	struct bwq_entry *oent = (typeof(oent))malloc(sizeof(*oent));
	struct boutq_data *odata = &oent->data.out;
	odata->comp_id = comp_id;
	odata->tv = in_data->tv;
	odata->msg = omsg;
	/* Put the processed message into output queue */
	bwq_nq(boutq, oent);
	return 0;
}

/**
 * Routine for Input Queue Worker. When data are avialable in the input queue,
 * one of the input queue workers will get an access to the input queue.
 * It then consume the input entry and release the input queue lock so that
 * the other workers can work on the next entry.
 * \param arg A pointer to ::bin_wkr_ctxt.
 * \return NULL (should be ignored)
 */
void* binqwkr_routine(void *arg)
{
	struct bwq_entry *ent;
loop:
	/* bwq_dq will block the execution if the queue is empty. */
	ent = bwq_dq(binq);
	if (!ent) {
		/* This is not supposed to happen. */
		berr("Error, ent == NULL\n");
	}
	if (process_input_entry(ent, (struct bin_wkr_ctxt*) arg) == -1) {
		/* XXX Do better error handling ... */
		berr("process input error ...");
	}
	binq_entry_free(ent);
	goto loop;
}

int process_output_entry(struct bwq_entry *ent, struct bout_wkr_ctxt *ctxt)
{
	struct boutq_data *d = &ent->data.out;
	struct bplugin *p;
	struct boutplugin *op;
	int rc = 0;
	LIST_FOREACH(p, &bop_head_s, link) {
		op = (typeof(op))p;
		rc = op->process_output(op, d);
		/** TODO Better error handling here */
		if (rc) {
			bwarn("Output plugin %s->process_output error, code:"
				       " %d\n", p->name, rc);
		}
	}
	return 0;
}

/**
 * Work routine for Output Queue Worker Thread.
 * \param arg A pointer to ::bout_wkr_ctxt.
 * \return Nothing as the worker thread(s) run forever.
 */
void* boutqwkr_routine(void *arg)
{
	struct bwq_entry *ent;
loop:
	/* bwq_dq will block the execution if the queue is empty. */
	ent = bwq_dq(boutq);
	if (!ent) {
		/* This is not supposed to happen. */
		berr("Error, ent == NULL\n");
	}
	if (process_output_entry(ent, (struct bout_wkr_ctxt *) arg) == -1) {
		/* XXX Do better error handling ... */
		berr("process input error, code %d\n", errno);
	}
	boutq_entry_free(ent);
	goto loop;
}

/**
 * Input Worker Thread & Output Worker Thread join point.
 */
void thread_join()
{
	int i;
	/* Joining the input queue workers */
	for (i=0; i<binqwkrN; i++){
		pthread_join(binqwkr[i], NULL);
	}

	/* Joining the output queue workers */
	for (i=0; i<boutqwkrN; i++){
		pthread_join(boutqwkr[i], NULL);
	}
}

/**
 * Daemon clean-up routine.
 */
void cleanup_daemon(int x)
{
	binfo("Baler Daemon exiting ... status %d\n", x);
	exit(x);
}

/**
 * Action when receives SIGUSR1 from logrotate
 */
void handle_logrotate(int x)
{
	int rc = 0;
	rc = blog_rotate(log_path);
	if (rc) {
		berr("Failed to open a new log file. '%s'\n",
						strerror(errno));
		cleanup_daemon(x);
	}
}

/**
 * \brief The main function.
 */
int main(int argc, char **argv)
{
	struct sigaction cleanup_act, logrotate_act;
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	cleanup_act.sa_handler = cleanup_daemon;
	cleanup_act.sa_flags = 0;
	cleanup_act.sa_mask = sigset;

	sigaction(SIGHUP, &cleanup_act, NULL);
	sigaction(SIGINT, &cleanup_act, NULL);
	sigaction(SIGTERM, &cleanup_act, NULL);

	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	logrotate_act.sa_handler = handle_logrotate;
	logrotate_act.sa_flags = 0;
	logrotate_act.sa_mask = sigset;
	sigaction(SIGUSR1, &logrotate_act, NULL);

	args_handling(argc, argv);

	/* Initializing daemon and worker threads according to the
	 * configuration. */
	initialize_daemon();

	if (config_path)
		config_file_handling(config_path);

	binfo("Baler is ready.");

	/* Then join the worker threads. */
	thread_join();

	/* On exit, clean up the daemon. */
	cleanup_daemon(0);
	return 0;
}
/**\}*/
