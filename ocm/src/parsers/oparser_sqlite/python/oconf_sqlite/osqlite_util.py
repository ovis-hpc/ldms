'''
Created on Oct 22, 2014

@author: nichamon
'''
import os
import time
import re
from difflib import SequenceMatcher

filename = os.path.basename(__file__)

COMP_ID_BITS = 32  # number of bits for comp_id
METRIC_TYPE_BITS = 32 # number of bits for metric_type_id

AVAI_SEVERITY_LEVELS = {0: "nominal",
                  1: "info",
                  2: "warning",
                  3: "critical",
                  'nominal': 0,
                  'info': 1,
                  'warning': 2,
                  'critical': 3}

USER_EVENT_MODEL_ID = 65535

# print utilities
def print_line():
    print "---------------------------------------"

def print_ovis_db_path(db_path, indent = ""):
    print indent + "ovis conf. database: " + db_path

# routine utilities
def get_ovis_conf_db():
    return os.getenv('OVIS_RUN_CONF', "/opt/ovis/conf/ovis_conf.db")

def get_format(s):
    digit_ptn = '([0-9]+)'
    result_pattern = ""
    fmt = ""
    num_var = 0
    in_digit = False

    vars = []
    var_s = ""
    for c in s:
        if c.isdigit():
            in_digit = True
            var_s += c
        else:
            if in_digit:
                result_pattern += digit_ptn
                fmt += '{0[' + str(num_var) + ']}'
                num_var += 1
                vars.append(var_s)
                var_s = ""
            in_digit = False
            result_pattern += c
            fmt += c
    if in_digit:
        result_pattern += digit_ptn
        fmt += '{0[' + str(num_var) + ']}'
        vars.append(var_s)
        num_var += 1
    return (result_pattern, fmt, num_var, vars)

def is_hexadecimal(s):
    import string
    return all(c in string.hexdigits for c in s)

def get_num_filled_zeros(s):
    if not is_hexadecimal(s):
        raise TypeError(s + " not a hexadecimal or a decimal")
    if s[0] == "0":
        return len(s)
    else:
        return 0

def have_leading_zeroes(s):
    if s[0] == "0":
        return True
    return False

def _get_integer(s):
    try:
        result = int(s)
    except ValueError:
        try:
            result = int(s, 16)
        except ValueError:
            raise ValueError(s + " not a hexadecimal or a decimal")
    return result

def _get_range(l):
    curr_s = l[0]
    curr = _get_integer(curr_s)
    prev = curr
    end_s = None

    s = curr_s
    for curr_s in l[1:]:
        curr = _get_integer(curr_s)
        if curr - prev == 1:
            end_s = curr_s
        elif curr == prev:
            pass
        else:
            if end_s:
                s += "-" + end_s
                end_s = None
            s += "," + curr_s
        prev = curr

    if end_s:
        s += "-" + end_s

    if ("-" in s) or ("," in s):
        s = "[" + s + "]"
    return s

def collapse_string(s_list):
    result = ""
    var_list = None
    prev_fmt = ""
    prev_num_var = 0
    for s in s_list:
        ptn, fmt, num_var, vars = get_format(s)
        if var_list is None:
            var_list = [[vars[i]] for i in xrange(num_var)]
            prev_ptn = ptn
            prev_fmt = fmt
            prev_num_var = num_var
            continue
        if prev_ptn == ptn:
            count = 0
            for i in xrange(num_var):
                var_list[i].append(vars[i])
                if len(set(var_list[i])) > 1:
                    count += 1
                if count <= 1:
                    continue
                ranges = []
                for j in xrange(num_var):
                    if j <= i:
                        var_list[j].pop()
                    ranges.append(_get_range(var_list[j]))
                result += prev_fmt.format(ranges)
                var_list = [[vars[j]] for j in xrange(num_var)]
                result += ","
                break

        else:
            ranges = []
            for j in xrange(num_var):
                ranges.append(_get_range(var_list[j]))
            result += prev_fmt.format(ranges)
            var_list = [[vars[j]] for j in xrange(num_var)]
            result += ","
        prev_ptn = ptn
        prev_fmt = fmt
        prev_num_var = num_var
    ranges = []
    for i in xrange(prev_num_var):
        ranges.append(_get_range(var_list[i]))
    result += prev_fmt.format(ranges)
    return result

def find_lcs(s1, s2):
    sm = SequenceMatcher(None, s1, s2)
    blocks = sm.get_matching_blocks()
    pattern = ""
    fmt = ""
    lcs = []
    num_var = 0
    prev_j1 = -1
    prev_j2 = -1
    prev_size = -1
    for m in blocks:
        if m.size == 0:
            if prev_j1 == -1:
                return ("", "", 0, "")
            if (prev_j1 == len(s1)) and (prev_j2 == len(s2)):
                pattern += ""
            else:
                fmt += "{0[" + str(num_var) + "]}"
                num_var += 1
                lcs.append("")
                if (prev_j1 != len(s1)) and (prev_j2 != len(s2)):
                    pattern += "(.+)"
                else:
                    pattern += "(.*)"
            lcs = " ".join(lcs)
            break;

        j1 = m[0] + m[2]
        j2 = m[1] + m[2]
        if prev_size != -1:
            if (m[0] - prev_j1 == 0) or (m[1] - prev_j2 == 0):
                pattern += "(.*)"
            else:
                pattern += "(.+)"
            fmt += "{0[" + str(num_var) + "]}"
            num_var += 1
        else:
            if m[0] == 0 and m[1] == 0:
                fmt += ""
                pattern += ""
            else:
                fmt += "{0[" + str(num_var) + "]}"
                num_var += 1
                if m[0] != 0 and m[1] != 0:
                    pattern += "(.+)"
                else:
                    pattern += "(.*)"
                lcs.append("")
        pattern += s1[m[0]:j1]
        fmt += s1[m[0]:j1]
        lcs.append(s1[m[0]:j1])
        prev_j1 = j1
        prev_j2 = j2
        prev_size = m[2]
    return (pattern, fmt, num_var, lcs)

'''
@brief Query all component types in the database

@return List of tuples (type, )
'''
def query_comp_types(cursor):
    try:
        cursor.execute("SELECT DISTINCT(type) FROM components;")
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query the information of all component types or a given component type

@return list of tuples (type, gif_path, visible, category)
'''
def query_comp_type_info(cursor, comp_type = None):
    try:
        if comp_type is None:
            cursor.execute("SELECT DISTINCT(type),gif_path,visible,category " \
                                                        "FROM components;")
        else:
            cursor.execute("SELECT DISTINCT(type),gif_path,visible,category " \
                                "FROM components WHERE type = ?;", (comp_type,))
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query all components in the database or the components with the given properties
        The properties could be one or combination of the following
         - component type
         - component name

@return list of tuples (name, type, identifier, comp_id, parent_id)
'''
def query_components(cursor, comp_id_list = None, comp_type = None, \
                     comp_name = None, parent_id_s = None):
    try:
        stmt = "SELECT name, type, identifier, comp_id, parent_id FROM components "
        cond = []
        cond_s = []
        if comp_id_list is not None:
            tmp = ", ".join(['?'] * len(comp_id_list))
            tmp = "comp_id IN (" + tmp + ")"
            cond += comp_id_list
            cond_s.append(tmp)
        if comp_type is not None:
            tmp = "type LIKE '" + comp_type + "'"
            cond_s.append(tmp)
        if comp_name is not None:
            tmp = "name LIKE '" + comp_name + "'"
            cond_s.append(tmp)

        if parent_id_s is not None:
            tmp = "parent_id = ?"
            cond_s.append(tmp)
            cond += [parent_id_s]

        if len(cond_s) > 0:
            stmt += " WHERE "
            stmt += "AND ".join(cond_s)
        stmt += " ORDER BY comp_id"

        if len(cond_s) == 0:
            cursor.execute(stmt)
        else:
            cursor.execute(stmt, cond)
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        else:
            return result
    except Exception:
        raise

'''
@brief Query the component info of the given comp_id

@return A tuple (name, type, identifier, comp_id,
                        comma-separated list of parent_ids)
'''
def query_component(cursor, comp_id):
    try:
        cursor.execute("SELECT name, type, identifier, comp_id, parent_id " \
                       "FROM components WHERE comp_id = ?", (comp_id, ))
        result = cursor.fetchone()
        return result
    except Exception:
        raise

'''
@brief Query the components that have no parents

@return List of tuples (name, type, identifier, comp_id)
'''
def query_root_components(cursor):
    try:
        cursor.execute("SELECT name, type, identifier, comp_id FROM components " \
                       "WHERE parent_id IS NULL;")
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query the children of the given component ID

@return List of tuples (name, type, identifier, comp_id)
'''
def query_children_components(cursor, parent_comp_id):
    try:
        cursor.execute("SELECT child FROM component_relations WHERE parent=?;", \
                       (parent_comp_id, ))
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        children = [child_comp_id[0] for child_comp_id in result]
        s = "(" +  ",".join(['?'] * len(children)) + ")"
        cursor.execute("SELECT name, type, identifier, comp_id FROM components " \
                       "WHERE comp_id IN " + s, children)
        result = cursor.fetchall()
        if len(result) < 1:
            raise Exception("Database Corrupted: comp_ids " + s + " exist in " \
                            "the component_relations table but not in " \
                            "the components table")
        return result
    except Exception:
        raise

'''
@brief Query all sampler template names

The function returns the query result ordered by the insertion order.

@return List of tuples (template name,)
'''
def query_template_names(cursor):
    try:
        cursor.execute("SELECT DISTINCT(name) FROM templates ORDER BY ROWID;")
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@breif Extract the metric name base from a string <metric name>[metric id]

@return The base of the metric name
'''

def get_metric_name_base(s):
    if '#' in s:
        return s.split('#')[0]
    return s.split('[')[0]

'''
@breif Extract the comp_id encoded in a metric_id
'''
def get_comp_id(metric_id):
    return int(metric_id) >> METRIC_TYPE_BITS

'''
@breif Extract the metric_type_id encoded in a metric_id
'''
def get_metric_type_id(metric_id):
    return int(metric_id) & 0xFFFF

'''
@brief Process a string in the format <metric_name>[metric_id]

The metric name might include the extension after the '#'.
For example, the given string is "iowait#cpu1[11111]".
The result is {'base_name': "iowait",
               'extension': "cpu1",
               'full_name': "iowait#cpu1",
               'metric_id': 11111}

@return Dictionary of base_name, extension, full_name, and metric_id

'''
def process_metric_name_id_string(s):
    pattern = '(.+)\[([0-9]+)\]'
    m = re.match(pattern, s)
    if m is None:
        return None
    tmp = m.group(1).split('#')
    if len(tmp) == 1:
        return {'base_name': tmp[0],
                'extension': "",
                'full_name': m.group(1),
                'metric_id': m.group(2)}
    else:
        return {'base_name': tmp[0],
                'extension': tmp[1],
                'full_name': m.group(1),
                'metric_id': m.group(2)}

'''
@brief Extract a comma-separated list of metric name ID strings.

@return List of the base of the metric names
@see get_metric_name_base
'''
def extract_metric_and_component(s):
    names = s.split(',')
    result = []
    for name in names:
        base = get_metric_name_base(name)
        if base not in result:
            result.append(base)
    return result

'''
@brief Query the configuration of ldmsd sampler plug-ins by template_name

If a template name is not given, the information of all templates will be queried.

The function returns the list of tuples.
Each tuple contains
    - template name: the template name defined in the metrics.conf
    - ldmsd sampler plug-in
    - attribute-values: the attributes and their values for configuring the
                        sampler plugin. The format is attr-1:value-1;attr-2:value2;...
    - metrics and their metric IDs: the format is metric-1[metric_id-1],metric-2[metric_id-2],...

@return List of tuples (template name,
                        ldmsd sampler plugin,
                        attribute-values,
                        metrics and their metric IDs)
'''
def query_template_info(cursor, template_name=None):
    try:
        if template_name is None:
            names = query_template_names(cursor)
            if names is None:
                return None
        else:
            names = [(template_name,)]
        result = []
        for name in names:
            cursor.execute("SELECT name,ldmsd_set,cfg,metrics FROM templates " \
                           "WHERE name = ? LIMIT 1", name)
            tmp = cursor.fetchall()
            if len(tmp) < 1:
                continue
            else:
                result += tmp
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query the sampler configuration by host

If a hostname is not given, the configuration of all samplers and all hosts
will be queried.
The function returns the list of tuples order by the insertion order.
Each tuple contains
    - hostname: host that will run the sampler
    - template name: the template name defined in the metrics.conf
    - metrics and their metric IDs: the format is metric-1[metric_id-1],metric-2[metric_id-2],...

@return List of tuples (hostname, template_name, metrics and their metric IDs)
'''
def query_template_apply_on(cursor, hostname = None):
    try:
        if hostname is None:
            cursor.execute("SELECT apply_on, name, metrics FROM templates " \
                           "ORDER BY ROWID")
        else:
            cursor.execute("SELECT apply_on, name, metrics FROM templates " \
                           "WHERE apply_on = ? ORDER BY ROWID;", (hostname,))
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query all metrics or metrics with the given properties

Conditions:
    - metric_id_list
    - metric_name: this could be given in sqlite regular expression
    - mtype_id_list

The function returns the list of tuples ordered by the metric IDs.
Each tuple contains
    - metric name
    - metric_id: a 64-bit integer
    - sampler: ldmsd sampler plug-in sampling the metric
    - metric_type_id: the last 32-bit integer of the metric ID
    - coll_comp: the component ID of the node that runs the sampler plug-in
    - prod_comp_id: the component ID of the component its state is
                    represented by the metric

@return List of tuples (metric name, metric id, sampler, metric_type_id,
                            coll_comp_id, prod_comp_id)
@see query_baler_ptn_metrics
'''
def query_metrics(cursor, metric_id_list = None, metric_name = None, \
                  mtype_id_list = None):
    try:
        stmt = "SELECT name, metric_id, sampler, metric_type_id, " \
                           "coll_comp, prod_comp_id FROM metrics"
        cond = []
        cond_s = []
        mid_s = ""
        mname_s = ""
        mtype_id_s = ""
        if metric_id_list is not None:
            mid_s = ", ".join(['?'] * len(metric_id_list))
            mid_s = "metric_id IN (" + mid_s + ")"
            cond += metric_id_list
            cond_s.append(mid_s)
        if metric_name is not None:
            mname_s = "name LIKE '" + metric_name + "'"
            cond_s.append(mname_s)
        if mtype_id_list is not None:
            mtype_id_s = ", ".join(['?'] * len(mtype_id_list))
            mtype_id_s = "metric_type_id IN (" + mtype_id_s + ")"
            cond += mtype_id_list
            cond_s.append(mtype_id_s)
        if len(mid_s + mname_s + mtype_id_s) > 0:
            stmt += " WHERE "
            stmt += " AND ".join(cond_s)
        stmt += " ORDER BY metric_id"

        if len(cond_s) == 0:
            cursor.execute(stmt)
        else:
            cursor.execute(stmt, cond)
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query Baler pattern metrics

The function returns the list of tuples ordered by the metric IDs.
Each tuple contains
    - metric name
    - metric_id: a 64-bit integer
    - sampler: The value is 'Baler'.
    - metric_type_id: the last 32-bit integer of the metric ID
    - coll_comp: the component ID of the node that runs the sampler plug-in
    - prod_comp_id: the component ID of the component its state is
                    represented by the metric

@return List of tuples (metric name, metric_id, sampler, metric_type_id,
                        coll_comp, prod_comp_id)
@see query_metrics
'''
def query_baler_ptn_metrics(cursor):
    try:
        cursor.execute("SELECT name, metric_id, sampler, metric_type_id, " \
                       "coll_comp, prod_comp_id FROM metrics WHERE name LIKE " \
                       "'%baler_ptn%' ORDER BY  metric_id")
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query the configuration of the given service or all services.

The function returns the list of tuples ordered by the command IDs.
Each tuple contains
    - service: one of 'ldmsd_aggregator', 'balerd', 'me', 'komondor'
    - hostname: the hostname of the node the service will run on
    - verb: verb command
    - attr_value: A colon ':' is used to separated between an attribute and its value.
                  A semi-colon ';' is used to separated between attribute value pairs.

@return List of tuples (service, hostname, verb, attr_value)
'''
def query_service_conf(cursor, service = None):
    try:
        if service is None:
            cursor.execute("SELECT service, hostname, verb, attr_value FROM " \
                           "services ORDER BY cmd_id;")
        else:
            cursor.execute("SELECT service, hostname, verb, attr_value FROM " \
                           "services WHERE service = ? ORDER BY cmd_id;", (service,))
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query the model configuration

The results are ordered by the model IDs.
Each tuple contains
    - model_id
    - model name: model plug-in name
    - parameters: None if no parameters
    - thresholds
    - report_flags: "" if no report_flags

@return List of tuples (model_id, model_name, parameters, thresholds, report_flags)
'''
def query_models(cursor, model_id_list = None):
    try:
        if model_id_list is None:
            cursor.execute("SELECT model_id, name, params, thresholds, " \
                           "report_flags FROM models ORDER BY model_id;")
        else:
            model_id_s = ", ".join(['?'] * len(model_id_list))
            cursor.execute("SELECT model_id, name, params, thresholds, " \
                           "report_flags FROM models WHERE model_id IN ({0}) " \
                           "ORDER BY model_id".format(model_id_s), model_id_list)
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query the actions, the executable path and action type

Each tuple contains
    - action name
    - executable path
    - action type: either 'not-corrective' or 'corrective'

@return List of tuples (action name, executable path, and action type)
'''
def query_actions(cursor, action_name_list = None):
    try:
        if action_name_list is None:
            cursor.execute("SELECT name, execute, type FROM actions ORDER BY ROWID")
        else:
            name_s = ", ".join(['?'] * len(action_name_list))
            cursor.execute("SELECT name, execute, type FROM actions " \
                           "WHERE name IN ({0}) ORDER BY ROWID".format(name_s), \
                           action_name_list)
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query event rules

The query result is ordered by the event IDs.

@return List of tuples (event_id, event_name, model_id)
'''
def query_event_templates(cursor):
    try:
        cursor.execute("SELECT event_id, event_name, model_id FROM " \
                       "rule_templates ORDER BY event_id")
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except:
        raise

'''
@brief Query event metrics

The query result is ordered by the event IDs.

@return List of tuples (event_id, comp_id, metric_id, metric_name)
'''
def query_event_metrics(cursor, event_id_list = None):
    try:
        if event_id_list is None:
            cursor.execute("SELECT event_id, comp_id, metric_id FROM rule_metrics " \
                           "ORDER BY event_id, metric_id")
        else:
            event_id_s = ", ".join(["?"] * len(event_id_list))
            cursor.execute("SELECT event_id, comp_id, metric_id " \
                           "FROM rule_metrics WHERE event_id in ({0}) " \
                           "ORDER BY event_id, metric_id".format(event_id_s),
                           event_id_list)
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query the message and actions of events

The query result is ordered by the event ID.

@return List of tuples (event_id, severity level, message, action_name)
'''
def query_event_msg_action(cursor, event_id_list = None, level_list = None):
    try:
        event_id_cond = ""
        level_cond = ""
        cond_value = []
        if event_id_list is not None:
            event_id_cond = ", ".join(["?" * len(event_id_list)])
            event_id_cond = "event_id IN (" + event_id_cond + ")"
        if level_list is not None:
            level_cond = ", ".join(["?"] * len(level_list))
            level_cond = "level IN (" + level_cond + ")"

        stmt = "SELECT event_id, level, message, action_name FROM rule_actions"
        if (event_id_cond != "") or (level_cond != ""):
            stmt += " WHERE "
            if event_id_cond != "":
                stmt += event_id_cond
                cond_value += event_id_list
                if level_cond != "":
                    stmt += " AND " + level_cond
                    cond_value += level_list
            else:
                if level_cond != "":
                    stmt += level_cond
                    cond_value += level_list
            stmt += " ORDER BY event_id"
            cursor.execute(stmt, cond_value)
        else:
            stmt += " ORDER BY event_id"
            cursor.execute(stmt)
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query the cable types ordered by type_id

@return list of tuple (type_id, type, description)
'''
def query_cable_types(cursor, cable_type_list = None):
    try:
        if cable_type_list is None:
            cursor.execute("SELECT type_id,type,description FROM cable_types " \
                           "ORDER BY type_id")
        else:
            tmp = ",".join(['?'] * len(cable_type_list))
            cursor.execute("SELECT type_id,type,description FROM cable_types " \
                           "WHERE type IN (" + tmp + ") ORDER BY type_id", \
                           cable_type_list)
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

'''
@brief Query the cables ordered by insertion order

@return list of tuple (type_id, component ID of source, component ID of destination)
'''
def query_cables(cursor, cable_type_id_list = None):
    try:
        if cable_type_id_list is None:
            cursor.execute("SELECT type_id,src,dest " \
                           "FROM cables ORDER BY ROWID")
        else:
            tmp = ",".join(['?'] * len(cable_type_id_list))
            cursor.execute("SELECT type_id,src,dest " \
                           "FROM cables WHERE cables.type_id IN (" + tmp + ") " \
                           "ORDER BY ROWID", cable_type_id_list)
        result = cursor.fetchall()
        if len(result) < 1:
            return None
        return result
    except Exception:
        raise

def get_severity_level(level):
    if level not in AVAI_SEVERITY_LEVELS:
        raise ValueError("Unrecognized severity level: " + level)
    return AVAI_SEVERITY_LEVELS[level]

def get_available_severity_levels():
    return {'level_code': [0, 1, 2, 3],
            'level_name': ['nominal', 'info', 'warning', 'critical']}

'''
@brief Rename if the file exist to <path>.old.<current unix timestamp>
'''
def rename_exist_file(path):
    if os.path.isfile(path):
        os.rename(path, path + ".old." + str(time.time()))
