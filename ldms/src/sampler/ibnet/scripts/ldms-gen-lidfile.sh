#! /bin/bash
#
#  Copyright (c) 2020 National Technology & Engineering Solutions
#  of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
#  NTESS, the U.S. Government retains certain rights in this software.
#
#  This software is available to you under a choice of one of two
#  licenses.  You may choose to be licensed under the terms of the GNU
#  General Public License (GPL) Version 2, available from the file
#  COPYING in the main directory of this source tree, or the BSD-type
#  license below:
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#
#       Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#       Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#
#       Neither the name of Sandia nor the names of any contributors may
#       be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
#       Neither the name of Open Grid Computing nor the names of any
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#       Modified source versions must be plainly marked as such, and
#       must not be misrepresented as being the original software.
#
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# /
# input file is output of ibnetdiscover -p
SRC="ibnetdiscover -p"
echo '# switches'
echo '# lid guid port-count portlist'
$SRC |grep -v sharp | grep ^SW | grep -v '???' | sort -n -k 2 -k 3 | sed -e 's/^SW *//' -e 's/ .x .*//' | while read -r lid port guid; do
  echo $lid $guid 1 $port
done
echo '# hcas'
echo '# lid guid port-count portlist'
$SRC |grep -v sharp | grep ^CA | grep -v '???' | sort -n -k 2 -k 3 | sed -e 's/^CA *//' -e 's/ .x .*//' | while read -r lid port guid; do
  echo $lid $guid 1 $port
done
