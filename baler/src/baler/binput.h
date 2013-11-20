/**
 * \file binput.h
 * \author Narate Taerat (narate@ogc.us)
 *
 * \defgroup binput Baler Input Plugin Interface
 * \{
 * Input Plugin is essentially a bare regular plugin. The baler daemon core
 * expects an input plugin to run almost independently and receive input
 * messages from its input sources (yes, one input plugin instance should be
 * able to handle multiple input sources of the same kind). Once the plugin is
 * done first-round tokenizing, it should post the tokens into the input queue
 * ::binq using function ::binq_post. For more information about generic Baler
 * Plugin, please see \ref bplugin.
 */
#ifndef __BINPUT_H
#define __BINPUT_H

#include "btypes.h"
#include "bwqueue.h"
#include "bplugin.h"
#include <time.h>
#include <sys/queue.h>
#include <sys/times.h>
#include <sys/time.h>
#include <string.h>

/**
 * Post input entry \a e  to the input queue.
 * This function must be called when the input plugin finishes tokenizing the
 * message.
 * \note Once this function is called, it will own the input entry \a e, and
 * all of the tokens in it. The caller should not reuse any of the input entry
 * or tokens in it.
 * \param e The Baler Work Queue Entry (::bwq_entry)
 * \return 0 on success.
 * \return -1 on error.
 */
int binq_post(struct bwq_entry *e); /* binq_post impl. is in balerd.c */

#endif // __BINPUT_H
/**\}*/
