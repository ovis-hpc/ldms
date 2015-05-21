#!/usr/bin/python
import argparse
from ldms import *

def show_meta(set):
    print("  METADATA --------");
    print("    Producer Name : {0}".format(set.producer_name_get()))
    print("    Instance Name : {0}".format(set.instance_name_get()))
    print("      Schema Name : {0}".format(set.schema_name_get()))
    print("             Size : {0}".format(set.meta_sz_get()))
    print("     Metric Count : {0}".format(len(set)))
    print("               GN : {0}".format(set.meta_gn_get()))
    print("  DATA ------------\n");
    print("        Timestamp : {0}".format(set.timestamp_get()))
    print("         Duration : {0}".format(set.transaction_duration_get()))
    print("       Consistent : {0}".format(set.is_consistent()))
    print("             Size : {0}".format(set.data_sz_get()))
    print("               GN : {0}".format(set.data_gn_get()))
    print("  -----------------");

def show_metrics(mset, meta=False):
    rc = ldms_xprt_update(mset, None, None)
    print("{0} : {1}".format(mset.instance_name_get(), mset.schema_name_get())),
    if mset.is_consistent():
        print("[consistent"),
    else:
        print("[inconsistent"),
    print("{0}]".format(mset.timestamp_get()))
    if meta:
        show_meta(mset)
    for i in range(0, len(mset)):
        print("{0:>32} : {1}".format(mset.metric_name_get(i), mset.metric_value_get(i)))

def show_schema(set):
    print("{0} : {1}".format(set.instance_name_get(), set.schema_name_get()))
    for i in range(0, len(set)):
        print("    {0:8} {1}".format(set.metric_type_as_str(i), set.metric_name_get(i)))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Query a host's metric sets.")
    parser.add_argument("--mem", type=int, default=1024*1024, help="Specify the memory used by metric sets.")
    parser.add_argument("--xprt", default="sock", help="Spedcify the transport type [sock,rdma,ugni].")
    parser.add_argument("--host", default='localhost', help="Specify the host name to query.")
    parser.add_argument("--port", default='9862', help="Specify the port number on the host.")
    parser.add_argument("--meta", action="store_true",
                        help="Use this option to show the meta data for each metric set.")
    parser.add_argument("--schema", action="store_true",
                        help="Use this option to display the schema for each set.")
    parser.add_argument("--verbose", action="store_true",
                        help="Use this option to display the contents of each metric set.")
    args = parser.parse_args()

    ldms_init(args.mem)
    x = ldms_xprt_new(args.xprt, None)
    if x == None:
        print("Error creating a transport of type {0}.".format(args.xprt))
        exit

    rc = ldms_xprt_connect_by_name(x, args.host, args.port, None, None)
    if rc:
        print("Error connecting to {0}:{1}.".format(args.host, args.port))
        exit

    dir = LDMS_xprt_dir(x)
    if not dir:
        print("Error getting the directory for {0}:{1}.".format(args.host, args.port))

    for name in dir:
        if args.verbose or args.schema:
            set = LDMS_xprt_lookup(x, name, 0)
            if args.schema:
                show_schema(set)
            else:
                show_metrics(set, meta=args.meta)
            print("")
        else:
            print(name)
