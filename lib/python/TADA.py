import docker
import subprocess
import sys
import time
import os
import json
import copy
import socket

class Network(object):
    def __init__(self, name, driver='bridge', scope='local'):
        self.client = docker.from_env()
        self.network_name = name
        try:
            self.network = self.client.networks.get(name)
        except:
            self.network = None
        if self.network is None:
            self.network = self.client.networks.create(name=name, driver=driver, scope=scope)

    @property
    def name(self):
        return self.network_name

    @property
    def short_id(self):
        return self.short_id

    def rm(self):
        self.network.remove()
        self.network = None

class LdmsdContainer(object):
    def __init__(self, hostname, network,
                 image="ovis-centos-build",
                 prefix='/opt/ovis', data_root='/DATA',
                 filer='10.10.0.16',
                 listen_port=10000,
                 listen_xprt='sock',
                 config_file=None,
                 log_file=None,
                 log_level='ERROR',
                 auth_name='munge',
                 sample_interval=1000000,
                 component_id=10000,
                 environment=None):
        self.client = docker.from_env()
        self.hostname = hostname
        self.image = image
        self.network_name = network.name
        self.prefix = prefix
        self.data_root = data_root
        self.config_file = config_file
        self.listen_port = listen_port
        self.listen_xprt = listen_xprt
        self.cont_name=self.hostname + '-' + str(self.listen_port)
        if log_file:
            self.log_file = log_file
        else:
            self.log_file = '/var/log/' + self.cont_name + '.log'
        self.log_level = log_level
        self.auth_name = auth_name
        self.filer = filer
        self.container = None
        self.component_id = component_id
        self.sample_interval=sample_interval
        if environment:
            self.environment = environment
        else:
            env = []
            p = "LD_LIBRARY_PATH=/opt/ovis/lib64"
            e = os.getenv("LD_LIBRARY_PATH")
            if e:
                p += ':' + e
            env.append(p)

            env.append("PATH=/opt/ovis/bin:/opt/ovis/sbin:" + os.getenv("PATH"))
            env.append("PYTHONPATH=/opt/ovis/lib/python2.7/site-packages")
            env.append("LDMSD_PLUGIN_LIBPATH=/opt/ovis/lib64/ovis-ldms")
            env.append("ZAP_LIBPATH=/opt/ovis/lib64/ovis-lib")
            env.append("COMPONENT_ID={0}".format(self.component_id))
            env.append("LISTEN_PORT={0}".format(self.listen_port))
            env.append("SAMPLE_INTERVAL={0}".format(self.sample_interval))
            self.environment = env

        self.container = self.client.containers.run(
            self.image,
            detach=True,
            network=self.network_name,
            tty=True,
            stdin_open=True,
            user='root',
            name=self.cont_name,
            hostname=self.hostname,
            remove=True,
            volumes = {
                self.prefix : { 'bind' : '/opt/ovis', 'mode' : 'ro' },
                self.data_root : { 'bind' : '/data', 'mode' : 'rw' }
            },
            security_opt = [ 'seccomp=unconfined' ]
        )

    @property
    def name(self):
        return self.container.name

    @property
    def ip4_address(self):
        self.container.reload()
        return self.container.attrs['NetworkSettings']['Networks'][self.network_name]['IPAddress']

    def df(self):
        code, output = self.container.exec_run("/usr/bin/df")
        print output

    def ip_addr(self):
        code, output = self.container.exec_run("/usr/bin/ip addr")
        print output

    def exec_run(self, cmd, environment=None):
        if not environment:
            environment = self.environment
        return self.container.exec_run(cmd, environment=self.environment)

    def kill(self):
        self.container.kill()
        self.container = None

    def test_running(self, cmd):
        rc, pid = self.container.exec_run('pgrep '+cmd)
        if rc:
            return False
        else:
            return True

    def ldms_ls(self, host='localhost', listen_port=10000, listen_xprt='sock', opts=''):
        cmd = 'ldms_ls -h {host} -x {xprt} -p {port} -a {auth} ' + opts
        cmd = cmd.format(
            xprt=listen_xprt,
            port=listen_port,
            host=host,
            log=self.log_file,
            log_level=self.log_level,
            auth=self.auth_name
        )
        return self.container.exec_run(cmd, environment=self.environment)

    def kill_ldmsd(self):
        rc, output = self.container.exec_run('pkill ldmsd')
        return rc, output

    def start_ldmsd(self):
        if self.auth_name == 'munge':
            if not self.test_running('munged'):
                self.container.exec_run('/usr/sbin/munged')
            if not self.test_running('munged'):
                raise ValueError("Could not start munged but auth=munge")

        if not self.log_file:
            self.log_file = '/var/log/' + self.container.name + '.log'

        cmd = 'ldmsd -x {xprt}:{port} -H {host} -l {log} -v {log_level} -a {auth} -m 1m '
        if self.config_file:
            cmd += '-c {config}'

        cmd = cmd.format(
            xprt=self.listen_xprt,
            port=self.listen_port,
            config=self.config_file,
            host=self.hostname,
            log=self.log_file,
            log_level=self.log_level,
            auth=self.auth_name
        )

        rc, output = self.container.exec_run(cmd, environment=self.environment)
        if rc != 0:
            print("Error {0} running \n{1}".format(rc, cmd))
            print("Output:")
            print(output)
        else:
            if not self.test_running('ldmsd'):
                print("The ldmsd daemon failed to start. Check the log file {0}".\
                      format(self.log_file))
                rc, out = self.exec_run('cat ' + self.log_file)
                print(out)
        return rc

    def config_ldmsd(self, cmds):
        if not self.test_running('ldmsd'):
            print("There is no running ldmsd to configure")
            return

        for c in cmds:
            cmd = "/bin/bash -c 'echo {command} | ldmsd_controller --host {host} " \
                  "--xprt {xprt} " \
                  "--port {port} " \
                  "--auth {auth} '" \
                      .format(
                          command=c,
                          xprt=self.listen_xprt,
                          port=self.listen_port,
                          auth=self.auth_name,
                          host=self.ip4_address)
            rc, output = self.container.exec_run(cmd, environment=self.environment)
            if rc != 0 or len(output) > 0:
                print("Error {0} running \n{1}".format(rc, cmd))
                print("Output: {0}".format(output))
                rc = 1
        return rc, output

class Test(object):
    TEST_PASSED = 0
    TEST_FAILED = 1
    TEST_SKIPPED = 2
    TADA_HOST = "tada-host"
    TADA_PORT = 9862

    def __init__(self, fname, prefix, data_root, tada_addr):
        self.config = None
        self.daemons = {}
        self.ldmsd = {}
        self.network = None
        self.prefix = prefix
        self.data_root = data_root
        self.tada_addr = tada_addr # host:port
        self.assertions = {}

        try:
            self.config = json.load(open(fname))
        except Exception as e:
            print(e)
            raise ValueError("The specified configuration file is invalid.")

        self.test_suite = self.config['test_suite']
        self.test_name = self.config['test_name']
        self.test_type = self.config['test_type']

        if 'daemons' in self.config:
            for sd in self.config['daemons']:
                daemon = {}
                if 'asset' in sd:
                    asset_name = sd['asset']
                    # find the asset to inherit from
                    for asset in self.config['define']:
                        if asset_name == asset['name']:
                            daemon = copy.deepcopy(asset)
                            break
                for attr in sd:
                    if attr in daemon:
                        # override or augment existing attribute
                        if type(sd[attr]) == list:
                            daemon[attr] += sd[attr]
                        elif type(sd[attr]) == dict:
                            for a in sd[attr]:
                                daemon[attr][a] = sd[attr][a]
                        else:
                            daemon[attr] = sd[attr]
                    else:
                        # set attribute from config
                        daemon[attr] = sd[attr]
                self.daemons[sd['host']] = daemon
        self.network = Network(self.config['test_suite'])

    def send_assert_status(self, assert_no, cond_str):
        status = self.assertions[assert_no]['result']
        if status == self.TEST_PASSED:
            resstr = "passed"
        elif status == self.TEST_FAILED:
            resstr = "failed"
        else:
            resstr = "skipped"
        msg = '{{ "msg-type" : "assert-status",'\
              '"test-suite" : "{0}",'\
              '"test-type" : "{1}",'\
              '"test-name" : "{2}",'\
              '"assert-no\" : {3},'\
              '"assert-desc" : "{4}",'\
              '"assert-cond" : "{5}",'\
              '"test-status" : "{6}" }}'\
                  .format(self.test_suite,
                          self.test_type,
                          self.test_name,
                          assert_no,
                          self.assertions[assert_no]['description'],
                          cond_str,
                          resstr)
        self.sock_fd.sendto(msg.encode('utf-8'), (self.tada_host, self.tada_port))

    def assert_test(self, assert_no, condition, cond_str):
        if condition:
            self.assertions[assert_no]['result'] = self.TEST_PASSED
        else:
            self.assertions[assert_no]['result'] = self.TEST_FAILED
        self.send_assert_status(assert_no, cond_str)
        return condition

    def assertion(self, assert_no, description):
        self.assertions[assert_no] = { "result" : self.TEST_SKIPPED, "description" : description }

    def get_host(self, name):
        return self.ldmsd[name]

    def start_hosts(self):
        # Create our own namespace for the daemon host names
        conts = {}

        # Start each daemon in a container
        for host, d in self.daemons.iteritems():
            cont = LdmsdContainer(self.config['test_suite']+'-'+host,
                                  self.network,
                                  listen_port=d['listen_port'],
                                  listen_xprt=d['listen_xprt'],
                                  prefix=self.prefix,
                                  data_root=self.data_root,
                                  log_level='DEBUG')
            cont.environment.append("TADA_ADDR={0}".format(self.tada_addr))
            conts[host] = cont

        self.ldmsd = conts

        # Add host entries for test
        for s_host, server in self.servers():
            # Add implicit hosts
            hostent = "{0} {1}".format(server.ip4_address, s_host)
            for c_host, client in self.servers():
                rc, out = client.exec_run("/bin/bash -c 'echo {0} >> /etc/hosts'".format(hostent))

    def start(self):
        # Send the start message to the TADA daemon
        self.sock_fd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        h = self.tada_addr.split(":")
        if len(h) > 1:
            self.tada_host = h[0]
            self.tada_port = int(h[1])
        else:
            self.tada_host = h[0]
            self.tada_port = TADA_PORT

        msg = '{{ "msg-type" : "test-start",'\
              '"test-suite" : "{0}",'\
              '"test-type" : "{1}",'\
              '"test-name" : "{2}",'\
              '"timestamp" : {3} }}'\
                  .format(self.test_suite,
                          self.test_type,
                          self.test_name,
                          time.time())

        self.sock_fd.sendto(msg.encode('utf-8'), (self.tada_host, self.tada_port))

    def start_daemons(self):
        """start all ldmsd daemons configured for this Test harness"""
        for host, daemon in self.daemons.iteritems():
            self.start_daemon(host)

    def kill_daemons(self):
        """kill all ldmsd daemons configured for this Test harness"""
        for host, daemon in self.daemons.iteritems():
            self.kill_daemon(host)

    def kill_daemon(self, host):
        """Kill the ldmsd daemon

        Postional Parameters:
        -- The host name of the container running the ldmsd daemon

        Returns:
        !0 if the ldmsd daemon was not running or permission denied
        """
        d = self.daemons[host]
        cont = self.ldmsd[host]

        rc, out = cont.kill_ldmsd()
        if rc != 0:
            print("The LDMSD container could not be killed: {0}.".format(out))
        return rc, out

    def start_daemon(self, host):
        """Start the ldmsd daemon

        Positional Parameters:
        -- The host name of the container

        Returns:
        !0 if the ldmsd daemon could not be started
        """
        d = self.daemons[host]
        cont = self.ldmsd[host]

        rc = cont.start_ldmsd()
        if rc != 0:
            print("The LDMSD container could not be created.")
            return rc

        # configure the samplers
        if 'samplers' in d:
            for s in d['samplers']:
                cmds = []
                cmds.append('load name={0}'.format(s['plugin']))
                if 'config' in s:
                    cmd = 'config name={0} '.format(s['plugin'])
                    for c in s['config']:
                        cmd += c + ' '
                    for a in s:
                        cmd = cmd.replace('%' + a + '%', str(s[a]))
                    cmds.append(cmd)
                    cont.config_ldmsd(cmds)

                if 'start' in s and s['start']:
                    cont.config_ldmsd([ 'start name={0} interval=1000000 offset=0'.\
                                        format(s['plugin']) ])

        # Apply any explicit daemon configuration
        if 'config' in d:
            cont.config_ldmsd(d['config'])

        return 0

    def restart(self, hosts=None):
        pass

    def old_start(self):
        # Create our own namespace for the daemon host names
        conts = {}

        # Start each daemon in a container
        for d in self.daemons:
            cont = LdmsdContainer(self.config['test_suite']+'-'+d['host'],
                                  self.network,
                                  listen_port=d['listen_port'],
                                  listen_xprt=d['listen_xprt'],
                                  prefix=self.prefix,
                                  data_root=self.data_root,
                                  log_level='DEBUG')
            cont.environment.append("TADA_ADDR={0}".format(self.tada_addr))
            rc = cont.start_ldmsd()
            if rc != 0:
                print("The LDMSD container could not be created.")
                return rc

            conts[d['host']] = cont

        self.ldmsd = conts

        # Add host entries for test
        for s_host, server in self.servers():
            # Add implicit hosts
            hostent = "{0} {1}".format(server.ip4_address, s_host)
            for c_host, client in self.servers():
                rc, out = client.exec_run("/bin/bash -c 'echo {0} >> /etc/hosts'".format(hostent))

        for d in self.daemons:
            cont = self.ldmsd[d['host']]

            # configure the samplers
            if 'samplers' in d:
                for s in d['samplers']:
                    cmds = []
                    cmds.append('load name={0}'.format(s['plugin']))
                    if 'config' in s:
                        cmd = 'config name={0} '.format(s['plugin'])
                        for c in s['config']:
                            cmd += c + ' '
                        for a in s:
                            cmd = cmd.replace('%' + a + '%', str(s[a]))
                        cmds.append(cmd)
                        cont.config_ldmsd(cmds)

                    if 'start' in s and s['start']:
                        cont.config_ldmsd([ 'start name={0} interval=1000000 offset=0'.format(s['plugin']) ])

            # Apply any explicit daemon configuration
            if 'config' in d:
                cont.config_ldmsd(d['config'])

        # Send the start message to the TADA daemon
        self.sock_fd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        h = self.tada_addr.split(":")
        if len(h) > 1:
            self.tada_host = h[0]
            self.tada_port = int(h[1])
        else:
            self.tada_host = h[0]
            self.tada_port = TADA_PORT

        msg = '{{ "msg-type" : "test-start",'\
              '"test-suite" : "{0}",'\
              '"test-type" : "{1}",'\
              '"test-name" : "{2}",'\
              '"timestamp" : {3} }}'\
                  .format(self.test_suite,
                          self.test_type,
                          self.test_name,
                          time.time())

        self.sock_fd.sendto(msg.encode('utf-8'), (self.tada_host, self.tada_port))
        return 0

    def finish(self):
        for assert_no, assert_data in self.assertions.iteritems():
            if assert_data['result'] != self.TEST_SKIPPED:
                continue
                self.send_assert_status(assert_no, 'none')

        msg = '{{ "msg-type" : "test-finish",'\
              '"test-suite" : "{0}",'\
              '"test-type" : "{1}",'\
              '"test-name" : "{2}",'\
              '"timestamp" : {3} }}'\
                  .format(self.test_suite,
                          self.test_type,
                          self.test_name,
                          time.time())
        self.sock_fd.sendto(msg.encode('utf-8'), (self.tada_host, self.tada_port))
        self.sock_fd.close()
        self.sock_fd = None

    def servers(self):
        return self.ldmsd.iteritems()

    def cleanup(self):
        for c in self.ldmsd:
            self.ldmsd[c].kill()

