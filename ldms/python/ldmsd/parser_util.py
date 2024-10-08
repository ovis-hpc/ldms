import os
import errno
import json
import subprocess
import socket
import time
import itertools as it
import collections
import ldmsd.hostlist as hostlist

AUTH_ATTRS = [
    'auth',
    'conf'
]

CORE_ATTRS = [
    'daemons',
    'aggregators',
    'samplers',
    'plugins',
    'stores'
]

DEFAULT_ATTR_VAL = {
    'maestro_comm' : False,
    'xprt'         : 'sock',
    'interval'     : 1000000,
    'auth'         : 'none',
    'mode'         : 'static'
}

INT_ATTRS = [
    'interval',
    'offset',
    'reconnect',
    'flush'
]

unit_strs = {
    'ms' : 1000,
    'us' : 1,
    'm' : 60000000,
    's' : 1000000,
    'h' : 3600000000,
    'd' : 86400000000
}
LDMS_YAML_ERR = 'Error parsing ldms_config yaml file'
LIST_ERR = 'spec must be a list of dictionaries, specified with "-" in the ldms_config yaml file'
DICT_ERR = 'spec must be a dictionary, with keys referencing a plugin instance name'

def check_intrvl_str(interval_s):
    """Check the format of the interval string is correct

    A time-interval string is an integer or float followed by a
    unit-string. A unit-string is any of the following:

    'us' - microseconds
    'ms' - milliseconds
    's'  - seconds
    'm'  - minutes
    'h'  - hours
    'd'  - days

    Unit strings are not case-sensitive.

    Examples:
    '1.5s' - 1.5 seconds
    '1.5S' - 1.5 seconds
    '2s'   - 2 seconds
    """
    error_str = f"{interval_s} is not a valid time-interval string\n"\
                f"'Only a single unit-string is allowed. e.g. '50s40us' is not a valid entry."\
                f"Examples of acceptable format:\n"\
                f"'1.5s' - 1.5 seconds\n"\
                f"'1.5S' - 1.5 seconds\n"\
                f"'2us'  - 2 microseconds\n"\
                f"'3m'   - 3 minutes\n"\
                f"'1h'   - 1 hour\n"\
                f"'1d'   - 1 day\n"\
                f"\n"
    if type(interval_s) == int or type(interval_s) == float:
        return interval_s
    if type(interval_s) != str:
        raise ValueError(f"{error_str}")
    interval_s = interval_s.lower()
    unit = next((unit for unit in unit_strs if unit in interval_s), None)
    if unit:
        if interval_s.split(unit)[1] != '':
            raise ValueError(f"{error_str}")
        ival_s = interval_s.split(unit)[0]
        try:
            ival_s = float(ival_s) * unit_strs[unit]
        except Exception as e:
            raise ValueError(f"{interval_s} is not a valid time-interval string")
    else:
        ival_s = interval_s
    return int(ival_s)

def check_opt(attr, spec):
    # Check for optional argument and return None if not present
    if attr in AUTH_ATTRS:
        if attr == 'auth':
            attr = 'name'
        if 'auth' in spec:
            spec = spec['auth']
    if attr in spec:
        if attr in INT_ATTRS:
            return check_intrvl_str(spec[attr])
        return spec[attr]
    else:
        if attr in DEFAULT_ATTR_VAL:
            return DEFAULT_ATTR_VAL[attr]
        else:
            return None

def check_required(attr_list, container, container_name):
    """Verify that each name in attr_list is in the container"""
    for name in attr_list:
        if name not in container:
            raise ValueError("The '{0}' attribute is required in a {1}".
                             format(name, container_name))

def NUM_STR(obj):
    return str(obj) if type(obj) in [ int, float ] else obj

def expand_names(name_spec):
    if type(name_spec) != str and isinstance(name_spec, collections.abc.Sequence):
        names = []
        for name in name_spec:
            names += hostlist.expand_hostlist(NUM_STR(name))
    else:
        names = hostlist.expand_hostlist(NUM_STR(name_spec))
    return names

def check_auth(auth_spec):
    name = check_opt('auth', auth_spec)
    if not name:
        return None, None, None
    plugin = check_opt('plugin', auth_spec['auth'])
    auth_opt = check_opt('conf', auth_spec)
    return name, plugin, auth_opt

def check_plugin_config(plugn, plugin_spec):
    if plugn not in plugin_spec:
        raise ValueError(f'Configuration for plugin instance "{plugn}"\n'\
                         f'is not defined in the top level "plugins" dictionary"')
    plugin = plugin_spec[plugn]
    check_required([ 'name' ], plugin, f'"plugin" entry. Error in "'+ plugn +'" configuration')
    check_required(['config'], plugin, '"plugin" entry')
    if type(plugin['config']) is not list:
        raise ValueError('"config" must be a list of configuration commands')
    for cfg in plugin['config']:
        if type(cfg) is not dict and type(cfg) is not str:
            raise ValueError('"config" list members must be a dictionary or a string')
    return plugin

def parse_to_cfg_str(cfg_obj):
    cfg_str = ''
    for key in cfg_obj:
        if key not in INT_ATTRS:
            if len(cfg_str) > 1:
                cfg_str += ' '
            cfg_str += key + '=' + str(cfg_obj[key])
    return cfg_str

def parse_yaml_bool(bool_):
    if bool_ is True or bool_ == 'true' or bool_ == 'True':
        return True
    else:
        return False

class YamlCfg(object):
    def build_daemons(self, config):
        """Generate a daemon spec list from YAML config

        Builds a dictionary of endpoint definitions. The 'config' is a
        list of daemon specifications. Each daemon specification contains
        'names', 'host', and 'endpoint' attributes. All attributes are
        expanded per the slurm hostlist rules. The length of the
        expanded name-list, must equal the length of the expanded
        host-list.

        Example:

        daemons:
          - names : "agg-[1-10]"
            hosts : "node[1-10]"
            endpoints :
              - names : "node-[1-10]-[10002]
                ports : "[10002]"
                maestro_comm : True
                xprt : sock
                auth :
                  name : munge
                  plugin : munge

        results in the following host-spec dictionary:

        {
        "agg-[[1-3]-[10002]" : {
          "agg-1" : {
            "endpoints": {
              "node-1-10002" : { "host" : "node-1", "port" : 10002 },
              "node-2-10002" : { "host" : "node-2", "port" : 10002 },
              "node-3-10002" : { "host" : "node-3", "port" : 10002 },
              ...
            }
          }
        }

        """
        ep_dict = {}
        node_config = config['daemons']
        if type(node_config) is not list:
            print(f'{LDMS_YAML_ERR}')
            print(f'daemons {LIST_ERR}')
            print(f'e.g. daemons:')
            print(f'       - names : &l1-agg "l1-aggs-[1-8]"')
            print(f'         hosts : &l1-agg-hosts "node-[1-8]"')
            sys.exit()
        for spec in node_config:
            check_required([ 'names', 'endpoints', 'hosts' ],
                           spec, '"daemons" entry')
            hosts = expand_names(spec['hosts'])
            dnames = expand_names(spec['names'])
            hostnames = hosts
            if len(dnames) != len(hostnames):
                hosts = [ [host]*(len(dnames)//len(hostnames)) for host in hostnames ]
                hosts = list(it.chain.from_iterable(hosts))
            ep_names = []
            ep_ports = []
            if type(spec['endpoints']) is not list:
                print(f'{LDMS_YAML_ERR}')
                print(f'endpoints {LIST_ERR}')
                print(f'e.g endpoints :')
                print(f'      - names : &l1-agg-endpoints "node-[1-8]-[10101]"')
                print(f'        ports : &agg-ports "[10101]"')
                print(f'        maestro_comm : True')
                print(f'        xprt  : sock')
                print(f'        auth  :')
                print(f'          name : munge1')
                print(f'          plugin : munge')
                sys.exit()
            for endpoints in spec['endpoints']:
                check_required(['names','ports'],
                               endpoints, '"endpoints" entry')
                cur_epnames = expand_names(endpoints['names'])
                ep_names.append(cur_epnames)
                cur_ports = expand_names(endpoints['ports'])
                _ports = cur_ports
                if len(cur_ports) != len(cur_epnames):
                    cur_ports = [ _ports for i in range(0, len(cur_epnames)//len(_ports)) ]
                    cur_ports = list(it.chain.from_iterable(cur_ports))
                ep_ports.append(cur_ports)
            ep_dict[spec['names']] = {}
            env = check_opt('environment', spec)
            for dname, host in zip(dnames, hosts):
                ep_dict[spec['names']][dname] = {}
                ep_dict[spec['names']][dname]['addr'] = host
                ep_dict[spec['names']][dname]['environment'] = env
                ep_dict[spec['names']][dname]['endpoints'] = {}
                for ep_, ep_port, ep in zip(ep_names, ep_ports, spec['endpoints']):
                    port = ep_port.pop(0)
                    ep_name = ep_.pop(0)
                    xprt = check_opt('xprt', ep)
                    auth_name = check_opt('auth', ep)
                    auth_conf = check_opt('conf', ep)
                    plugin = check_opt('plugin', ep['auth'])
                    maestro_comm = parse_yaml_bool(check_opt('maestro_comm', ep))
                    h = {
                        'name' : ep_name,
                        'port' : port,
                        'xprt' : xprt,
                        'maestro_comm' : maestro_comm,
                        'auth' : { 'name' : auth_name, 'conf' : auth_conf, 'plugin' : plugin }
                    }
                    ep_dict[spec['names']][dname]['endpoints'][ep_name] = h
                    ep_dict[spec['names']][dname]['addr'] = host
            if len(ep_dict[spec['names']]) == 0:
                print(f'Error processing regex of hostnames {spec["hosts"]} and daemons {spec["names"]}.'\
                      f'Number of hosts must be a multiple of daemons with appropriate ports or equivalent to length of daemons.\n'\
                      f'Regex {spec["hosts"]} translates to {len(hostnames)} hosts\n'\
                      f'Regex {spec["names"]} translates to {len(dnames)} daemons\n')
                sys.exit()
        return ep_dict

    def build_aggregators(self, config):
        aggregators = {}
        if 'aggregators' not in config:
            return aggregators
        agg_conf = config['aggregators']
        if type(agg_conf) is not list:
            print(f'{LDMS_YAML_ERR}')
            print(f'aggregators {LIST_ERR}')
            print(f'e.g. aggregators:')
            print(f'       - daemons: "l1-aggregators"')
            print(f'         peers :')
            print(f'           - daemons : "samplers"')
            print(f'             ...     :  ...')
            return aggregators
        for agg_spec in agg_conf:
            check_required([ 'daemons' ],
                           agg_spec, '"aggregators" entry')
            names = expand_names(agg_spec['daemons'])
            group = agg_spec['daemons']
            plugins = check_opt('plugins', agg_spec)
            if plugins:
                if plugins is not list:
                    print(f'Error: "plugins" must be a list of plugin instance names"\n')
                for plugin in plugins:
                    check_plugin_config(plugin, self.plugins)
            daemons_ = None
            for daemons in config['daemons']:
                if group == daemons['names']:
                    daemons_ = daemons
            if daemons_ is None:
                raise ValueError(f"No daemons matched matched daemon key {group}")
            if group not in aggregators:
                aggregators[group] = {}
            subscribe = check_opt('subscribe', agg_spec)
            if subscribe:
                for stream in subscribe:
                    check_required([ 'stream', 'regex' ], stream, "stream specification")
            for name in names:
                aggregators[group][name] = { 'state' : 'stopped' } # 'running', 'error'
                if subscribe:
                    aggregators[group][name]['subscribe'] = subscribe
                if plugins:
                    aggregators[group][name]['plugins'] = plugins
        return aggregators

    def build_producers(self, config):
        """
        Return a dictionary keyed by the group name. Each dictionary
        entry is a list of producers in that group.
        """
        producers = {}
        for agg in config.get('aggregators', []):
            if 'peers' not in agg:
                continue
            if type(agg['peers']) is not list:
                print(f'{LDMS_YAML_ERR}')
                print(f'peers {LIST_ERR}')
                print(f'e.g. peers:')
                print(f'       - daemons: "samplers"')
                print(f'         endpoints : "sampler-endpoints"')
                print(f'         ...       : ...')
                continue
            for prod in agg['peers']:
                check_required([ 'endpoints', 'updaters',
                                 'reconnect', 'type', ],
                               prod, '"peers" entry')
                # Use endpoints for producer names and remove names attribute?
                if prod['daemons'] not in self.daemons:
                    dmn_grps = prod['daemons'].split(',')
                    eps = prod['endpoints'].split(',')
                else:
                    dmn_grps = [ prod['daemons'] ]
                    eps = [ prod['endpoints'] ]
                for daemons, endpoints in zip(dmn_grps, eps):
                    names = expand_names(endpoints)
                    endpoints = expand_names(endpoints)
                    group = agg['daemons']
                    smplr_dmns = expand_names(daemons)
                    if group not in producers:
                        producers[group] = {}

                    upd_spec = prod['updaters']
                    # Expand and generate all the producers
                    typ = prod['type']
                    reconnect = check_intrvl_str(prod['reconnect'])
                    ports_per_dmn = len(endpoints) / len(smplr_dmns)
                    ppd = ports_per_dmn
                    try:
                        for name in names:
                            if ppd > 1:
                                smplr_dmn = smplr_dmns[0]
                                ppd -= 1
                            else:
                                smplr_dmn = smplr_dmns.pop(0)
                                ppd = ports_per_dmn
                            endpoint = endpoints.pop(0)
                            prod = {
                                'daemon'    : smplr_dmn,
                                'dmn_grp'   : daemons,
                                'name'      : name,
                                'endpoint'  : endpoint,
                                'type'      : typ,
                                'group'     : group,
                                'reconnect' : reconnect,
                                'updaters'  : upd_spec
                            }
                            producers[group][endpoint] = prod
                    except:
                        print(f'Error building producer config:\n'\
                              f'Please ensure "endpoints" is configured to the correct number of ports specified.')
        return producers

    def build_updaters(self, config):
        """
        Return a dictionary based on the aggregator. Each dictionary
        entry is a list of updaters in that group.
        """
        updaters = {}
        updtr_cnt = 0
        for agg in config.get('aggregators', []):
            if 'peers' not in agg:
                continue
            for prod in agg['peers']:
                if type(prod['updaters']) is not list:
                    print(f'Error parsing ldms_config yaml file')
                    print(f'Updater spec must be a list of dictionaries, specified with "-" designator in the ldms_config yaml file')
                    print(f'e.g. updaters:')
                    print(f'       - mode     : pull')
                    print(f'         interval : "1.0s"')
                    print(f'         sets     :')
                    print(f'           - regex : ".*"')
                    print(f'             field : inst')
                    continue
                for updtr_spec in prod['updaters']:
                    check_required([ 'interval', 'sets', ],
                                   updtr_spec, '"updaters" entry')
                    group = agg['daemons']
                    if group not in updaters:
                        updaters[group] = {}
                    grp_updaters = updaters[group]
                    updtr_name = f'updtr_{updtr_cnt}'
                    if updtr_name in grp_updaters:
                        raise ValueError(f"Duplicate updater name '{updtr_name}''. "\
                                         f"An updater name must be unique within the group")
                    updtr = {
                        'name'      : updtr_name,
                        'interval'  : check_intrvl_str(updtr_spec['interval']),
                        'group'     : agg['daemons'],
                        'sets'      : updtr_spec['sets'],
                        'producers' : [{ 'regex' : '.*' }]
                    }
                    if 'offset' in updtr_spec:
                        updtr['offset'] = check_intrvl_str(updtr_spec['offset'])
                    if 'mode' in updtr_spec:
                        updtr['mode'] = updtr_spec['mode']
                    else:
                        updtr['mode'] = 'pull'
                    grp_updaters[updtr_name] = updtr
                    updtr_cnt += 1
        return updaters

    def build_stores(self, config):
        """
        Return a dictionary keyed by the group name. Each dictionary
        entry is a list of stores in that group.
        """
        if 'stores' not in config:
            return None
        stores = {}
        if type(config['stores']) is not dict:
            print(f'{LDMS_YAML_ERR}')
            print(f'store {DICT_ERR}')
            print(f'e.g. stores:')
            print(f'       sos-meminfo :')
            print(f'         daemons   : "l1-aggregators"')
            print(f'         container : ldms_data')
            print(f'         ...       : ...')
            return None
        for store in config['stores']:
            store_spec = config['stores'][store]
            store_spec['name'] = store
            check_required([ 'plugin', 'container' ],
                           store_spec, '"store" entry')
            decomp = check_opt('decomp', store_spec)
            decomposition = check_opt('decomposition', store_spec)
            if not decomp and not decomposition:
                check_required(['schema'], store_spec, '"store" entry')
            schema = check_opt('schema', store_spec)
            regex = check_opt('regex', store_spec)
            if decomp and not schema and not regex:
                raise ValueError("Decomposition plugin configuration requires either"
                                 " 'schema' or 'regex' attribute'")
            group = store_spec['daemons']
            if group not in stores:
                stores[group] = {}
            grp_stores = stores[group]
            if store in grp_stores:
                raise ValueError(f"Duplicate store name '{store}'. "
                            "A store name must be unique within the group")
            check_opt('flush', store_spec)
            check_plugin_config(store_spec['plugin'], self.plugins)
            grp_stores[store] = store_spec
        return stores

    def build_samplers(self, config):
        """
        Generate samplers from YAML config.
        Return a dictionary keyed by the samplers group name. Each dictionary
        entry is a single ldms daemon's sampler configuration.
        """
        if 'samplers' not in config:
            return None
        smplrs = {}
        if type(config['samplers']) is not list:
            print(f'{LDMS_YAML_ERR}')
            print(f'samplers {LIST_ERR}')
            print(f'e.g. samplers:')
            print(f'       - daemons : "samplers"')
            print(f'         plugins :')
            print(f'           - name        : meminfo')
            print(f'             interval    : "1.0s"')
            print(f'             offset      : "0s"')
            print(f'             config :')
            print(f'               - schema : meminfo')
            print(f'                 component_id : "10001"')
            print(f'                 producer : "node-1"')
            print(f'                 perm : "0777"')
            return None
        for smplr_spec in config['samplers']:
            check_required([ 'daemons', 'plugins' ],
                           smplr_spec, '"sampler" entry')
            for plugin in smplr_spec['plugins']:
                check_plugin_config(plugin, self.plugins)
            smplrs[smplr_spec['daemons']] = smplr_spec
        return smplrs

    def build_plugins(self, config):
        """
        Generate plugins to load from a YAML config.
        Return a dictionary keyed by the plugin's group name. Each dictionary entry
        is a single plugin's configuration.
        """
        if 'plugins' not in config:
            return None
        if type(config['plugins']) is not dict:
            print(f'{LDMS_YAML_ERR}')
            print(f'store {DICT_ERR}')
            print(f'e.g. plugins:')
            print(f'       meminfo1 :')
            print(f'         name      : meminfo')
            print(f'         interval  : 1.0s')
            print(f'         config    : [ { schema : meminfo }, { ... : ... } ]')

        plugins = {}
        plugn_spec = config['plugins']
        for plugn in plugn_spec:
            if plugn in plugins:
                raise ValueError(f'Duplicate plugin name "{plugin_name}". '
                                 f'Plugin must be unique within a group.')
            check_plugin_config(plugn, plugn_spec)
            plugins[plugn] = plugn_spec[plugn]
        return plugins

    def __init__(self, client, name, cluster_config, args):
        """
        Build configuration groups out of the YAML configuration
        """
        self.client = client
        self.name = name
        self.args = args
        self.cluster_config = cluster_config
        self.daemons = self.build_daemons(cluster_config)
        self.plugins = self.build_plugins(cluster_config)
        self.aggregators = self.build_aggregators(cluster_config)
        self.producers = self.build_producers(cluster_config)
        self.updaters = self.build_updaters(cluster_config)
        self.stores = self.build_stores(cluster_config)
        self.samplers = self.build_samplers(cluster_config)

    def ldmsd_arg_list(self, local_path, dmn_grp, dmn):
        start_list = [ 'ldmsd' ]
        for ep in self.daemons[dmn_grp][dmn]['endpoints']:
            if self.daemons[dmn_grp][dmn]['endpoints'][ep]['maestro_comm'] is True:
                ep_ = self.daemons[dmn_grp][dmn]['endpoints'][ep]
                start_list.append('-x')
                start_list.append(f'{ep_["xprt"]}:{ep_["port"]}')
                auth = check_opt('auth', ep_)
                if auth:
                    auth_plugin = check_opt('plugin', ep_['auth'])
                    auth_opt = check_opt('conf', ep_)
                    start_list.append('-a')
                    start_list.append(auth_plugin)
                    if auth_opt:
                        if len(auth_opt.split('=')) < 2:
                            auth_opt = f'conf={auth_opt}'
                        start_list.append('-A')
                        start_list.append(auth_opt)
        start_list.append('-c')
        start_list.append(f'{local_path}/{dmn}.conf')
        start_list.append('-r')
        start_list.append(f'{local_path}/{dmn}.pid')
        start_list.append('-l')
        start_list.append(f'{local_path}/{dmn}.log')
        start_list.append(f'-F')
        return start_list

    def write_listeners(self, dstr, dmn_grp, dmn_name, auth_list={}):
        for endp in self.daemons[dmn_grp][dmn_name]['endpoints']:
            ep = self.daemons[dmn_grp][dmn_name]['endpoints'][endp]
            auth, plugin, auth_opt = check_auth(ep)
            if auth:
                if auth not in auth_list:
                    auth_list[auth] = { 'conf' : auth_opt }
                    dstr += f'auth_add name={auth}'
                    dstr = self.write_opt_attr(dstr, 'plugin', plugin, endline=False)
                    dstr = self.write_opt_attr(dstr, 'conf', auth_opt)
            dstr += f'listen xprt={ep["xprt"]} port={ep["port"]}'
            dstr = self.write_opt_attr(dstr, 'auth', auth, endline=False)
            dstr = self.write_opt_attr(dstr, 'conf', auth_opt)
        return dstr, auth_list

    def write_opt_attr(self, dstr, attr, val, endline=True):
        # Include leading space
        if val is not None:
            dstr += f' {attr}={val}'
        if endline:
            dstr += f'\n'
        return dstr

    def write_producers(self, dstr, group_name, dmn, auth_list):
        if group_name in self.producers:
            ''' Balance samplers across aggregators '''
            ppd = -(len(self.producers[group_name]) // -len(self.aggregators[group_name].keys()))
            rem = len(self.producers[group_name]) % len(self.aggregators[group_name].keys())
            prdcrs = list(self.producers[group_name].keys())
            aggs = list(self.daemons[group_name].keys())
            agg_idx = int(aggs.index(dmn))
            prdcr_idx = int(ppd * agg_idx)
            prod_group = prdcrs[prdcr_idx:prdcr_idx+ppd]
            i = 0
            auth = None
            for ep in prod_group:
                producer = self.producers[group_name][ep]
                auth = check_opt('auth', self.daemons[producer['dmn_grp']][producer['daemon']]['endpoints'][ep])
                auth_opt = check_opt('conf', self.daemons[producer['dmn_grp']][producer['daemon']]['endpoints'][ep])
                if auth not in auth_list:
                    auth_list[auth] = { 'conf' : auth_opt }
                    plugin = check_opt('plugin', self.daemons[producer['dmn_grp']][producer['daemon']]['endpoints'][ep]['auth'])
                    if plugin is None:
                        print(f'Please specify auth plugin type for producer "{producer["daemon"]}" with auth name "{auth}"\n'\
                               'configuration file generation will continue, but auth will likely be denied.\n')
                        plugin = auth
                    dstr += f'auth_add name={auth} plugin={plugin}'
                    dstr = self.write_opt_attr(dstr, 'conf', auth_list[auth]['conf'])
            for ep in prod_group:
                regex = False
                producer = self.producers[group_name][ep]
                pname = producer['name']
                port = self.daemons[producer['dmn_grp']][producer['daemon']]['endpoints'][ep]['port']
                xprt = self.daemons[producer['dmn_grp']][producer['daemon']]['endpoints'][ep]['xprt']
                hostname = self.daemons[producer['dmn_grp']][producer['daemon']]['addr']
                auth = check_opt('auth', self.daemons[producer['dmn_grp']][producer['daemon']]['endpoints'][ep])
                ptype = producer['type']
                reconnect = producer['reconnect']
                dstr += f'prdcr_add name={pname} '\
                        f'host={hostname} '\
                        f'port={port} '\
                        f'xprt={xprt} '\
                        f'type={ptype} '\
                        f'reconnect={reconnect}'
                dstr = self.write_opt_attr(dstr, 'auth', auth)
                last_sampler = pname
                if 'regex' in producer:
                    regex = True
                    dstr += f'prdcr_start_regex regex={producer["regex"]}\n'
                if not regex:
                    dstr += f'prdcr_start_regex regex=.*\n'
            return dstr, auth_list

    def write_env(self, dstr, grp, dname):
        if grp not in self.daemons:
            return 1
        if dname not in self.daemons[grp]:
            return 1
        if check_opt('environment', self.daemons[grp][dname]):
            if type(self.daemons[grp][dname]['environment']) is not dict:
                print(f'Error: Environment variables must be a yaml key:value dictionary\n')
                sys.exit()
            for attr in self.daemons[grp][dname]['environment']:
                dstr += f'env {attr}={self.daemons[grp][dname]["environment"]}\n'
        return dstr

    def write_sampler(self, dstr, smplr_grp, sname):
        if smplr_grp not in self.samplers:
            return dstr
        dstr = self.write_env(dstr, smplr_grp, sname)
        dstr, auth_list = self.write_listeners(dstr, smplr_grp, sname)
        for plugin in self.samplers[smplr_grp]['plugins']:
            plugn = self.plugins[plugin]
            dstr += f'load name={plugn["name"]}\n'
            for cfg_ in plugn['config']:
                if type(cfg_) is dict:
                    hostname = socket.gethostname()
                    cfg_args = {}
                    prod = check_opt('producer', cfg_)
                    inst = check_opt('instance', cfg_)
                    if not prod:
                        cfg_args['producer'] = f'{hostname}'
                    if not inst:
                        cfg_args['instance'] = f'{hostname}/{plugn["name"]}'
                    for attr in cfg_:
                        if attr == 'name' or attr == 'interval':
                            continue
                    cfg_args[attr] = cfg_[attr]
                    cfg_str = parse_to_cfg_str(cfg_args)
                else:
                    cfg_str = cfg_

                interval = check_intrvl_str(plugn['interval'])
                dstr += f'config name={plugn["name"]} {cfg_str}\n'
            dstr += f'start name={plugn["name"]} interval={interval}'
            offset = check_opt('offset', plugn)
            dstr = self.write_opt_attr(dstr, 'offset', offset)
        return dstr

    def write_samplers(self, dstr, smplr_group):
        for inst_name in self.samplers[smplr_group]['plugins']:
            plugin = self.plugins[inst_name]
            sname = plugin['name']
            dstr += f'load name={sname}\n'
            for cfg_ in plugin['config']:
                if type(cfg_) is dict:
                    hostname = socket.gethostname()
                    if args.local:
                        cfg_args = { 'producer'     : f'{hostname}',
                                     'instance'     : f'{hostname}/{plugin["name"]}',
                                     'component_id' : '${LDMS_COMPONENT_ID}' }
                    else:
                        cfg_args = {}
                    prod = check_opt('producer', cfg_)
                    inst = check_opt('instance', cfg_)
                    if not prod:
                        cfg_args['producer'] = '{hostname}'
                    if not inst:
                        cfg_args['instance'] = '{hostname}/{plugin["name"]}'
                    for attr in cfg_:
                        if attr == 'name' or attr == 'interval':
                            continue
                    cfg_args[attr] = cfg_[attr]
                    cfg_str = parse_to_cfg_str(cfg_args)
                else:
                    cfg_str = cfg_

                interval = check_intrvl_str(plugin['interval'])
                dstr += f'config name={sname} {cfg_str}\n'
            dstr += f'start name={sname} interval={interval}'
            offset = check_opt('offset', plugin)
            dstr = self.write_opt_attr(dstr, 'offset', offset)
            return dstr

    def write_stream_subscribe(self, dstr, group_name, agg):
        subscribe = check_opt('subscribe', self.aggregators[group_name][agg])
        if subscribe:
            for stream in subscribe:
                regex = check_opt('regex', stream)
                if regex is None:
                    regex = '.*'
                dstr += f'prdcr_subscribe stream={stream["stream"]} '\
                        f'regex={regex}\n'
        return dstr

    def write_aggregator(self, dstr, group_name, dmn):
        # Agg config
        try:
            ''' "Balance" agg configuration if all samplers are included in each aggregator '''
            if group_name not in self.aggregators:
                return dstr
            auth_list = {}
            dstr, auth_list = self.write_listeners(dstr, group_name, dmn, auth_list)
            dstr, auth_list = self.write_producers(dstr, group_name, dmn, auth_list)
            dstr = self.write_stream_subscribe(dstr, group_name, dmn)
            dstr = self.write_agg_plugins(dstr, group_name, dmn)
            dstr = self.write_updaters(dstr, group_name)
            dstr = self.write_stores(dstr, group_name)
            return dstr
        except Exception as e:
            ea, eb, ec = sys.exc_info()
            print('Agg config Error: '+str(e)+' Line:'+str(ec.tb_lineno))
            raise ValueError

    def write_agg_plugins(self, dstr, group_name, agg):
        # Write independent plugin configuration for group <group_name>
        plugins = check_opt('plugins', self.aggregators[group_name][agg])
        if plugins is not None:
            for plugn in plugins:
                plugin = self.plugins[plugn]
                dstr += f'load name={plugin["name"]}\n'
                for cfg_ in plugin["config"]:
                    if type(cfg_) is dict:
                        cfg_str = parse_to_cfg_str(plugin["config"])
                    else:
                        cfg_str = cfg_
                    dstr += f'config name={plugin["name"]} {cfg_str}\n'
        return dstr

    def write_updaters(self, dstr, group_name):
        if group_name in self.updaters:
            updtr_group = self.updaters[group_name]
            for updtr in updtr_group:
                interval = check_intrvl_str(updtr_group[updtr]['interval'])
                updtr_str = f'updtr_add name={updtr_group[updtr]["name"]}'
                if 'mode' in updtr_group[updtr]:
                    mode = updtr_group[updtr]['mode']
                else:
                    mode = 'pull'
                # Check mode
                if mode == 'push':
                    updtr_str = f'{updtr_str} push=True'
                elif mode == 'onchange':
                    updtr_str = f'{updtr_str} push=onchange'
                elif mode == 'auto_interval' or 'auto':
                    updtr_str = f'{updtr_str} auto_interval=True'
                dstr += f'{updtr_str} '\
                         f'interval={interval}'
                offset = check_opt('offset', updtr_group[updtr])
                dstr = self.write_opt_attr(dstr, 'offset', offset)
                for prod in updtr_group[updtr]['producers']:
                    dstr += f'updtr_prdcr_add name={updtr_group[updtr]["name"]} '\
                             f'regex={prod["regex"]}\n'
                dstr += f'updtr_start name={updtr_group[updtr]["name"]}\n'
        return dstr

    def write_stores(self, dstr, group_name):
        if group_name in self.stores:
            store_group = self.stores[group_name]
            loaded_plugins = []
            for store in store_group:
                if store_group[store]['plugin'] not in loaded_plugins:
                    if store_group[store]['plugin'] not in self.plugins:
                        print(f'Error: Storage policy plugin reference {store_group[store]["plugin"]} '\
                              f'is not defined in the top level "plugins" dictionary"\n'
                              f'Continuing...\n')
                        continue
                    plugin = self.plugins[store_group[store]['plugin']]
                    dstr += f'load name={plugin["name"]}\n'
                    for cfg_ in plugin['config']:
                        if type(cfg_) is dict:
                            cfg_str = parse_to_cfg_str(cfg_)
                        else:
                            cfg_str = cfg_
                        dstr += f'config name={plugin["name"]} '\
                                 f'{cfg_str}\n'
                    loaded_plugins.append(store_group[store]['plugin'])
                strgp_add = f'strgp_add name={store} plugin={plugin["name"]} '
                strgp_add += f'container={store_group[store]["container"]} '
                strgp_add += f'schema={store_group[store]["schema"]}'
                dstr += strgp_add
                flush = check_opt('flush', store_group[store])
                dstr = self.write_opt_attr(dstr, 'flush', flush)
                dstr += f'strgp_start name={store}\n'
        return dstr

    def daemon_config(self, path, dname):
        """
        Write a specific daemon's V4 configuration to file.
        """
        dmn = None
        grp = None
        for dmn_grp in self.daemons:
            if dname in self.daemons[dmn_grp]:
                dmn = self.daemons[dmn_grp][dname]
                grp = dmn_grp
                break
        if dmn is None:
            print(f'Error: {dname} does not exist in YAML configuration file {path}\n')
            return 1
        dstr = ''
        dstr = self.write_sampler(dstr, grp, dname)
        dstr = self.write_aggregator(dstr, grp, dname)
        return f'{dstr}\0'

    def config_v4(self, path):
        """
        Read the group configuration from ETCD and generate a version 4 LDMSD configuration
        This configuration assumes that the environemnt variables COMPONENT_ID, HOSTNAME
        all exist on the machines relevant to the ldmsd cluster.
        """
        for group_name in self.daemons:
            # Sampler config
            if self.samplers != None:
                try:
                    # TO DO: Refactor sampler config architecture to more easily reference appropriate groups
                    if group_name in self.samplers:
                        fd = open(f'{path}/{group_name}-samplers.conf', 'w+')
                        dstr = ''
                        dstr = self.write_samplers(dstr, group_name)
                        for dmn_name in self.daemons[group_name]:
                            dstr, auth_list = self.write_listeners(dstr, group_name, dmn_name)
                        fd.write(dstr)
                    if fd:
                        fd.close()
                except Exception as e:
                    a, b, d = sys.exc_info()
                    print(f'Error generating sampler configuration: {str(e)} {str(d.tb_lineno)}')
                    sys.exit()
            else:
                print(f'"samplers" not found in configuration file. Skipping...')

            # Write aggregators in daemon group
            if group_name in self.aggregators:
                for dmn in self.aggregators[group_name]:
                    fd = open(f'{path}/{dmn}.conf', 'w+')
                    dstr = ''
                    dstr = self.write_aggregator(dstr, group_name, dmn)
                    fd.write(dstr)
                    fd.close()
