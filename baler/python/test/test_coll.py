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

logger = logging.getLogger(__name__)

class TestMapper(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.m = abhttp.Mapper([(1, "One.")])
        pass

    @classmethod
    def tearDownClass(self):
        # do nothing
        pass

    def test_batch_add(self):
        m = self.m
        data = [
            (128, "This is one."),
            (129, "This is two."),
        ]
        m += data
        for (_id, _str) in data:
            idx = m.get_id(_str)
            strx = m.get_str(_id)
            self.assertEqual(_id, idx, "wrong ID")
            self.assertEqual(_str, strx, "wrong ID")

    def test_add_new(self):
        m = self.m
        _id = 130
        _str = "This is three."
        m.add(_id, _str)
        _idx = m.get_id(_str)
        _strx = m.get_str(_id)
        self.assertEqual(_id, _idx, "wrong ID")
        self.assertEqual(_str, _strx, "wrong string")
        pass

    class OnAdd(object):
        def __init__(self):
            self._on_add_call = 0

        def adding(self, _id, _str):
            self._id = _id
            self._str = _str

        def onAdd(self, _id, _str, arg):
            (t, a) = arg
            self._on_add_call += 1
            t.assertEqual(self._id, _id)
            t.assertEqual(self._str, _str)
            t.assertEqual(a, self)

    def _on_add(self, _id, _str, arg):
        self._on_add_call += 1
        self.assertEqual(self._id, _id)
        self.assertEqual(self._str, _str)
        self.assertEqual(self, arg)

    def test_get_ent(self):
        m = self.m
        e = m.get_ent(1, "New one.")
        self.assertNotEqual(e, None)

    def test_add_existed(self):
        m = self.m
        _id = 1
        _str = "New one."
        _idx = 155
        _strx = "One."
        with self.assertRaises(KeyError):
            m.add(_id, _str)
        with self.assertRaises(KeyError):
            m.add(_idx, _strx)

    def test_iterator(self):
        m = self.m
        n = len(m._id_ent)
        _n = 0
        for e in m:
            _n += 1
            _idx = m.get_id(e.str)
            _strx = m.get_str(e.id)
            self.assertEqual(e.id, _idx)
            self.assertEqual(e.str, _strx)
        self.assertEqual(n, _n)


class HuHa(abhttp.Slots):
    __slots__ = ['x', 'y']

    def __init__(self, x, y):
        self.x = x
        self.y = y


class TestUnifiedMapper(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        m0 = abhttp.Mapper()
        m1 = abhttp.Mapper()
        m0.batch_add([
            (1, "One."),
            (2, "Two."),
            (3, "Three."),
            (4, "Four."),
        ])
        m1.batch_add([
            (1, "One."),
            (2, "Two."),
            (33, "Three."),
            (5, "Five."),
        ])
        um = abhttp.UnifiedMapper([
                    ("zero", m0),
                    ("one", m1)
                ])
        self.um = um
        self.m0 = m0
        self.m1 = m1

    def test_translate(self):
        um = self.um
        self.assertEqual(1, um.translate_id("zero", "one", 1))
        self.assertEqual(2, um.translate_id("zero", "one", 2))
        self.assertEqual(33, um.translate_id("zero", "one", 3))

    def test_translate_none(self):
        um = self.um
        self.assertEqual(None, um.translate_id("zero", "one", 4))
        self.assertEqual(None, um.translate_id("zero", "one", 5))

    def test_assignment(self):
        um = self.um
        ua = um.get_unassigned()
        cset0 = set(x for x in self.m0._str_ent)
        cset1 = set(x for x in self.m1._str_ent)
        self.assertEqual(ua, cset0|cset1)
        um.auto_assign()
        ua = um.get_unassigned()
        self.assertEqual(len(ua), 0)
        um.assign(100, "One Hundred.")
        um.assign(100, "One Hundred.") # repeated assignment should be fine.
        with self.assertRaises(KeyError):
            um.assign(100, "Another One Hundred.")
        self.um.update_mapper("zero", [(99, "Ninety Nine.", None)])
        tmp = {}
        strset = set()
        for e in um:
            _id = e.id
            _str = e.str
            self.assertFalse(_id in tmp)
            self.assertFalse(_str in strset)
            if _id != None:
                tmp[_id] = _str
            else:
                self.assertEqual(_str, "Ninety Nine.")
            strset.add(_str)
        cmpset = set()
        cmpset.add("One Hundred.")
        for _str in self.m0._str_ent:
            cmpset.add(_str)
        for _str in self.m1._str_ent:
            cmpset.add(_str)
        self.assertEqual(strset, cmpset)

    def test_remove(self):
        um = self.um
        s0 = set((e.id, e.str) for e in um)
        ua0 = um.get_unassigned()
        m3 = abhttp.Mapper([
                (500, "Five Hundred."),
                (600, "Six Hundred."),
            ])
        um.add_mapper("extra", m3)
        ua1 = um.get_unassigned()
        self.assertTrue("Five Hundred." in ua1)
        self.assertTrue("Six Hundred." in ua1)
        um.remove_mapper("extra")
        ua2 = um.get_unassigned()
        self.assertEqual(ua0, ua2)
        s1 = set((e.id, e.str) for e in um)
        self.assertEqual(s0, s1)

    def test_save_load(self):
        # setup the unified map so that we have some assigned, some unassigned,
        # and some having objects
        um = abhttp.UnifiedMapper([
                ("a", abhttp.Mapper([
                        (1, "One"),
                        (2, "Two"),
                ])),
                ("b", abhttp.Mapper([
                        (3, "Three"),
                        (4, "Four"),
                ])),
        ])
        um.assign(1, "One")
        um.assign(4, "Four")
        um._umapper.set_obj(_id=4, _obj=HuHa(7,8))

        um.save("tmp/um.yaml")
        _um = abhttp.UnifiedMapper()
        _um.load("tmp/um.yaml")
        s0 = [x for x in um]
        s1 = [y for y in _um]
        s0.sort()
        s1.sort()
        logger.info("s0: %s", s0)
        logger.info("s1: %s", s1)
        self.assertEqual(s0, s1)

    @classmethod
    def tearDownClass(self):
        pass


if __name__ == "__main__":
    LOGFMT = '%(asctime)s %(name)s %(levelname)s: %(message)s'
    logging.basicConfig(format=LOGFMT)
    logger.setLevel(logging.INFO)
    unittest.main()
