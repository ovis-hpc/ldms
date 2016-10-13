#!/usr/bin/env python

# Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2016 Sandia Corporation. All rights reserved.
#
# Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
# license for use of this work by or on behalf of the U.S. Government.
# Export of this program may require a license from the United States
# Government.
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the BSD-type
# license below:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#      Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#      Redistributions in binary form must reproduce the above
#      copyright notice, this list of conditions and the following
#      disclaimer in the documentation and/or other materials provided
#      with the distribution.
#
#      Neither the name of Sandia nor the names of any contributors may
#      be used to endorse or promote products derived from this software
#      without specific prior written permission.
#
#      Neither the name of Open Grid Computing nor the names of any
#      contributors may be used to endorse or promote products derived
#      from this software without specific prior written permission.
#
#      Modified source versions must be plainly marked as such, and
#      must not be misrepresented as being the original software.
#
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import logging
import unittest
import abhttp
import time

logger = logging.getLogger(__name__)

class TestTimestamp(unittest.TestCase):
    def test_format(self):
        sec = 1465862400
        ts0 = abhttp.Timestamp(sec, 0)
        s0 = str(ts0)
        self.assertEqual(s0, "2016-06-13T19:00:00.000000-05:00")

    def test_parse(self):
        ts = abhttp.Timestamp.fromStr("2016-06-13T19:00:00.000000-05:00")
        self.assertEqual(ts.sec, 1465862400)


class TestLogMessage(unittest.TestCase):
    def setUp(self):
        self.msgs = [
            abhttp.LogMessage(ts=abhttp.Timestamp(0, 0), host="host0", msg="text0", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(0, 0), host="host0", msg="text1", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(0, 0), host="host1", msg="text0", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(0, 0), host="host1", msg="text1", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(0, 1), host="host0", msg="text0", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(0, 1), host="host0", msg="text1", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(0, 1), host="host1", msg="text0", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(0, 1), host="host1", msg="text1", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(1, 0), host="host0", msg="text0", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(1, 0), host="host0", msg="text1", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(1, 0), host="host1", msg="text0", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(1, 0), host="host1", msg="text1", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(1, 1), host="host0", msg="text0", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(1, 1), host="host0", msg="text1", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(1, 1), host="host1", msg="text0", pos=None),
            abhttp.LogMessage(ts=abhttp.Timestamp(1, 1), host="host1", msg="text1", pos=None),
        ]

    def test_lt(self):
        for i in range(len(self.msgs)-1):
            a = self.msgs[i]
            b = self.msgs[i+1]
            self.assertEqual(a < b, True, "incorrect '<' operation")
            self.assertEqual(b < a, False, "incorrect '<' operation")

    def test_gt(self):
        for i in range(len(self.msgs)-1):
            a = self.msgs[i]
            b = self.msgs[i+1]
            self.assertEqual(b > a, True, "incorrect '>' operation")
            self.assertEqual(a > b, False, "incorrect '>' operation")

    def test_le(self):
        for i in range(len(self.msgs)-1):
            a = self.msgs[i]
            b = self.msgs[i+1]
            self.assertEqual(a <= b, True, "incorrect '<=' operation")
            self.assertEqual(a <= a, True, "incorrect '==' operation")
            self.assertEqual(b <= a, False, "incorrect '<=' operation")

    def test_ge(self):
        for i in range(len(self.msgs)-1):
            a = self.msgs[i]
            b = self.msgs[i+1]
            self.assertEqual(b >= a, True, "incorrect '>=' operation")
            self.assertEqual(a >= a, True, "incorrect '==' operation")
            self.assertEqual(a >= b, False, "incorrect '>=' operation")

    def test_eq(self):
        for i in range(len(self.msgs)-1):
            a = self.msgs[i]
            b = self.msgs[i+1]
            self.assertEqual(a == a, True, "incorrect '==' operation")
            self.assertEqual(a == b, False, "incorrect '==' operation")

    def test_ne(self):
        for i in range(len(self.msgs)-1):
            a = self.msgs[i]
            b = self.msgs[i+1]
            self.assertEqual(a != a, False, "incorrect '==' operation")
            self.assertEqual(a != b, True, "incorrect '==' operation")

    def test_print(self):
            msg = abhttp.LogMessage(ts=abhttp.Timestamp(0, 0),
                                    host="host0", msg="text0", pos=None)
            logger.warn("msg: %s", msg)


class TestPixel(unittest.TestCase):
    def setUp(self):
        self.pixels = [
            abhttp.Pixel(ptn_id=0, sec=0, comp_id=0, count=0),
            abhttp.Pixel(ptn_id=0, sec=0, comp_id=0, count=1),
            abhttp.Pixel(ptn_id=0, sec=0, comp_id=1, count=0),
            abhttp.Pixel(ptn_id=0, sec=0, comp_id=1, count=1),
            abhttp.Pixel(ptn_id=0, sec=1, comp_id=0, count=0),
            abhttp.Pixel(ptn_id=0, sec=1, comp_id=0, count=1),
            abhttp.Pixel(ptn_id=0, sec=1, comp_id=1, count=0),
            abhttp.Pixel(ptn_id=0, sec=1, comp_id=1, count=1),
            abhttp.Pixel(ptn_id=1, sec=0, comp_id=0, count=0),
            abhttp.Pixel(ptn_id=1, sec=0, comp_id=0, count=1),
            abhttp.Pixel(ptn_id=1, sec=0, comp_id=1, count=0),
            abhttp.Pixel(ptn_id=1, sec=0, comp_id=1, count=1),
            abhttp.Pixel(ptn_id=1, sec=1, comp_id=0, count=0),
            abhttp.Pixel(ptn_id=1, sec=1, comp_id=0, count=1),
            abhttp.Pixel(ptn_id=1, sec=1, comp_id=1, count=0),
            abhttp.Pixel(ptn_id=1, sec=1, comp_id=1, count=1),
        ]

        self.pixels2 = [
            abhttp.Pixel(ptn_id=0, sec=0, comp_id=0, count=0),
            abhttp.Pixel(ptn_id=0, sec=0, comp_id=0, count=1),
            abhttp.Pixel(ptn_id=0, sec=0, comp_id=1, count=0),
            abhttp.Pixel(ptn_id=0, sec=0, comp_id=1, count=1),
            abhttp.Pixel(ptn_id=0, sec=1, comp_id=0, count=0),
            abhttp.Pixel(ptn_id=0, sec=1, comp_id=0, count=1),
            abhttp.Pixel(ptn_id=0, sec=1, comp_id=1, count=0),
            abhttp.Pixel(ptn_id=0, sec=1, comp_id=1, count=1),
            abhttp.Pixel(ptn_id=1, sec=0, comp_id=0, count=0),
            abhttp.Pixel(ptn_id=1, sec=0, comp_id=0, count=1),
            abhttp.Pixel(ptn_id=1, sec=0, comp_id=1, count=0),
            abhttp.Pixel(ptn_id=1, sec=0, comp_id=1, count=1),
            abhttp.Pixel(ptn_id=1, sec=1, comp_id=0, count=0),
            abhttp.Pixel(ptn_id=1, sec=1, comp_id=0, count=1),
            abhttp.Pixel(ptn_id=1, sec=1, comp_id=1, count=0),
            abhttp.Pixel(ptn_id=1, sec=1, comp_id=1, count=1),
        ]

    def test_lt(self):
        for i in range(len(self.pixels)-1):
            a = self.pixels[i]
            b = self.pixels[i+1]
            self.assertLess(a, b)

    def test_gt(self):
        for i in range(len(self.pixels)-1):
            a = self.pixels[i]
            b = self.pixels[i+1]
            self.assertGreater(b, a)

    def test_le(self):
        for i in range(len(self.pixels)-1):
            a = self.pixels[i]
            a2 = self.pixels2[i]
            b = self.pixels[i+1]
            self.assertLessEqual(a, a2)
            self.assertLessEqual(a, b)

    def test_ge(self):
        for i in range(len(self.pixels)-1):
            a = self.pixels[i]
            a2 = self.pixels2[i]
            b = self.pixels[i+1]
            self.assertGreaterEqual(a2, a)
            self.assertGreaterEqual(b, a)

    def test_eq(self):
        for i in range(len(self.pixels)):
            a = self.pixels[i]
            a2 = self.pixels2[i]
            self.assertEqual(a, a2)

    def test_ne(self):
        for i in range(len(self.pixels)-1):
            a = self.pixels[i]
            b = self.pixels[i+1]
            self.assertNotEqual(a, b)

class TestPattern(unittest.TestCase):
    def test_merge(self):
        p0 = abhttp.Pattern(1, 2, "2015-01-01 00:00:00.000000",
                             "2015-01-01 00:00:01.000000", "abcdefg")
        p1 = abhttp.Pattern(1, 4, "2015-01-01 00:00:00.000500",
                             "2015-01-01 00:00:02.000000", "abcdefg")
        p2 = p0 + p1
        self.assertEqual(p2.ptn_id, p0.ptn_id)
        self.assertEqual(p2.first_seen, p0.first_seen)
        self.assertEqual(p2.last_seen, p1.last_seen)
        self.assertEqual(p2.count, p0.count + p1.count)
        self.assertEqual(p2.text, p0.text)
        self.assertEqual(p2.text, p1.text)

        p3 = abhttp.Pattern(1, 4, "2015-01-01 00:00:00.000500",
                             "2015-01-01 00:00:02.000000", "hijklmnop")

        with self.assertRaises(ValueError):
            p4 = p3 + p0

    def test_eq(self):
        p0 = abhttp.Pattern(1, 2, "2015-01-01 00:00:00.000000",
                             "2015-01-01 00:00:01.000000", "abcdefg")
        p1 = abhttp.Pattern(2, 2, "2015-01-01 00:00:00.000000",
                             "2015-01-01 00:00:01.000000", "abcdefg")
        self.assertFalse(p0 == p1)
        self.assertTrue(p0 != p1)
        self.assertFalse(p0 == None)
        self.assertFalse(None == p0)

    def test_merge_none(self):
        px = abhttp.Pattern(1, 2, "2015-01-01 00:00:00.000000",
                             "2015-01-01 00:00:01.000000", "abcdefg")
        p0 = abhttp.Pattern(1, 2, "2015-01-01 00:00:00.000000",
                             "2015-01-01 00:00:01.000000", "abcdefg")

        self.assertEqual(px, p0)

        p0 += None
        self.assertEqual(px, p0)

        p1 = None
        p1 += p0
        self.assertEqual(px, p1)

        p2 = p0 + None
        p3 = None + p0
        self.assertEqual(px, p2)
        self.assertEqual(px, p3)

        logger.warn("p0: %s", p0)
        logger.warn("p1: %s", p1)
        logger.warn("p2: %s", p2)
        logger.warn("p3: %s", p3)


class TestIDSet(unittest.TestCase):
    def test_tocsv(self):
        s = abhttp.IDSet()
        s.add_number(1)
        s.add_numbers(xrange(5,11))
        s.add_numbers(xrange(21,31))
        s.add_number(55)
        ss = s.to_csv()
        logger.warn("ss: %s", ss)
        self.assertEqual(ss, "1,5-10,21-30,55")

    def test_addcsv(self):
        t = abhttp.IDSet()
        t.add_csv("1,5-10,21-30")
        t.add_csv("55")
        s = abhttp.IDSet()
        s.add_number(1)
        s.add_numbers(xrange(5,11))
        s.add_numbers(xrange(21,31))
        s.add_number(55)
        self.assertEqual(s,t)

    def test_addsmart(self):
        t = abhttp.IDSet()
        t.add_smart(1)
        t.add_smart(xrange(5, 11))
        t.add_smart("21-30,55")
        t.add_csv("55")
        s = abhttp.IDSet()
        s.add_number(1)
        s.add_numbers(xrange(5,11))
        s.add_numbers(xrange(21,31))
        s.add_number(55)
        self.assertEqual(s,t)

    def test_constructor(self):
        t = abhttp.IDSet("1,5-10,21-30,55")
        s = abhttp.IDSet([1,xrange(5,11), xrange(21,31), 55])
        self.assertEqual(s,t)


if __name__ == "__main__":
    LOGFMT = '%(asctime)s %(name)s %(levelname)s: %(message)s'
    logging.basicConfig(format=LOGFMT)
    unittest.main()
