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
 * \file bcommon.h
 * \author Narate Taerat (narate@ogc.us)
 * \defgroup bcommon Baler Common Header.
 * \{
 */
#ifndef __BCOMMON_H
#define __BCOMMON_H

#include <string.h>
#include <stdint.h>

#define BTKN_SYMBOL_STR  ",.:;`'\"<>\\/|[]{}()+-*=~!@#$%^&?_"

/**
 * \brief Baler's Offset to pointer macro.
 * \param relm Relative memory address.
 * \param off Offset value.
 * \return The pointer ((void*)relm)+off if \a off > 0.
 * \return NULL if \a off <= 0.
 */
#define BPTR(relm, off) ((off<=0)?(NULL):((((void*)relm)+(off))))

/**
 * \brief Baler's Pointer to Offset macro.
 * \param relm Relative memory address.
 * \param ptr The pointer.
 * \return Offset (in bytes).
 */
#define BOFF(relm, ptr) (((void*)ptr) - ((void*)relm))

/* ENUM with STRING helper */
#define BENUM(PREFIX, NAME) PREFIX ## NAME
#define BENUM_STR(PREFIX, NAME) #PREFIX #NAME

/*
 * Usage Example:
 * #define MY_ENUM_LIST(PREFIX, MACRO) \
 * 	MACRO(PREFIX, ONE), \
 * 	MACRO(PREFIX, TWO), \
 * 	MACRO(PREFIX, THREE),
 *
 * enum my_enum {
 * 	MY_ENUM_LIST(MY_ENUM_, BENUM)
 * };
 *
 * char *my_enum_str[] = {
 * 	MY_ENUM_LIST(, BENUM_STR) //PREFIX can be empty too
 * };
 *
 * This code will get expanded into the following:
 *
 * enum my_enum {
 * 	MY_ENUM_ONE,
 * 	MY_ENUM_TWO,
 * 	MY_ENUM_THREE,
 * };
 *
 * char *my_enum_str[] = {
 * 	"ONE",
 * 	"TWO",
 * 	"THREE",
 * };
 *
 */

/**
 * Find \c key in \c table and returns the index.
 *
 * '''IMPORTANT''' The strings in this function are compared with character case
 * ignored.
 * \param table Array of char*.
 * \param table_len The number of strings in the table.
 * \param key The element to look for.
 * \returns index of \c key in \c table.
 * \returns -1 if not found.
 */
static
int bget_str_idx(const char *table[], int table_len, const char *key)
{
	int i;
	for (i=0; i<table_len; i++) {
		if (strcasecmp(table[i], key) == 0)
			return i;
	}
	return table_len;
}

/**\}*/
#endif /* __BCOMMON_H */
