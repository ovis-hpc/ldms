#!/bin/bash
source <path_to_examples>/CrayXC40/baler_env.sh

# NOTE: you cannot use 'localhost' must be ipaddr
bhttpd -s <path_to_baler_store_dir>/store.master -l <path_to_baler_store_dir>/bhttpd.master.log -a <ip_addr_of_the_master_host> -p 18888
