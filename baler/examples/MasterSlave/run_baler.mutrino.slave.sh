#!/bin/bash
source <path_to_examples>/CrayXC40/baler_env.sh
balerd -l <path_to_baler_store_dir>/baler.slave.log -s <path_to_baler_store_dir>/store.slave -C <path_to_examples/MasterSlave>/baler.slave.cfg -m slave -p 54000  -v DEBUG -I 1 -O 1




