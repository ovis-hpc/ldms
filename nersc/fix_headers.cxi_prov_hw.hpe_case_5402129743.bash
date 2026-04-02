#!/bin/bash
#echo "[>>] Hack cxi_prov_hw.h for HPE Case: 5402129743"
sed -i 's/#include "cassini_user_defs.h"/#include "cassini_user_defs.h"\n#include <string.h>/' /usr/include/cxi_prov_hw.h
