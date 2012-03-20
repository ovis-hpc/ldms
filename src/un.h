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
#ifndef _UN_H
#define _UH_H

/* These are helper functions for writing LDMS metric data providers */

extern void un_set_quiet(int q);
extern void un_say(const char *fmt, ...);
extern int un_send_req(int sock, struct sockaddr *sa, ssize_t sa_len,
		       char *data, ssize_t data_len);
extern int un_recv_rsp(int sock, struct sockaddr *sa, ssize_t sa_len,
		       char *data, ssize_t data_len);
extern int un_connect(char *my_name, char *sockname);
extern int un_def_set(char *set_name, ssize_t set_size);
extern int un_rem_set(int set_no);
extern int un_def_metric(int set_no, char *metric_name, char *metric_type);
extern uint64_t un_set_u8(int set_no, int metric_no, uint8_t u8);
extern uint64_t un_set_s8(int set_no, int metric_no, int8_t s8);
extern uint64_t un_set_u16(int set_no, int metric_no, uint16_t u16);
extern uint64_t un_set_s16(int set_no, int metric_no, int16_t s16);
extern uint64_t un_set_u32(int set_no, int metric_no, uint32_t u32);
extern uint64_t un_set_s32(int set_no, int metric_no, int32_t s32);
extern uint64_t un_set_u64(int set_no, int metric_no, uint64_t u64);
extern uint64_t un_set_s64(int set_no, int metric_no, int32_t s64);

extern int un_ls_plugins(char *, size_t);
extern int un_load_plugin(char *plugin, char *err_str);
extern int un_init_plugin(char *plugin, char *set_name, char *err_str);
extern int un_term_plugin(char *plugin, char *err_str);
extern int un_start_plugin(char *plugin, unsigned long period, char *err_str);
extern int un_stop_plugin(char *plugin, char *err_str);
extern int un_stop_plugin(char *plugin, char *err_str);
extern int un_config_plugin(char *plugin, char *str, char *err_str);
extern void un_close(void);
#endif
