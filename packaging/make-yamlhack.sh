#!/bin/bash
# script for repackaging yamlhack from yaml-devel on rhel7.
rm -rf rhel7
mkdir -p rhel7/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp ldms-yamlhack.spec rhel7/SOURCES
	rpmbuild --define "_topdir `pwd`/rhel7" -ba ldms-yamlhack.spec

