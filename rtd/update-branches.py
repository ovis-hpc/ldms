#!/usr/bin/env python
"""
Updates all other branches to the latest master

Since this repository is structured such that different branches build with
different themes, the result is a lot of branches that can fall behind master.
This script simply updates all branches to master. It is meant to be run by
somebody with push access to "origin".

    ./update-branches.py
"""

import os
import subprocess
import sys


path_to_conf = os.path.join(os.path.abspath(os.path.dirname(__file__)), 'docs')
sys.path.append(path_to_conf)

from conf import branch_to_theme_mapping    # noqa


branches = branch_to_theme_mapping.keys()

for branch in branches:
    if branch == 'master':
        continue

    print(u'*' * 77)
    print(u'Syncing branch {} to master...'.format(branch))
    subprocess.check_output(['git', 'checkout', branch])
    subprocess.check_output(['git', 'merge', '--ff-only', 'master'])
    subprocess.check_output(['git', 'push', 'origin', branch])
    print(u'*' * 77)
    print(u'\n')

print(u'Returning to master branch...')
subprocess.check_output(['git', 'checkout', 'master'])
