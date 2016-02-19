#!/bin/bash
source <path_to_examples>/CrayXC40/baler_env.sh

# NOTE: ipaddr below. cannot be 'localhost'
bhttpd -s <path_to_baler_store_dir>/store.slave -l <path_to_baler_store_dir>/bhttpd.slave.log -a <ip_addr_of_slave_host> -p 18889
