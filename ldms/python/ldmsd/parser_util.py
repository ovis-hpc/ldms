import os, sys
import errno
import json
import subprocess
import re
import socket
import time
import bisect
import itertools as it
import collections
import ldmsd.hostlist as hostlist

AUTH_ATTRS = [
    'auth',
    'conf'
]

BYTE_ATTRS = [
    'rx_rate',
    'quota'
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
        raise ValueError(f'{error_str}')
    interval_s = interval_s.lower()
    unit = next((unit for unit in unit_strs if unit in interval_s), None)
    if unit:
        if interval_s.split(unit)[1] != '':
            raise ValueError(f'{error_str}')
        ival_s = interval_s.split(unit)[0]
        try:
            ival_s = float(ival_s) * unit_strs[unit]
        except Exception as e:
            raise ValueError(f'"{interval_s}" is not a valid time-interval string\n')
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
        if attr in BYTE_ATTRS:
            try:
                if spec[attr] is not None:
                    return int(spec[attr])
            except:
                raise ValueError(f'Error: "{spec[attr]}" is not a valid integer')
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
            raise ValueError(f'The "{name}" attribute is required in {container_name}\n')

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
        raise ValueError(f'Configuration for plugin instance "{plugn}" '
                         f'is not defined in the top level "plugins" dictionary\n')
    plugin = plugin_spec[plugn]
    check_required([ 'name' ], plugin, f'"plugin" entry. Error in "'+ plugn +'" configuration')
    check_required(['config'], plugin, '"plugin" entry')
    if type(plugin['config']) is not list:
        raise TypeError('"config" must be a list of configuration commands\n')
    for cfg in plugin['config']:
        if type(cfg) is not dict and type(cfg) is not str:
            raise TypeError('"config" list members must be a dictionary or a string\n')

def dist_list(list_, n):
    q, r = divmod(len(list_), n)
    dist_list = []
    idx = 0
    for i in range(1, n + 1):
        s = idx
        idx += q + 1 if i <= r else q
        dist_list.append(list_[s:idx])
    return dist_list

#logn search function for lists
def bin_search(slist, x):
    idx = bisect.bisect_left(slist, x)
    if idx < len(slist) and slist[idx] == x:
        return True
    return None

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

def perm_handler(perm_str):
    if perm_str is None:
        return perm_str
    if type(perm_str) is not str:
        raise TypeError(f'Error: YAML "perms" value must be a string')
    perms = 0
    m = perm_handler.string_pattern.fullmatch(perm_str)
    if m:
        if m.group(1):
            perms += 0o400
        if m.group(2):
            perms += 0o200
        if m.group(3):
            perms += 0o040
        if m.group(4):
            perms += 0o020
        if m.group(5):
            perms += 0o004
        if m.group(6):
            perms += 0o002
    else:
        m = perm_handler.octal_pattern.fullmatch(perm_str)
        if m:
            try:
                perms = int(perm_str, base=8)
            except:
                raise ValueError(f'Error: permission string \"{perm_str}\" is not a valid octal')
        else:
            raise ValueError(f'Error: YAML permisson string "{perm_str} is not a valid octal"\n'
                             f'Must represent either a valid octal number, or use unix-like vernacular\n'
                             f'Allowed format: (r|-)(w|-)-(r|-)(w|-)-(r|-)(w|-)-')

    return '0'+oct(perms)[2:]
perm_handler.string_pattern = re.compile('(?:(r)|-)(?:(w)|-)-(?:(r)|-)(?:(w)|-)-(?:(r)|-)(?:(w)|-)-')
perm_handler.octal_pattern = re.compile('^0?[0-7]{1,3}')

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
        "agg-1" : {
          "endpoints": {
            "node-1-10002" : { "host" : "node-1", "port" : 10002 },
             ...
          }
        "agg-2" : {
          ...
          }
        }

        """
        ep_dict = {}
        node_config = config['daemons']
        if type(node_config) is not list:
            raise TypeError(f'{LDMS_YAML_ERR}\n'
                            f'daemons {LIST_ERR}\n'
                            f'e.g. daemons:\n'
                            f'       - names : &l1-agg "l1-aggs-[1-8]"\n'
                            f'         hosts : &l1-agg-hosts "node-[1-8]"\n')
        for spec in node_config:
            check_required([ 'names', 'endpoints', 'hosts' ],
                           spec, '"daemons" entry')
            dnames = expand_names(spec['names'])
            hosts = expand_names(spec['hosts'])
            hostnames = hosts
            if len(dnames) != len(hostnames):
                hosts = [ [host]*(len(dnames)//len(hostnames)) for host in hostnames ]
                hosts = list(it.chain.from_iterable(hosts))
            ep_names = []
            ep_ports = []
            ep_hosts = []
            if type(spec['endpoints']) is not list:
                raise TypeError(f'{LDMS_YAML_ERR}\n'
                                f'endpoints {LIST_ERR}\n'
                                f'e.g endpoints :\n'
                                f'      - names : &l1-agg-endpoints "node-[1-8]-[10101]"\n'
                                f'        ports : &agg-ports "[10101]"\n'
                                f'        maestro_comm : True\n'
                                f'        xprt  : sock\n'
                                f'        auth  :\n'
                                f'          name : munge1\n'
                                f'          plugin : munge\n')
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
                if check_opt('hosts', endpoints):
                    cur_ephosts = expand_names(endpoints['hosts'])
                    _ephosts = cur_ephosts
                    if len(cur_ephosts) != len(cur_epnames):
                        cur_ephosts = [ [host]*(len(cur_epnames)//len(_ephosts)) for host in _ephosts ]
                        cur_ephosts = list(it.chain.from_iterable(cur_ephosts))
                    ep_hosts.append(cur_ephosts)
                else:
                    ep_hosts.append([])
            env = check_opt('environment', spec)
            cli_opt = {}
            for key in spec:
                if key not in ['hosts','names','endpoints','environment']:
                    cli_opt[key] = spec[key]
            for dname, host in zip(dnames, hosts):
                ep_dict[dname] = {}
                ep_dict[dname]['addr'] = host
                ep_dict[dname]['environment'] = env
                ep_dict[dname]['endpoints'] = {}
                if len(cli_opt):
                    ep_dict[dname]['cli_opt'] = cli_opt
                dcount = 0
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
                    if check_opt('bind_all', ep):
                        h['bind_all'] = ep['bind_all']
                    _ephost = check_opt('hosts', ep)
                    if _ephost:
                        h['host'] = ep_hosts[dcount].pop(0)
                    ep_dict[dname]['endpoints'][ep_name] = h
                    dcount += 1
            if len(ep_dict) == 0:
                raise ValueError(f'Error processing regex of hostnames {spec["hosts"]} and daemons {spec["names"]}.\n'
                                 f'Number of hosts must be a multiple of daemons with appropriate ports or equivalent to length of daemons.\n'
                                 f'Regex {spec["hosts"]} translates to {len(hostnames)} hosts\n'
                                 f'Regex {spec["names"]} translates to {len(dnames)} daemons\n')
        return ep_dict

    def build_advertisers(self, spec):
        if 'advertise' not in spec:
            return
        ad_grp = spec['advertise']
        check_required(['names', 'hosts', 'xprt', 'port', 'reconnect'],
                       ad_grp, '"advertise" entry')
        names = expand_names(ad_grp['names'])
        dmns = expand_names(spec['daemons'])
        if len(names) != len(dmns):
            raise ValueError(f'Please provide a regex for "names" that is equal to the number of daemons '
                             f'to advertise from.\n')
            sys.exit()
        auth_name, plugin, auth_opt = check_auth(ad_grp)
        perm = check_opt('perm', ad_grp)
        perm = perm_handler(perm)
        rail = check_opt('rail', ad_grp)
        quota = check_opt('quota', ad_grp)
        rx_rate = check_opt('rx_rate', ad_grp)
        ad_list = expand_names(spec['daemons'])
        self.advertisers[spec['daemons']] = {'names'     : ad_grp['names'],
                                             'hosts'     : ad_grp['hosts'],
                                             'xprt'      : ad_grp['xprt'],
                                             'port'      : ad_grp['port'],
                                             'reconnect' : ad_grp['reconnect'],
                                             'auth'      : { 'name' : auth_name,
                                                             'conf' : auth_opt,
                                                             'plugin' : plugin },
                                             'perm'      : perm,
                                             'rail'      : rail,
                                             'quota'   : quota,
                                             'rx_rate'   : rx_rate,
                                             'ad_list'   : ad_list
        }

    def build_prdcr_listeners(self, spec):
        if 'prdcr_listen' not in spec:
            return
        for pl in spec['prdcr_listen']:
            check_required(['name', 'reconnect'], pl,
                           '"prdcr_listen" entry')
            node_listen = {}
            regex = check_opt('regex', pl)
            dstart = check_opt('disable_start', pl)
            ip = check_opt('ip', pl)
            rail = check_opt('rail', pl)
            quota = check_opt('quota', pl)
            rx_rate = check_opt('rx_rate', pl)
            node_listen[pl['name']] = { 'reconnect'     : pl['reconnect'],
                                        'disable_start' : dstart,
                                        'ip'            : ip,
                                        'rail'          : rail,
                                        'quota'         : quota,
                                        'rx_rate'       : rx_rate,
                                        'regex'         : regex
            }
            self.prdcr_listeners[spec['daemons']] = node_listen

    def build_aggregators(self, config):
        aggregators = {}
        if 'aggregators' not in config:
            return aggregators
        agg_conf = config['aggregators']
        if type(agg_conf) is not list:
            raise TypeError(f'{LDMS_YAML_ERR}\n'
                            f' aggregators {LIST_ERR}\n'
                            f'e.g. aggregators:\n'
                            f'       - daemons: "l1-aggregators"\n'
                            f'         peers :\n'
                            f'           - daemons : "samplers"\n'
                            f'             ...     :  ...\n')
        for agg_spec in agg_conf:
            check_required([ 'daemons' ],
                           agg_spec, '"aggregators" entry')
            names = expand_names(agg_spec['daemons'])
            group = agg_spec['daemons']
            plugins = check_opt('plugins', agg_spec)
            if plugins:
                if plugins is not list:
                    raise ValueError(f'"plugins" must be a list of plugin instance names"\n')
                for plugin in plugins:
                    check_plugin_config(plugin, self.plugins)
            if group not in aggregators:
                aggregators[group] = {}
            subscribe = check_opt('subscribe', agg_spec)
            if subscribe:
                for stream in subscribe:
                    check_required([ 'stream', 'regex' ], stream, "stream specification")
            for name in names:
                aggregators[group][name] = { 'state' : 'stopped' } # 'running', 'error'
                self.build_advertisers(agg_spec)
                self.build_prdcr_listeners(agg_spec)
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
                raise TypeError(f'{LDMS_YAML_ERR}\n'
                                f'peers {LIST_ERR}\n'
                                f'e.g. peers:\n'
                                f'       - daemons: "samplers"\n'
                                f'         endpoints : "sampler-endpoints"\n'
                                f'         ...       : ...\n')
            for prod in agg['peers']:
                check_required([ 'endpoints', 'reconnect', 'type', ],
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

                    upd_spec = check_opt('updaters', prod)
                    # Expand and generate all the producers
                    typ = prod['type']
                    reconnect = check_intrvl_str(prod['reconnect'])
                    perm = check_opt('perm', prod)
                    perm = perm_handler(perm)
                    rail = check_opt('rail', prod)
                    quota = check_opt('quota', prod)
                    rx_rate = check_opt('rx_rate', prod)
                    cache_ip = check_opt('cache_ip', prod)
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
                                'perm'      : perm,
                                'rail'      : rail,
                                'quota'     : quota,
                                'rx_rate'   : rx_rate,
                                'cache_ip'  : cache_ip,
                                'updaters'  : upd_spec
                            }
                            producers[group][endpoint] = prod
                    except:
                        raise ValueError(f'Mismatch in producer config:\n'
                                         f'Please ensure "endpoints" is configured to the correct number of ports specified.\n')
        return producers

    def build_updaters(self, config):
        """
        Return a dictionary based on the aggregator. Each dictionary
        entry is a list of updaters in that group.
        """
        updaters = {}
        updtr_cnt = 0
        for agg in config.get('aggregators', []):
            if 'peers' not in agg and 'prdcr_listen' not in agg:
                continue
            peer_list = []
            if 'peers' in agg:
                peer_list += agg['peers']
            if 'prdcr_listen' in agg:
                peer_list += agg['prdcr_listen']
            for prod in peer_list:
                if prod['updaters'] is None:
                    continue
                if type(prod['updaters']) is not list:
                    raise TypeError(f'{LDMS_YAML_ERR}\n'
                                    f'"updaters" {LIST_ERR}\n'
                                    f'e.g. updaters:\n'
                                    f'       - mode     : pull\n'
                                    f'         interval : "1.0s"\n'
                                    f'         sets     :\n'
                                    f'           - regex : ".*"\n'
                                    f'             field : inst\n')
                for updtr_spec in prod['updaters']:
                    check_required([ 'interval' ],
                                   updtr_spec, '"updaters" entry')
                    updtr_sets = check_opt('sets', updtr_spec)
                    if updtr_sets and type(updtr_sets) is not list:
                        raise ValueError(f'Error parsing YAML configuration in "updaters". "sets" must be a list of dictionaries')
                    group = agg['daemons']
                    if group not in updaters:
                        updaters[group] = {}
                    grp_updaters = updaters[group]
                    updtr_name = f'updtr_{updtr_cnt}'
                    if updtr_name in grp_updaters:
                        raise ValueError(f'Duplicate updater name "{updtr_name}". '
                                         f'An updater name must be unique within the group\n')
                    perm = check_opt('perm', updtr_spec)
                    perm = perm_handler(perm)
                    prod_regex = check_opt('producers', updtr_spec)
                    prod_list = []
                    if prod_regex:
                        if len(expand_names(prod_regex)) > 1:
                            for pname in expand_names(prod_regex):
                                prod_list.append(pname)
                        else:
                            prod_list.append(prod_regex)
                    else:
                        prod_list.append('.*')
                    updtr = {
                        'name'      : updtr_name,
                        'interval'  : check_intrvl_str(updtr_spec['interval']),
                        'perm'      : perm,
                        'group'     : agg['daemons'],
                        'sets'      : updtr_sets,
                        'producers' : prod_list
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
            raise ValueError(f'{LDMS_YAML_ERR}\n'
                             f'store {DICT_ERR}\n'
                             f'e.g. stores:\n'
                             f'       sos-meminfo :\n'
                             f'         daemons   : "l1-aggregators"\n'
                             f'         container : ldms_data\n'
                             f'         ...       : ...\n')
        for store in config['stores']:
            store_spec = config['stores'][store]
            store_spec['name'] = store
            check_required([ 'plugin', ],
                           store_spec, '"store" entry')
            container = check_opt('container', store_spec)
            if not container:
                container = ""
            decomp = check_opt('decomposition', store_spec)
            if not decomp:
                check_required(['schema'], store_spec, '"store" entry')
            schema = check_opt('schema', store_spec)
            regex = check_opt('regex', store_spec)
            if decomp and not schema and not regex:
                raise ValueError(f'Decomposition plugin configuration requires either'
                                 f' "schema" or "regex" attribute"\n')
            if decomp and schema and regex:
                raise ValueError(f'Please specify either "schema" or "regex" when using decomposition - these parameters are mutually exclusive.\n')
            group = store_spec['daemons']
            if group not in stores:
                stores[group] = {}
            grp_stores = stores[group]
            if store in grp_stores:
                raise ValueError(f'Duplicate store name "{store}". '
                                 f'A store name must be unique within the group\n')
            check_opt('flush', store_spec)
            perm = check_opt('perm', store_spec)
            store_spec['perm'] = perm_handler(perm)
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
            raise TypeError(f'{LDMS_YAML_ERR}\n'
                            f'samplers {LIST_ERR}\n'
                            f'e.g. samplers:\n'
                            f'       - daemons : "samplers"\n'
                            f'         plugins :\n'
                            f'           - name        : meminfo\n'
                            f'             interval    : "1.0s"\n'
                            f'             offset      : "0s"\n'
                            f'             config :\n'
                            f'               - schema : meminfo\n'
                            f'                 component_id : "10001"\n'
                            f'                 producer : "node-1"\n'
                            f'                 perm : "rwx-rwx-rwx"\n')
        for smplr_spec in config['samplers']:
            check_required([ 'daemons', 'plugins' ],
                           smplr_spec, '"sampler" entry')
            self.build_advertisers(smplr_spec)
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
            raise TypeError(f'{LDMS_YAML_ERR}\n'
                            f'store {DICT_ERR}\n'
                            f'e.g. plugins:\n'
                            f'       meminfo1 :\n'
                            f'         name      : meminfo\n'
                            f'         interval  : 1.0s\n'
                            f'         config    : [ { schema : meminfo }, { ... : ... } ]\n')
        plugins = {}
        plugn_spec = config['plugins']
        for plugn in plugn_spec:
            if plugn in plugins:
                raise ValueError(f'Duplicate plugin name "{plugin_name}". '
                                 f'Plugin must be unique within a group.\n')
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
        self.advertisers = {}
        self.prdcr_listeners = {}
        self.daemons = self.build_daemons(cluster_config)
        self.plugins = self.build_plugins(cluster_config)
        self.aggregators = self.build_aggregators(cluster_config)
        self.producers = self.build_producers(cluster_config)
        self.updaters = self.build_updaters(cluster_config)
        self.stores = self.build_stores(cluster_config)
        self.samplers = self.build_samplers(cluster_config)

    def ldmsd_arg_list(self, local_path, dmn_grp, dmn):
        start_list = [ 'ldmsd' ]
        for ep in self.daemons[dmn]['endpoints']:
            if self.daemons[dmn]['endpoints'][ep]['maestro_comm'] is True:
                ep_ = self.daemons[dmn]['endpoints'][ep]
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

    def write_advertisers(self, dstr, dmn_grp, dname, auth_list):
        if dmn_grp not in self.advertisers:
            return dstr, auth_list
        ad_grp = self.advertisers[dmn_grp]
        for host in expand_names(self.advertisers[dmn_grp]['hosts']):
            auth, plugin, auth_opt = check_auth(ad_grp)
            perm = check_opt('perm', ad_grp)
            rail = check_opt('rail', ad_grp)
            quota = check_opt('quota', ad_grp)
            rx_rate = check_opt('rx_rate', ad_grp)
            if auth not in auth_list:
                auth_list[auth] = { 'conf' : auth_opt }
                dstr += f'auth_add name={auth}'
                dstr = self.write_opt_attr(dstr, 'plugin', plugin)
                dstr = self.write_opt_attr(dstr, 'conf', auth_opt, endline=True)
            dstr += f'advertiser_add name={dname}-{host} host={host} xprt={ad_grp["xprt"]} port={ad_grp["port"]} '\
                    f'reconnect={ad_grp["reconnect"]}'
            dstr = self.write_opt_attr(dstr, 'auth', auth)
            dstr = self.write_opt_attr(dstr, 'perm', perm)
            dstr = self.write_opt_attr(dstr, 'rail', rail)
            dstr = self.write_opt_attr(dstr, 'quota', quota)
            dstr = self.write_opt_attr(dstr, 'rx_rate', rx_rate, endline=True)
            dstr += f'advertiser_start name={dname}-{host}\n'
        return dstr, auth_list

    def write_prdcr_listeners(self, dstr, dmn_grp):
        if dmn_grp not in self.prdcr_listeners:
            return dstr
        plisten = self.prdcr_listeners[dmn_grp]
        for pl in plisten:
            dstr += f'prdcr_listen_add name={pl}'
            dstart = check_opt('disable_start', plisten[pl])
            regex = check_opt('regex', plisten[pl])
            ip = check_opt('ip', plisten[pl])
            dstr = self.write_opt_attr(dstr, 'disable_start', dstart)
            dstr = self.write_opt_attr(dstr, 'regex', regex)
            dstr = self.write_opt_attr(dstr, 'ip', ip, endline=True)
            dstr += f'prdcr_listen_start name={pl}\n'
        return dstr

    def write_listeners(self, dstr, dmn_grp, dmn_name, auth_list={}):
        for endp in self.daemons[dmn_name]['endpoints']:
            ep = self.daemons[dmn_name]['endpoints'][endp]
            auth, plugin, auth_opt = check_auth(ep)
            if auth:
                if auth not in auth_list:
                    auth_list[auth] = { 'conf' : auth_opt }
                    dstr += f'auth_add name={auth}'
                    dstr = self.write_opt_attr(dstr, 'plugin', plugin)
                    dstr = self.write_opt_attr(dstr, 'conf', auth_opt, endline=True)
            dstr += f'listen xprt={ep["xprt"]} port={ep["port"]}'
            dstr = self.write_opt_attr(dstr, 'auth', auth)
            dstr = self.write_opt_attr(dstr, 'conf', auth_opt)
            bind_all = check_opt('bind_all', ep)
            if bind_all is True or bind_all == "true":
                host = "0.0.0.0"
            else:
                host = check_opt('host', ep)
                if host is None:
                    host = self.daemons[dmn_name]["addr"]
            dstr = self.write_opt_attr(dstr, 'host', host, endline=True)
        return dstr, auth_list

    def write_opt_attr(self, dstr, attr, val, endline=False):
        # Include leading space
        if val is not None:
            dstr += f' {attr}={val}'
        if endline:
            dstr += f'\n'
        return dstr

    def write_producers(self, dstr, group_name, dmn, auth_list):
        if group_name in self.producers:
            ''' Balance samplers across aggregators '''
            prdcrs = list(self.producers[group_name].keys())
            aggs = expand_names(group_name)
            agg_idx = int(aggs.index(dmn))
            prod_group = dist_list(prdcrs, len(aggs))[agg_idx]
            i = 0
            auth = None
            for ep in prod_group:
                producer = self.producers[group_name][ep]
                auth = check_opt('auth', self.daemons[producer['daemon']]['endpoints'][ep])
                auth_opt = check_opt('conf', self.daemons[producer['daemon']]['endpoints'][ep])
                if auth not in auth_list:
                    auth_list[auth] = { 'conf' : auth_opt }
                    plugin = check_opt('plugin', self.daemons[producer['daemon']]['endpoints'][ep]['auth'])
                    if plugin is None:
                        plugin = auth
                    dstr += f'auth_add name={auth} plugin={plugin}'
                    dstr = self.write_opt_attr(dstr, 'conf', auth_list[auth]['conf'], endline=True)
            for ep in prod_group:
                regex = False
                producer = self.producers[group_name][ep]
                pname = producer['name']
                port = self.daemons[producer['daemon']]['endpoints'][ep]['port']
                xprt = self.daemons[producer['daemon']]['endpoints'][ep]['xprt']
                hostname = check_opt('host', self.daemons[producer['daemon']]['endpoints'][ep])
                if hostname is None:
                    hostname = self.daemons[producer['daemon']]['addr']
                auth = check_opt('auth', self.daemons[producer['daemon']]['endpoints'][ep])
                perm = check_opt('perm', producer)
                rail = check_opt('rail', self.daemons[producer['daemon']]['endpoints'][ep])
                rx_rate = check_opt('rx_rate', self.daemons[producer['daemon']]['endpoints'][ep])
                quota = check_opt('quota', self.daemons[producer['daemon']]['endpoints'][ep])
                cache_ip = check_opt('cache_ip', producer)
                ptype = producer['type']
                reconnect = producer['reconnect']
                dstr += f'prdcr_add name={pname} '\
                        f'host={hostname} '\
                        f'port={port} '\
                        f'xprt={xprt} '\
                        f'type={ptype} '\
                        f'reconnect={reconnect}'
                dstr = self.write_opt_attr(dstr, 'cache_ip', cache_ip)
                dstr = self.write_opt_attr(dstr, 'perm', perm)
                dstr = self.write_opt_attr(dstr, 'auth', auth)
                dstr = self.write_opt_attr(dstr, 'rail', rail)
                dstr = self.write_opt_attr(dstr, 'rx_rate', rx_rate)
                dstr = self.write_opt_attr(dstr, 'quota', quota, endline=True)
                last_sampler = pname
                if 'regex' in producer:
                    dstr += f'prdcr_start_regex regex={producer["regex"]}\n'
                else :
                    dstr += f'prdcr_start name={pname}\n'
        return dstr, auth_list

    def write_options(self, dstr, dname):
        if 'cli_opt' not in self.daemons[dname]:
            return dstr
        cli_opt = self.daemons[dname]['cli_opt']
        for opt in cli_opt:
            if type(cli_opt[opt]) is dict:
                dstr += f'{opt}'
                for arg in cli_opt[opt]:
                    if arg == 'perm':
                        cli_opt[opt][arg] = perm_handler(cli_opt[opt][arg])
                    dstr += f' {arg}={cli_opt[opt][arg]}'
                dstr += '\n'
            else:
                dstr += f'option --{opt} {cli_opt[opt]}\n'
        return dstr

    def write_env(self, dstr, dname):
        if check_opt('environment', self.daemons[dname]):
            if type(self.daemons[dname]['environment']) is not dict:
                raise TypeError(f'Environment variables must be a yaml key:value dictionary\n')
            for attr in self.daemons[dname]['environment']:
                dstr += f'env {attr}={self.daemons[dname]["environment"][attr]}\n'
        return dstr

    def write_sampler(self, dstr, sname):
        if not self.samplers:
            return dstr
        for smplr_grp in self.samplers:
            if bin_search(expand_names(smplr_grp), sname):
                dstr, auth_list = self.write_listeners(dstr, smplr_grp, sname)
                dstr, auth_list = self.write_advertisers(dstr, smplr_grp, sname, auth_list)
                for plugin in self.samplers[smplr_grp]['plugins']:
                    plugn = self.plugins[plugin]
                    dstr += f'load name={plugin} plugin={plugn["name"]}\n'
                    for cfg_ in plugn['config']:
                        if type(cfg_) is dict:
                            hostname = socket.gethostname()
                            cfg_args = {}
                            for attr in cfg_:
                                if attr == 'name' or attr == 'interval' or attr == 'reconnect':
                                    continue
                                if attr == 'perm':
                                    cfg_[attr] = perm_handler(cfg_[attr])
                                cfg_args[attr] = cfg_[attr]
                            if 'producer' not in cfg_args:
                                cfg_args['producer'] = f'{hostname}'
                            if 'instance' not in cfg_args:
                                cfg_args['instance'] = f'{hostname}/{plugin}'
                            cfg_str = parse_to_cfg_str(cfg_args)
                        else:
                            cfg_str = cfg_

                        interval = check_intrvl_str(plugn['interval'])
                        dstr += f'config name={plugin} {cfg_str}\n'
                    dstr += f'start name={plugin} interval={interval}'
                    offset = check_opt('offset', plugn)
                    dstr = self.write_opt_attr(dstr, 'offset', offset, endline=True)
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

    def write_aggregator(self, dstr, dmn):
        # Agg config
        try:
            ''' "Balance" agg configuration if all samplers are included in each aggregator '''
            if not self.aggregators:
                return dstr
            for group_name in self.aggregators:
                if bin_search(expand_names(group_name), dmn):
                    auth_list = {}
                    dstr, auth_list = self.write_listeners(dstr, group_name, dmn, auth_list)
                    dstr, auth_list = self.write_producers(dstr, group_name, dmn, auth_list)
                    dstr = self.write_prdcr_listeners(dstr, group_name)
                    dstr = self.write_stream_subscribe(dstr, group_name, dmn)
                    dstr = self.write_agg_plugins(dstr, group_name, dmn)
                    dstr = self.write_updaters(dstr, group_name, dmn)
                    dstr = self.write_stores(dstr, dmn)
            return dstr
        except Exception as e:
            ea, eb, ec = sys.exc_info()
            raise Exception('Agg config Error: '+str(e)+' Line:'+str(ec.tb_lineno))

    def write_agg_plugins(self, dstr, group_name, agg):
        # Write independent plugin configuration for group <group_name>
        plugins = check_opt('plugins', self.aggregators[group_name][agg])
        if plugins is not None:
            for plugn in plugins:
                plugin = self.plugins[plugn]
                dstr += f'load name={plugn} plugin={self.plugins[plugn]["name"]}\n'
                for cfg_ in plugin["config"]:
                    if type(cfg_) is dict:
                        cfg_str = parse_to_cfg_str(plugin["config"])
                    else:
                        cfg_str = cfg_
                    dstr += f'config name={plugn} {cfg_str}\n'
        return dstr

    def write_updaters(self, dstr, group_name, dmn):
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
                perm = check_opt('perm', updtr_group[updtr])
                offset = check_opt('offset', updtr_group[updtr])
                dstr = self.write_opt_attr(dstr, 'perm', perm)
                dstr = self.write_opt_attr(dstr, 'offset', offset, endline=True)
                for pname in updtr_group[updtr]['producers']:
                    dstr += f'updtr_prdcr_add name={updtr} regex={pname}\n'
                if updtr_group[updtr]['sets']:
                    for s in updtr_group[updtr]['sets']:
                        dstr += f'updtr_match_add name={updtr} regex={s["regex"]} match={s["field"]}\n'
                dstr += f'updtr_start name={updtr}\n'
        return dstr

    def write_stores(self, dstr, dmn):
        if self.stores is None:
            return dstr
        for group_name in self.stores:
            if dmn not in expand_names(group_name):
                continue
            store_group = self.stores[group_name]
            loaded_plugins = []
            for store in store_group:
                if store_group[store]['plugin'] not in loaded_plugins:
                    if store_group[store]['plugin'] not in self.plugins:
                        raise ValueError(f'Storage policy plugin reference {store_group[store]["plugin"]} '
                                         f'is not defined in the top level "plugins" dictionary"\n')
                    plugin = self.plugins[store_group[store]['plugin']]
                    dstr += f'load name={store_group[store]["plugin"]} plugin={plugin["name"]}\n'
                    for cfg_ in plugin['config']:
                        if type(cfg_) is dict:
                            cfg_str = parse_to_cfg_str(cfg_)
                        else:
                            cfg_str = cfg_
                        dstr += f'config name={store_group[store]["plugin"]} '\
                                 f'{cfg_str}\n'
                    loaded_plugins.append(store_group[store]['plugin'])
                decomp = check_opt('decomposition', store_group[store])
                schema = check_opt('schema', store_group[store])
                regex = check_opt('regex', store_group[store])
                flush = check_opt('flush', store_group[store])
                perm = check_opt('perm', store_group[store])
                dstr += f'strgp_add name={store} plugin={store_group[store]["plugin"]} '
                dstr += f'container={store_group[store]["container"]} '
                dstr = self.write_opt_attr(dstr, 'decomposition', decomp)
                dstr = self.write_opt_attr(dstr, 'schema', schema)
                dstr = self.write_opt_attr(dstr, 'regex', regex)
                dstr = self.write_opt_attr(dstr, 'perm', perm)
                dstr = self.write_opt_attr(dstr, 'flush', flush, endline=True)
                dstr += f'strgp_start name={store}\n'
        return dstr

    def daemon_config(self, path, dname):
        """
        Write a specific daemon's V4 configuration to file.
        """
        dmn = None
        grp = None
        ddmns = []
        if dname in self.daemons:
                dmn = self.daemons[dname]
                ddmns.append(dmn)
        if len(ddmns) > 1:
            raise ValueError(f'Daemon {dname} has been defined multiple times in the YAML configuration file "{path}"\n')
        if dmn is None:
            raise ValueError(f'Daemon {dname} does not exist in YAML configuration file {path}\n')
        dstr = ''
        dstr = self.write_env(dstr, dname)
        dstr = self.write_options(dstr, dname)
        dstr = self.write_sampler(dstr, dname)
        dstr = self.write_aggregator(dstr, dname)
        return f'{dstr}'

    def config_v4(self, path):
        """
        Read the group configuration from a YAML file and generate a version 4 LDMSD configuration
        This configuration assumes that the environemnt variables COMPONENT_ID, HOSTNAME
        all exist on the machines relevant to the ldmsd cluster.
        """
        for dmn in self.daemons:
            try:
                dstr = self.daemon_config(self.args.ldms_config, dmn)
                if len(dstr) > 1:
                    fd = open(f'{path}/{dmn}.conf', 'w+')
                    fd.write(dstr)
                    fd.close()
            except Exception as e:
               a, b, c = sys.exc_info()
               raise Exception(f'Error generating configuration file for {dmn}: {str(e)}')
        return 0
