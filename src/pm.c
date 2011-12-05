/*
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "papi.h"

#define FLOPS 1000000
#define THRESHOLD 100000
#define ERROR_RETURN(retval) { fprintf(stderr, "Error %d %s:line %d: \n", retval,__FILE__,__LINE__);  exit(retval); }

int main(int argc, char *argv[])
{
	int retval, i;
	int EventSet = PAPI_NULL, count = 0, err_count = 0;
	long long values;
	PAPI_event_info_t info;
	char errstring[PAPI_MAX_STR_LEN];


	retval = PAPI_library_init( PAPI_VER_CURRENT );
	if ( retval != PAPI_VER_CURRENT )
		exit(__LINE__);

	retval = PAPI_create_eventset( &EventSet );
	if ( retval != PAPI_OK )
		exit(__LINE__);

	for ( i = 0; i < PAPI_MAX_PRESET_EVENTS; i++ ) {
		if ( PAPI_get_event_info( PAPI_PRESET_MASK | i, &info ) != PAPI_OK )
			continue;
		if ( !( info.count ) )
			continue;
		printf( "Preset %-14s\n", info.symbol );
#if 0
		retval = PAPI_add_event( EventSet, ( int ) info.event_code );
		if ( retval != PAPI_OK ) {
			PAPI_perror( retval, errstring, PAPI_MAX_STR_LEN );
			fprintf( stdout, "Error: %s\n", errstring );
			err_count++;
		}
#endif
	}

	for ( i = 0; i < 4096; i++ ) {
		if ( PAPI_get_event_info( PAPI_NATIVE_MASK | i, &info ) != PAPI_OK )
			continue;
#if 0
		if ( !( info.count ) )
			continue;
#endif
		printf( "Native[%d] %-14s\n", i, info.symbol );
#if 0
		retval = PAPI_add_event( EventSet, ( int ) info.event_code );
		if ( retval != PAPI_OK ) {
			PAPI_perror( retval, errstring, PAPI_MAX_STR_LEN );
			fprintf( stdout, "Error: %s\n", errstring );
			err_count++;
		}
#endif
	}
#if 0
	retval = PAPI_start( EventSet );
	if ( retval != PAPI_OK ) {
		PAPI_perror( retval, errstring, PAPI_MAX_STR_LEN );
		fprintf( stdout, "Error Starting: %s\n", errstring );
		err_count++;
	} else {
		retval = PAPI_stop( EventSet, &values );
		if ( retval != PAPI_OK ) {
			PAPI_perror( retval, errstring, PAPI_MAX_STR_LEN );
			fprintf( stdout, "Error Stopping: %s\n", errstring );
			err_count++;
		} else {
			printf( "successful\n" );
			count++;
		}
	}
	retval = PAPI_remove_event( EventSet, ( int ) info.event_code );
	if ( retval != PAPI_OK )
		exit(__LINE__);
#endif
	retval = PAPI_destroy_eventset( &EventSet );
	if ( retval != PAPI_OK )
		exit(__LINE__);
}


