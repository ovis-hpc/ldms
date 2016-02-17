#!/bin/bash
source <path_to_examples>/CrayXC40/baler_env.sh
balerd -l <path_to_baler_store_dir>/baler.log -s <path_to_baler_store_dir>/store -C <path_to_examples>/CrayXC40/baler.cfg -m master -p 54000  -v DEBUG




