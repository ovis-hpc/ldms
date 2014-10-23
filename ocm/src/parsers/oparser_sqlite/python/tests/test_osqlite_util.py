#!/usr/bin/env python
'''
Created on Oct 30, 2014

@author: nichamon
'''
import unittest
import sqlite3

from oconf_sqlite import osqlite_util
from argparse import ArgumentParser

class TestOSqlite_util(unittest.TestCase):
    cursor = None

    def setUp(self):
        self.identifiers0 = ["aaa"]
        self.identifiers1 = ["aaa1bbb1", "aaa1bbb2"]
        self.identifiers2 = ["aaa1bbb1", "ccc1ddd1"]
        self.identifiers3 = ["aaa1bbb1", "aaa1bbb2", "ccc1ddd1"]

    def tearDown(self):
        pass

    def test_get_format(self):
        result = osqlite_util.get_format(self.identifiers0[0])
        self.assertTupleEqual(result, (self.identifiers0[0], self.identifiers0[0], 0, []))

        result = osqlite_util.get_format(self.identifiers1[0])
        self.assertTupleEqual(result, \
                        ("aaa([0-9]+)bbb([0-9]+)", "aaa{0[0]}bbb{0[1]}", 2, ["1", "1"]))

        result = osqlite_util.get_format("aaa1bbb")
        self.assertTupleEqual(result, ("aaa([0-9]+)bbb", "aaa{0[0]}bbb", 1, ["1"]))

        result = osqlite_util.get_format("10Gb ethernet 1")
        self.assertTupleEqual(result, ("([0-9]+)Gb ethernet ([0-9]+)", \
                                       "{0[0]}Gb ethernet {0[1]}", 2, ["10", "1"]))

    def test_get_range(self):
        _input = [str(x) for x in range(0,4)]
        result = osqlite_util._get_range(_input)
        self.assertEqual(result, "[0-3]")

        _input = ["4","3","2"]
        result = osqlite_util._get_range(_input)
        self.assertEqual(result, "[4,3,2]")

        _input = [str(x) for x in range(0,4)] + ["6"] + ["8", "9", "10"]
        result = osqlite_util._get_range(_input)
        self.assertEqual(result, "[0-3,6,8-10]")

        _input = ["0008", "0009", "0010"]
        result = osqlite_util._get_range(_input)
        self.assertEqual(result, "[0008-0010]")

    def test_collapse_string(self):
        result = osqlite_util.collapse_string(self.identifiers1)
        self.assertEqual(result, "aaa1bbb[1-2]")

        result = osqlite_util.collapse_string(self.identifiers2)
        self.assertEqual(result, "aaa1bbb1,ccc1ddd1")

        result = osqlite_util.collapse_string(self.identifiers3)
        self.assertEqual(result, "aaa1bbb[1-2],ccc1ddd1")

        s_list = ["10Gb Operational ethernet Port 1", "10Gb Operational ethernet Port 2", \
                  "10Gb Operational ethernet Port 3", "10Gb Operational ethernet Port 4"]
        result = osqlite_util.collapse_string(s_list)
        self.assertEqual(result, "10Gb Operational ethernet Port [1-4]")

    def test_find_lcs(self):
        result = osqlite_util.find_lcs("mgmt1.cpu1", "mgmt2.cpu1")
        exp_result = ("mgmt(.+).cpu1", "mgmt{0[0]}.cpu1", 1, "mgmt .cpu1")
        self.assertEqual(result, exp_result)

        result = osqlite_util.find_lcs("mgmt1.cpu1", "prod1.cpu1")

    def test_query_components(self):
        '''
        Query the component of type Cluster
        '''
        result = osqlite_util.query_components(self.cursor, comp_type = "Cluster")
        self.assertTupleEqual(result[0], \
                ("Test Cluster", "Cluster", "cluster", 1, None))

        '''
        Query the components of type Compute Node
        '''
        result = osqlite_util.query_components(self.cursor, comp_type = "Compute Node")
        self.assertEqual(result, [("node0001", "Compute Node", "node1", 2, "1"),
                 ("node0002", "Compute Node", "node2", 3, "1"),
                 ("node0003", "Compute Node", "node3", 4, "1")])

        '''
        Query the components of type hdd
        '''
        result = osqlite_util.query_components(self.cursor, comp_type = "HDD")
        self.assertEqual(result, [("HDD2", "HDD", "hdd2", 11, "1"),
                                  ("HDD1", "HDD", "hdd1", 12, "1")])

        '''
        Query the components of name 'node1'
        '''
        result = osqlite_util.query_components(self.cursor, comp_name = "node0001")
        exp_result = [("node0001", "Compute Node", "node1", 2, "1")]
        self.assertEqual(result, exp_result)

        '''
        Query the components of name 'cpu1'
        '''
        result = osqlite_util.query_components(self.cursor, comp_name = "cpu1")
        exp_result = [("cpu1", "cpu", "node1_cpu1", 5, "2"),
                      ("cpu1", "cpu", "node2_cpu1", 6, "3"),
                      ("cpu1", "cpu", "node3_cpu1", 7, "4")]
        self.assertEqual(result, exp_result)

        '''
        Query the components of type 'Compute Node' and name 'node2'
        '''
        result = osqlite_util.query_components(self.cursor, comp_type = "Compute Node", \
                                               comp_name = "node0002")
        exp_result = [("node0002", "Compute Node", "node2", 3, "1")]
        self.assertEqual(result, exp_result)

        '''
        Query the components that have the given parent ids
        '''
        result = osqlite_util.query_components(self.cursor, parent_id_s="2,14")
        exp_result = [("ResourceManager", "ResourceManager", "resourcemanager", 15, "2,14")]
        self.assertEqual(result, exp_result)

        '''
        Query all components
        '''
        result = osqlite_util.query_components(self.cursor)
        exp_result = [("Test Cluster", "Cluster", "cluster", 1, None),
                    ("node0001", "Compute Node", "node1", 2, "1"),
                    ("node0002", "Compute Node", "node2", 3, "1"),
                    ("node0003", "Compute Node", "node3", 4, "1"),
                    ("cpu1", "cpu", "node1_cpu1", 5, "2"),
                    ("cpu1", "cpu", "node2_cpu1", 6, "3"),
                    ("cpu1", "cpu", "node3_cpu1", 7, "4"),
                    ("cpu2", "cpu", "node1_cpu2", 8, "2"),
                    ("cpu2", "cpu", "node2_cpu2", 9, "3"),
                    ("cpu2", "cpu", "node3_cpu2", 10, "4"),
                    ("HDD2", "HDD", "hdd2", 11, "1"),
                    ("HDD1", "HDD", "hdd1", 12, "1"),
                    ("SSD", "SSD", "ssd", 13, "1"),
                    ("Hadoop", "Hadoop", "hadoop", 14, None),
                    ("ResourceManager", "ResourceManager", "resourcemanager", 15, "2,14"),
                    ("192.168.0.100", "IP over Ethernet", "192.168.0.100", 16, "2,3"),
                    ("192.168.0.101", "IP over Ethernet", "192.168.0.101", 17, "2,4"),
                    ("192.168.100.100", "IPoIB", "192.168.100.100", 18, "2,3"),
                    ("192.168.100.101", "IPoIB", "192.168.100.101", 19, "2,4")]

        self.assertEqual(result, exp_result)

    def test_query_comp_type_info(self):
        '''
        Query all component types
        '''
        result = osqlite_util.query_comp_type_info(self.cursor)
        exp_result = [("Cluster", "/test/gif_path", 1, "physical"),
                      ("Compute Node", "", 0, None),
                      ("cpu", "", 1, None),
                      ("HDD", "", 1, None),
                      ("SSD", "", 1, None),
                      ("Hadoop", "", 1, "hadoop"),
                      ("ResourceManager", "", 1, "hadoop"),
                      ("IP over Ethernet", None, 1, None),
                      ("IPoIB", None, 1, None)]
        self.assertEqual(result, exp_result)

        '''
        Query a particular component type
        '''
        result = osqlite_util.query_comp_type_info(self.cursor, "Cluster")
        exp_result = [("Cluster", "/test/gif_path", True, "physical")]
        self.assertEqual(result, exp_result)

    def test_query_root_components(self):
        result = osqlite_util.query_root_components(self.cursor)
        exp_result = [("Test Cluster", "Cluster", "cluster", 1),
                      ("Hadoop", "Hadoop", "hadoop", 14)]
        self.assertEqual(result, exp_result)

    def test_query_children_components(self):
        # 2 is the comp_id of node1
        result = osqlite_util.query_children_components(self.cursor, 2)
        exp_result = [("cpu1", "cpu", "node1_cpu1", 5),
                      ("cpu2", "cpu", "node1_cpu2", 8),
                      ("ResourceManager", "ResourceManager", "resourcemanager", 15),
                      ("192.168.0.100", "IP over Ethernet", "192.168.0.100", 16),
                      ("192.168.0.101", "IP over Ethernet", "192.168.0.101", 17),
                      ("192.168.100.100", "IPoIB", "192.168.100.100", 18),
                      ("192.168.100.101", "IPoIB", "192.168.100.101", 19)]
        self.assertEqual(result, exp_result)

    def test_query_template_names(self):
        result = osqlite_util.query_template_names(self.cursor)
        exp_result = [("procdiskstats", ), ("procstat",)]
        self.assertEqual(result, exp_result)

    def test_get_metric_name_base(self):
        result = osqlite_util.get_metric_name_base("idle")
        self.assertEqual(result, "idle")

        result = osqlite_util.get_metric_name_base("idle#cpu1")
        self.assertEqual(result, "idle")

        result = osqlite_util.get_metric_name_base("idle#cpu1[1234]")
        self.assertEqual(result, "idle")

        result = osqlite_util.get_metric_name_base("idle[1234]")
        self.assertEqual(result, "idle")

    def test_process_metric_name_id_string(self):
        result = osqlite_util.process_metric_name_id_string("base#extend[")
        self.assertIsNone(result)

        result = osqlite_util.process_metric_name_id_string("base[1234]")
        exp_result = {'base_name': "base",
                      'extension': "",
                      'full_name': "base",
                      'metric_id': "1234"}
        self.assertEqual(result, exp_result)

        result = osqlite_util.process_metric_name_id_string("base#extend[1234]")
        exp_result = {'base_name': "base",
                      'extension': "extend",
                      'full_name': "base#extend",
                      'metric_id': "1234"}
        self.assertEqual(result, exp_result)

    def test_extract_metric_name_bases(self):
        s = "iowait#cpu1[30064771073],idle#cpu1[30064771074]," \
                       "user#cpu1[30064771075],sys#cpu1[30064771076]," \
                       "iowait#cpu2[42949672961],idle#cpu2[42949672962]," \
                       "user#cpu2[42949672963],sys#cpu2[42949672964]"
        result = osqlite_util.extract_metric_and_component(s)
        exp_result = ["iowait", "idle", "user", "sys"]
        self.assertEqual(result, exp_result)

    def test_query_template_info(self):
        # template of the procstat
        result = osqlite_util.query_template_info(self.cursor, "procstat")
        procstat_template = [("procstat", "procstatutil",
                              "interval:2000000",
                              "iowait#cpu1[30064771073],idle#cpu1[30064771074]," \
                              "user#cpu1[30064771075],sys#cpu1[30064771076]," \
                              "iowait#cpu2[42949672961],idle#cpu2[42949672962]," \
                              "user#cpu2[42949672963],sys#cpu2[42949672964]")]
        self.assertEqual(result, procstat_template)

        # all templates
        result = osqlite_util.query_template_info(self.cursor)
        procdiskstats_template = [("procdiskstats", "procdiskstats",
                                   "device:sda,sdb;interval:2000000",
                                   "reads_comp#sdb[55834574855]," \
                                   "reads_merg#sdb[55834574856]," \
                                   "time_read#sdb[55834574857]," \
                                   "reads_comp#sda[51539607557]," \
                                   "reads_merg#sda[51539607558]")]
        exp_result = procdiskstats_template + procstat_template
        self.assertEqual(result, exp_result)

    def test_get_comp_id(self):
        a = 1
        b = 2
        c = (a << 32) | b
        result = osqlite_util.get_comp_id(c)
        self.assertEqual(result, a)

    def test_get_metric_type_id(self):
        a = 1
        b = 2
        c = (a << 32) | b
        result = osqlite_util.get_metric_type_id(c)
        self.assertEqual(result, b)

    def test_query_template_apply_on(self):
        # Apply one template on the node
        result = osqlite_util.query_template_apply_on(self.cursor, "node0001")
        exp_result_node1 = [("node0001", "procstat", "iowait[21474836481],idle[21474836482]," \
                       "user[21474836483],sys[21474836484]")]
        self.assertEqual(result, exp_result_node1)

        result = osqlite_util.query_template_apply_on(self.cursor, "node0003")
        exp_result_node3 = [("node0003", "procdiskstats", "reads_comp#sdb[55834574855]," \
                       "reads_merg#sdb[55834574856],time_read#sdb[55834574857]," \
                       "reads_comp#sda[51539607557],reads_merg#sda[51539607558]"),
                      ("node0003", "procstat", "iowait#cpu1[30064771073]," \
                       "idle#cpu1[30064771074],user#cpu1[30064771075]," \
                       "sys#cpu1[30064771076],iowait#cpu2[42949672961]," \
                       "idle#cpu2[42949672962],user#cpu2[42949672963]," \
                       "sys#cpu2[42949672964]")]
        self.assertEqual(result, exp_result_node3)

        exp_result_node2 = [("node0002", "procstat", "iowait#cpu1[25769803777]," \
                             "idle#cpu1[25769803778],user#cpu1[25769803779]," \
                             "sys#cpu1[25769803780],iowait#cpu2[38654705665]," \
                             "idle#cpu2[38654705666],user#cpu2[38654705667]," \
                             "sys#cpu2[38654705668]")]
        result = osqlite_util.query_template_apply_on(self.cursor)
        exp_result = exp_result_node3 + exp_result_node2 + exp_result_node1
        self.assertEqual(result, exp_result)

    def test_query_metrics(self):
        metric_id_list = [21474836481, 21474836482]
        result = osqlite_util.query_metrics(self.cursor, metric_id_list)
        exp_result = [("iowait", 21474836481, "procstatutil", 1, "node0001", 5),
                      ("idle", 21474836482, "procstatutil", 2, "node0001", 5)]
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

        result = osqlite_util.query_metrics(self.cursor, metric_name = '%#baler_ptn')
        exp_result = [("node0001#baler_ptn", 8589934602, "Baler", 10, "node0001", 2),
                      ("node0002#baler_ptn", 12884901898, "Baler", 10, "node0002", 3),
                      ("node0003#baler_ptn", 17179869194, "Baler", 10, "node0003", 4)]
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

        result = osqlite_util.query_metrics(self.cursor, [21474836481, 21474836482], "iowait")
        exp_result = [("iowait", 21474836481, "procstatutil", 1, "node0001", 5)]
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

        result = osqlite_util.query_metrics(self.cursor, mtype_id_list = [10])
        exp_result = [("node0001#baler_ptn", 8589934602, "Baler", 10, "node0001", 2),
                      ("node0002#baler_ptn", 12884901898, "Baler", 10, "node0002", 3),
                      ("node0003#baler_ptn", 17179869194, "Baler", 10, "node0003", 4)]
        try:
            self.assertEqual(result, exp_result)
        except AssertionError, e:
            print ""
            print result
            print "------------------"
            print exp_result
            print "=================="
            raise AssertionError(e)

        result = osqlite_util.query_metrics(self.cursor, metric_name = '[*]#baler_ptn')
        self.assertTrue(result is None)

    def test_is_hexadecimal(self):
        result = osqlite_util.is_hexadecimal("0000a")
        self.assertTrue(result)

        result = osqlite_util.is_hexadecimal("a")
        self.assertTrue(result)

        result = osqlite_util.is_hexadecimal("h")
        self.assertFalse(result)

    def test_get_num_filled_zeros(self):
        result = osqlite_util.get_num_filled_zeros("a")
        self.assertEqual(result, 0)

        result = osqlite_util.get_num_filled_zeros("0000a")
        self.assertEqual(result, 5)

        try:
            result = osqlite_util.get_num_filled_zeros("000h0")
        except TypeError:
            self.assertTrue(True)
        except Exception:
            self.assertTrue(False)

        result = osqlite_util.get_num_filled_zeros("10")
        self.assertEqual(result, 0)

        result = osqlite_util.get_num_filled_zeros("010")
        self.assertEqual(result, 3)

    def test_query_service_conf(self):
        result = osqlite_util.query_service_conf(self.cursor, "me")
        exp_result = [("me", "node0001", "config", "name:consumer_kmd;" \
                       "host:node0001;xprt:sock;port:10004"),
                      ("me", "node0001", "start_consumer", "name:consumer_kmd")]
        self.assertEqual(result, exp_result)

    def test_query_baler_ptn_metrics(self):
        result = osqlite_util.query_baler_ptn_metrics(self.cursor)
        exp_result = [("node0001#baler_ptn", 8589934602, "Baler", 10, "node0001", 2),
                      ("node0002#baler_ptn", 12884901898, "Baler", 10, "node0002", 3),
                      ("node0003#baler_ptn", 17179869194, "Baler", 10, "node0003", 4)]
        self.assertEqual(result, exp_result)

    def test_query_models(self):
        result = osqlite_util.query_models(self.cursor, [1])
        exp_result = [(1, "model_low_exact", None, "0,0,0", "1111")]
        self.assertEqual(result, exp_result)

        result = osqlite_util.query_models(self.cursor)
        exp_result = [(1, "model_low_exact", None, "0,0,0", "1111"),
                      (2, "model_high_exact", None, "60,60,70", "1011"),
                      (4, "model_baler_rules", "/etc/ovis/baler_test_rules,1m", "0,0,0", "")]
        self.assertEqual(result, exp_result)

    def test_query_action(self):
        result = osqlite_util.query_actions(self.cursor, ['nothing'])
        exp_result = [("nothing", "/opt/ovis/action_scripts/do_nothing", "not-corrective")]
        self.assertEqual(result, exp_result)

        result = osqlite_util.query_actions(self.cursor)
        exp_result = [("nothing", "/opt/ovis/action_scripts/do_nothing", "not-corrective"),
                      ("resolve", "/opt/ovis/action_scripts/resolve", "corrective"),
                      ("log", "/opt/ovis/action_scripts/ovis_actions/iscb_actions.py info", "corrective")]
        self.assertEqual(result, exp_result)

    def test_query_event_templates(self):
        result = osqlite_util.query_event_templates(self.cursor)
        exp_result = [(1, "Test Event", 4),
                      (2, "CPU load", 2),
                      (3, "CPU idle", 2),
                      (4, "reboot", 65535)]
        self.assertEqual(result, exp_result)

    def test_query_event_metircs(self):
        result = osqlite_util.query_event_metrics(self.cursor, [2])
        exp_result = [(2, 6, 25769803780),
                      (2, 9, 38654705668)]
        self.assertEqual(result, exp_result)

        result = osqlite_util.query_event_metrics(self.cursor, [1, 2])
        exp_reult = [(1, 3, 12884901898),
                     (1, 4, 17179869194),
                     (2, 6, 25769803780),
                     (2, 9, 38654705668)]
        self.assertEqual(result, exp_reult)

        result = osqlite_util.query_event_metrics(self.cursor)
        exp_reult = [(1, 3, 12884901898),
                     (1, 4, 17179869194),
                     (2, 6, 25769803780),
                     (2, 9, 38654705668),
                     (3, 6, 25769803778),
                     (3, 7, 30064771074),
                     (4, 2, 12616466433),
                     (4, 3, 16911433729),
                     (4, 4, 21206401025)]
        self.assertEqual(result, exp_reult)

        result = osqlite_util.query_event_metrics(self.cursor, [10000000])
        self.assertTrue(result is None)

    def test_query_event_msg_action(self):
        result = osqlite_util.query_event_msg_action(self.cursor, [1])
        exp_result = [(1, 3, "Test Event", "log"),
                      (1, 3, "Test Event", "resolve")]
        self.assertEqual(result, exp_result)

        result = osqlite_util.query_event_msg_action(self.cursor, [1], [3])
        exp_result = [(1, 3, "Test Event", "log"),
                      (1, 3, "Test Event", "resolve")]
        self.assertEqual(result, exp_result)

        result = osqlite_util.query_event_msg_action(self.cursor, level_list=[0])
        exp_result = [(2, 0, "Normal", "resolve"),
                      (3, 0, "Normal", "resolve")]
        self.assertEqual(result, exp_result)

        result = osqlite_util.query_event_msg_action(self.cursor)
        exp_result = [(1, 3, "Test Event", "log"),
                      (1, 3, "Test Event", "resolve"),
                      (2, 0, "Normal", "resolve"),
                      (2, 2, "system time is high.", "nothing"),
                      (2, 3, "system time is very high.", "nothing"),
                      (3, 0, "Normal", "resolve"),
                      (3, 3, "CPU is mostly in idle.", "log"),
                      (4, 3, "Reboot signaled by users", "log")]

        result = osqlite_util.query_event_msg_action(self.cursor, event_id_list=[10000])
        self.assertTrue(result is None)

    def test_query_cable_types(self):
        result = osqlite_util.query_cable_types(self.cursor, ["IPoIB"])
        exp_result = [(1, "IPoIB", "Infiniband connection")]
        self.assertEqual(result, exp_result)

        result = osqlite_util.query_cable_types(self.cursor)
        exp_result = [(1, "IPoIB", "Infiniband connection"), \
                      (2, "IP over Ethernet", "Ethernet connection")]
        self.assertEqual(result, exp_result)

    def test_query_cables(self):
        result = osqlite_util.query_cables(self.cursor, cable_type_id_list = [1])
        exp_result = [(1, 2, 4), (1, 2, 3)]
        self.assertEqual(result, exp_result)

        result = osqlite_util.query_cables(self.cursor)
        exp_result = [(1, 2, 4), (1, 2, 3), \
                      (2, 2, 4), (2, 2, 3)]
        self.assertEqual(result, exp_result)

def suite(testcase = None):
    if testcase is None:
        return unittest.TestLoader().loadTestsFromTestCase(TestOSqlite_util)
    else:
        return unittest.TestSuite(map(TestOSqlite_util, [testcase]))

if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument('-t', '--test', help = "Function name to be tested", \
                        default = None)
    parser.add_argument('--conf_db', help="Path to the test database. " \
                        "The test database is at " \
                        "${install-dir}/var/oconvert_tests/ovis_conf.db", \
                        default = "/opt/ovis/var/oconvert_tests/ovis_conf.db")
    args = parser.parse_args()
    try:
        conn = sqlite3.connect(args.conf_db)
        TestOSqlite_util.cursor = conn.cursor()
    except Exception, e:
        raise Exception(e)
    if args.test is not None:
        args.test = "test_" + args.test
    suite = suite(args.test)
    unittest.TextTestRunner(verbosity=2).run(suite)
    conn.close()
