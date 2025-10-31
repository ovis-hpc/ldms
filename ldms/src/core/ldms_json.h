/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2025 Open Grid Computing, Inc. All rights reserved.
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
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
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
  * \brief Format an LDMS metric set as a JSON string
  *
  * Format the specified metric set \c, as a JSON string
  * and return this value as a NUL terminated character string.
  * The buffer returned must be freed by the caller when the
  * memory is no longer required.
  *
  * If there is an error formatting the set, a NULL ptr is
  * returned and \c errno indicates the error.
  *
  * \param s The LDMS metric set handle
  * \param fmt A JSON output format specifier. Currently igored.
  *
  * \return A character string containing the LDMS metric set
  *         formatted as a JSON string
  * \return NULL if there is a memory allocation error.
  */
char *ldms_set_as_json_string(ldms_set_t s, char *fmt);

/**
  * \brief Print an LDMS metric set to a file
  *
  * Format the specified metric set \c, as a JSON string
  * and print this data to the specified file pointer \c fp.
  * The function returns the number of bytes written to the file.
  *
  * If there is an error formatting the set or writing to the
  * file the return value will be < 0 and \c the error code
  * contained in \c errno.
  *
  * \param fp The FILE handle
  * \param s The LDMS metric set handle
  * \param fmt A JSON output format specifier. Currently igored.
  *
  * \return The number of bytes written to \c fp. If the value
  *         returned is < 0, consult \c errno for the reason.
  */
size_t ldms_fprint_set_as_json(FILE *fp, ldms_set_t s, char *fmt);
