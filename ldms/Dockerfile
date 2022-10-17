ARG OPENSUSE_LEAP_VERSION=15.2

FROM amr-registry.caas.intel.com/doit/intel-opensuse-leap:$OPENSUSE_LEAP_VERSION
# The following ARG is required
ARG OPENSUSE_LEAP_VERSION

ARG http_proxy=http://proxy-dmz.intel.com:911
ARG https_proxy=http://proxy-dmz.intel.com:911
ARG no_proxy="localhost,intel.com,127.0.0.0/8,192.168.0.0/16,10.0.0.0/8,172.16.0.0/12,/var/run/docker.sock"
ARG LEVEL_ZERO_RPM=level-zero-1.7.15-i710.el8.x86_64.rpm
ARG LEVEL_ZERO_DEVEL_RPM=level-zero-devel-1.7.15-i706.el8.x86_64.rpm

# Software installation
RUN zypper addrepo https://download.opensuse.org/repositories/openSUSE:Leap:$OPENSUSE_LEAP_VERSION:Update/standard/openSUSE:Leap:$OPENSUSE_LEAP_VERSION:Update.repo
RUN zypper refresh
RUN zypper update -y

RUN zypper install -y rpmdevtools rpmlint rpmbuild wget

RUN zypper install -y gcc10-c++
RUN update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-10 100
RUN update-alternatives --set g++ /usr/bin/g++-10

# Install OneAPI Level 0 headers and libs
RUN wget "https://gfx-assets-build.fm.intel.com/artifactory/gfx-rpms/testing-release/agama/master/rhel-8.5/${LEVEL_ZERO_RPM}" && rpm -ivh "${LEVEL_ZERO_RPM}"
RUN wget "https://gfx-assets-build.fm.intel.com/artifactory/gfx-rpms/testing-release/agama/master/rhel-8.5/${LEVEL_ZERO_DEVEL_RPM}" && rpm -ivh "${LEVEL_ZERO_DEVEL_RPM}"

# Install tools for LDMS build
RUN zypper install -y autoconf automake libtool make bison flex openssl-devel
RUN zypper install -y python3-devel python3-Cython

# GUS dependencies
RUN zypper --non-interactive install numactl gcc-c++ glibc-32bit glibc-devel-32bit libgcc_s1-32bit libncurses5 libncurses5-32bit

RUN zypper clean --all

# docker build -t dai-nre-cpp-bld-opensuse-leap-15.3:4 .
