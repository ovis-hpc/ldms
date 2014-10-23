'''
Created on Oct 23, 2014

@author: nichamon
'''

import sys
import osqlite_util
from __builtin__ import str
from oconf_sqlite.osqlite_util import USER_EVENT_MODEL_ID

INDENT = "\t"
NEWLINE = "\n"

class component:
    def __init__(self, name, comp_type = None, comp_id = None, identifier = None):
        self.name = name
        self.comp_type = comp_type
        self.comp_id = comp_id
        self.identifier = identifier

    def str_type_identifier_format(self):
        return self.comp_type + "{" + self.identifier + "}"

    def __str__(self):
        s = "(name: " + str(self.name) + ","
        s += "type: " + str(self.comp_type) + ","
        s += "comp_id: " + str(self.comp_id) + ","
        s += "identifier: " + str(self.identifier) + ")"
        return s

    def __eq__(self, other):
        if self.name != other.name:
            return False
        if self.comp_type != other.comp_type:
            return False
        if self.comp_id != other.comp_id:
            return False
        if self.identifier != other.identifier:
            return False
        return True

    def __ne__(self, other):
        return not self.__eq__(other)

class component_type:
    def __init__(self, comp_type, gif_path = None, category = None, visible = 1):
        self.comp_type = comp_type
        self.attr_ordering = ['gif_path', 'category', 'visible']
        self.attr = {}
        self.attr['gif_path'] = gif_path
        self.attr['category'] = category
        if visible == 1 or visible == "True" or visible == "true" or visible == True:
            self.attr['visible'] = "true"
        else:
            self.attr['visible'] = "false"
        self.components = {}  # the key is the comp_id.
        self.give_name = []   # a tuple of identifiers and names

    def set_gif_path(self, gif_path):
        self.attr['gif_path'] = gif_path

    def set_category(self, category):
        self.attr['category'] = category

    def set_visible(self, visible):
        self.attr['visible'] = visible

    def get_gif_path(self):
        return self.attr['gif_path']

    def get_comp_type_attr(self):
        return self.attr

    def get_comp_type_attr_ordering(self):
        return self.attr_ordering

    def add_component(self, component):
        self.components[component.comp_id] = component

    def get_components(self):
        return self.components

    def get_component(self, comp_id):
        return self.components[comp_id]

    def add_give_name(self, (identifiers, names)):
        self.give_name.append((identifiers, names))

    def get_give_name(self):
        return self.give_name

    def __str__(self):
        s = "component:" + NEWLINE
        s += INDENT + "type: " + self.comp_type + NEWLINE
        for key in self.attr_ordering:
            if self.attr[key]:
                if (key == "visible") and (self.attr[key] == "true"):
                    continue
                if self.attr[key] is None:
                    continue
                s += INDENT + key + ": " + str(self.attr[key]) + NEWLINE
        for identifier_s, name_s in self.give_name:
            s += INDENT + "give_name:" + NEWLINE
            s += INDENT + INDENT + "identifiers: " + identifier_s + NEWLINE
            s += INDENT + INDENT + "names: " + name_s + NEWLINE
        return s[:-1]       # Get rid of the last NEWLINE

    def __eq__(self, other):
        if self.comp_type != other.comp_type:
            return False

        for key in self.attr_ordering:
            if self.attr[key] is not None:
                if other.attr[key] is None:
                    return False
                if self.attr[key] != other.attr[key]:
                    return False
            else:
                if other.attr[key] is not None:
                    return False

        if self.components != other.components:
            return False

        if self.give_name != other.give_name:
            return False
        return True

    def __ne__(self, other):
        return not self.__eq__(other)

class component_tree:
    def __init__(self):
        self.root_list = []
        self.node_dict = {}

    def add_node(self, parent_key, node, node_key):
        if node_key not in self.node_dict:
            self.node_dict[node_key] = {'data': node, 'children': []}

        if parent_key is not None:
            self.node_dict[parent_key]['children'].append(node_key)
        else:
            self.root_list.append(node_key)

    def get_children(self, key):
        return self.node_dict[key]['children']

    def get_roots(self):
        return self.root_list

    def get_node_data(self, key):
        return self.node_dict[key]['data']

    def set_node_data(self, key, data):
        self.node_dict[key]['data'] = data

    def is_leaf(self, key):
        if len(self.get_children(key)) > 0:
            return False
        return True

    def __str__(self):
        s = "root:"
        s += NEWLINE
        s += INDENT + ", ".join(map(str, self.root_list))
        s += NEWLINE
        for key in self.node_dict:
            s += INDENT + str(key) + " ---> "
            children_s = []
            for child in self.get_children(key):
                children_s.append(self.get_data(key).__str__())

            s += ", ".join(children_s)
            s += NEWLINE
        return s

    def __eq__(self, other):
        if self.root_list != other.root_list:
            return False

        if self.node_dict != other.node_dict:
            return False
        return True

    def __nq__(self, other):
        return not self.__eq__(other)

class sampler_template:
    def __init__(self, name, ldmsd_set=None, comp_metrics = None, comp_metrics_ordering = [], **cfg):
        self.name = name
        self.ldmsd_set = ldmsd_set
        if comp_metrics is None:
            self.comp_metrics = None
        else:
            self.comp_metrics = comp_metrics
        if comp_metrics_ordering is None:
            self.comp_metrics_ordering = None
        else:
            self.comp_metrics_ordering = comp_metrics_ordering
        self.cfg = {}
        self.cfg_ordering = []
        for name, value in cfg.items():
            self.cfg[name] = value
            self.cfg_ordering.append(name)

    def add_component_and_metrics(self, comp_type, metrics):
        if comp_type in self.comp_metrics:
            raise Exception("Metrics of type " + comp_type + " already added.")
        self.comp_metrics[comp_type] = metrics
        self.comp_metrics_ordering.append(comp_type)

    def set_component_and_metrics(self, comp_metrics, ordering):
        self.comp_metrics = comp_metrics
        self.comp_metrics_ordering = ordering

    def get_component_and_metrics(self):
        return (self.comp_metrics, self.comp_metrics_ordering)

    def get_metric_text(self, comp_type):
        if comp_type in self.comp_metrics_ordering:
            return ",".join(self.comp_metrics[comp_type])
        else:
            return None

    def add_cfg(self, verb, value):
        self.cfg[verb] = value
        self.cfg_ordering.append(verb)

    def set_cfg(self, cfg, ordering):
        self.cfg = cfg
        self.cfg_ordering = ordering

    def get_cfg(self):
        return (self.cfg, self.cfg_ordering)

    def __str__(self):
        s = "template:" + NEWLINE
        s += INDENT + "template_name: " + str(self.name) + NEWLINE
        s += INDENT + "sampler: " + self.ldmsd_set + NEWLINE
        s += INDENT + INDENT + "config:" + NEWLINE
        if len(self.cfg) == 0:
            raise Exception("There is no conf. info for template '" + self.name + "'")
        for key in self.cfg_ordering:
            s += INDENT + INDENT + INDENT + str(key) + ": " + \
                    str(self.cfg[key]) + NEWLINE
        for comp_type in self.comp_metrics_ordering:
            s += INDENT + INDENT + "component:" + NEWLINE
            s += INDENT + INDENT + INDENT + "type: " + str(comp_type) + NEWLINE
            s += INDENT + INDENT + INDENT + "metrics: " + \
                    ",".join(self.comp_metrics[comp_type]) + NEWLINE
        return s

    def __eq__(self, other):
        if self.name != other.name:
            return False
        if self.ldmsd_set != other.ldmsd_set:
            return False
        if self.comp_metrics != other.comp_metrics:
            return False
        if self.cfg != other.cfg:
            return False
        return True

    def __nq__(self, other):
        return not self.__eq__(other)

class apply_on_node:
    class apply_on_template:
        def __init__(self, name, comp_info = None, comp_ordering = None):
            self.name = name
            self.component = comp_info
            if comp_ordering:
                self.component_ordering = comp_ordering
            else:
                self.component_ordering = []

        def add_component(self, comp_type, identifier, name = None):
            if comp_type not in self.component:
                self.component[comp_type] = {'identifier': [], 'name': []}
                self.component_ordering.append(comp_type)
            self.component[comp_type]['identifier'].add(identifier)
            if name:
                self.component[comp_type]['name'].add(name)

        def get_components(self):
            return (self.component, self.component_ordering)

        def set_components(self, component, ordering):
            self.component = component
            self.component_ordering = ordering

        def __str__(self):
            s = INDENT + "template_name: " + self.name + NEWLINE
            name_s = ""
            for comp_type in self.component_ordering:
                ids = self.component[comp_type]['identifier']
                names = self.component[comp_type]['name']
                id_s = osqlite_util.collapse_string(ids)
                if len(names) > 0:
                    name_s = osqlite_util.collapse_string(names)
                s += INDENT + INDENT + "component: " + comp_type + "{" + id_s + "}" + NEWLINE
                if name_s != "":
                    s += INDENT + INDENT + INDENT + "name: " + name_s + NEWLINE
            return s

        def __eq__(self, other):
            if self.name != other.name:
                return False
            if self.component_ordering != other.component_ordering:
                return False
            if self.component != other.component:
                return False
            return True

        def __nq__(self, other):
            return not self.__eq__(other)

    def __init__(self, comp_type, identifier, templates = None, temp_ordering = None):
        self.comp_type = comp_type
        self.identifier = identifier
        if templates:
            self.templates = templates
        else:
            self.templates = {}
        if temp_ordering:
            self.templates_ordering = temp_ordering
        else:
            self.templates_ordering = []

    def add_template(self, template):
        if template.name in self.templates:
            raise Exception("template " + template.name + " already added.")
        self.templates_ordering.append(template.name)
        self.templates[template.name] = template

    def get_templates(self):
        return (self.templates, self.templates_ordering)

    def __str__(self):
        s = "apply_on:" + NEWLINE
        s += INDENT + "host: " + self.comp_type + "{" + \
                    self.identifier + "}" + NEWLINE
        for temp in self.templates_ordering:
            s += self.templates[temp].__str__()
        return s

    def __eq__(self, other):
        if self.comp_type != other.comp_type:
            return False
        if self.identifier != other.identifier:
            return False
        if self.templates_ordering != other.templates_ordering:
            return False
        if self.templates != other.templates:
            return False
        return True

    def __nq__(self, other):
        return not self.__eq__(other)

class service:
    def __init__(self, name, hostname):
        self.available_services = ["ldmsd_aggregator", "ldmsd_sampler",
                                   "balerd", "me", "komondor", "ocmd"]
        if name not in self.available_services:
            raise ValueError("Unrecognized service " + name)
        self.name = name
        self.hostname = hostname
        self.verb_attrs = []

    def add_verb_attr(self, verb, attr_values, attr_order):
        self.verb_attrs.append({'verb': verb,
                                'attr_values': (attr_values, attr_order)})

    def merge_verb_attr(self, new_verb_attr_list):
        self.verb_attrs += new_verb_attr_list

    def get_verb_attrs(self):
        return self.verb_attrs

    def __eq__(self, other):
        if self.name != other.name:
            return False
        if self.hostname != other.hostname:
            return False
        if self.verb_attrs != other.verb_attrs:
            return False
        return True

    def __nq__(self, other):
        return not self.__eq__(other)

    def __str__(self):
        s = "hostname: " + self.hostname + NEWLINE
        s += INDENT + "service: " + self.name + NEWLINE
        if len(self.verb_attrs) == 0:
            return s

        s += INDENT + "commands:" + NEWLINE
        for verb_attr in self.verb_attrs:
            s += INDENT + INDENT + verb_attr['verb'] + ":" + NEWLINE
            attr_values, order = verb_attr['attr_values']
            for attr in order:
                s += INDENT + INDENT + INDENT + attr + ": " + \
                            attr_values[attr] + NEWLINE
        return s

class model:
    def __init__(self, model_id, name, params = None, thresholds = None, report_flags = None):
        self.model_id = model_id
        self.name = name
        self.params = params
        self.thresholds = thresholds
        if report_flags is None:
            self.report_flags = ""
        else:
            self.report_flags = report_flags

    def set_params(self, params):
        self.params = params

    def set_thresholds(self, thresholds):
        self.thresholds = thresholds

    def set_report_flags(self, report_flags):
        self.report_flags = report_flags

    def get_model_id(self):
        return self.model_id

    def get_name(self):
        return self.name

    def get_params(self):
        return self.params

    def get_thersholds(self):
        return self.thresholds

    def get_report_flags(self):
        return self.report_flags

    def __eq__(self, other):
        if self.model_id != other.model_id:
            return False
        if self.name != other.name:
            return False
        if self.params != other.params:
            return False
        if self.thresholds != other.thresholds:
            return False
        if self.report_flags != other.report_flags:
            return False
        return True

    def __nq__(self, other):
        return not self.__eq__(other)

    def __str__(self):
        s = "model:" + NEWLINE
        s += INDENT + "name: " + self.name + NEWLINE
        s += INDENT + "model_id: " + str(self.model_id) + NEWLINE
        if self.thresholds is not None:
            s += INDENT + "thresholds: " + self.thresholds + NEWLINE
        if self.params is not None:
            s += INDENT + "parameters: " + self.params + NEWLINE
        if (self.report_flags is not None) and (self.report_flags != ""):
            s += INDENT + "report_flags: " + self.report_flags + NEWLINE
        return s

class action:
    def __init__(self, name, execute, action_type = None):
        self.name = name
        self.execute = execute
        if action_type is None:
            self.action_type = "not-corrective"
        else:
            if action_type not in ["not-corrective", "corrective"]:
                raise ValueError("action_type must be either " \
                                 "'not-corrective' or 'corrective'")
            self.action_type = action_type

    def __str__(self):
        s = "action:" + NEWLINE
        s += INDENT + "name: " + self.name + NEWLINE
        s += INDENT + "execute: " + self.execute + NEWLINE
        if (self.action_type is not None) and (self.action_type == "corrective"):
            s += INDENT + "action_type: " + self.action_type + NEWLINE
        return s

    def __eq__(self, other):
        if self.name != other.name:
            return False
        if self.execute != other.execute:
            return False
        if self.action_type != other.action_type:
            return False
        return True

    def __nq__(self, other):
        return self.__eq__(other)

class event:
    def __init__(self, event_id, name, model_id):
        self.event_id = event_id
        self.name = name
        self.model_id = model_id
        self.metrics = {}
        self.components = {}
        self.severity = {}
        self.valid_levels = osqlite_util.get_available_severity_levels()['level_name']
        self.metrics_text = ""
        self.components_text = ""

    def set_metrics(self, metrics):
        self.metrics = metrics

    def add_metric(self, metric_id, metric_name):
        self.metrics[metric_id] = metric_name

    def add_component(self, comp_id, comp_type, identifier):
        self.components[comp_id] = {'comp_type': comp_type, 'identifier': identifier}

    def set_components(self, components):
        self.components = components

    def get_metrics(self):
        return self.metrics

    def get_components(self):
        return self.components

    def set_metrics_text(self, metric_s):
        self.metrics_text = metric_s

    def set_components_text(self, component_s):
        self.components_text = component_s

    def _process_metric_names(self):
        tmp = [self.metrics[x] for x in sorted(self.metrics.keys())]
        return osqlite_util.collapse_string(tmp)

    def _process_components_text(self):
        if len(self.components) == 0:
            return ""
        comps = sorted(self.components.keys())
        comps = map(str, comps)
        return osqlite_util.collapse_string(comps)

    def get_metrics_text(self):
        return self.metrics_text

    def get_components_text(self):
        return self.components_text

    def set_severity_level(self, level_name, msg, actions):
        if level_name not in self.valid_levels:
            raise ValueError("Invalid level " + str(level_name))
        if not isinstance(actions, (list)):
            actions = [actions]
        self.severity[level_name] = {'msg': msg,
                                     'action': actions}

    def set_severity_msg(self, level_name, msg):
        if level_name not in self.valid_levels:
            raise ValueError("Invalid level " + str(level_name))
        if level_name not in self.severity:
            self.severity[level_name] = {'msg': "", 'action': []}
        self.severity[level_name]['msg'] = msg

    def add_severity_action(self, level_name, action):
        if level_name not in self.valid_levels:
            raise ValueError("Invalid level " + str(level_name))
        if level_name not in self.severity:
            self.severity[level_name] = {'msg': "", 'action': []}
        if isinstance(action, (list)):
            self.severity[level_name]['action'] += action
        else:
            self.severity[level_name]['action'].append(action)

    def __eq__(self, other):
        if self.name != other.name:
            return False
        if self.model_id != other.model_id:
            return False
        if self.metrics != other.metrics:
            return False
        if self.components != other.components:
            return False
        if self.severity != other.severity:
            return False
        if self.metrics_text != other.metrics_text:
            return False
        if self.components_text != other.components_text:
            return False
        return True

    def __nq__(self, other):
        return not self.__eq__(other)

    def __str__(self):
        if self.model_id != USER_EVENT_MODEL_ID:
            s = "event:" + NEWLINE
        else:
            s = "user-event:" + NEWLINE
        s += INDENT + "name: " + self.name + NEWLINE
        if self.model_id != USER_EVENT_MODEL_ID:
            s += INDENT + "model_id: " + str(self.model_id) + NEWLINE
        num_indent_component = 1
        if len(self.metrics) > 0:
            s += INDENT + "metric:" + NEWLINE
            if self.metrics_text == "":
                s += INDENT + INDENT + "metric_name: " + self._process_metric_names() + NEWLINE
            else:
                s += INDENT + INDENT + "metric_name: " + self.metrics_text + NEWLINE
            num_indent_component = 2
        if self.components_text == "":
            s += (INDENT * num_indent_component) + "components: " + \
                        self._process_components_text() + NEWLINE
        else:
            s += (INDENT * num_indent_component) + "components: " + \
                        self.components_text + NEWLINE
        if len(self.severity) > 0:
            s += INDENT + "severity:" + NEWLINE
            for level in self.valid_levels:
                if level not in self.severity:
                    continue
                s += INDENT + INDENT + "level: " + level + NEWLINE
                if (self.severity[level]['msg'] != "") and \
                            (self.severity[level]['msg'] is not None):
                    s += INDENT + INDENT + INDENT + "msg: " + \
                            self.severity[level]['msg'] + NEWLINE

                if len(self.severity[level]['action']) > 0:
                    s += INDENT + INDENT + INDENT + "action_name: " + \
                            ",".join(self.severity[level]['action']) + NEWLINE
        return s

class cable_type:
    def __init__(self, type, desc):
        self.type = type
        self.desc = desc

    def get_type(self):
        return self.type

    def __str__(self):
        s = "cable_type:" + NEWLINE
        s += INDENT + "type: " + self.type + NEWLINE
        s += INDENT + "desc: " + self.desc + NEWLINE
        return s

    def __eq__(self, other):
        if self.type != other.type:
            return False
        if self.desc != other.desc:
            return False
        return True

    def __nq__(self, other):
        return not self.__eq__(other)

class cable:
    def __init__(self, type, name, src = None, dest = None):
        self.type = type
        self.name = name
        if src is None:
            self.src = {'type': None, 'identifier': None, 'comp_id': None}
        else:
            self.src = src
        if dest is None:
            self.dest = {'type': None, 'identifier': None, 'comp_id': None}
        else:
            self.dest = dest

    def set_src(self, src_type, src_identifier, src_comp_id):
        self.src['type'] = src_type
        self.src['identifier'] = src_identifier
        self.src['comp_id'] = src_comp_id

    def set_dest(self, dest_type, dest_identifier, dest_comp_id):
        self.dest['type'] = dest_type
        self.dest['identifier'] = dest_identifier
        self.dest['comp_id'] = dest_comp_id

    def get_src(self):
        return self.src

    def get_dest(self):
        return self.dest

    def __str__(self):
        s = INDENT + self.name
        s += INDENT + self.type
        s += INDENT + self.src['type'] + "{" + self.src['identifier'] + "}"
        s += INDENT + self.dest['type'] + "{" + self.dest['identifier'] + "}"
        s += NEWLINE
        return s

    def __eq__(self, other):
        if self.name != other.name:
            return False
        if self.type != other.type:
            return False
        if self.src != other.src:
            return False
        if self.dest != other.dest:
            return False
        return True

    def __nq__(self, other):
        return not self.__eq__(other)

class OCSqliteError(Exception):
    pass
