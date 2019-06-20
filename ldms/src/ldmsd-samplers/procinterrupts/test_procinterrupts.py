#!/usr/bin/env python

# Copyright (c) 2019 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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

import os
import re
import pdb
import sys
import time
import socket
import shutil
import logging
import unittest

from collections import namedtuple

from ldmsd.chroot import D, LDMSData, LDMSChrootTest, try_sudo, xvars, \
                         Src, SrcData

HOSTNAME = socket.gethostname()
LDMSD_PREFIX = HOSTNAME + ":" + str(LDMSChrootTest.PORT)
INST_NAME = LDMSD_PREFIX + "/test"
SCHEMA_NAME = "procinterrupts"
DIR = "test_procinterrupts" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

src = [
    # 1 Src() per source file
    Src("/proc/interrupts",
"""\
           CPU0       CPU1       CPU2       CPU3       CPU4       CPU5       CPU6       CPU7
  0:          9          0          0          0          0          0          0          0   IO-APIC   2-edge      timer
  8:          0          0          0          0          0          1          0          0   IO-APIC   8-edge      rtc0
  9:          0          4          0          0          0          0          0          0   IO-APIC   9-fasteoi   acpi
 16:          0          0         31          0          0          0          0          0   IO-APIC  16-fasteoi   ehci_hcd:usb1
 19:          0          0          0          0          0          0          0          0   IO-APIC  19-fasteoi   ath9k
 23:    2186688     429113          0      39853          0          0          0          0   IO-APIC  23-fasteoi   ehci_hcd:usb2
 24:          0          0          0          0          0          0          0          0   PCI-MSI 327680-edge      xhci_hcd
 25:          0          0          0          0          0       4180     227798          0   PCI-MSI 6815744-edge      xhci_hcd
 26:          0          0          0          0          0          0          0          0   PCI-MSI 6815745-edge      xhci_hcd
 27:          0          0          0          0          0          0          0          0   PCI-MSI 6815746-edge      xhci_hcd
 28:          0          0          0          0          0          0          0          0   PCI-MSI 6815747-edge      xhci_hcd
 29:          0          0          0          0          0          0          0          0   PCI-MSI 6815748-edge      xhci_hcd
 30:          0          0          0          0          0          0          0          0   PCI-MSI 6815749-edge      xhci_hcd
 31:          0          0          0          0          0          0          0          0   PCI-MSI 6815750-edge      xhci_hcd
 32:          0          0          0          0          0          0          0          0   PCI-MSI 6815751-edge      xhci_hcd
 33:          0          0          0          0     565764          0      16664          0   PCI-MSI 512000-edge      ahci[0000:00:1f.2]
 34:          0          0          0          0          0          0          0          0   PCI-MSI 7340032-edge      ahci[0000:0e:00.0]
 35:        343          0          0          0          0          0          0    1482895   PCI-MSI 409600-edge      eno1
 36:          0       4885          0          0          0    2400055          0          0   PCI-MSI 32768-edge      i915
 37:          0          0         13          0          0          0          0          0   PCI-MSI 360448-edge      mei_me
 38:          0          0          0          0       1235          0          0          0   PCI-MSI 442368-edge      snd_hda_intel:card0
NMI:        132        141        124        124        125        144        125        132   Non-maskable interrupts
LOC:    3988207    2613488    2562788    2043852    2180437    3826532    3084422    2936689   Local timer interrupts
SPU:          0          0          0          0          0          0          0          0   Spurious interrupts
PMI:        132        141        124        124        125        144        125        132   Performance monitoring interrupts
IWI:         25         19         49         28         12     321769         67         39   IRQ work interrupts
RTR:          0          0          0          0          0          0          0          0   APIC ICR read retries
RES:     447957     195661     170009      62647      59698      98636     115634      69047   Rescheduling interrupts
CAL:     610299     573181     567683     608060     585890     566966     570622     521710   Function call interrupts
TLB:     573702     577286     570469     611601     588059     568480     572863     524609   TLB shootdowns
TRM:          0          0          0          0          0          0          0          0   Thermal event interrupts
THR:          0          0          0          0          0          0          0          0   Threshold APIC interrupts
DFR:          0          0          0          0          0          0          0          0   Deferred Error APIC interrupts
MCE:          0          0          0          0          0          0          0          0   Machine check exceptions
MCP:        572        573        573        573        573        573        573        573   Machine check polls
HYP:          0          0          0          0          0          0          0          0   Hypervisor callback interrupts
ERR:          0
MIS:          0
PIN:          0          0          0          0          0          0          0          0   Posted-interrupt notification event
NPI:          0          0          0          0          0          0          0          0   Nested posted-interrupt event
PIW:          0          0          0          0          0          0          0          0   Posted-interrupt wakeup event
"""),
    # More Src() record for 2nd ldms update
    Src("/proc/interrupts",
"""\
           CPU0       CPU1       CPU2       CPU3       CPU4       CPU5       CPU6       CPU7
  0:          9          0          0          0          0          0          0          0   IO-APIC   2-edge      timer
  8:          0          0          0          0          0          1          0          0   IO-APIC   8-edge      rtc0
  9:          0          4          0          0          0          0          0          0   IO-APIC   9-fasteoi   acpi
 16:          0          0         31          0          0          0          0          0   IO-APIC  16-fasteoi   ehci_hcd:usb1
 19:          0          0          0          0          0          0          0          0   IO-APIC  19-fasteoi   ath9k
 23:    2186688     429113          0      39853          0          0          0          0   IO-APIC  23-fasteoi   ehci_hcd:usb2
 24:          0          0          0          0          0          0          0          0   PCI-MSI 327680-edge      xhci_hcd
 25:          0          0          0          0          0       4181     227799          0   PCI-MSI 6815744-edge      xhci_hcd
 26:          0          0          0          0          0          0          0          0   PCI-MSI 6815745-edge      xhci_hcd
 27:          0          0          0          0          0          0          0          0   PCI-MSI 6815746-edge      xhci_hcd
 28:          0          0          0          0          0          0          0          0   PCI-MSI 6815747-edge      xhci_hcd
 29:          0          0          0          0          0          0          0          0   PCI-MSI 6815748-edge      xhci_hcd
 30:          0          0          0          0          0          0          0          0   PCI-MSI 6815749-edge      xhci_hcd
 31:          0          0          0          0          0          0          0          0   PCI-MSI 6815750-edge      xhci_hcd
 32:          0          0          0          0          0          0          0          0   PCI-MSI 6815751-edge      xhci_hcd
 33:          0          0          0          1     565764          0      16664          0   PCI-MSI 512000-edge      ahci[0000:00:1f.2]
 34:          0          0          0          0          0          0          0          0   PCI-MSI 7340032-edge      ahci[0000:0e:00.0]
 35:        343          0          0          0          0          0          0    1482895   PCI-MSI 409600-edge      eno1
 36:          0       4885          0          0          0    2400055          0          0   PCI-MSI 32768-edge      i915
 37:          0          0         13          0          0          0          0          0   PCI-MSI 360448-edge      mei_me
 38:          0          0          0          0       1235          0          0          0   PCI-MSI 442368-edge      snd_hda_intel:card0
NMI:        132        141        124        125        125        144        125        132   Non-maskable interrupts
LOC:    3988207    2613488    2562788    2043852    2180437    3826532    3084422    2936689   Local timer interrupts
SPU:          0          0          0          0          0          0          0          0   Spurious interrupts
PMI:        132        141        124        124        125        144        125        132   Performance monitoring interrupts
IWI:         25         19         50         28         12     321769         67         39   IRQ work interrupts
RTR:          0          0          0          0          0          0          0          0   APIC ICR read retries
RES:     447957     195661     170009      62647      59698      98636     115634      69047   Rescheduling interrupts
CAL:     610299     573181     567683     608060     585890     566966     570622     521710   Function call interrupts
TLB:     573702     577286     570469     611601     588059     568480     572863     524609   TLB shootdowns
TRM:          0          0          0          0          0          0          0          0   Thermal event interrupts
THR:          0          0          0          0          0          0          0          0   Threshold APIC interrupts
DFR:          0          0          0          0          0          0          0          0   Deferred Error APIC interrupts
MCE:          0          0          0          0          0          0          0          0   Machine check exceptions
MCP:        572        573        573        573        573        573        573        573   Machine check polls
HYP:          0          0          0          0          0          0          0          0   Hypervisor callback interrupts
ERR:          1
MIS:          2
PIN:          0          0          0          0          0          0          0          0   Posted-interrupt notification event
NPI:          0          0          0          0          0          0          0          0   Nested posted-interrupt event
PIW:          1          1          1          1          1          1          1          1   Posted-interrupt wakeup event
"""),
]

TKN = re.compile('\\S+')
def src2ldmsdata(src):
    assert(type(src) == Src)
    lines = iter(src.content.splitlines())
    hdr = next(lines)
    hdr_tkns = TKN.findall(hdr)
    cols = len(hdr_tkns)
    D.cols = cols
    metrics = dict()
    for l in lines:
        D.l = l
        tkns = TKN.findall(l)
        D.tkns = tkns
        name = tkns[0].rstrip(':')
        n = min(cols, len(tkns) - 1)
        for c in range(0, n):
            val = int(tkns[c+1])
            mname = 'irq.%s#%d' % (name, c)
            metrics[mname] = val
    return LDMSData({
                'instance_name': INST_NAME,
                'schema_name': SCHEMA_NAME,
                'metrics': metrics,
           })

class ProcinterruptsTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"

    @classmethod
    def getPluginName(cls):
        return "procinterrupts"

    @classmethod
    def getSrcData(cls, tidx):
        return SrcData(src[tidx], src2ldmsdata(src[tidx]))


if __name__ == "__main__":
    try_sudo()
    pystart = os.getenv("PYTHONSTARTUP")
    if pystart:
        execfile(pystart)
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = DIR + "/test.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)
