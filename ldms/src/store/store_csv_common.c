/*
 * Copyright (c) 2016-2017 Sandia Corporation. All rights reserved.
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


#define PLUGNAME 0
#define store_csv_common_lib
#include <stdlib.h>
#include <errno.h>
#include "store_csv_common.h"
#define DSTRING_USE_SHORT
#include "ovis_util/dstring.h"


/** parse an option of the form name=bool where bool is
 * lead with tfyn01 or uppercase versions of same or value
 * string is "", so assume user meant name=true and absence of name
 * in options defaults to false.
 */
int parse_bool(struct csv_plugin_static *cps, struct attr_value_list *avl,
		const char *name, bool *bval)
{
	if (!cps || !avl || !name || !bval)
		return EINVAL;
	const char *val = av_value(avl, name);
	if (val) {
		switch (val[0]) {
		case '1':
		case 't':
		case 'T':
		case 'y':
		case 'Y':
		case '\0':
			*bval = true;
			break;
		case '0':
		case 'f':
		case 'F':
		case 'n':
		case 'N':
			*bval = false;
			break;
		default:
			cps->msglog(LDMSD_LERROR, "%s: bad %s=%s\n",
				cps->pname, name, val);
			return EINVAL;
		}
	}
	return 0;
}

static char *bad_replacement = "/malloc/failed";

int replace_string(char **strp, const char *val)
{
	if (!strp)
		return EINVAL;
	if (!val) {
		if (*strp != bad_replacement)
			free(*strp);
		*strp = NULL;
		return 0;
	}
	if (*strp != bad_replacement)
		free(*strp);
	char *new = strdup(val);
	if (new) {
		*strp = new;
		return 0;
	}
	*strp = bad_replacement;
	return ENOMEM;
}

/* get a handle to the onp we should use. return NULL only if no notify configured anywhere. */
static struct ovis_notification **get_onph(struct csv_store_handle_common *s_handle,
	struct csv_plugin_static *cps)
{
	if (!s_handle->notify && !cps->notify)
		return NULL;
	/* clean up case of redundant specification. */
	if (s_handle->notify && cps->notify &&
		strcmp(s_handle->notify, cps->notify) == 0) {
		s_handle->notify = NULL;
	}
	uint32_t wto = 6000;
	unsigned mqs = 1000;
	unsigned retry = 10;
	mode_t perm = 0700;
	/* init if unopened */
	if (s_handle->notify && s_handle->onp == NULL) {
		bool fifo = s_handle->notify_isfifo;
		s_handle->onp = ovis_notification_open(s_handle->notify,
				wto, mqs, retry,
				(ovis_notification_log_fn)cps->msglog,
				perm, fifo);
		if (s_handle->onp)
			cps->msglog(LDMSD_LDEBUG,"Created onp %s\n",
				s_handle->notify);
		else
			cps->msglog(LDMSD_LDEBUG,"Create fail for sh.onp %s\n",
				s_handle->notify);
	}
	if (cps->notify && !cps->onp) {
		bool fifo = cps->notify_isfifo;
		cps->onp = ovis_notification_open(cps->notify,
				wto, mqs, retry,
				(ovis_notification_log_fn)cps->msglog,
				perm, fifo);
		if (cps->onp)
			cps->msglog(LDMSD_LDEBUG,"Created onp %s\n",
				cps->notify);
		else
			cps->msglog(LDMSD_LDEBUG,"Create fail for cps.onp %s\n",
				cps->notify);
	}
	if (s_handle->onp)
		return &(s_handle->onp);
	else
		return &(cps->onp);
}

void notify_output(const char *event, const char *name, const char *ftype,
	struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps,
	const char * container, const char *schema) {
	if (!cps)
		return;
	if (s_handle && !s_handle->notify && !cps->notify)
		return;
	if (!event || !name || !ftype || !s_handle ||
		!container || !schema) {
		cps->msglog(LDMSD_LDEBUG,"Invalid argument in notify_output"
				"(%s, %s, %s, %p, %p)\n",
				event ? event : "missing event",
				name ? name : "missing name",
				ftype ? ftype : "missing ftype",
				container ? container : "missing container",
				schema ? schema : "missing schema",
				s_handle, cps);
		return;
	}
	struct ovis_notification **onph = get_onph(s_handle, cps);
	if (! onph || ! *onph) {
		cps->msglog(LDMSD_LDEBUG,"onp not set in handle or cps\n");
		return;
	}

	int *hcp = (NULL != s_handle->onp) ? &(s_handle->hooks_closed) :
						&(cps->hooks_closed);
	char *msg;
	if (*hcp) {
		cps->msglog(LDMSD_LINFO, "Request by storecsv with output closed: %s\n",
			s_handle->notify);
		return;
	}
	dsinit(ds);
	dscat(ds,event);
	dscat(ds," ");
	dscat(ds,cps->pname);
	dscat(ds," ");
	dscat(ds,container);
	dscat(ds," ");
	dscat(ds,schema);
	dscat(ds," ");
	dscat(ds,ftype);
	dscat(ds," ");
	dscat(ds,name);
	msg = dsdone(ds);
	if (!msg) {
		cps->msglog(LDMSD_LERROR,
			"Out of memory in notify_output for %s\n",name);
		return;
	}
	int rc = ovis_notification_add(*onph, msg);
	switch (rc) {
	case 0:
		cps->msglog(LDMSD_LDEBUG,"Notification of %s\n", msg); //
		break;
	case EINVAL:
		cps->msglog(LDMSD_LERROR,"Notification error by %s for %s: %s\n",
			cps->pname, name, msg);
		break;
	case ESHUTDOWN:
		cps->msglog(LDMSD_LERROR,"Disconnected output detected. Closing.\n");
		ovis_notification_close(*onph);
		*hcp = 1;
		*onph = NULL;
		break;
	default:
		cps->msglog(LDMSD_LERROR,"Unexpected error type %d in notify_spool\n",rc);
	}
	free(msg);
}

struct swap_data {
	size_t nstorekeys;
	size_t usedkeys;
	time_t appx;
	struct old_file *old;
	struct csv_plugin_static *cps;
};


/**
 * configurations for a container+schema that can override the vals in config_init
 * Locking and cfgstate are caller's job.
 */
int config_custom_common(struct attr_value_list *kwl, struct attr_value_list *avl, struct storek_common *sk, struct csv_plugin_static *cps)
{
	//defaults to init
	sk->notify_isfifo = cps->notify_isfifo;

	int rc = 0;
	char *notify =  av_value(avl, "notify");
	if (notify && strlen(notify) >= 2 ) {
		char *tmp1 = strdup(notify);
		if (!tmp1) {
			rc = ENOMEM;
		} else {
			sk->notify = tmp1;
			rc = parse_bool(cps, avl, "notify_isfifo",
				&(sk->notify_isfifo));
		}
	} else {
		if (notify) {
			cps->msglog(LDMSD_LERROR, "%s: notify "
				"must be specificed correctly. "
				"got instead %s\n", cps->pname,
				notify ) ;
			rc = EINVAL;
		} else {
			if (cps->notify) {
				sk->notify = strdup(cps->notify);
				if (!sk->notify) {
					rc = errno;
					cps->msglog(LDMSD_LERROR,
						"%s: config_schema_common out of memory\n",
						cps->pname);
				}
			}
		}
	}

	return rc;
}


void clear_storek_common(struct storek_common *s)
{
	if (!s)
		return;
	free(s->notify);
	s->notify = NULL;
}
/**
 * configurations for the whole store. these will be defaults if not overridden.
 * some implementation details are for backwards compatibility
 */
int config_init_common(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg, struct csv_plugin_static *cps)
{
	if (!cps || !avl)
		return EINVAL;

	int rc = 0;
	char *notify =  av_value(avl, "notify");
	if (notify && strlen(notify) >= 2 ) {
		char *tmp1 = strdup(notify);
		if (!tmp1) {
			rc = ENOMEM;
		} else {
			cps->notify = tmp1;
		}
	} else {
		if (notify) {
			cps->msglog(LDMSD_LERROR, "%s: notify "
				"must be specificed correctly. "
				"got instead %s\n", cps->pname,
				notify ) ;
			rc = EINVAL;
		}
	}
	if (!rc)
		rc = parse_bool(cps, avl, "notify_isfifo",
			&(cps->notify_isfifo));

	return rc;
}

void close_store_common(struct csv_store_handle_common *s_handle, struct csv_plugin_static *cps) {
	if (!s_handle || !cps) {
		cps->msglog(LDMSD_LERROR,
			"%s: close_store_common with null argument\n",
			cps->pname);
		return;
	}

	notify_output(NOTE_CLOSE, s_handle->filename, NOTE_DAT, s_handle, cps,
			        s_handle->container, s_handle->schema);
	notify_output(NOTE_CLOSE, s_handle->headerfilename, NOTE_HDR, s_handle, cps, s_handle->container, s_handle->schema);
	replace_string(&(s_handle->filename), NULL);
	replace_string(&(s_handle->headerfilename),  NULL);
	/* free(s_handle->notify); skip. handle notify is always copy of pg or sk notify or null */
	s_handle->notify = NULL;

}

void print_csv_plugin_common(struct csv_plugin_static *cps)
{
	cps->msglog(LDMSD_LALL, "notify: %s\n", cps->notify);
	cps->msglog(LDMSD_LALL, "notify is fifo: %s\n", cps->notify_isfifo ?
		"true" : "false");
	cps->msglog(LDMSD_LALL, "onp: %p\n", cps->onp);
}

void print_csv_store_handle_common(struct csv_store_handle_common *h, struct csv_plugin_static *p)
{
	if (!p)
		return;
	if (!h) {
		p->msglog(LDMSD_LALL, "csv store handle dump: NULL handle.\n");
		return;
	}
	p->msglog(LDMSD_LALL, "csv store handle dump:\n");
	p->msglog(LDMSD_LALL, "filename: %s\n", h->filename);
	p->msglog(LDMSD_LALL, "headerfilename: %s\n", h->headerfilename);
	p->msglog(LDMSD_LALL, "notify:%s\n", h->notify);
	p->msglog(LDMSD_LALL, "notify_isfifo:%s\n", h->notify_isfifo ?
			                "true" : "false");
	p->msglog(LDMSD_LALL, "onp: %p\n", h->onp);
}
