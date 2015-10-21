/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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

#include "olog.h"
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{

	ovis_log_level_set(OL_CRITICAL);
	olog(OL_INFO, "this is a bogus log level\n");
	ovis_log_level_set(OL_DEBUG);
	olog(23, "this is a bogus log level\n");
	olog(OL_CRITICAL, "olog called before init\n");
	oldebug("test debug\n");
	olinfo("test info\n");
	olwarn("test warning\n");
	olerr("test error\n");
	olcrit("test critical\n");
	oluser("test user\n");

	int rc = ovis_log_init(argv[0], "/root/olog_test.log","debug");
	if (!rc) {
		printf("FAIL: /root init test succeeded. are you root?\n");
	} else {
		printf("PASS: /root init failed as expected.\n");
	}

	ovis_log_final();
	rc = ovis_log_init(argv[0], "./ologtest.log","debug");
	if (!rc) {
		printf("PASS: local init test succeeded.\n");
		printf("See ./ologtest.log\n");
	} else {
		printf("FAIL: local init failed. %d\n",rc);
		printf("Run in a directory with write permission\n");
		exit(1);
	}
	
	ovis_loglevels_t lev;
	for (lev = OL_NONE; lev < OL_ENDLEVEL; lev++) {
		olog(lev, "test level %d %s\n",(int)lev, ol_to_string(lev));
	}
	oldebug("test debug\n");
	olinfo("test info\n");
	olwarn("test warning\n");
	olerr("test error\n");
	olcrit("test critical\n");
	oluser("test user\n");

	const char *names[] = {
	"debug","info","warn","error","critical","user","always","quiet" };
	size_t i = 0;
	for (i = 0; i < sizeof(names)/sizeof(char *); i++) {
		ovis_loglevels_t lev = ol_to_level(names[i]);
		printf("PASS: got level %d(%s) from %s\n",(int)lev,
			ol_to_string(lev), names[i]);
	}

	for (lev = OL_NONE; lev < OL_ENDLEVEL; lev++) {
		int sl = ol_to_syslog(lev);
		printf("PASS: Got syslog %d from level %s\n",
			sl,ol_to_string(lev));
	}
	int eno = 0;
#define TOPENO 256 /* hp uses high values with gaps sometimes. */
	for (eno = 0; eno < TOPENO; eno++) {
		printf("%d: %s\n", eno, ovis_rcname(eno));
	}

	ovis_log_final();
	oluser("FAIL: We should never see this\n");

	return 0;
}
