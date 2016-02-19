#!/bin/bash
source <path_to_examples>/CrayXC40/baler_env.sh
balerd -l <path_to_baler_store_dir>/baler.master.log -s <path_to_baler_store_dir>/store.master -C <path_to_examples>/MasterSlave/baler.master.cfg -m master -p 54000  -v DEBUG -I 1 -O 1




