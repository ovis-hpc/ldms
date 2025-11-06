#!/bin/bash

add_zypper_repo() {
    local repo_uri="$1"
    local repo_alias="$2"

    zypper addrepo --no-gpgcheck "${repo_uri}" "${repo_alias}"
    if [ $? -ne 0 ]; then
        echo Failed to add zypper repository "${repo_alias}"
        exit 1
    fi
}

zypper_install() {
    zypper install --no-confirm "$@"

    if [ $? -ne 0 ]; then
        echo Failed to install: "$@"
        exit 1
    fi
}

# install libpfm-devel
add_zypper_repo http://localhost:2526/repos/sle-15sp2-module-basesystem sle-15sp2-module-basesystem
zypper_install libpfm-devel

# install papi-devel
add_zypper_repo http://localhost:2526/repos/sle-15sp2-module-development-tools_updates sle-15sp2-module-development-tools_updates
zypper_install papi-devel

# install nvidia datacenter-gpu-manager
add_zypper_repo http://localhost:2526/repos/nersc-nvidia nersc-nvidia
zypper_install datacenter-gpu-manager

cd /root
./Build-overlay.sh
