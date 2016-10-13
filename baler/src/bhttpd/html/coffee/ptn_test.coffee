# Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
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

# This is the meta-clustering test
ctrl = $("#ctrl")[0]
window.clusterCtrl = new baler.MetaClusterCtrl()
clusterCtrl.setDoneCb((data) ->
    console.log("Done ... ")
    console.log(data)
)
ctrl.appendChild(clusterCtrl.domobj)

# Pattern test
baler.get_ptns (data, textStatus, jqXHR) ->
    patterns = data.result
    baler.get_meta (data, textStatus, jqXHR) ->
        grp = []
        for p in data.map
            grp[p[0]] = p[1]
        p = new baler.PtnTable(patterns, grp, data.cluster_names)
        p.sort()
        t = $("#test0")[0]
        t.appendChild(p.domobj)


# Metric pattern test
baler.get_metric_ptns (data, textStatus, jqXHR) ->
    patterns = data.result
    baler.get_metric_meta (data, textStatus, jqXHR) ->
        grp = []
        for p in data.map
            grp[p[0]] = p[1]
        p = new baler.PtnTable(patterns, grp, data.cluster_names)
        p.sort()
        t = $("#test1")[0]
        t.appendChild(p.domobj)
0
