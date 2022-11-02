
/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MIT
 *
 * Portions created by Nichamon Naksinehaboon Copyright (c) 2023
 * Sandia National Laboratories and Open Grid Computing. All Rights Reserved.
 *
 * Portions created by Benjamin Allan are Copyright (c) 2015
 * Sandia National Laboratories and Open Grid Computing. All Rights Reserved.
 *
 * Portions created by Alan Antonuk are Copyright (c) 2012-2013
 * Alan Antonuk. All Rights Reserved.
 *
 * Portions created by VMware are Copyright (c) 2007-2012 VMware, Inc.
 * All Rights Reserved.
 *
 * Portions created by Tony Garnock-Jones are Copyright (c) 2009-2010
 * VMware, Inc. and Tony Garnock-Jones. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * ***** END LICENSE BLOCK *****
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <stdint.h>
#include <amqp.h>
#include <amqp_framing.h>

#include "ldms.h"
#include "rabbit_utils.h"

#include <time.h>
#include <sys/time.h>
#include <unistd.h>

static ovis_log_t pi_log;

void rabbit_store_pi_log_set(ovis_log_t _pi_log)
{
	pi_log = _pi_log;
}

void lrmq_die(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

int lrmq_die_on_error(int x, char const *context)
{
  if (x < 0) {
    ovis_log(pi_log, OVIS_LERROR, "%s: %s\n", context, amqp_error_string2(x));
  }
  return x;
}

int lrmq_die_on_amqp_error(amqp_rpc_reply_t x, char const *context)
{
  switch (x.reply_type) {
  case AMQP_RESPONSE_NORMAL:
    return 0;

  case AMQP_RESPONSE_NONE:
    ovis_log(pi_log, OVIS_LERROR, "%s: missing RPC reply type!\n", context);
    break;

  case AMQP_RESPONSE_LIBRARY_EXCEPTION:
    ovis_log(pi_log, OVIS_LERROR, "%s: %s\n", context, amqp_error_string2(x.library_error));
    break;

  case AMQP_RESPONSE_SERVER_EXCEPTION:
    switch (x.reply.id) {
    case AMQP_CONNECTION_CLOSE_METHOD: {
      amqp_connection_close_t *m = (amqp_connection_close_t *) x.reply.decoded;
      ovis_log(pi_log, OVIS_LERROR, "%s: server connection error %d, message: %.*s\n",
              context,
              m->reply_code,
              (int) m->reply_text.len, (char *) m->reply_text.bytes);
      break;
    }
    case AMQP_CHANNEL_CLOSE_METHOD: {
      amqp_channel_close_t *m = (amqp_channel_close_t *) x.reply.decoded;
      ovis_log(pi_log, OVIS_LERROR, "%s: server channel error %d, message: %.*s\n",
              context,
              m->reply_code,
              (int) m->reply_text.len, (char *) m->reply_text.bytes);
      break;
    }
    default:
      ovis_log(pi_log, OVIS_LERROR, "%s: unknown server error, method id 0x%08X\n", context, x.reply.id);
      break;
    }
    break;
  }

  return -1;
}

static void dump_row(long count, int numinrow, int *chs)
{
  int i;

  ovis_log(pi_log, OVIS_LINFO,"%08lX:", count - numinrow);

  if (numinrow > 0) {
    for (i = 0; i < numinrow; i++) {
      if (i == 8) {
        ovis_log(pi_log, OVIS_LINFO," :");
      }
      ovis_log(pi_log, OVIS_LINFO," %02X", chs[i]);
    }
    for (i = numinrow; i < 16; i++) {
      if (i == 8) {
        ovis_log(pi_log, OVIS_LINFO," :");
      }
      ovis_log(pi_log, OVIS_LINFO,"   ");
    }
    ovis_log(pi_log, OVIS_LINFO,"  ");
    for (i = 0; i < numinrow; i++) {
      if (isprint(chs[i])) {
        ovis_log(pi_log, OVIS_LINFO,"%c", chs[i]);
      } else {
        ovis_log(pi_log, OVIS_LINFO,".");
      }
    }
  }
  ovis_log(pi_log, OVIS_LINFO,"\n");
}

static int rows_eq(int *a, int *b)
{
  int i;

  for (i=0; i<16; i++)
    if (a[i] != b[i]) {
      return 0;
    }

  return 1;
}

void lrmq_amqp_dump(void const *buffer, size_t len)
{
  unsigned char *buf = (unsigned char *) buffer;
  long count = 0;
  int numinrow = 0;
  int chs[16];
  int oldchs[16] = {0};
  int showed_dots = 0;
  size_t i;

  for (i = 0; i < len; i++) {
    int ch = buf[i];

    if (numinrow == 16) {
      int i;

      if (rows_eq(oldchs, chs)) {
        if (!showed_dots) {
          showed_dots = 1;
          ovis_log(pi_log, OVIS_LINFO,"          .. .. .. .. .. .. .. .. : .. .. .. .. .. .. .. ..\n");
        }
      } else {
        showed_dots = 0;
        dump_row(count, numinrow, chs);
      }

      for (i=0; i<16; i++) {
        oldchs[i] = chs[i];
      }

      numinrow = 0;
    }

    count++;
    chs[numinrow++] = ch;
  }

  dump_row(count, numinrow, chs);

  if (numinrow != 0) {
    ovis_log(pi_log, OVIS_LINFO,"%08lX:\n", count);
  }
}


uint64_t lrmq_now_microseconds(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t) tv.tv_sec * 1000000 + (uint64_t) tv.tv_usec;
}

void lrmq_microsleep(int usec)
{
  struct timespec req;
  req.tv_sec = 0;
  req.tv_nsec = 1000 * usec;
  nanosleep(&req, NULL);
}
