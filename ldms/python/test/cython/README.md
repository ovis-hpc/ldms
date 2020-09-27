Cython LDMS Test
================

SYNOPSIS
--------
```
    ./test.py
```

DESCRIPTION
-----------

The test program (`test.py`) spawns:
1. `sync_server.py` -- an LDMS provider using LDMS Python module with
   synchronous model (blocking function calls),
2. `async_server.py` -- an LDMS provider using LDMS Python module with
   asynchronous model (function callbacks), and
3. `ldmsd` with `ldmsd.cfg` that aggregates sets from `sync_server.py` and
   `async_server.py`.
Then, `test.py` performs various client-side operations (such as `dir`,
`lookup`, and `update`) with synchronous and asynchronous models and verify
the results.

The following is the list of files concerning LDMS Cython test with short
descriptions:

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
