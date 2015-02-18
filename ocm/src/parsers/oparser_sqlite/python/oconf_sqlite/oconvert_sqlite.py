import osqlite_util

from oconf_sqlite.osqlite_classes import component_type, component, \
                                        NEWLINE, INDENT,sampler_template, \
                                        apply_on_node, service, model, \
                                        action, event, component_tree, \
                                        cable_type, cable, OCSqliteError

from oconf_sqlite.osqlite_util import USER_EVENT_MODEL_ID
import sqlite3


CONF_FILES = {"components.conf": None,
              "metrics.conf": None,
              "model_events.conf": None,
              "services.conf": None,
              "cables.conf": None}

CONF_PATH = ""

SEVERITY_LEVELS = osqlite_util.get_available_severity_levels()['level_name']

class CLIError(Exception):
    '''Generic exception to raise and log different fatal errors.'''
    def __init__(self, msg):
        super(CLIError).__init__(type(self))
        self.msg = "E: %s" % msg
    def __str__(self):
        return self.msg
    def __unicode__(self):
        return self.msg

# This function assumes that the components are ordered by comp_ids before
# the function is called.
def is_new_give_name(comp_name, prev_name, found_name, is_name_repeated):
    if comp_name == prev_name:
        return False

    if comp_name in found_name:
        return True

    if is_name_repeated:
        return True
    else:
        return False

def process_identifier_name_map(identifiers, names):
    # TODO: call with the sony-test
    n_names = len(names)
    n_identifiers = len(identifiers)

    if (n_names == 0) or (n_identifiers == 0):
        raise ValueError("Unexpected identifier and name map. " \
                         "The list of either names or identifiers is empty")

    if n_names == 1:
        return (osqlite_util.collapse_string(identifiers), names[0])

    # Number of names > 1
    if n_names != n_identifiers:
        error_s = "Number of names and number of identifiers are different. " \
                  "Identifiers: " + ",".join(identifiers) + ". " \
                  "Names: " + ",".join(names)
        raise ValueError("Unexpected identifier and name map. " + error_s)

    identifiers_s = osqlite_util.collapse_string(identifiers)
    names_s = osqlite_util.collapse_string(names)
    if identifiers_s.count("[") > 1:
        pass
    return (identifiers_s, names_s)

def get_give_name(identifiers, names):
    prev_name = ""
    found_name = set()
    is_name_repeated = False
    tmp_identifiers = []
    tmp_names = []

    result_give_name = []
    var_list = None
    for identifier, name in zip(identifiers, names):
        ptn, fmt, num_var, vars = osqlite_util.get_format(identifier)
        if prev_name == "":    # first component
            tmp_identifiers.append(identifier)
            tmp_names.append(name)
            var_list = [[vars[i]] for i in xrange(num_var)]
            found_name.add(name)
            prev_name = name
            continue

        if name == prev_name:
            is_name_repeated = True

        if (num_var != len(var_list)) or is_new_give_name(name, prev_name,
                            found_name, is_name_repeated):
            result_give_name.append(process_identifier_name_map(tmp_identifiers, \
                                                                tmp_names))
            tmp_identifiers = [identifier]
            tmp_names = [name]
            var_list = [[vars[i]] for i in xrange(num_var)]
            if not is_name_repeated:
                found_name = set()
        else:
            count = 0
            for i in xrange(num_var):
                var_list[i].append(vars[i])
                if len(set(var_list[i])) > 1:
                    count += 1
                if count <= 1:
                    continue
                result_give_name.append(process_identifier_name_map(tmp_identifiers, \
                                                                    tmp_names))
                tmp_identifiers = [identifier]
                tmp_names = [name]
                var_list = [[vars[i]] for i in xrange(num_var)]
                if not is_name_repeated:
                    found_name = set()
                break
            tmp_identifiers.append(identifier)
            tmp_names.append(name)
        found_name.add(name)
        prev_name = name
    result_give_name.append(process_identifier_name_map(tmp_identifiers, tmp_names))
    return result_give_name

def process_component_type(cursor, comp_type, gif_path, category, visible, all_comp_map):
    identifiers = []
    names = []
    try:
        ctype = component_type(comp_type=comp_type, gif_path=gif_path, category=category, visible=visible)
        all_comps = osqlite_util.query_components(cursor, comp_type = comp_type) # ordered by comp_id
        if all_comps is None:
            raise OCSqliteError("No components of type " + comp_type)
        for name, _type, identifier, comp_id, parent_id in all_comps:
            ptn, fmt, num_var, vars = osqlite_util.get_format(identifier)
            comp = component(name, _type, comp_id, identifier)
            all_comp_map[comp_id] = comp
            ctype.add_component(comp)
            identifiers.append(identifier)
            names.append(name)
        give_names = get_give_name(identifiers, names)
        for give_name in give_names:
            ctype.add_give_name(give_name)
        return (ctype, all_comp_map)
    except AssertionError:
        raise
    except Exception:
        raise

def get_component_types(cursor):
    all_comp_types = {}
    comp_types_order = []
    all_comp_map = {}
    comp_types = osqlite_util.query_comp_type_info(cursor)

    if comp_types is None:
        raise OCSqliteError("No component types in the database")
    for comp_type, gif_path, visible, category in comp_types:
        if gif_path is None:
            # This is to filter out the cable components.
            # For all component types defined in components.conf,
            # gif_path is an empty string if it isn't given.
            # The parser should give a more reliable way
            # to detect whether a component should be printed
            # into the components.conf file or not
            continue
        comp_types_order.append(comp_type)
        ctype, all_comp_map = process_component_type(cursor, comp_type = comp_type, \
                                            gif_path = gif_path, \
                                            visible = visible, \
                                            category = category, \
                                            all_comp_map=all_comp_map)
        all_comp_types[comp_type] = ctype
    return (all_comp_types, comp_types_order, all_comp_map)

def get_component_types_text(cursor, all_comp_types, comp_types_order):
    s = ""
    for comp_type in comp_types_order:
        s += all_comp_types[comp_type].__str__() + NEWLINE
    return s

def _construct_component_tree(cursor, all_comp_map, comp_tree, node_comp_id):
    try:
        children = osqlite_util.query_children_components(cursor, node_comp_id)
        if children is None:
            return
        for name, type, identifier, comp_id in children:
            if comp_id not in all_comp_map:
                # Since all_comp_map contains all components to be defined
                # in components.conf, any comp_id that isn't in the all_comp_map
                # shouldn't be included in the tree.
                # Examples of comp_ids that are not in all_comp_map
                # are cable components.
                continue
            child = all_comp_map[comp_id]
            comp_tree.add_node(node_comp_id, {'comp': child, 'visited': False}, child.comp_id)
            _construct_component_tree(cursor, all_comp_map, comp_tree, comp_id)
    except Exception:
        raise

def construct_component_tree(cursor, all_comp_map):
    try:
        comp_tree = component_tree()
        tmp = osqlite_util.query_root_components(cursor)
        for name, type, identifier, comp_id in tmp:
            root = all_comp_map[comp_id]
            comp_tree.add_node(None, {'comp': root, 'visited': False}, comp_id)
            _construct_component_tree(cursor, all_comp_map, comp_tree, comp_id)
        return comp_tree
    except Exception:
        raise

def _get_component_tree_text(comp_tree, comp_id, num_indent):
    node = comp_tree.get_node_data(comp_id)
    if node is None:
        raise OCSqliteError("comp " + str(comp_id) + " not found")
    comp = node['comp']
    leaves = {}
    leaves_order = []
    if comp_tree.is_leaf(comp_id) or node['visited']:
        '''
        If leave, just return the empty string and itself
        '''
        node['visited'] = True
        return ("", node)
    else:
        s = (INDENT * num_indent) + comp.comp_type + "{" + comp.identifier + "}"
        s += "/" + NEWLINE
        num_indent += 1
        for child in comp_tree.get_children(comp_id):
            tmp = _get_component_tree_text(comp_tree, child, num_indent)
            leaf = tmp[1]
            if leaf is not None:
                comp = leaf['comp']
                if comp.comp_type not in leaves:
                    leaves[comp.comp_type] = []
                    leaves_order.append(comp.comp_type)
                if comp.identifier not in leaves[comp.comp_type]:
                    leaves[comp.comp_type].append(comp.identifier)
            else:
                s += tmp[0]
        if len(leaves) > 0:
            for child_type in leaves_order:
                s += (INDENT * num_indent) + child_type + \
                        "{" + osqlite_util.collapse_string(leaves[child_type]) + "}" \
                        + NEWLINE
        node['visited'] = True
        return (s, None)

def get_component_tree_text(comp_tree):
    s = "component_tree:" + NEWLINE
    num_indent = 1
    for root in comp_tree.get_roots():
        s += _get_component_tree_text(comp_tree, root, num_indent)[0]
    return s

def get_template_metric_component_types(cursor, metric_s):
    metrics = metric_s.split(',')
    comp_type_metrics = {}
    comp_type_info = {}
    mtype_id_order = []
    ctype_order = []
    metric_info = {}
    for metric in metrics:
        tmp = osqlite_util.process_metric_name_id_string(metric)
        if tmp is None:
            raise OCSqliteError(metric + \
                        ": Couldn't get metric_name and metric_id")
        metric_info[int(tmp['metric_id'])] = tmp

    tmp = ",".join(['?'] * len(metric_info.keys()))
    cursor.execute("SELECT metric_id,metric_type_id,prod_comp_id,type,identifier " \
                   "FROM metrics JOIN components WHERE " \
                   "prod_comp_id = comp_id AND metric_id IN (" + tmp + ")", \
                   metric_info.keys())
    queried_comps = cursor.fetchall()
    if len(queried_comps) < 1:
        raise OCSqliteError("producer component of metric " + \
                        str(metric_info['metric_id']) + " not found")
    for metric_id, mtype_id, prod_comp_id, comp_type, identifier in queried_comps:
        if comp_type not in comp_type_info:
            comp_type_info[comp_type] = {'mbase_name': [metric_info[metric_id]['base_name']], \
                                         'mfull_name': [metric_info[metric_id]['full_name']], \
                                         'identifier': {}}
            mtype_id_order.append(osqlite_util.get_metric_type_id(metric_id))
            ctype_order.append(comp_type)
        else:
            if metric_info[metric_id]['base_name'] not in comp_type_info[comp_type]['mbase_name']:
                comp_type_info[comp_type]['mbase_name'].append(metric_info[metric_id]['base_name'])
            if metric_info[metric_id]['full_name'] not in comp_type_info[comp_type]['mfull_name']:
                comp_type_info[comp_type]['mfull_name'].append(metric_info[metric_id]['full_name'])
        if identifier not in comp_type_info[comp_type]['identifier']:
            comp_type_info[comp_type]['identifier'][identifier] = [metric_info[metric_id]['extension']]
        else:
            if metric_info[metric_id]['extension'] not in comp_type_info[comp_type]['identifier'][identifier]:
                comp_type_info[comp_type]['identifier'][identifier].append(metric_info[metric_id]['extension'])
    if len(ctype_order) == 0:
        return None

    ctype_order_idx = sorted(xrange(len(mtype_id_order)), key = lambda k: mtype_id_order[k])
    order = []
    for i in ctype_order_idx:
        comp_type = ctype_order[i]
        comp_type_metrics[comp_type] = []
        key = 'mbase_name'
        if len(comp_type_info[comp_type]['identifier']) == 1:
            id = comp_type_info[comp_type]['identifier'].keys()[0]
            if len(comp_type_info[comp_type]['identifier'][id]) > 1:
                key = 'mfull_name'
        comp_type_metrics[comp_type] = comp_type_info[comp_type][key]
        order.append(comp_type)
    return (mtype_id_order[ctype_order_idx[0]], comp_type_metrics, order)

def get_template_config(cfg_s):
    cfg = {}
    order = []
    cfg_list = cfg_s.split(";")
    for c in cfg_list:
        tmp = c.split(":")
        cfg[tmp[0]] = tmp[1]
        order.append(tmp[0])
    if len(cfg) == 0:
        return None
    return (cfg, order)

def get_sampler_templates(cursor):
    templates = osqlite_util.query_template_info(cursor)
    # Nov 24, 2014
    # Reverse the order because the oparser_sqlite inserts in reverse order
    # of the text file
    if templates is None:
        raise OCSqliteError("No sampler templates")
    sampler_templates = {}
    _sampler_templates_order = []
    mtype_id_order = []
    for name, ldmsd_set, cfg, metrics in templates:
        temp = sampler_template(name, ldmsd_set)
        tmp = get_template_metric_component_types(cursor, metrics)
        mtype_id, comp_metrics, order = tmp
        mtype_id_order.append(mtype_id)
        if comp_metrics is None:
            print "template " + name + \
                            ": failed to get component metrics."
            return None
        temp.set_component_and_metrics(comp_metrics, order)
        template_cfg, order = get_template_config(cfg)
        if template_cfg is None:
            raise OCSqliteError("template " + name + \
                                    ": Failed to get template configuration")
        temp.set_cfg(template_cfg, order)
        sampler_templates[temp.name] = temp
        _sampler_templates_order.append(temp.name)
    templ_idx_order = sorted(xrange(len(mtype_id_order)), key = lambda k: mtype_id_order[k])
    sampler_templates_order = [_sampler_templates_order[i] for i in templ_idx_order]
    return (sampler_templates, sampler_templates_order)

def get_sampler_template_text(cursor, all_templates, order):
    s = ""
    for temp_name in order:
        s += all_templates[temp_name].__str__()
    return s

def get_apply_on_metric_component(cursor, metric_s, template):
    metric_info = {'extension': [], 'metric_id': []}
    metrics = metric_s.split(',')
    for metric in metrics:
        tmp = osqlite_util.process_metric_name_id_string(metric)
        if tmp is None:
            raise OCSqliteError(metric + ": Couldn't get metric_name and metric_id")

        metric_info['metric_id'].append(tmp['metric_id'])
        metric_info['extension'].append(tmp['extension'])

    tmp = ",".join(['?'] * len(metric_info['metric_id']))
    cursor.execute("SELECT metrics.metric_id,components.type, components.identifier " \
                   "FROM components JOIN metrics WHERE " \
                   "metrics.prod_comp_id = components.comp_id AND " \
                   "metrics.metric_id in (" + tmp + ")", metric_info['metric_id'])
    queried_comps = cursor.fetchall()
    if len(queried_comps) < 1:
        raise OCSqliteError("Producer component of metric " + \
                                str(metric_info['metric_id']) + " not found")
    comps = {}
    comps_order = []
    for metric_id, comp_type, identifier in queried_comps:
        if comp_type not in comps:
            comps[comp_type] = {'identifier': [], 'name': []}
            comps_order.append(comp_type)

        idx = metric_info['metric_id'].index(str(metric_id))
        if identifier not in comps[comp_type]['identifier']:
            comps[comp_type]['identifier'].append(identifier)
            template_metrics_s = template.get_metric_text(comp_type)
            if ('#' not in template_metrics_s) and \
                            (metric_info['extension'][idx] != ""):
                comps[comp_type]['name'].append(metric_info['extension'][idx])
    return (comps, comps_order)

def get_template_apply_on(cursor, all_templates):
    apply_on_hosts = {}
    order = []
    tmp = osqlite_util.query_template_apply_on(cursor)
    if tmp is None:
        raise OCSqliteError("No apply on record")

    # Reverse the order because the parser inserts the apply_on in reverse order
    tmp.reverse()
    for host, template_name, metric_s in tmp:   # for each template
        apply_on = None
        '''
        Get the host to apply the templates on
        '''
        _comp = osqlite_util.query_components(cursor, comp_name = host)
        if len(_comp) > 1:
            raise OCSqliteError("More than 1 component associated with the host " + _comp)
        name, comp_type, identifier, comp_id, parent_id = _comp[0]
        '''
        Only one iteration
        '''
        if name not in apply_on_hosts:
            apply_on_hosts[name] = apply_on_node(comp_type, identifier)
            order.append(name)

        apply_on = apply_on_hosts[name]
        template = all_templates[template_name]
        comps, comps_order = get_apply_on_metric_component(cursor, metric_s, template)
        temp = apply_on_node.apply_on_template(template_name)
        temp.set_components(comps, comps_order)
        apply_on.add_template(temp)
    return (apply_on_hosts, order)

def get_template_apply_on_text(cursor, all_templates):
    s = ""
    apply_on_hosts, order = get_template_apply_on(cursor, all_templates)
    for host in order:
        s += apply_on_hosts[host].__str__()
    return s

def process_service_attr_values(s):
    attr_values = s.split(";")
    result = {}
    order = []
    for x in attr_values:
        tmp = x.split(":", 1)
        order.append(tmp[0])
        result[tmp[0]] = tmp[1]
    return (result, order)

def process_balerd_hosts(s):
    hosts = s.split(";")
    names = []
    for host in hosts:
        tmp = host.split(":")
        names.append(tmp[0])
    return ({'names': osqlite_util.collapse_string(names)}, ["names"])

def get_services_aggregator(agg_conf):
    services = {}
    add_attr = {}
    add_attr_order = []
    added_hosts = []
    tmp_add_verb_attr = []
    for name, hostname, verb, _attr_values in agg_conf:
        if hostname not in services:
            services[hostname] = service(name, hostname)
        service_obj = services[hostname]

        attr_values, order = process_service_attr_values(_attr_values)
        if verb == "add":
            host = attr_values['host']
            if "sets" in attr_values:
                attr_values['sets'] = attr_values['sets'].replace(host + "/", "")

            attr_values['host'] = ""
            if add_attr == attr_values:
                added_hosts.append(host)
            else:
                """
                Done with the current 'add' verb. This is true because the query
                is ordered by the cmd_id. All hosts (specified with the
                current host) that have the same interval, xprt, port, etc
                as the current one are found already.
                """
                if len(added_hosts) > 0:
                    added_hosts.sort()
                    add_attr['host'] = osqlite_util.collapse_string(added_hosts)
                    tmp_add_verb_attr.append({'verb': verb, \
                                              'attr_values': (add_attr, add_attr_order)})
                add_attr = attr_values
                add_attr_order = order
                added_hosts = [host]
        else:
            if len(added_hosts) > 0:
                added_hosts.sort()
                add_attr['host'] = osqlite_util.collapse_string(added_hosts)
                tmp_add_verb_attr.append({'verb': "add", \
                                          'attr_values': (add_attr, add_attr_order)})
                tmp_add_verb_attr.reverse()
                service_obj.merge_verb_attr(tmp_add_verb_attr)
                tmp_add_verb_attr = []
                add_attr = {}
                add_attr_order = []
                added_hosts = []

            if "hosts" in attr_values:
                value_s = osqlite_util.collapse_string(attr_values['hosts'].split(","))
                attr_values['hosts'] = value_s

            service_obj.add_verb_attr(verb, attr_values, order)
    return services

def get_services(cursor, _service):
    result = osqlite_util.query_service_conf(cursor, _service)
    services = {}
    if result is None:
        return None

    if _service == "ldmsd_aggregator":
        return get_services_aggregator(result)

    for name, hostname, verb, attr_values in result:
        if hostname not in services:
            services[hostname] = service(name, hostname)
        service_obj = services[hostname]

        if (_service == "balerd") and (verb == "hosts"):
            tmp = process_balerd_hosts(attr_values)
        else:
            tmp = process_service_attr_values(attr_values)

        service_obj.add_verb_attr(verb, tmp[0], tmp[1])
    return services

def get_services_text(cursor):
    services = ["ldmsd_aggregator", "balerd", "me", "komondor"]
    s = ""
    for service in services:
        result = get_services(cursor, service)
        for host in result:
            s += result[host].__str__()
    return s

def get_models(cursor):
    _models = osqlite_util.query_models(cursor)
    models = []
    for model_id, name, params, thresholds, report_flags in _models:
        models.append(model(model_id, name, params, thresholds, report_flags))
    return models

def get_models_text(cursor, models):
    s = ""
    for model in models:
        s += model.__str__()
    return s

def get_actions(cursor):
    _actions = osqlite_util.query_actions(cursor)
    actions = []
    for name, execute, action_type in _actions:
        actions.append(action(name, execute, action_type))
    return actions

def get_actions_text(cursor, actions):
    s = ""
    for action in actions:
        s += action.__str__()
    return s

def _get_event(cursor, event_id, event_name, model_id):
    e = event(event_id, event_name, model_id)
    _metrics = osqlite_util.query_event_metrics(cursor, [event_id])
    if _metrics is not None:
        for event_id, comp_id, metric_id in _metrics:
            if model_id != USER_EVENT_MODEL_ID:
                mnames = osqlite_util.query_metrics(cursor, \
                                            metric_id_list = [metric_id])
                if mnames is None:
                    raise OCSqliteError("Metric ID " + str(metric_id) + " not found")
                mname, metric_id, a1, a2, coll_comp, a2 = mnames[0]
                e.add_metric(metric_id, mname)
                # Query the component type and the identifier of the coll_comp
                comp = osqlite_util.query_components(cursor, comp_name = coll_comp)
            else:
                comp = osqlite_util.query_components(cursor, comp_id_list=[comp_id])
            if comp is None:
                raise OCSqliteError(event.name + ": " + "component name " + \
                                        coll_comp + " not found")
            if len(comp) > 1:
                raise OCSqliteError(event.name + ": node " + coll_comp + \
                                        " not unique")
            comp_name, comp_type, identifier, comp_id, parent_id = comp[0]
            e.add_component(comp_id, comp_type, identifier)
    _actions = osqlite_util.query_event_msg_action(cursor, [event_id])
    if _actions is None:
        return e
    for event_id, level, msg, action in _actions:
        level_name = osqlite_util.get_severity_level(level)
        e.add_severity_action(level_name, action)
        e.set_severity_msg(level_name, msg)
    return e

def get_events(cursor):
    try:
        _events = osqlite_util.query_event_templates(cursor)
        if _events is None:
            return None

        events = {}
        for event_id, ename, model_id in _events:
            events[event_id] = _get_event(cursor, event_id, ename, model_id)
        return events
    except Exception:
        raise

def _process_event_components(cursor, event):
    components_text = ""
    comp_types = {}
    for comp_id in sorted(event.components.keys()):
        ctype = event.components[comp_id]['comp_type']
        if ctype not in comp_types:
            comp_types[ctype] = []
        comp_types[ctype].append(event.components[comp_id]['identifier'])
    for ctype in comp_types:
        cursor.execute("SELECT COUNT(*) FROM components WHERE type = ?", (ctype,))
        num = cursor.fetchone()[0]
        if components_text != "":
            components_text += ","
        if num == len(comp_types[ctype]):
            components_text += ctype + "{*}"
        else:
            components_text += ctype + "{"
            components_text += osqlite_util.collapse_string(comp_types[ctype])
            components_text += "}"
    return components_text

def get_event_metrics_components_text(cursor, event):
    metrics_text = ""
    components_text = ""
    try:
        if event.model_id == USER_EVENT_MODEL_ID:
            components_text = _process_event_components(cursor, event)
            return (metrics_text, components_text)

        mname_fmt = {'base': "", 'ext': []}
        for metric_id in sorted(event.metrics.keys()):
            mname = event.metrics[metric_id]
            # Find the name format
            if 'baler_ptn' in mname:
                mname_fmt['base'] = '%baler_ptn%'
                idx = mname.index('#')
                ext = mname[:idx]
            elif '#' in mname:
                idx = mname.index('#') + 1
                mname_fmt['base'] = mname[:idx] + "%"
                ext = mname[idx:]
            else:
                mname_fmt['base'] = mname
                ext = ""
            if ext not in mname_fmt['ext']:
                mname_fmt['ext'].append(ext)

        # Query distinct components collecting the metrics
        cursor.execute("SELECT COUNT(DISTINCT(comp_id)) FROM metrics " \
                       "JOIN components WHERE coll_comp = components.name AND " \
                       "metrics.name LIKE '" + mname_fmt['base'] + "' ORDER BY comp_id")
        exp_num = cursor.fetchone()
        if exp_num is None:
            raise Exception(event.name + ": components collecting the metrics not found")
        exp_num = exp_num[0]
        if len(event.components) == exp_num:
            components_text = "*"
        else:
            components_text = _process_event_components(cursor, event)

        # Find the total number of metrics with the name format
        cursor.execute("SELECT COUNT(DISTINCT(name)) FROM metrics " \
                       "WHERE name LIKE '" + mname_fmt['base'] + "'")

        exp_count = cursor.fetchone()
        if exp_count is None:
            raise OCSqliteError(event.name + ": metric " + mname_fmt + " not found")
        exp_count = exp_count[0]
        if len(mname_fmt['ext']) == exp_count:
            if 'baler_ptn' in mname_fmt['base']:
                metrics_text = "[*]#baler_ptn"
            else:
                metrics_text = mname_fmt['base'].replace("%", "[*]")
        else:
            mname_fmt['base'] = mname_fmt['base'].replace("%", "")
            if 'baler_ptn' in mname_fmt['base']:
                full_name_s = [s + "#" + mname_fmt['base'] for s in sorted(mname_fmt['ext'])]
                metrics_text = osqlite_util.collapse_string(full_name_s)
            else:
                full_name_s = [mname_fmt['base'] + s for s in sorted(mname_fmt['ext'])]
                metrics_text = osqlite_util.collapse_string(full_name_s)

        return (metrics_text, components_text)
    except Exception:
        raise

def get_events_text(cursor, events):
    s = ""
    try:
        for event_id in events:
            text = get_event_metrics_components_text(cursor, events[event_id])
            events[event_id].set_metrics_text(text[0])
            events[event_id].set_components_text(text[1])
            s += events[event_id].__str__()
        return s
    except Exception:
        raise

def get_cable_types(cursor):
    try:
        cable_types = osqlite_util.query_cable_types(cursor)
        if cable_types is None:
            raise Exception("No cable types")
        cable_types.reverse()   # oparser_sqlite inserts cable types reversely from cables.conf.
        all_cable_types = {}
        cable_types_order = []
        for type_id, type, desc in cable_types:
            cbtype = cable_type(type, desc)
            cable_types_order.append(type_id)
            all_cable_types[type_id] = cbtype
        return (all_cable_types, cable_types_order)
    except Exception:
        raise

def get_cable_types_text(cursor, all_cable_types, cable_types_order):
    s = ""
    for i in cable_types_order:
        s += all_cable_types[i].__str__()
    return s

def get_cables(cursor, all_cable_types):
    cables = osqlite_util.query_cables(cursor)
    if cables is None:
        raise OCSqliteError("No cables")
    cables.reverse()
    cable_types = {}
    cable_types_order = []
    for type_id, src_comp_id, dest_comp_id in cables:
        if type_id not in cable_types:
            cable_types[type_id] = []
            cable_types_order.append(type_id)
        cb_type = all_cable_types[type_id].get_type()
        src = osqlite_util.query_component(cursor, src_comp_id)
        if src is None:
            raise OCSqliteError("comp_id " + str(src_comp_id) + " not found")
        src_type = src[1]
        src_identifier = src[2]
        dest = osqlite_util.query_component(cursor, dest_comp_id)
        if dest is None:
            raise Exception("comp_id " + str(dest_comp_id) + " not found")
        dest_type = dest[1]
        dest_identifier = dest[2]
        parent_id_s = str(src_comp_id) + "," + str(dest_comp_id)
        candidate_cables = osqlite_util.query_components(cursor, \
                                                parent_id_s = parent_id_s)
        if candidate_cables is None:
            raise OCSqliteError("No cables that link between comp_ids " + \
                            str(src_comp_id) + " and " + str(dest_comp_id))
        cb = None
        for name, type, identifier, comp_id, parent_id_s in candidate_cables:
            if type == cb_type:
                cb = cable(type, name)
                cb.set_src(src_type, src_identifier, src_comp_id)
                cb.set_dest(dest_type, dest_identifier, dest_comp_id)
                cable_types[type_id].append(cb)
                break
        if cb is None:
            raise OCSqliteError("No cables of type " + cb_type + "that link " \
                            "between comp_ids " + str(src_comp_id) + \
                            " and " + str(dest_comp_id))
    cables = []
    for cb_type in cable_types_order:
        cables += cable_types[cb_type]
    return cables

def get_cables_text(cursor, all_cables):
    s = "cables:" + NEWLINE
    for cb in all_cables:
        s += cb.__str__()
    return s

def open_conf_file(conf, output_dir):
    conf_path = output_dir + "/" + conf
    osqlite_util.rename_exist_file(conf_path)
    try:
        f = open(conf_path, "a")
        return f
    except IOError:
        raise

def write_component_conf(cursor, output_dir):
    try:
        print "Writing components.conf ..."
        f = None
        f = open_conf_file("components.conf", output_dir)
        print "\tDefine component types and components"
        all_comp_types, comp_types_order, all_comp_map = get_component_types(cursor)
        f.write(get_component_types_text(cursor, all_comp_types, comp_types_order))
        print "\tDefine component tree"
        comp_tree = construct_component_tree(cursor, all_comp_map)
        f.write(get_component_tree_text(comp_tree))
        f.close()
        print "\tDone"
    except sqlite3.OperationalError, e:
        print "\t\tSKIPPED: " + repr(e)
    except OCSqliteError, e:
        print "\t\tSKIPPED: " + repr(e)
    except Exception, e:
        print repr(e)
        raise
    finally:
        if f:
            f.close()

def write_metric_conf(cursor, output_dir):
    try:
        print "Writing metrics.conf ..."
        f = None
        f = open_conf_file("metrics.conf", output_dir)
        print "\tDefine templates"
        all_templates, all_templates_order = get_sampler_templates(cursor)
        f.write(get_sampler_template_text(cursor, all_templates, all_templates_order))
        print "\tDefine configuration for ldmsd_sampler hosts"
        f.write(get_template_apply_on_text(cursor, all_templates))
        f.close()
        print "\tDone"
    except sqlite3.OperationalError, e:
        print "\t\tSKIPPED: " + repr(e)
    except OCSqliteError, e:
        print "\t\tSKIPPED: " + repr(e)
    except Exception, e:
        raise
    finally:
        if f:
            f.close()

def write_services_conf(cursor, output_dir):
    try:
        print "Writing services.conf ..."
        f = None
        f = open_conf_file("services.conf", output_dir)
        print "\tDefine services"
        f.write(get_services_text(cursor))
        f.close()
        print "\tDone"
    except sqlite3.OperationalError, e:
        print "\t\tSKIPPED: " + repr(e)
    except OCSqliteError, e:
        print "\t\tSKIPPED: " + repr(e)
    except Exception:
        raise
    finally:
        if f:
            f.close()

def write_model_events_conf(cursor, output_dir):
    try:
        print "Writing model_events.conf ..."
        f = None
        f = open_conf_file("model_events.conf", output_dir)
        count_skipped = 3
        try:
            print "\tDefine models"
            all_models = get_models(cursor)
            f.write(get_models_text(cursor, all_models))
            count_skipped -= 1
        except sqlite3.OperationalError, e:
            print "\t\tSKIPPED: " + repr(e)
        except OCSqliteError, e:
            print "\t\tSKIPPED: " + repr(e)
        except Exception:
            raise

        try:
            print "\tDefine actions"
            all_actions = get_actions(cursor)
            f.write(get_actions_text(cursor, all_actions))
            count_skipped -= 1
        except sqlite3.OperationalError, e:
            print "\t\tSKIPPED: " + repr(e)
        except OCSqliteError, e:
            print "\t\tSKIPPED: " + repr(e)
        except Exception:
            raise

        try:
            print "\tDefine events"
            all_events = get_events(cursor)
            f.write(get_events_text(cursor, all_events))
            count_skipped -= 1
        except sqlite3.OperationalError, e:
            print "\t\tSKIPPED: " + repr(e)
        except OCSqliteError, e:
            print "\t\tSKIPPED: " + repr(e)
        except Exception:
            raise

        f.close()
        if count_skipped < 3:
            print "\tDone"
    except Exception:
        raise
    finally:
        if f:
            f.close()

def write_cables_conf(cursor, output_dir):
    try:
        print "Writing cables.conf ..."
        f = None
        f = open_conf_file("cables.conf", output_dir)
        print "\tDefine cable types"
        all_cable_types, cable_types_order = get_cable_types(cursor)
        f.write(get_cable_types_text(cursor, all_cable_types, cable_types_order))
        print "\tDefine cables"
        all_cables = get_cables(cursor, all_cable_types)
        f.write(get_cables_text(cursor, all_cables))
        f.close()
        print "\tDone"
    except sqlite3.OperationalError, e:
        print "\t\tSKIPPED: " + repr(e)
    except OCSqliteError, e:
        print "\t\tSKIPPED: " + repr(e)
    except Exception:
        raise
    finally:
        if f:
            f.close()
