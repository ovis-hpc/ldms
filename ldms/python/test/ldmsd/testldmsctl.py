'''
Created on Nov 2, 2017

@author: monn
'''
import unittest
import subprocess as sp
import os
import signal
from subprocess import CalledProcessError
from time import sleep
import errno

class TestLdmsctl(unittest.TestCase):
    HOST = "localhost"
    XPRT = "sock"
    DATA_PORT = "10001"
    CFG_PORT = "20001"
    SOCKNAME = "/tmp/ovis/var/run/ldmsd.sock"
    AUTHPATH = "~/.ldmsauth.conf"
    CONFFILE = "/tmp/ovis/conf/test.conf"
    
    def setUp(self):
        try:
            os.remove(self.SOCKNAME)
        except OSError:
            pass
        cmd = "ldmsd -x {0}:{1} -p sock:{2} -p unix:{3} -v QUIET -F".format(
                                            self.XPRT, self.DATA_PORT,
                                            self.CFG_PORT, self.SOCKNAME)
        self.ldmsd = sp.Popen(cmd, stdout=sp.PIPE, shell=True, preexec_fn=os.setsid)
        # Wait for ldmsd to setup itself
        sleep(1)

    def tearDown(self):
        os.killpg(os.getpgid(self.ldmsd.pid), signal.SIGTERM)

    def test_unix_domain_socket(self):
        cmd = "echo \"greeting\" | ldmsctl -S {0} -a {1}".format(self.SOCKNAME, self.AUTHPATH)
        try:
            ldmsctl_out = sp.check_output(cmd, shell=True)
            self.assertEqual(ldmsctl_out, "\n", ldmsctl_out)
        except CalledProcessError as e:
            self.assertTrue(False, e.output)

    def test_auto_get_secretword(self):
        cmd = "echo \"greeting\" | ldmsctl -h {0} -p {1}".format(self.HOST, self.CFG_PORT)
        try:
            ldmsctl_out = sp.check_output(cmd, shell=True)
            self.assertEqual(ldmsctl_out, "\n", ldmsctl_out)
        except CalledProcessError as e:
            self.assertTrue(False, e.output)

    def test_tcpip_socket(self):
        cmd = "echo \"greeting\" | ldmsctl -h {0} -p {1}".format(self.HOST, self.CFG_PORT)
        try:
            ldmsctl_out = sp.check_output(cmd, shell=True)
            self.assertEqual(ldmsctl_out, "\n", ldmsctl_out)
        except CalledProcessError as e:
            self.assertTrue(False, e.output)

    def test_inband(self):
        cmd = "echo \"greeting\" | ldmsctl -h {0} -p {1} -x {2} -i".format(self.HOST,
                                                    self.DATA_PORT, self.XPRT)
        
        try:
            ldmsctl_out = sp.check_output(cmd, shell=True)
            self.assertEqual(ldmsctl_out, "\n", ldmsctl_out)
        except CalledProcessError as e:
            self.assertTrue(False, e.output)

    def test_recv_resp(self):
        cmd = "echo \"greeting name=foo\" | ldmsctl -h {0} -p {1}".format(
                                                self.HOST, self.CFG_PORT)
        try:
            ldmsctl_out = sp.check_output(cmd, shell=True)
            self.assertEqual(ldmsctl_out, "Hello 'foo'\n", ldmsctl_out)
        except CalledProcessError as e:
            self.assertTrue(False, e.output)

    def test_keyword(self):
        cmd = "echo \"greeting test\" | ldmsctl -h {0} -p {1}".format(
                                                self.HOST, self.CFG_PORT)     
        try:
            ldmsctl_out = sp.check_output(cmd, shell=True)
            self.assertEqual(ldmsctl_out, "Hi\n", ldmsctl_out)
        except CalledProcessError as e:
            self.assertTrue(False, e.output)

    def test_script(self):
        cmd = "ldmsctl -h {0} -p {1} -X 'echo greeting name=foo'".format(
                                                self.HOST, self.CFG_PORT)
        try:
            ldmsctl_out = sp.check_output(cmd, shell=True)
            self.assertEqual(ldmsctl_out, "Hello 'foo'\n", ldmsctl_out)
        except CalledProcessError as e:
            self.assertTrue(False, e.output)

    def test_source(self):
        pass

if __name__ == "__main__":
    try:
        os.makedirs("/tmp/ovis/var/run/")
        os.makedirs("/tmp/ovis/var/log/")
        os.makedirs("/tmp/ovis/conf/")
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise

    unittest.TestLoader.testMethodPrefix = "test_"
    unittest.main()