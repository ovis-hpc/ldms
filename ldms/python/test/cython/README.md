Cython LDMS Test
================

- `test.py`: The main test script. This file contains the detailed explanation
  of the test and examples of LDMS consumer routines in Python.
- `async_server.py`: An example of LDMS provider using Python with asychronous
  programming style (callbacks). This file is executed by `test.py`.
- `sync_server.py`: An example of LDMS provider using Python with synchronous
  programming style (blocking function calls). This file is executed by
  `test.py`.
- `agg.py`: A wrapper that sets SIGHUP for PDEATHSIG (parent death) before
  executing `ldmsd` (replacing the process). The wrapping is required so that
  the `ldmsd` is also terminated when the parent process (`test.py`) has
  terminated. This is executed by `test.py`.
- `ldmsd.cfg`: The configuration file for `ldmsd` aggregator that is run by
  `agg.py`.
- `set_test.py`: A small module used by `async_server.py` and `sync_server.py`
  for creating the set schema and setting the metric data. It is also used by
  `test.py` for `ldms_ls` result parsing and metric value checking.
