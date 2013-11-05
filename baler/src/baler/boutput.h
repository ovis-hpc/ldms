/**
 * \file boutput.h
 * \author Narate Taerat (narate@ogc.us)
 *
 * \defgroup boutput Baler Output Plugin
 * \{
 * This module contains an interface and definitions for implementing Baler
 * Output Plugin. Similar to implementing Baler Input Plugin, the Output Plugin
 * needs to implement ::create_plugin_instance() function that returns an
 * instance of ::boutplugin (which is an extension of ::bplugin). The life cycle
 * of the output plugin is the same as input plugin (create -> config -> start
 * -> EVENT_LOOP -> stop -> free).
 */
#ifndef __BOUTPUT_H
#define __BOUTPUT_H

#include "bplugin.h"
#include "bwqueue.h"

/*
 * Real definition is defined afterwards.
 */
struct boutplugin;

/**
 * Baler Output Plugin interface structure.
 */
struct boutplugin {
	struct bplugin base;

	/**
	 * \brief Process output function.
	 *
	 * This function will be called when an output from Baler Daemon Core is
	 * ready.  Please note that the calls can be overlapped and
	 * asynchronous. The output plugin implementing this function should
	 * guard this function if necessary.
	 *
	 * \return 0 if success
	 * \return Error code if fail
	 * \param this The plugin instance.
	 * \param odata The output data from Baler core.
	 */
	int (*process_output)(struct boutplugin *this, struct boutq_data *odata);
};

/**
 * Output plugins can get balerd's store path from this function.
 * \return store path.
 */
const char *bget_store_path();

/**
 * Set global store path.
 *
 * This function will copy the input \c path to the global \c store_path.
 *
 * \param path The path.
 * \return 0 on success.
 * \return Error code on error.
 */
int bset_store_path(const char *path);

#endif /* __BOUTPUT_H */

/**\}*/
