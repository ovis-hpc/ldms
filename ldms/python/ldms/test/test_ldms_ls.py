import os
import subprocess as sp
import unittest
import logging
import signal
 
class TestLdmsls(unittest.TestCase):

    SCHEMA_NAMES = {'AAA', 'BBB', 'ABC'}
    XPRT = "sock"
    PORT = "10001"
    AUTH_FILE = "~/.ldmsauth.conf"
    
    LDMS_LS_CMD = "ldms_ls -x " + XPRT + " -p " + PORT + " -a " + AUTH_FILE

    def setUp(self):
        cmd = "test_ldms -x " + self.XPRT + " -p " + self.PORT + " -a " + self.AUTH_FILE
        for sn in self.SCHEMA_NAMES:
            cmd += " -s " + sn
        for sn in self.SCHEMA_NAMES:
            cmd += " -i " + sn + ":set_" + sn
        self.proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True, preexec_fn=os.setsid)

    def tearDown(self):
        os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)

    def test_Regex_Instance(self):
        cmd = self.LDMS_LS_CMD + " -E " + "A"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_AAA", "set_ABC"]
        for s in proc.stdout:
            s = s.strip()
            self.assertTrue(s in expected_sets, "Fail '-E' test. Unexpected set " + s)
            expected_sets.remove(s)
        self.assertTrue(len(expected_sets) == 0, "Fail '-E' test. Set " + ", ".join(expected_sets) + " not returned")

        cmd = self.LDMS_LS_CMD + " -E " + "BB C"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_BBB", "set_ABC"]
        for s in proc.stdout:
            s = s.strip()
            self.assertTrue(s in expected_sets, "Fail '-E' test. Unexpected set " + s)
            expected_sets.remove(s)
        self.assertTrue(len(expected_sets) == 0, "Fail '-E' test. Set " + ", ".join(expected_sets) + " not returned")

    def test_Regex_Instance_Exact_String(self):
        cmd = self.LDMS_LS_CMD + " -E " + "^set_AAA$"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_AAA"]
        for s in proc.stdout:
            s = s.strip()
            self.assertTrue(s in expected_sets, "Fail '-E' test. Unexpected set " + s)
            expected_sets.remove(s)
        self.assertTrue(len(expected_sets) == 0, "Fail '-E' test. Set " + ", ".join(expected_sets) + " not returned")

    def test_Regex_Instance_Not_Exist(self):
        cmd = self.LDMS_LS_CMD + " -E " + "BA"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        count = 0
        for s in proc.stdout:
            s = s.strip()
            if count == 0:
                self.assertTrue(s == "ldms_ls: No metric sets matched the given criteria", "Failed -E <not match>")
            else: 
                self.assertEqual(count, 1, "Unexpected string {0}".format(s))
            count += 1

    def test_Regex_Schema(self):
        cmd = self.LDMS_LS_CMD + " -E -S " + "A"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_AAA", "set_ABC"]
        for s in proc.stdout:
            s = s.strip()
            self.assertTrue(s in expected_sets, "Fail '-E -S ' test. Unexpected set " + s)
            expected_sets.remove(s)
        self.assertTrue(len(expected_sets) == 0, "Fail '-E -S' test. Set " + ", ".join(expected_sets) + " not returned")

        cmd = self.LDMS_LS_CMD + " -E -S " + "BB C"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_BBB", "set_ABC"]
        for s in proc.stdout:
            s = s.strip()
            self.assertTrue(s in expected_sets, "Fail '-E -S ' test. Unexpected set " + s)
            expected_sets.remove(s)
        self.assertTrue(len(expected_sets) == 0, "Fail '-E -S' test. Set " + ", ".join(expected_sets) + " not returned")

    def test_Regex_Schema_Exact_String(self):
        cmd = self.LDMS_LS_CMD + " -E -S " + "^AAA$"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_AAA"]
        for s in proc.stdout:
            s = s.strip()
            self.assertTrue(s in expected_sets, "Fail '-E -S' test. Unexpected set " + s)
            expected_sets.remove(s)
        self.assertTrue(len(expected_sets) == 0, "Fail '-E -S' test. Set " + ", ".join(expected_sets) + " not returned")

    def test_Regex_Schema_Not_Exist(self):
        cmd = self.LDMS_LS_CMD + " -E -S " + "BA"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        count = 0
        for s in proc.stdout:
            s = s.strip()
            if count == 0:
                self.assertTrue(s == "ldms_ls: No metric sets matched the given criteria", "Failed -E -S <not match>")
            else: 
                self.assertEqual(count, 1, "Unexpected string {0}".format(s))
            count += 1

    def test_Regex_Instance_verbose(self):
        cmd = self.LDMS_LS_CMD + " -v -E " + "A"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_AAA", "set_ABC"]
        for s in proc.stdout:
            s = s.strip()
            if "last update" in s:
                ss = s.split(":")[0]
                self.assertTrue(ss in expected_sets, "Fail '-v -E' test. Unexpected set " + s)
                expected_sets.remove(ss)
        self.assertTrue(len(expected_sets) == 0, "Fail '-v -E' test. Set " + ", ".join(expected_sets) + " not returned")        

        cmd = self.LDMS_LS_CMD + " -v -E " + "BB C"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_BBB", "set_ABC"]
        for s in proc.stdout:
            s = s.strip()
            if "last update" in s:
                ss = s.split(":")[0]
                self.assertTrue(ss in expected_sets, "Fail '-v -E' test. Unexpected set " + s)
                expected_sets.remove(ss)
        self.assertTrue(len(expected_sets) == 0, "Fail '-v -E' test. Set " + ", ".join(expected_sets) + " not returned")

    def test_Regex_Instance_verbose_Not_Exist(self):
        cmd = self.LDMS_LS_CMD + " -v -E " + "CB"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        count = 0
        for s in proc.stdout:
            s = s.strip()
            if count == 0:
                self.assertTrue(s == "ldms_ls: Error 2 looking up metric set.", "Failed -v -E <not match>")
            else: 
                self.assertEqual(count, 1, "Unexpected string {0}".format(s))
            count += 1

    def test_Regex_Schema_verbose(self):
        cmd = self.LDMS_LS_CMD + " -v -E -S " + "A"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_AAA", "set_ABC"]
        for s in proc.stdout:
            s = s.strip()
            if "last update" in s:
                ss = s.split(":")[0]
                self.assertTrue(ss in expected_sets, "Fail '-v -E -S' test. Unexpected set " + s)
                expected_sets.remove(ss)
        self.assertTrue(len(expected_sets) == 0, "Fail '-v -E -S' test. Set " + ", ".join(expected_sets) + " not returned")

        cmd = self.LDMS_LS_CMD + " -v -E -S " + "BB C"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        expected_sets = ["set_BBB", "set_ABC"]
        for s in proc.stdout:
            s = s.strip()
            if "last update" in s:
                ss = s.split(":")[0]
                self.assertTrue(ss in expected_sets, "Fail '-v -E -S' test. Unexpected set " + s)
                expected_sets.remove(ss)
        self.assertTrue(len(expected_sets) == 0, "Fail '-v -E -S' test. Set " + ", ".join(expected_sets) + " not returned")

    def test_Regex_Schema_verbose_Not_Exist(self):
        cmd = self.LDMS_LS_CMD + " -v -E -S " + "CB"
        proc = sp.Popen(cmd, stdout=sp.PIPE, shell=True)
        count = 0
        for s in proc.stdout:
            s = s.strip()
            if count == 0:
                self.assertTrue(s == "ldms_ls: Error 2 looking up metric set.", "Failed -v -E -S <not match>")
            else: 
                self.assertEqual(count, 1, "Unexpected string {0}".format(s))
            count += 1

if __name__ == "__main__":
    LOGFMT = '%(asctime)s %(name)s %(levelname)s: %(message)s'
    logging.basicConfig(format=LOGFMT)
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)
    # default prefix is 'test_'
    unittest.TestLoader.testMethodPrefix = "test_"
    unittest.main()