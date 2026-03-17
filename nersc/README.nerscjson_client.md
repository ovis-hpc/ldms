Test: inject message using the python module nerscjson_client.log
---

```
python3 -c "import nerscjson_client.log as nerscjson; nerscjson.log('test', {'abc': 123})"
```

Journal shows the messages

```
journalctl  -f -u nersc-ldmsd.sampler.service


Dec 02 17:21:21 nid001077 ldmsd[149050]:  CRITICAL: sampler.hello_sampler.hello_sampler: stream_type: JSON, msg: "{"@timestamp": "2025-12-02T09:21:21.394261-08:00", "data": {"abc": 123}, "ndc": {"system": "muller"}, "msgtype": "test", "host": "nid001077"}", msg_len: 142, entity: 0x7f204400ff80
```


Test: start a message that loads pyhton modules
---

Create: srun.python_load.john 

```
#!/bin/bash
#SBATCH -A <your_project_account>
#SBATCH -C cpu
#SBATCH -J my_python_job
#SBATCH -o my_python_job_%j.out
#SBATCH -t 0:05:00
#SBATCH -N 1
#SBATCH -c 1
#SBATCH --ntasks-per-node=1
#SBATCH -q regular

# Load the NERSC Python module
module load python
module avail
module load tensorflow/2.9.0

# Activate a custom conda environment (if you have one)
# conda activate my_custom_env

# Run your Python script
srun python your_script.py
```

Create: your_script.py

```
#!/usr/bin/env python

import os
import sys

if __name__ == '__main__':
    print("hello")
```

Ran the script

```
sbatch --reservation testing_jstile -N 1 -C gpu -A nstaff srun.python_load.john
```

ldms Log shows the messages, or an error appears in the job log

Pass: 
```
journalctl -u nersc-ldmsd.sampler.service
...
Dec 02 17:32:35 nid001077 ldmsd[149050]:  CRITICAL: sampler.hello_sampler.hello_sampler: stream_type: JSON, msg: "{"@timestamp":"2025-12-02T09:32:35.080", "data":{"username": "jstile", "module": {"name": "conda/Miniforge3-24.7.1-0", "modulefile_path": "/opt/nersc/pe/modulefiles/conda/Miniforge3-24.7.1-0.lua"}, "userload": "no" }, "ndc": {"system": "muller"}, "msgtype": "lmod", "host": "nid001077"}", msg_len: 287, entity: 0x7f2044001840
Dec 02 17:32:35 nid001077 ldmsd[149050]:  CRITICAL: sampler.hello_sampler.hello_sampler: stream_type: JSON, msg: "{"@timestamp":"2025-12-02T09:32:35.084", "data":{"username": "jstile", "module": {"name": "python/3.11", "modulefile_path": "/opt/nersc/pe/modulefiles/python/3.11.lua"}, "userload": "yes" }, "ndc": {"system": "muller"}, "msgtype": "lmod", "host": "nid001077"}", msg_len: 260, entity: 0x7f20440017c0
```

Fail:
```
cat my_python_job_286704.out

slurmstepd: error: container_p_join: setns failed for /cray/tmp/job_container/286704/.ns: Invalid argument
slurmstepd: error: container_g_join failed: 286704
slurmstepd: error: exec_wait_signal_child: write(fd:21) to unblock task 0 failed
```
