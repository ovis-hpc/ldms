#!/usr/bin/env python
'''
Created on Oct 29, 2014

@author: nichamon
'''

import unittest
import sqlite3
from oconf_sqlite import oconvert_sqlite
from oconf_sqlite.osqlite_classes import component_type, component, component_tree,\
    sampler_template, apply_on_node, service, model, action, event, cable_type,\
    cable
from argparse import ArgumentParser

NEWLINE = oconvert_sqlite.NEWLINE
INDENT = oconvert_sqlite.INDENT

cluster_type = component_type(comp_type="Cluster", gif_path="/test/gif_path", \
                           visible=1, category="physical")
compute_node_type = component_type(comp_type="Compute Node", visible=0, gif_path="")
cpu_type = component_type(comp_type="cpu", gif_path="")
hdd_type = component_type(comp_type="HDD", gif_path="")
ssd_type = component_type(comp_type="SSD", gif_path="")
hadoop_type = component_type(comp_type="Hadoop", category="hadoop", gif_path="")
resourcemanager_type = component_type(comp_type="ResourceManager", category="hadoop", gif_path="")

cluster = component("Test Cluster", "Cluster", 1, "cluster")
node1 = component("node0001", "Compute Node", 2, "node1")
node2 = component("node0002", "Compute Node", 3, "node2")
node3 = component("node0003", "Compute Node", 4, "node3")
node1_cpu1 = component("cpu1", "cpu", 5, "node1_cpu1")
node2_cpu1 = component("cpu1", "cpu", 6, "node2_cpu1")
node3_cpu1 = component("cpu1", "cpu", 7, "node3_cpu1")
node1_cpu2 = component("cpu2", "cpu", 8, "node1_cpu2")
node2_cpu2 = component("cpu2", "cpu", 9, "node2_cpu2")
node3_cpu2 = component("cpu2", "cpu", 10, "node3_cpu2")
hdd2 = component("HDD2", "HDD", 11, "hdd2")
hdd1 = component("HDD1", "HDD", 12, "hdd1")
ssd = component("SSD", "SSD", 13, "ssd")
hadoop = component("Hadoop", "Hadoop", 14, "hadoop")
resourcemanager = component("ResourceManager", "ResourceManager", 15, "resourcemanager")

procstat_template = sampler_template("procstat", "procstatutil", \
                              {'cpu': ["iowait", "idle", "user", "sys"]}, \
                              ["cpu"], interval="2000000")

procdiskstat_template = sampler_template("procdiskstats", "procdiskstats", \
                            {'SSD': ["reads_comp", "reads_merg", "time_read"], \
			                 'HDD': ["reads_comp", "reads_merg"]}, \
                             ["HDD", "SSD"], \
                             interval="2000000", device="sda,sdb")
all_templates = {procstat_template.name: procstat_template,
                procdiskstat_template.name: procdiskstat_template}
all_templates_order = [procstat_template.name, procdiskstat_template.name]

node1_procstat = apply_on_node.apply_on_template("procstat", \
                                {'cpu': {'identifier': ["node1_cpu1"], \
                                         'name': []}},
                                ["cpu"])
node1_apply_on = apply_on_node("Compute Node", "node1", \
                               {'procstat': node1_procstat}, ["procstat"])

node2_procstat = apply_on_node.apply_on_template("procstat", \
                    {'cpu': {'identifier': ["node2_cpu1", "node2_cpu2"], \
                            'name': ["cpu1", "cpu2"]}}, \
                    ["cpu"])
node2_apply_on = apply_on_node("Compute Node", "node2", \
                               {'procstat': node2_procstat}, ["procstat"])

node3_procstat = apply_on_node.apply_on_template("procstat", \
                    {'cpu': {'identifier': ["node3_cpu1", "node3_cpu2"], \
                             'name': ["cpu1", "cpu2"]}}, ["cpu"])
node3_diskstats = apply_on_node.apply_on_template("procdiskstats", \
                    {'HDD': {'identifier': ["hdd1"], 'name': ["sda"]}, \
                     'SSD': {'identifier': ["ssd"], 'name': ["sdb"]}}, \
                    ["HDD", "SSD"])
node3_apply_on = apply_on_node("Compute Node", "node3", \
                    {'procstat': node3_procstat, 'procdiskstats': node3_diskstats},
                    ["procstat", "procdiskstats"])

me = service("me", "node0001")
me.add_verb_attr("config", {'name': "consumer_kmd", 'host': "node0001", \
                            'xprt': "sock", 'port': "10004"},
                            ["name", "host", "xprt", "port"])
me.add_verb_attr("start_consumer", {'name': "consumer_kmd"}, ["name"])

balerd = service("balerd", "node0001")
balerd.add_verb_attr("hosts", {'names': "node[0001-0003]"}, ["names"])
balerd.add_verb_attr("tokens", {'type': "ENG",
                                 'path': "/etc/ovis/eng-dictionary"},
                                ["type", "path"])
balerd.add_verb_attr("plugin", {'name': "bout_sos_img", 'delta_ts': "60",
                                'store_name': "img_60"},
                                ["name", "delta_ts", "store_name"])
balerd.add_verb_attr("plugin", {'name': "bout_sos_msg"}, ["name"])

aggregator = service("ldmsd_aggregator", "node0001")
aggregator.add_verb_attr("add",
                         {'type': "active", 'interval': "2000000",
                          'xprt': "sock", 'port': "10001",
                          'host': "node[0001-0002]", 'sets': "procstatutil"},
                         ["type", "interval", "xprt", "port", "host", "sets"])
aggregator.add_verb_attr("add",
                         {'type': "active", 'interval': "2000000",
                          'xprt': "sock", 'port': "10001",
                          'host': "node0003",
                          'sets': "procstatutil,procdiskstats"},
                          ["type", "interval", "xprt", "port", "host", "sets"])
aggregator.add_verb_attr("config",
                         {'name': "store_sos", 'path': "$OVIS_DATA/metrics",
                          'time_limit': "600", 'init_size': "67108864",
                          'owner': ":ovis"},
                          ["name", "path", "time_limit", "init_size", "owner"])
aggregator.add_verb_attr("store",
                         {'name': "store_sos", 'container': "sos_procstatutil",
                          'comp_type': "node", 'set': "procstatutil",
                          'set': "procstatutil",
                          'hosts': "node[0001-0003]", 'metrics': "user,iowait,sys"},
                         ["name", "container", "comp_type", "set", "hosts", "metrics"])
aggregator.add_verb_attr("store",
                         {'name': "store_sos", 'container': "sos_procdiskstats",
                          'comp_type': "node", 'set': "procdiskstats",
                          'hosts': "node0003"},
                         ["name", "container", "comp_type", "set", "hosts"])

komondor = service("komondor", "node0001")
komondor.add_verb_attr("store", {'path': "$OVIS_DATA/events/events"}, ["path"])

model_1 = model(1, "model_low_exact", thresholds = "0,0,0", report_flags = "1111")
model_2 = model(2, "model_high_exact", thresholds = "60,60,70", report_flags= "1011")
model_4 = model(4, "model_baler_rules", thresholds="0,0,0", params="/etc/ovis/baler_test_rules,1m")

action_nothing = action("nothing", "/opt/ovis/action_scripts/do_nothing")
action_resolve = action("resolve", "/opt/ovis/action_scripts/resolve", "corrective")
action_log = action("log", "/opt/ovis/action_scripts/ovis_actions/iscb_actions.py info", "corrective")

event_test = event(1, "Test Event", 4)
event_test.set_metrics({12884901898: "node0002#baler_ptn",
                        17179869194: "node0003#baler_ptn"})
event_test.set_components({3: {'comp_type': "Compute Node", 'identifier': "node2"},
                          4: {'comp_type': "Compute Node", 'identifier': "node3"}})
event_test.set_severity_level("critical", "Test Event", ["log", "resolve"])

event_cpu_load = event(2, "CPU load", 2)
event_cpu_load.set_metrics({25769803780: "sys#cpu1",
                           38654705668: "sys#cpu2"})
event_cpu_load.set_components({3: {'comp_type': "Compute Node", 'identifier': "node2"}})
event_cpu_load.set_severity_level("nominal", "Normal", ["resolve"])
event_cpu_load.set_severity_level("warning", "system time is high.", ["nothing"])
event_cpu_load.set_severity_level("critical", "system time is very high.", ["nothing"])

event_cpu_idle = event(3, "CPU idle", 2)
event_cpu_idle.set_metrics({25769803778: "idle#cpu1",
                           30064771074: "idle#cpu1"})
event_cpu_idle.set_components({3: {'comp_type': "Compute Node", 'identifier': "node2"},
                               4: {'comp_type': "Compute Node", 'identifier': "node3"}})
event_cpu_idle.set_severity_level("nominal", "Normal", ["resolve"])
event_cpu_idle.set_severity_level("critical", "CPU is mostly in idle.", ["log"])

event_reboot = event(3, "reboot", 65535)
event_reboot.set_severity_level("critical", "Reboot signaled by users", "log")
event_reboot.set_components({2: {'comp_type': "Compute Node", 'identifier': "node1"},
                             3: {'comp_type': "Compute Node", 'identifier': "node2"},
                             4: {'comp_type': "Compute Node", 'identifier': "node3"}})

cable_type_IPoE = cable_type("IP over Ethernet", "Ethernet connection")
cable_type_IPoIB = cable_type("IPoIB", "Infiniband connection")

cable_IPoE_node1_node2 = cable("IP over Ethernet", "192.168.0.100", \
                               {'type': node1.comp_type, \
                                'identifier': node1.identifier, \
                                'comp_id': node1.comp_id}, \
                               {'type': node2.comp_type, \
                                'identifier': node2.identifier, \
                                'comp_id': node2.comp_id})

cable_IPoE_node1_node3 = cable("IP over Ethernet", "192.168.0.101", \
                               {'type': node1.comp_type, \
                                'identifier': node1.identifier, \
                                'comp_id': node1.comp_id}, \
                               {'type': node3.comp_type, \
                                'identifier': node3.identifier, \
                                'comp_id': node3.comp_id})
cable_IPoIB_node1_node2 = cable("IPoIB", "192.168.100.100", \
                               {'type': node1.comp_type, \
                                'identifier': node1.identifier, \
                                'comp_id': node1.comp_id}, \
                               {'type': node2.comp_type, \
                                'identifier': node2.identifier, \
                                'comp_id': node2.comp_id})
cable_IPoIB_node1_node3 = cable("IPoIB", "192.168.100.101", \
                               {'type': node1.comp_type, \
                                'identifier': node1.identifier, \
                                'comp_id': node1.comp_id}, \
                               {'type': node3.comp_type, \
                                'identifier': node3.identifier, \
                                'comp_id': node3.comp_id})

TEST_ALL_COMP_TYPES = {}
TEST_COMP_TYPES_ORDER = []
TEST_ALL_COMP_MAP = {}
TEST_COMP_TREE = component_tree()
TEST_ALL_CABLE_TYPES = {1: cable_type_IPoIB, 2: cable_type_IPoE}
TEST_CABLE_TYPES_ORDER = [2, 1]
TEST_ALL_CABLES = [cable_IPoE_node1_node2, cable_IPoE_node1_node3, \
                   cable_IPoIB_node1_node2, cable_IPoIB_node1_node3]

def init_once():
    global cluster_type
    global compute_node_type
    global cpu
    global hdd_type
    global TEST_ALL_COMP_MAP
    global TEST_COMP_TREE
    global TEST_ALL_COMP_TYPES
    global TEST_COMP_TYPES_ORDER

    cluster_type.add_component(cluster)
    cluster_type.give_name = [("cluster", "Test Cluster")]

    compute_node_type.add_component(node1)
    compute_node_type.add_component(node2)
    compute_node_type.add_component(node3)
    compute_node_type.give_name = [("node[1-3]", "node[0001-0003]")]

    cpu_type.add_component(node1_cpu1)
    cpu_type.add_component(node1_cpu2)
    cpu_type.add_component(node2_cpu1)
    cpu_type.add_component(node2_cpu2)
    cpu_type.add_component(node3_cpu1)
    cpu_type.add_component(node3_cpu2)
    cpu_type.give_name = [("node[1-3]_cpu1", "cpu1"), ("node[1-3]_cpu2", "cpu2")]

    hdd_type.add_component(hdd2)
    hdd_type.add_component(hdd1)
    hdd_type.give_name = [("hdd[2,1]", "HDD[2,1]")]

    ssd_type.add_component(ssd)
    ssd_type.give_name = [("ssd", "SSD")]

    hadoop_type.add_component(hadoop)
    hadoop_type.give_name = [("hadoop", "Hadoop")]

    resourcemanager_type.add_component(resourcemanager)
    resourcemanager_type.give_name = [("resourcemanager", "ResourceManager")]

    TEST_ALL_COMP_TYPES[cluster_type.comp_type] = cluster_type
    TEST_ALL_COMP_TYPES[compute_node_type.comp_type] = compute_node_type
    TEST_ALL_COMP_TYPES[cpu_type.comp_type] = cpu_type
    TEST_ALL_COMP_TYPES[hdd_type.comp_type] = hdd_type
    TEST_ALL_COMP_TYPES[ssd_type.comp_type] = ssd_type
    TEST_ALL_COMP_TYPES[hadoop_type.comp_type] = hadoop_type
    TEST_ALL_COMP_TYPES[resourcemanager_type.comp_type] = resourcemanager_type

    TEST_COMP_TYPES_ORDER = [cluster_type.comp_type, compute_node_type.comp_type, \
                             cpu_type.comp_type, hdd_type.comp_type, \
                             ssd_type.comp_type, hadoop_type.comp_type, \
                             resourcemanager_type.comp_type]

    TEST_ALL_COMP_MAP[cluster.comp_id] = cluster
    TEST_ALL_COMP_MAP[node1.comp_id] = node1
    TEST_ALL_COMP_MAP[node2.comp_id] = node2
    TEST_ALL_COMP_MAP[node3.comp_id] = node3
    TEST_ALL_COMP_MAP[node1_cpu1.comp_id] = node1_cpu1
    TEST_ALL_COMP_MAP[node1_cpu2.comp_id] = node1_cpu2
    TEST_ALL_COMP_MAP[node2_cpu1.comp_id] = node2_cpu1
    TEST_ALL_COMP_MAP[node2_cpu2.comp_id] = node2_cpu2
    TEST_ALL_COMP_MAP[node3_cpu1.comp_id] = node3_cpu1
    TEST_ALL_COMP_MAP[node3_cpu2.comp_id] = node3_cpu2
    TEST_ALL_COMP_MAP[hdd1.comp_id] = hdd1
    TEST_ALL_COMP_MAP[hdd2.comp_id] = hdd2
    TEST_ALL_COMP_MAP[ssd.comp_id] = ssd
    TEST_ALL_COMP_MAP[hadoop.comp_id] = hadoop
    TEST_ALL_COMP_MAP[resourcemanager.comp_id] = resourcemanager

    TEST_COMP_TREE.add_node(None, {'comp': cluster, 'visited': False}, cluster.comp_id)
    TEST_COMP_TREE.add_node(cluster.comp_id, {'comp': node1, 'visited': False}, node1.comp_id)
    TEST_COMP_TREE.add_node(node1.comp_id, {'comp': node1_cpu1, 'visited': False}, node1_cpu1.comp_id)
    TEST_COMP_TREE.add_node(node1.comp_id, {'comp': node1_cpu2, 'visited': False}, node1_cpu2.comp_id)
    TEST_COMP_TREE.add_node(cluster.comp_id, {'comp': node2, 'visited': False}, node2.comp_id)
    TEST_COMP_TREE.add_node(node2.comp_id, {'comp': node2_cpu1, 'visited': False}, node2_cpu1.comp_id)
    TEST_COMP_TREE.add_node(node2.comp_id, {'comp': node2_cpu2, 'visited': False}, node2_cpu2.comp_id)
    TEST_COMP_TREE.add_node(cluster.comp_id, {'comp': node3, 'visited': False}, node3.comp_id)
    TEST_COMP_TREE.add_node(node3.comp_id, {'comp': node3_cpu1, 'visited': False}, node3_cpu1.comp_id)
    TEST_COMP_TREE.add_node(node3.comp_id, {'comp': node3_cpu2, 'visited': False}, node3_cpu2.comp_id)
    TEST_COMP_TREE.add_node(cluster.comp_id, {'comp': hdd2, 'visited': False}, hdd2.comp_id)
    TEST_COMP_TREE.add_node(cluster.comp_id, {'comp': hdd1, 'visited': False}, hdd1.comp_id)
    TEST_COMP_TREE.add_node(cluster.comp_id, {'comp': ssd, 'visited': False}, ssd.comp_id)
    TEST_COMP_TREE.add_node(None, {'comp': hadoop, 'visited': False}, hadoop.comp_id)
    TEST_COMP_TREE.add_node(hadoop.comp_id, {'comp': resourcemanager, 'visited': False}, resourcemanager.comp_id)
    TEST_COMP_TREE.add_node(node1.comp_id, {'comp': resourcemanager, 'visited': False}, resourcemanager.comp_id)

class TestOConvert_sqlite(unittest.TestCase):
    cursor = None

    def setUp(self):
        self.identifiers0 = ["aaa"]
        self.identifiers1 = ["aaa1bbb1", "aaa1bbb2"]
        self.identifiers2 = ["aaa1bbb1", "ccc1ddd1"]
        self.identifiers3 = ["aaa1bbb1", "aaa1bbb2", "ccc1ddd1"]

        self.names0 = ["aaa"]
        self.names1 = ["bbb1", "bbb2"]
        self.names2 = ["bbb1", "ddd1"]
        self.names3 = ["bbb1", "bbb2", "ddd1"]

    def tearDown(self):
        pass

    def test_process_identifier_name_map(self):
        result = oconvert_sqlite.process_identifier_name_map(self.identifiers0, self.names0)
        self.assertTupleEqual(result, ("aaa", "aaa"))

        result = oconvert_sqlite.process_identifier_name_map(self.identifiers1, self.names0)
        self.assertTupleEqual(result, ("aaa1bbb[1-2]", "aaa"))

        result = oconvert_sqlite.process_identifier_name_map(self.identifiers1, self.names1)
        self.assertTupleEqual(result, ("aaa1bbb[1-2]", "bbb[1-2]"))

        result = oconvert_sqlite.process_identifier_name_map(self.identifiers2, self.names2)
        self.assertTupleEqual(result, ("aaa1bbb1,ccc1ddd1", "bbb1,ddd1"))

        result = oconvert_sqlite.process_identifier_name_map(self.identifiers3, self.names3)
        self.assertTupleEqual(result, ("aaa1bbb[1-2],ccc1ddd1", "bbb[1-2],ddd1"))

    def test_is_new_give_name(self):
        # The decision chart given the names are given by the increasing order of component ids.
        # if curr_name == prev_name: False
        # else:
        #    if curr_name is already found: True
        #    else:
        #        if name is repeated in general: True
        #        else: False

        # prev_name == curr_name
        #  curr_name isn't found yet
        self.assertFalse(oconvert_sqlite.is_new_give_name("A", "A", [], True))
        self.assertFalse(oconvert_sqlite.is_new_give_name("A", "A", [], False))

        #  curr_name is found yet
        self.assertFalse(oconvert_sqlite.is_new_give_name("A", "A", ["A"], True))
        self.assertFalse(oconvert_sqlite.is_new_give_name("A", "A", ["A"], False))

        # prev_name != curr_name
        #  curr_name isn't found yet
        self.assertTrue(oconvert_sqlite.is_new_give_name("B", "A", ["A", "B"], True))
        self.assertTrue(oconvert_sqlite.is_new_give_name("B", "A", ["A", "B"], False))

        #  curr_name is found yet
        self.assertTrue(oconvert_sqlite.is_new_give_name("B", "A", ["A"], True))
        self.assertFalse(oconvert_sqlite.is_new_give_name("B", "A", ["A"], False))

        found_comp_name = set(["PS1", "PS2", "PS3"])
        self.assertTrue(oconvert_sqlite.is_new_give_name("PS1", "PS3", \
                                                         found_comp_name, False))

        # Names = ["PS1", "PS2", "PS3", "PS1", "PS2", "PS3"]
        # Iterate through the 2nd 'PS2'
        # found_comp_name is empty according to the process_component_type function
        self.assertFalse(oconvert_sqlite.is_new_give_name("PS2", "PS1", \
                                                         [], False))

    def test_get_give_name(self):
        identifiers = ["prod1.ost1", "prod1.ost2", "prod2.ost1", "prod2.ost2"]
        names = ["ost0000", "ost0004", "ost0008", "ost0009"]
        result = oconvert_sqlite.get_give_name(identifiers, names)
        exp_result = [("prod1.ost[1-2]", "ost[0000,0004]"), \
                      ("prod2.ost[1-2]", "ost[0008-0009]")]
        self.assertEqual(result, exp_result)

    def test_process_component_type(self):
        all_comp_map = {}
        result = oconvert_sqlite.process_component_type(self.cursor, \
                                                cluster_type.comp_type, \
                                                cluster_type.attr['gif_path'], \
                                                cluster_type.attr['category'], \
                                                cluster_type.attr['visible'],
                                                all_comp_map)
        all_comp_map[cluster.comp_id] = cluster
        exp_result = (cluster_type, all_comp_map)
        try:
            self.assertEqual(result, exp_result)
        except AssertionError:
            print ""
            print result
            print "----------------------"
            print exp_result
            print "======================"

        '''
        test case that in a give_name block, multiple identifiers and names are given
        '''
        all_comp_map = {}
        result = oconvert_sqlite.process_component_type(self.cursor, \
                                                compute_node_type.comp_type, \
                                                compute_node_type.attr['gif_path'], \
                                                compute_node_type.attr['category'], \
                                                compute_node_type.attr['visible'], \
                                                all_comp_map)
        all_comp_map[node1.comp_id] = node1
        all_comp_map[node2.comp_id] = node2
        all_comp_map[node3.comp_id] = node3
        exp_result = (compute_node_type, all_comp_map)
        try:
            self.assertEqual(result, exp_result)
        except AssertionError:
            print ""
            print result
            print "----------------------"
            print exp_result
            print "======================"

        '''
        Test the case that have 2 sets of give_name's and
        test the case that a give_name block has a name with multiple identifiers
        '''
        all_comp_map = {}
        result = oconvert_sqlite.process_component_type(self.cursor, \
                                                cpu_type.comp_type, \
                                                cpu_type.attr['gif_path'], \
                                                cpu_type.attr['category'], \
                                                cpu_type.attr['visible'], \
                                                all_comp_map)
        all_comp_map[node1_cpu1.comp_id] = node1_cpu1
        all_comp_map[node1_cpu2.comp_id] = node1_cpu2
        all_comp_map[node2_cpu1.comp_id] = node2_cpu1
        all_comp_map[node2_cpu2.comp_id] = node2_cpu2
        all_comp_map[node3_cpu1.comp_id] = node3_cpu1
        all_comp_map[node3_cpu2.comp_id] = node3_cpu2
        exp_result = (cpu_type, all_comp_map)
        self.assertEqual(result, exp_result)

        all_comp_map = {}
        result = oconvert_sqlite.process_component_type(self.cursor, \
                                                hdd_type.comp_type, \
                                                hdd_type.attr['gif_path'], \
                                                hdd_type.attr['category'], \
                                                hdd_type.attr['visible'], \
                                                all_comp_map)
        all_comp_map[hdd1.comp_id] = hdd1
        all_comp_map[hdd2.comp_id] = hdd2
        exp_result = (hdd_type, all_comp_map)
        self.assertEqual(result, exp_result)

    def test_get_component_types(self):
        result = oconvert_sqlite.get_component_types(self.cursor)
        exp_result = (TEST_ALL_COMP_TYPES, TEST_COMP_TYPES_ORDER, TEST_ALL_COMP_MAP)
#         self.assertEqual(result, exp_result)
        try:
            self.assertTupleEqual(result, exp_result)
        except AssertionError, e:
            print ""
            if result[1] != exp_result[1]:
                print "order different"
                print result[1]
                print "--------------------"
                print exp_result[1]
                print "===================="
                raise AssertionError(e)
            for ctype in TEST_COMP_TYPES_ORDER:
                if result[0][ctype] != exp_result[0][ctype]:
                    print "all_comp_types differnet"
                    print result[0][ctype]
                    print "--------------------"
                    print exp_result[0][ctype]
                    print "===================="
                    raise AssertionError(e)
            if result[2] != exp_result[2]:
                print "all_comp_map different"
                print result[2]
                print "--------------"
                print exp_result[2]
                print "================="
                raise AssertionError(e)

    def test_get_component_types_text(self):
        result = oconvert_sqlite.get_component_types_text(self.cursor, \
                                                    TEST_ALL_COMP_TYPES, \
                                                    TEST_COMP_TYPES_ORDER)
        exp_result = "" \
                    "component:\n" \
                    "\ttype: Cluster\n" \
                    "\tgif_path: /test/gif_path\n" \
                    "\tcategory: physical\n" \
                    "\tgive_name:\n" \
                    "\t\tidentifiers: cluster\n" \
                    "\t\tnames: Test Cluster\n" \
                    "component:\n" \
                    "\ttype: Compute Node\n" \
                    "\tvisible: false\n" \
                    "\tgive_name:\n" \
                    "\t\tidentifiers: node[1-3]\n" \
                    "\t\tnames: node[0001-0003]\n" \
                    "component:\n" \
                    "\ttype: cpu\n" \
                    "\tgive_name:\n" \
                    "\t\tidentifiers: node[1-3]_cpu1\n" \
                    "\t\tnames: cpu1\n" \
                    "\tgive_name:\n" \
                    "\t\tidentifiers: node[1-3]_cpu2\n" \
                    "\t\tnames: cpu2\n" \
                    "component:\n" \
                    "\ttype: HDD\n" \
                    "\tgive_name:\n" \
                    "\t\tidentifiers: hdd[2,1]\n" \
                    "\t\tnames: HDD[2,1]\n" \
                    "component:\n" \
                    "\ttype: SSD\n" \
                    "\tgive_name:\n" \
                    "\t\tidentifiers: ssd\n" \
                    "\t\tnames: SSD\n" \
                    "component:\n" \
                    "\ttype: Hadoop\n" \
                    "\tcategory: hadoop\n" \
                    "\tgive_name:\n" \
                    "\t\tidentifiers: hadoop\n" \
                    "\t\tnames: Hadoop\n" \
                    "component:\n" \
                    "\ttype: ResourceManager\n" \
                    "\tcategory: hadoop\n" \
                    "\tgive_name:\n" \
                    "\t\tidentifiers: resourcemanager\n" \
                    "\t\tnames: ResourceManager\n"
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

    def test_construct_component_tree(self):
        result = oconvert_sqlite.construct_component_tree(self.cursor, TEST_ALL_COMP_MAP)
        self.assertEqual(result, TEST_COMP_TREE)

    def test_get_component_tree_text(self):
        result = oconvert_sqlite.get_component_tree_text(TEST_COMP_TREE)
        exp_result = "" \
                    "component_tree:\n" \
                    "\tCluster{cluster}/\n" \
                    "\t\tCompute Node{node1}/\n" \
                    "\t\t\tcpu{node1_cpu[1-2]}\n" \
                    "\t\t\tResourceManager{resourcemanager}\n" \
                    "\t\tCompute Node{node2}/\n" \
                    "\t\t\tcpu{node2_cpu[1-2]}\n" \
                    "\t\tCompute Node{node3}/\n" \
                    "\t\t\tcpu{node3_cpu[1-2]}\n" \
                    "\t\tHDD{hdd[2,1]}\n" \
                    "\t\tSSD{ssd}\n" \
                    "\tHadoop{hadoop}/\n" \
                    "\t\tResourceManager{resourcemanager}\n"
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

        comp_tree = component_tree()
        comp_tree.add_node(None, {'comp': node1, 'visited': False}, node1.comp_id)
        comp_tree.add_node(node1.comp_id, {'comp': resourcemanager, 'visited': False}, resourcemanager.comp_id)
        comp_tree.add_node(resourcemanager.comp_id, {'comp': hdd1, 'visited': False}, hdd1.comp_id)
        comp_tree.add_node(None, {'comp': hadoop, 'visited': False}, hadoop.comp_id)
        comp_tree.add_node(hadoop.comp_id, {'comp': resourcemanager, 'visited': False}, resourcemanager.comp_id)
        result = oconvert_sqlite.get_component_tree_text(comp_tree)
        exp_result = "" \
                    "component_tree:\n" \
                    "\tCompute Node{node1}/\n" \
                    "\t\tResourceManager{resourcemanager}/\n" \
                    "\t\t\tHDD{hdd1}\n" \
                    "\tHadoop{hadoop}/\n" \
                    "\t\tResourceManager{resourcemanager}\n"
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

    def test_get_template_metric_component_types(self):
        s = "iowait#cpu1[30064771073],idle#cpu1[30064771074]," \
                "user#cpu1[30064771075],sys#cpu1[30064771076]," \
                "iowait#cpu2[42949672961],idle#cpu2[42949672962]," \
                "user#cpu2[42949672963],sys#cpu2[42949672964]"
        result = oconvert_sqlite.get_template_metric_component_types(self.cursor, s)
        metrics, comp = procstat_template.get_component_and_metrics()
        exp_result = (1, metrics, comp)
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

        s = "reads_comp#sdb[55834574855]," \
                "reads_merg#sdb[55834574856]," \
                "time_read#sdb[55834574857]," \
                "reads_comp#sda[51539607557]," \
                "reads_merg#sda[51539607558]"
        result = oconvert_sqlite.get_template_metric_component_types(self.cursor, s)
        metrics, comp = procdiskstat_template.get_component_and_metrics()
        exp_result = (5, metrics, comp)
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

    def test_get_template_config(self):
        s = "interval:2000000"
        result = oconvert_sqlite.get_template_config(s)
        exp_result = ({'interval': "2000000"}, ["interval"])
        self.assertEqual(result, exp_result)

        s = "interval:2000000;device:sda,sdb"
        result = oconvert_sqlite.get_template_config(s)
        exp_result = ({'interval': "2000000",
                      'device': "sda,sdb"}, ["interval", "device"])
        self.assertEqual(result, exp_result)

    def test_get_sampler_templates(self):
        result = oconvert_sqlite.get_sampler_templates(self.cursor)
        exp_result = (all_templates, all_templates_order)
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            for x in result:
                print x
            print "---------------"
            for y in exp_result:
                print y
            print "==============="
            raise AssertionError(e)

    def test_get_sampler_template_text(self):
        result = oconvert_sqlite.get_sampler_template_text(self.cursor, \
                                        all_templates, all_templates_order)
        exp_result = "" \
                    "template:\n" \
                    "\ttemplate_name: procstat\n" \
                    "\tsampler: procstatutil\n" \
                    "\t\tconfig:\n" \
                    "\t\t\tinterval: 2000000\n" \
                    "\t\tcomponent:\n" \
                    "\t\t\ttype: cpu\n" \
                    "\t\t\tmetrics: iowait,idle,user,sys\n" \
                    "template:\n" \
                    "\ttemplate_name: procdiskstats\n" \
                    "\tsampler: procdiskstats\n" \
                    "\t\tconfig:\n" \
                    "\t\t\tdevice: sda,sdb\n" \
                    "\t\t\tinterval: 2000000\n" \
                    "\t\tcomponent:\n" \
                    "\t\t\ttype: HDD\n" \
                    "\t\t\tmetrics: reads_comp,reads_merg\n" \
                    "\t\tcomponent:\n" \
                    "\t\t\ttype: SSD\n" \
                    "\t\t\tmetrics: reads_comp,reads_merg,time_read\n"
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

    def test_get_apply_on_metric_component(self):
        '''
        Metrics with no extension
        '''
        s = "iowait[21474836481],idle[21474836482],user[21474836483]," \
                "sys[21474836484]"
        result = oconvert_sqlite.get_apply_on_metric_component(self.cursor, s, procstat_template)
        comps = {'cpu': {'identifier': ["node1_cpu1"], 'name': []}}
        comps_ordering = ["cpu"]
        exp_result = (comps, comps_ordering)
        try:
            self.assertTupleEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

        '''
        Metrics with extensions
        '''
        s = "iowait#cpu1[25769803777],idle#cpu1[25769803778]," \
                "user#cpu1[25769803779],sys#cpu1[25769803780]," \
                "iowait#cpu2[38654705665],idle#cpu2[38654705666]," \
                "user#cpu2[38654705667],sys#cpu2[38654705668]"
        result = oconvert_sqlite.get_apply_on_metric_component(self.cursor, s, procstat_template)
        comps = {'cpu': {'identifier': ["node2_cpu1", "node2_cpu2"], \
                         'name': ["cpu1", "cpu2"]}}
        comps_ordering = ["cpu"]
        exp_result = (comps, comps_ordering)
        try:
            self.assertTupleEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

        '''
        Metrics with multiple components and extension
        '''
        s = "reads_comp#sdb[55834574855],reads_merg#sdb[55834574856]," \
                "time_read#sdb[55834574857],reads_comp#sda[51539607557]," \
                "reads_merg#sda[51539607558]"
        result = oconvert_sqlite.get_apply_on_metric_component(self.cursor, s, procdiskstat_template)
        comps = {'SSD': {'identifier': ["ssd"], 'name': ["sdb"]},
                 'HDD': {'identifier': ["hdd1"], 'name': ["sda"]}}
        comps_ordering = ["HDD", "SSD"]
        exp_result = (comps, comps_ordering)
        try:
            self.assertTupleEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

    def test_get_template_apply_on(self):
        result = oconvert_sqlite.get_template_apply_on(self.cursor, all_templates)
        exp_result = ({'node0001': node1_apply_on, 'node0002': node2_apply_on, \
                      'node0003': node3_apply_on}, ["node0001", "node0002", "node0003"])
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

    def test_get_template_apply_on_text(self):
        result = oconvert_sqlite.get_template_apply_on_text(self.cursor, all_templates)
        exp_result = "" \
                    "apply_on:\n" \
                    "\thost: Compute Node{node1}\n" \
                    "\ttemplate_name: procstat\n" \
                    "\t\tcomponent: cpu{node1_cpu1}\n" \
                    "apply_on:\n" \
                    "\thost: Compute Node{node2}\n" \
                    "\ttemplate_name: procstat\n" \
                    "\t\tcomponent: cpu{node2_cpu[1-2]}\n" \
                    "\t\t\tname: cpu[1-2]\n" \
                    "apply_on:\n" \
                    "\thost: Compute Node{node3}\n" \
                    "\ttemplate_name: procstat\n" \
                    "\t\tcomponent: cpu{node3_cpu[1-2]}\n" \
                    "\t\t\tname: cpu[1-2]\n" \
                    "\ttemplate_name: procdiskstats\n" \
                    "\t\tcomponent: HDD{hdd1}\n" \
                    "\t\t\tname: sda\n" \
                    "\t\tcomponent: SSD{ssd}\n" \
                    "\t\t\tname: sdb\n"
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

    def test_process_service_attr_values(self):
        result = oconvert_sqlite.process_service_attr_values("type:active;interval:2000000;owner::ovis")
        exp_result = ({'type': "active", 'interval': "2000000", 'owner': ":ovis"},
                        ["type", "interval", "owner"])
        self.assertEqual(result, exp_result)

    def test_process_balerd_hosts(self):
        result = oconvert_sqlite.process_balerd_hosts("node0001:8589934602;node0002:12884901898;node0003:171")
        exp_result = balerd.verb_attrs[0]['attr_values']
        self.assertEqual(result, exp_result)

    def test_get_services(self):
        result = oconvert_sqlite.get_services(self.cursor, "me")
        exp_result = {'node0001': me}
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            for host in result:
                print result[host]
            print "--------------------"
            for host in exp_result:
                print exp_result[host]
            print "===================="
            raise AssertionError(e)

        result = oconvert_sqlite.get_services(self.cursor, "balerd")
        exp_result = {'node0001': balerd}
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            for host in result:
                print result[host]
            print "--------------------"
            for host in exp_result:
                print exp_result[host]
            print "===================="
            raise AssertionError(e)

        result = oconvert_sqlite.get_services(self.cursor, "ldmsd_aggregator")
        exp_result = {'node0001': aggregator}
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            for host in result:
                print result[host]
            print "--------------------"
            for host in exp_result:
                print exp_result[host]
            print "===================="
            raise AssertionError(e)

    def test_get_services_text(self):
        result = oconvert_sqlite.get_services_text(self.cursor)
        exp_result = aggregator.__str__() + \
                        balerd.__str__() + \
                        me.__str__() + \
                        komondor.__str__()
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "--------------------"
            print exp_result
            print "===================="
            raise AssertionError(e)

    def test_get_models(self):
        result = oconvert_sqlite.get_models(self.cursor)
        exp_result = [model_1, model_2, model_4]
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            for key in result:
                print result[key]
            print "--------------------"
            for key in exp_result:
                print exp_result[key]
            print "===================="
            raise AssertionError(e)

    def test_get_actions(self):
        result = oconvert_sqlite.get_actions(self.cursor)
        exp_result = [action_nothing, action_resolve, action_log]
        self.assertEqual(result, exp_result)

    def test_get_models_text(self):
        result = oconvert_sqlite.get_models_text(self.cursor, \
                                                 [model_1, model_2, model_4])
        exp_result = "model:\n" \
                        "\tname: model_low_exact\n" \
                        "\tmodel_id: 1\n" \
                        "\tthresholds: 0,0,0\n" \
                        "\treport_flags: 1111\n" \
                        "model:\n" \
                        "\tname: model_high_exact\n" \
                        "\tmodel_id: 2\n" \
                        "\tthresholds: 60,60,70\n" \
                        "\treport_flags: 1011\n" \
                        "model:\n" \
                        "\tname: model_baler_rules\n" \
                        "\tmodel_id: 4\n" \
                        "\tthresholds: 0,0,0\n" \
                        "\tparameters: /etc/ovis/baler_test_rules,1m\n"
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

    def test_get_actions_text(self):
        result = oconvert_sqlite.get_actions_text(self.cursor, \
                                [action_nothing, action_resolve, action_log])
        exp_result = "action:\n" \
                        "\tname: nothing\n" \
                        "\texecute: /opt/ovis/action_scripts/do_nothing\n" \
                        "action:\n" \
                        "\tname: resolve\n" \
                        "\texecute: /opt/ovis/action_scripts/resolve\n" \
                        "\taction_type: corrective\n" \
                        "action:\n" \
                        "\tname: log\n" \
                        "\texecute: /opt/ovis/action_scripts/ovis_actions/iscb_actions.py info\n" \
                        "\taction_type: corrective\n"
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            for i in range(0, len(result)):
                if result[i] != exp_result[i]:
                    print result[i:]
                    print "------------------"
                    print exp_result[i:]
            print "==============="
            raise AssertionError(e)

    def test_get_events(self):
        result = oconvert_sqlite.get_events(self.cursor)
        exp_result = {1: event_test,
                      2: event_cpu_load,
                      3: event_cpu_idle,
                      4: event_reboot}
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            for key in result:
                print result[key]
            print "--------------------"
            for key in exp_result:
                print exp_result[key]
            print "===================="
            raise AssertionError(e)

    def test_get_event_metrics_components_text(self):
        result = oconvert_sqlite.get_event_metrics_components_text(self.cursor, event_test)
        exp_result = ("node[0002-0003]#baler_ptn", "Compute Node{node[2-3]}")
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

        result = oconvert_sqlite.get_event_metrics_components_text(self.cursor, event_cpu_load)
        exp_result = ("sys#[*]", "Compute Node{node2}")
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

        result = oconvert_sqlite.get_event_metrics_components_text(self.cursor, event_cpu_idle)
        exp_result = ("idle#cpu1", "*")
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

        result = oconvert_sqlite.get_event_metrics_components_text(self.cursor, event_reboot)
        exp_result = ("", "Compute Node{*}")
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

    def test_get_events_text(self):
        all_events = {1: event_test,
                      2: event_cpu_load,
                      3: event_cpu_idle,
                      4: event_reboot}
        result = oconvert_sqlite.get_events_text(self.cursor, all_events)
        exp_result = "" \
                    "event:\n" \
                    "\tname: Test Event\n" \
                    "\tmodel_id: 4\n" \
                    "\tmetric:\n" \
                    "\t\tmetric_name: node[0002-0003]#baler_ptn\n" \
                    "\t\tcomponents: Compute Node{node[2-3]}\n" \
                    "\tseverity:\n" \
                    "\t\tlevel: critical\n" \
                    "\t\t\tmsg: Test Event\n" \
                    "\t\t\taction_name: log,resolve\n" \
                    "event:\n" \
                    "\tname: CPU load\n" \
                    "\tmodel_id: 2\n" \
                    "\tmetric:\n" \
                    "\t\tmetric_name: sys#[*]\n" \
                    "\t\tcomponents: Compute Node{node2}\n" \
                    "\tseverity:\n" \
                    "\t\tlevel: nominal\n" \
                    "\t\t\tmsg: Normal\n" \
                    "\t\t\taction_name: resolve\n" \
                    "\t\tlevel: warning\n" \
                    "\t\t\tmsg: system time is high.\n" \
                    "\t\t\taction_name: nothing\n" \
                    "\t\tlevel: critical\n" \
                    "\t\t\tmsg: system time is very high.\n" \
                    "\t\t\taction_name: nothing\n" \
                    "event:\n" \
                    "\tname: CPU idle\n" \
                    "\tmodel_id: 2\n" \
                    "\tmetric:\n" \
                    "\t\tmetric_name: idle#cpu1\n" \
                    "\t\tcomponents: *\n" \
                    "\tseverity:\n" \
                    "\t\tlevel: nominal\n" \
                    "\t\t\tmsg: Normal\n" \
                    "\t\t\taction_name: resolve\n" \
                    "\t\tlevel: critical\n" \
                    "\t\t\tmsg: CPU is mostly in idle.\n" \
                    "\t\t\taction_name: log\n" \
                    "user-event:\n" \
                    "\tname: reboot\n" \
                    "\tcomponents: Compute Node{*}\n" \
                    "\tseverity:\n" \
                    "\t\tlevel: critical\n" \
                    "\t\t\tmsg: Reboot signaled by users\n" \
                    "\t\t\taction_name: log\n"
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

    def test_get_cable_types(self):
        result = oconvert_sqlite.get_cable_types(self.cursor)
        exp_result = ({1: cable_type_IPoIB, 2: cable_type_IPoE}, [2, 1])
        self.assertEqual(result, exp_result)

    def test_get_cable_types_text(self):
        result = oconvert_sqlite.get_cable_types_text(self.cursor, TEST_ALL_CABLE_TYPES, \
                                                      TEST_CABLE_TYPES_ORDER)
        exp_result = "" \
                    "cable_type:\n" \
                    "\ttype: IP over Ethernet\n" \
                    "\tdesc: Ethernet connection\n" \
                    "cable_type:\n" \
                    "\ttype: IPoIB\n" \
                    "\tdesc: Infiniband connection\n"
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "---------------"
            print exp_result
            print "==============="
            raise AssertionError(e)

    def test_get_cables(self):
        result = oconvert_sqlite.get_cables(self.cursor, TEST_ALL_CABLE_TYPES)
        exp_result = TEST_ALL_CABLES
        self.assertEqual(result, exp_result)

    def test_get_cables_text(self):
        result = oconvert_sqlite.get_cables_text(self.cursor, TEST_ALL_CABLES)
        exp_result = "cables:\n" \
                    "\t192.168.0.100\tIP over Ethernet\tCompute Node{node1}\tCompute Node{node2}\n" \
                    "\t192.168.0.101\tIP over Ethernet\tCompute Node{node1}\tCompute Node{node3}\n" \
                    "\t192.168.100.100\tIPoIB\tCompute Node{node1}\tCompute Node{node2}\n" \
                    "\t192.168.100.101\tIPoIB\tCompute Node{node1}\tCompute Node{node3}\n"
        self.assertEqual(result, exp_result)

def suite(testcase = None):
    if testcase is None:
        return unittest.TestLoader().loadTestsFromTestCase(TestOConvert_sqlite)
    else:
        return unittest.TestSuite(map(TestOConvert_sqlite, [testcase]))

if __name__ == "__main__":
    init_once()
    parser = ArgumentParser()
    parser.add_argument('--test', help = "Function name to be tested", \
                        default = None)
    parser.add_argument('--conf_db', help="Path to the test database. " \
                        "The test database is at " \
                        "${install-dir}/var/oconvert_tests/ovis_conf.db", \
                        default = "/opt/ovis/var/oconvert_tests/ovis_conf.db")
    args = parser.parse_args()
    try:
        conn = sqlite3.connect(args.conf_db)
        TestOConvert_sqlite.cursor = conn.cursor()
    except Exception, e:
        raise Exception(e)

    if args.test is not None:
        args.test = "test_" + args.test
    suite = suite(args.test)
    unittest.TextTestRunner(verbosity=2).run(suite)
    conn.close()
