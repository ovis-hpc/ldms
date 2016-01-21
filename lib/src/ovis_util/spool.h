/* 
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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

#ifndef ovis_util_spool_h_seen
#define ovis_util_spool_h_seen

/** Fork a slave which listens for commands from us but will
 * not share any output related state.
 * The slave takes these commands and double-forks independent processes
 * that share no state with the calling daemon.
 *
 * This should be called before any output files are opened
 * or multithreading is started, thus avoiding all potential for
 * competing buffer flushes.
 *
 * This can only be called once, since presumably if the child 
 * dies later the restart will be after IO/threads are running.
 * \return 0 ok, or errno.
 */
extern int sheller_init();

/** Stop the sheller. No other sheller calls will work after this. */
extern int sheller_final();

/* Dispatch an argument list to the child spawner for use with execl.
 * Strings in argv may contain embedded spaces.
 * argv[0] should be the full path of an executable.
 * After sheller_final is called, calls will be ignored.
 * Strings in argv should be no longer than 131071 bytes.
 * \param argc length of argv which must be < 1024.
 * \param argv array with name of executable and its args.
 * \return 0 on success, ECHILD after final() or pipe failure, and ENODEV
 * 	before init().
 */
extern int sheller_call(int argc, const char **argv);

/**
 Spawn spoolexec with arguments outfile (which must be closed) and
 spooldir (which must exist).
 Spoolexec is detached. After this call, caller should never
 refer to outfile again for any purpose, including recreating it.

 The least effort call is ovis_file_spool("/bin/mv","datafile","/tmp");
 Generally, it's a good idea to use a script wrapper instead of mv to
 catch and log filesystem errors and manage permissions. 
 The function ignores NULL inputs, returning an error if receiving 
 inconsistently NULL inputs (e.g. spoolexec defined but spooldir not).

 It's a good idea that any files being spooled be flushed, synced,
 and closed before spooling or repeated output may occur from the 
 forked child. If sheller_init() is used before anything else in main,
 all these flush and sync issues cannot occur. If sheller_init is not
 called in main(), ovis_file_spool will do so at its first chance.

 After sheller_final is called, ovis_file_spool will do nothing.

 \param msgaddr address of a string pointer that will be reassigned
   with a string message if return is nonzero.
 \return errno values.
*/
extern int ovis_file_spool(const char *spoolexec, const char *outfile,
	const char *spooldir, const char **msgaddr);

#endif /* ovis_util_spool_h_seen */
