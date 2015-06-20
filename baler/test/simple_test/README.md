SYNOPSIS
========

This directory contains simple test cases for balerd and bquery. It basically
uses **gen-log.pl** to generate system log messages and pass those to
**balerd**. Testers should modify **env.sh** to have correct baler environment,
and should modify **config.sh** to suite testers' need before calling the test
script **run-test.sh**.

Files that you care
===================

- **env.sh** - This file contains environment variables needed to run balerd and
  bquery. It is *sourced* by **config.sh**.
- **config.sh** - This file contains environment variables about *testing*
  configuration, e.g. starting timestamp, and number of nodes.
- **run-test.sh** - This is the test script file. Invoke this script after
  appropriately modify **env.sh** and **config.sh**. Please also note that this
  script will call **clean.sh** to clean up existing store and log files before
  running the actual test cases.

Other files
===========
- **ptn.txt** - The expected pattern results. This is used by **check-ptn.pl**.
- **check-ptn.pl** - Pattern checking script. This checks whether or not bquery
  reports expected patterns.
- **check-log.pl** - Log message checking script. This checks whether the output
  messages from bquery (iterating beginning-to-end) match the input messages
  from **gen-log.pl**.
- **check-img.pl** - Image pixel checking script. This script checks the image
  pixel results.
- **clean.sh** - The script to clean log files and store. This script is invoked
  by **run-test.sh** before run-test.sh performs the test cases.
- **common.sh** - Common shell functions and variables.
- **README.md** - This file.
- **syslog2baler.pl** - A soft link to the script that send log messages to
  balerd.
