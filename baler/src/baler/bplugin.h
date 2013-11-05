/**
 * \file bplugin.h
 * \author Narate Taerat (narate@ogc.us)
 * \defgroup bplugin Baler Generic Plugin Interface
 * \{
 * Generic Plugin Interface and some utilities.
 * In general, a plugin must implement "create_plugin_instance" function, and
 * the main program will invoke "create_plugin_instance" function to create
 * plugin instance. The function should create, initialize and return the newly
 * created and initialized plugin instance.
 *
 * The plugin instance will be configured, throgh
 * ::bplugin::config, and started through ::bplugin::start.
 *
 * A life cycle of a plugin is as follows:
 * create -> config -> start -> [EVENT_LOOP] -> (on_exit: stop && free).
 * For the Input Plugin, Baler Daemon Core does not do any call back to the
 * plugin in the EVENT_LOOP period. For the Output Plugin, Baler Daemon Core
 * will callback to the ::boutplugin::process_output() during EVENT_LOOP period
 * when there is an output from the core ready to get processed.
 */
#ifndef __BPLUGINS_H
#define __BPLUGINS_H

#include "btypes.h"
#include <sys/queue.h>

struct bplugin; /* Real definition is declared later in this file */

/**
 * Baler Plugin Instance Interface.
 */
struct bplugin {
	LIST_ENTRY(bplugin) link; /**< Link to be used in linked list of
					plugins.*/
	char *name; /**< Plugin name */
	char *version; /**< Plugin version */
	void *context; /**< Context of the instance */
	/**
	 * Configuration function. The configuration function might be called
	 * multiple times to configure the instance.
	 *
	 * \param this The pointer to ::bplugin instance.
	 * \param arg_head The head of the argument list.
	 *
	 * \return The function should return 0 on success and appropriate errno
	 * 	on error.
	 */
	int (*config)(struct bplugin *this, struct bpair_str_head *arg_head);

	/**
	 * Start function. The start function should start the routine of the
	 * plugin.
	 *
	 * \param this The pointer to the instance of ::bplugin.
	 *
	 * \return The function should return 0 on success and appriproate errno
	 * 	on error.
	 */
	int (*start)(struct bplugin *this);

	/**
	 * Stop function. This function will be called to stop the routine of
	 * the plugin.
	 *
	 * \param this The ::bplugin instance.
	 *
	 * \return 0 on success.
	 * \return Error number on error.
	 */
	int (*stop)(struct bplugin *this);

	/**
	 * Free function. This function will be called to free the plugin
	 * instance \a this. It is not guaranteed that ::bplugin::stop will be
	 * called before this function. So, the plugin may need to invoke stop
	 * from this function.
	 *
	 * \param this The ::bplugin instance.
	 *
	 * \return 0 on success.
	 * \return Error number on error.
	 */
	int (*free)(struct bplugin *this);
};

/**
 * Generic, convenient free function for ::bplugin.
 * All it does is only freeing the resources pointed by members of ::bplugin and
 * the plugin \a p iteslf.
 * \param p The ::bplugin to be freed.
 */
static
void bplugin_free(struct bplugin *p)
{
	if (p->name)
		free(p->name);
	if (p->version)
		free(p->version);
	if (p->context)
		free(p->context);
	free(p);
}

#endif /* __BPLUGINS_H */
/**\}*/
