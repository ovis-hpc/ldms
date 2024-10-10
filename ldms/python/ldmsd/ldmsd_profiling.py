import json
import pandas as pd

from ldmsd.ldmsd_communicator import *

def profiling_as_json(xprt, host, port):
    comm = Communicator(xprt=xprt, host=host, port=port)
    comm.connect()
    o = comm.profiling()
    return json.loads(o[1])

def get_hosts(o):
    return o['xprt'].keys()

def get_streams(o):
    return o['stream'].keys()

def lookup_df(o, host):
    df = pd.DataFrame(o['xprt'][host]['LOOKUP'])
    return df

def update_df(o, host):
    df = pd.DataFrame(o['xprt'][host]['UPDATE'])
    return df

def send_df(o, host):
    df = pd.DataFrame(o['xprt'][host]['SEND'])
    return df

def set_delete_df(o, host):
    df = pd.DataFrame(o['xprt'][host]['SET_DELETE'])
    return df

def stream_publish_df(o, host):
    df = pd.DataFrame(o['xprt'][host]['STREAM_PUBLISH'])
    return df

def stream_by_stream_df(o, stream_name = None, src = None):
    d = o['stream']
    if stream_name is not None:
        d = d[stream_name]
        if src is not None:
            d = d[src]
    df = pd.DataFrame(d)
    return df
