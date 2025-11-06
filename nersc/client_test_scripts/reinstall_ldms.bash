#!/bin/bash
set -x
rpm -e nerscjson_client
rpm -e ovis-ldms
rpm -i ovis-ldms-UNKNOWN.x86_64.rpm 
zypper install -y nerscjson_client
