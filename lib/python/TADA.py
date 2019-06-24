import docker
import subprocess
import sys
import time
import os
import json
import copy

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
    def __init__(self, fname, prefix, data_root, tada_addr):
        self.config = None
        self.daemons = None
        self.ldmsd = []
        self.network = None
        self.prefix = prefix
        self.data_root = data_root
        self.tada_addr = tada_addr # host:port

        try:
            self.config = json.load(open(fname))
        except Exception as e:
            print(e)
            raise ValueError("The specified configuration file is invalid.")

        daemons = []
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
                daemons.append(daemon)
        self.daemons = daemons
        self.network = Network(self.config['name'])

    def get_host(self, name):
        return self.ldmsd[name]

    @property
    def name(self):
        return self.config['name']

    @property
    def description(self):
        return self.config['description']

    def start(self, hosts=[]):
        # Create our own namespace for the daemon host names
        conts = {}

        # Start each daemon in a container
        for d in self.daemons:
            cont = LdmsdContainer(self.config['name']+'-'+d['host'],
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
            # Add explicit hosts
            for h in hosts:
                rc, out = server.exec_run("/bin/bash -c 'echo {0} >> /etc/hosts'".format(h))

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

        return 0

    def servers(self):
        return self.ldmsd.iteritems()

    def cleanup(self):
        for c in self.ldmsd:
            self.ldmsd[c].kill()
