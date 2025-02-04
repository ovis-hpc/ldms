LDMS Containers
===============

``ovis-hpc/ldms-containers`` git repository contains recipes and scripts
for building Docker Images of various components in LDMS, namely:

-  ``ovishpc/ldms-dev``: an image containing dependencies for building
   OVIS binaries and developing LDMS plugins.
-  ``ovishpc/ldms-samp``: an image containing ``ldmsd`` binary and
   sampler plugins.
-  ``ovishpc/ldms-agg``: an image containing ``ldmsd`` binary, sampler
   plugins, and storage plugins (including SOS).
-  ``ovishpc/ldms-maestro``: an image containing ``maestro`` and
   ``etcd``.
-  ``ovishpc/ldms-ui``: an image containing UI back-end elements,
   providing LDMS data access over HTTP (``uwsgi`` + ``django`` +
   `ovis-hpc/numsos <https://github.com/nick-enoent/numsos>`__ +
   `ovis-hpc/sosdb-ui <https://github.com/nick-enoent/sosdb-ui>`__ +
   `ovis-hpc/sosdb-grafana <https://github.com/nick-enoent/sosdb-grafana>`__)
-  ``ovishpc/ldms-grafana``: an image containing ``grafana`` and the SOS
   data source plugin for grafana
   (`sosds <https://github.com/nick-enoent/dsosds>`__)

Table of Contents:

-  `Brief Overview About Docker
   Containers <#brief-overview-about-docker-containers>`__
-  `Sites WITHOUT internet access <#sites-without-internet-access>`__
-  `SYNOPSIS <#SYNOPSIS>`__
-  `EXAMPLES <#EXAMPLES>`__
-  `LDMS Sampler Container <#ldms-sampler-container>`__
-  `LDMS Aggregator Container <#ldms-aggregator-container>`__
-  `Maestro Container <#maestro-container>`__
-  `LDMS UI Back-End Container <#ldms-ui-back-end-container>`__
-  `LDMS-Grafana Container <#ldms-grafana-container>`__
-  `SSH port forwarding to grafana <#ssh-port-forwarding-to-grafana>`__
-  `Building Containers <#building-containers>`__

Brief Overview About Docker Containers
--------------------------------------

A docker container is a runnable instance of an image. In Linux, it is
implemented using namespaces
(`namespaces(7) <https://man7.org/linux/man-pages/man7/namespaces.7.html>`__).
``docker create`` command creates a container that can later be started
with ``docker start``, while ``docker run`` creates and starts the
container in one go. When a container starts, the first process being
run, or a root process, is the program specified by the ``--entrypoint``
CLI option or ``ENTRYPOINT`` Dockerfile directive. When the root process
exits or is killed, the container status becomes ``exited``.
``docker stop`` command sends ``SIGTERM`` to the root process, and
``docker kill`` command send ``SIGKILL`` to the root process. The other
processes in the container are also terminated or killed when the root
process is terminated or killed. ``docker ps`` shows "running"
containers, while ``docker ps -a`` shows ALL containers (including the
exited one).

When a container is created (before started), its mount namespace
(`mount_namespaces(7) <https://man7.org/linux/man-pages/man7/mount_namespaces.7.html>`__)
is prepared by the Docker engine. This isolates container's filesystems
from the host. The Docker Image is the basis of the filesystem mounted
in the container. The image itself is read-only, and the modification to
the files/directories inside the container at runtime is done on the
writable layer on top of the image. They are "unified" and presented to
the container as a single filesystem by OverlayFS (most preferred by
Docker, but other drivers like ``btrfs`` could also be used). A Docker
Image is actually a collection of "layers" of root directories (``/``).
When a container is ``stopped`` (the root process exited/killed), the
writable top layer still persists until ``docker rm`` command removes
the container.

The network namespace
(`network_namespace <https://man7.org/linux/man-pages/man7/network_namespaces.7.html>`__)
and the process namespace (`process
namespace <https://man7.org/linux/man-pages/man7/process_namespaces.7.html>`__)
of a container are normally isolated, but could also use host's
namespaces. The LDMS sampler containers (``ovishpc/ldms-samp``) require
host process namespace (``--pid=host`` option) so that the ``ldmsd``
reads host's ``/proc`` data. Otherwise, we will be collecting
container's metric data. Other LDMS containers do not need host process
namespace. For the network namespace, it is advisable to use host's
network namespace (``--network=host``) to fully utilize RDMA hardware on
the host with minimal effort in network configuration.

Sites WITHOUT internet access
-----------------------------

#. On your laptop (or a machine that HAS the Internet access)

.. code:: sh

   $ docker pull ovishpc/ldms-dev
   $ docker pull ovishpc/ldms-samp
   $ docker pull ovishpc/ldms-agg
   $ docker pull ovishpc/ldms-maestro
   $ docker pull ovishpc/ldms-ui
   $ docker pull ovishpc/ldms-grafana

   $ docker save ovishpc/ldms-dev > ovishpc-ldms-dev.tar
   $ docker save ovishpc/ldms-samp > ovishpc-ldms-samp.tar
   $ docker save ovishpc/ldms-agg > ovishpc-ldms-agg.tar
   $ docker save ovishpc/ldms-maestro > ovishpc-ldms-maestro.tar
   $ docker save ovishpc/ldms-ui > ovishpc-ldms-ui.tar
   $ docker save ovishpc/ldms-grafana > ovishpc-ldms-grafana.tar

   # Then, copy these tar files to the site

#. On the site that has NO Internet access

.. code:: sh

   $ docker load < ovishpc-ldms-dev.tar
   $ docker load < ovishpc-ldms-samp.tar
   $ docker load < ovishpc-ldms-agg.tar
   $ docker load < ovishpc-ldms-maestro.tar
   $ docker load < ovishpc-ldms-ui.tar
   $ docker load < ovishpc-ldms-grafana.tar

Then, the images are available locally (no need to ``docker pull``).

SYNOPSIS
--------

In this section, the options in ``[ ]`` are optional. Please see the
``#`` comments right after the options for the descriptions. Please also
note that the options BEFORE the Docker Image name are for
``docker run``, and the options AFTER the image name are for the
entrypoint script. The following is the information regarding entrypoint
options for each image:

-  ``ovishpc/ldms-dev`` entrypoint options are pass-through to
   ``/bin/bash``.
-  ``ovishpc/ldms-samp`` entrypoint options are pass-through to ldmsd.
-  ``ovishpc/ldms-agg`` entrypoint options are pass-through to ldmsd.
-  ``ovishpc/ldms-maestro`` entrypoint options are ignored.
-  ``ovishpc/ldms-ui`` entrypoint options are pass-through to uwsgi.
-  ``ovishpc/ldms-grafana`` entrypoint options are pass-through to
   grafana-server program.

.. code:: sh

   # Pulling images
   $ docker pull ovishpc/ldms-dev
   $ docker pull ovishpc/ldms-samp
   $ docker pull ovishpc/ldms-agg
   $ docker pull ovishpc/ldms-maestro
   $ docker pull ovishpc/ldms-ui
   $ docker pull ovishpc/ldms-grafana

   # munge remark: munge.key file must be owned by 101:101 (which is munge:munge in
   #               the container) and has 0600 mode.

   # ovishpc/ldms-maestro
   $ docker run -d --name=<CONTAINER_NAME> --network=host --privileged
            [ -v /run/munge:/run/munge:ro ] # expose host's munge to the container
            [ -v /on-host/munge.key:/etc/munge/munge.key:ro ] # use container's munged with custom key
            -v /on-host/ldms_cfg.yaml:/etc/ldms_cfg.yaml:ro # bind ldms_cfg.yaml, used by maestro_ctrl
            ovishpc/ldms-maestro # the image name


   # ovishpc/ldms-samp
   $ docker run -d --name=<CONTAINER_NAME> --network=host --pid=host --privileged
            -e COMPID=<NUMBER> # set COMPID environment variable
            [ -v /run/munge:/run/munge:ro ] # expose host's munge to the container
            [ -v /on-host/munge.key:/etc/munge/munge.key:ro ] # use container's munged with custom key
            ovishpc/ldms-samp # the image name
                 -x <XPRT>:<PORT>  # transport, listening port
                 [ -a munge ] # use munge authentication
                 [ OTHER LDMSD OPTIONS ]


   # ovishpc/ldms-agg
   $ docker run -d --name=<CONTAINER_NAME> --network=host --pid=host --privileged
            -e COMPID=<NUMBER> # set COMPID environment variable
            [ -v /on-host/storage:/storage:rw ] # bind 'storage/'. Could be any path, depending on ldmsd configuration
            [ -v /on-host/dsosd.json:/etc/dsosd.json:ro ] # bind dsosd.json configuration, if using dsosd to export SOS data
            [ -v /run/munge:/run/munge:ro ] # expose host's munge to the container
            [ -v /on-host/munge.key:/etc/munge/munge.key:ro ] # use container's munged with custom key
            ovishpc/ldms-agg # the image name
                 -x <XPRT>:<PORT>  # transport, listening port
                 [ -a munge ] # use munge authentication
                 [ OTHER LDMSD OPTIONS ]
   # Run dsosd to export SOS data
   $ docker exec -it <CONTAINER_NAME> /bin/bash
   (<CONTAINER_NAME>) $ rpcbind
   (<CONTAINER_NAME>) $ export DSOSD_DIRECTORY=/etc/dsosd.json
   (<CONTAINER_NAME>) $ dsosd >/var/log/dsosd.log 2>&1 &
   (<CONTAINER_NAME>) $ exit


   # ovishpc/ldms-ui
   $ docker run -d --name=<CONTAINER_NAME> --network=host --privileged
            -v /on-host/dsosd.conf:/opt/ovis/etc/dsosd.conf # dsosd.conf file, required to connect to dsosd
            -v /on-host/settings.py:/opt/ovis/ui/sosgui/settings.py # sosdb-ui Django setting file
            ovishpc/ldms-ui # the image name
                [ --http-socket=<ADDR>:<PORT> ] # addr:port to serve, ":80" by default
                [ OTHER uWSGI OPTIONS ]


   # ovishpc/ldms-grafana
   $ docker run -d --name=<CONTAINER_NAME> --network=host --privileged
            [ -v /on-host/grafana.ini:/etc/grafana/grafana.ini:ro ] # custom grafana config
            [ -e GF_SERVER_HTTP_ADDR=<ADDR> ] # env var to override Grafana IP address binding (default: all addresses)
            [ -e GF_SERVER_HTTP_PORT=<PORT> ] # env var to override Grafana port binding (default: 3000)
            ovishpc/ldms-grafana # the image name
                 [ OTHER GRAFANA-SERVER OPTIONS ] # other options to grafana-server


   # -------------------------------------
   #      configuration files summary
   # -------------------------------------
   # - /on-host/dsosd.json: contains dictionary mapping hostname - container
   #   location in the host, e.g.
   #   {
   #     "host1": {
   #       "dsos_cont":"/storage/cont_host1"
   #     },
   #     "host2": {
   #       "dsos_cont":"/storage/cont_host2"
   #     }
   #   }
   #
   # - /on-host/dsosd.conf: contains host names (one per line) of the dsosd, e.g.
   #   host1
   #   host2
   #
   # - /on-host/settings.py: Django settings. Pay attention to DSOS_ROOT and
   #   DSOS_CONF variables.

EXAMPLES
--------

In this example, we have 8-nodes cluster with host names cygnus-01 to
cygnus-08. ``cygnus-0[1-4]`` are used as compute nodes (deploying
``ovishpc/ldms-samp`` containers). ``cygnus-0[5-6]`` are used as L1
aggregator (``ovishpc/ldms-agg`` containers without storage).
``cygnus-07`` is used as L2 aggregator with a DSOS storage
(``ovishpc/ldms-agg`` with dsosd). ``cygnus-07`` will also host
``ovishpc/maestro``, ``ovishpc/ldms-ui`` and ``ovishpc/ldms-grafana``
containers. We will be running commands from ``cygnus-07``. The cluster
has ``munged`` pre-configured and running on all nodes with the same
key.

Configuration files used in this example are listed at the end of the
section. The following is a list of commands that deploys various
containers on the cygnus cluster:

.. code:: sh

   # Start sampler containers on cygnus-01,02,03,04
   root@cygnus-07 $ pdsh -w cygnus-0[1-4] 'docker run -d --name=samp --network=host --pid=host --privileged -v /run/munge:/run/munge:ro -e COMPONENT_ID=${HOSTNAME#cygnus-0} ovishpc/ldms-samp -x rdma:411 -a munge'
   # Notice the COMPONENT_ID environment variable setup using Bash substitution.
   #  The COMPONENT_ID environment variable is later used in LDMSD sampler plugin
   #  configuration `component_id: ${COMPONENT_ID}` in the `ldms_cfg.yaml` file.

   # Start L1 aggregator containers on cygnus-05,06
   root@cygnus-07 $ pdsh -w cygnus-0[5-6] docker run -d --name=agg1 --network=host --pid=host  --privileged -v /run/munge:/run/munge:ro ovishpc/ldms-agg -x rdma:411 -a munge

   # Start L2 aggregator container on cygnus-07
   root@cygnus-07 $ docker run -d --name=agg2 --network=host --pid=host --privileged -v /run/munge:/run/munge:ro -v /store:/store:rw ovishpc/ldms-agg -x rdma:411 -a munge

   # Start dsosd in the `agg2`, our L2 aggregator container
   root@cygnus-07 $ echo 'rpcbind ; dsosd > /var/log/dsosd.log 2>&1 &' | docker exec -i agg2 /bin/bash

   # Start maestro container on cygnus-07
   root@cygnus-07 $ docker run -d --name=maestro --network=host --privileged -v /run/munge:/run/munge:ro -v ${PWD}/ldms_cfg.yaml:/etc/ldms_cfg.yaml:ro ovishpc/ldms-maestro

   # Start Django UI container
   root@cygnus-07 $ docker run -d --name=ui --network=host --privileged -v ${PWD}/dsosd.conf:/opt/ovis/etc/dsosd.conf -v ${PWD}/settings.py:/opt/ovis/ui/sosgui/settings.py ovishpc/ldms-ui

   # Start Grafana container
   root@cygnus-07 $ docker run -d --name=grafana --privileged --network=host ovishpc/ldms-grafana

Related configuration files

.. code:: sh

   # dsosd.conf
   cygnus-07

.. code:: yaml

   # ldms_cfg.yaml
   xprt: &xprt "rdma"
   daemons:
     - names : &samp-names "samp-[1-4]"
       hosts : &samp-hosts "cygnus-0[1-4]-iw"
       endpoints :
         - names : &samp-eps "cygnus-0[1-4]-iw-ep"
           ports : 411
           xprt : *xprt
           maestro_comm : True
           auth :
             name : munge
             plugin : munge
     - names : &L1-names "agg-[11-12]"
       hosts : &L1-hosts "cygnus-0[5-6]-iw"
       endpoints :
         - names : &L1-eps "agg-[11-12]-ep"
           ports : 411
           xprt : *xprt
           maestro_comm : True
           auth :
             name : munge
             plugin : munge
     - names : &L2-name "agg-2"
       hosts : &L2-host "cygnus-07-iw"
       endpoints :
         - names : &L2-ep "agg-2-ep"
           ports : 411
           xprt : *xprt
           maestro_comm : True
           auth :
             name : munge
             plugin : munge

   aggregators:
     - daemons   : *L1-names
       peers     :
         - daemons   : *samp-names
           endpoints : *samp-eps
           reconnect : 1s
           type      : active
           updaters  :
             - mode     : pull
               interval : "1.0s"
               offset   : "200ms"
               sets     :
                 - regex : .*
                   field : inst
     - daemons : *L2-name
       peers:
         - daemons : *L1-names
           endpoints : *L1-eps
           reconnect : 1s
           type      : active
           updaters  :
             - mode     : pull
               interval : "1.0s"
               offset   : "400ms"
               sets     :
                 - regex : .*
                   field : inst

   samplers:
     - daemons : *samp-names
       plugins :
         - name        : meminfo # Variables can be specific to plugin
           interval    : "1s" # Used when starting the sampler plugin
           offset      : "0s"
           config : &simple_samp_config
               component_id : "${COMPONENT_ID}"
               perm : "0777"

   stores:
     - name      : sos-meminfo
       daemons   : *L2-name
       container : meminfo
       schema    : meminfo
       flush     : 10s
       plugin :
         name   : store_sos
         config :
           path : /store

.. code:: py

   # settings.py
   """
   Django settings for sosgui project.

   Generated by 'django-admin startproject' using Django 1.8.2.

   For more information on this file, see
   https://docs.djangoproject.com/en/1.8/topics/settings/

   For the full list of settings and their values, see
   https://docs.djangoproject.com/en/1.8/ref/settings/
   """

   # Build paths inside the project like this: os.path.join(BASE_DIR, ...)
   import os
   import json

   log = open('/var/log/sosgui/settings.log', 'a')
   BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

   # Quick-start development settings - unsuitable for production
   # See https://docs.djangoproject.com/en/1.8/howto/deployment/checklist/

   # SECURITY WARNING: keep the secret key used in production secret!
   SECRET_KEY = 'blablablablablablablablablablablablablablablablablabla'

   # SECURITY WARNING: don't run with debug turned on in production!
   DEBUG = True

   ALLOWED_HOSTS = [
       '*',
   ]

   APPEND_SLASH = False

   STATIC_ROOT = os.path.join(BASE_DIR, "assets")

   AUTH_USER_MODEL = 'sosdb_auth.SosdbUser'

   # Application definition

   INSTALLED_APPS = (
       'corsheaders',
       'django.contrib.admin',
       'django.contrib.auth',
       'django.contrib.contenttypes',
       'django.contrib.sessions',
       'django.contrib.messages',
       'django.contrib.staticfiles',
       'container',
       'jobs',
       'objbrowser',
       'sos_db',
       'sosdb_auth',
   )

   try:
       from . import ldms_settings
       INSTALLED_APPS = INSTALLED_APPS + ldms_settings.INSTALLED_APPS
   except:
       pass

   try:
       from . import grafana_settings
       INSTALLED_APPS = INSTALLED_APPS + grafana_settings.INSTALLED_APPS
   except:
       pass

   try:
       from . import baler_settings
       INSTALLED_APPS = INSTALLED_APPS + baler_settings.INSTALLED_APPS
   except:
       pass

   MIDDLEWARE = (
       'corsheaders.middleware.CorsMiddleware',
       'django.contrib.sessions.middleware.SessionMiddleware',
       'django.middleware.common.CommonMiddleware',
       'django.contrib.auth.middleware.AuthenticationMiddleware',
       'django.contrib.messages.middleware.MessageMiddleware',
       'django.middleware.clickjacking.XFrameOptionsMiddleware',
       'django.middleware.security.SecurityMiddleware',
   )

   ROOT_URLCONF = 'sosgui.urls'

   TEMPLATES = [
       {
           'BACKEND': 'django.template.backends.django.DjangoTemplates',
           'DIRS': [
               '/opt/ovis/ui/templates',
           ],
           'APP_DIRS': True,
           'OPTIONS': {
               'context_processors': [
                   'django.contrib.auth.context_processors.auth',
                   'django.template.context_processors.debug',
                   'django.template.context_processors.request',
                   'django.contrib.messages.context_processors.messages',
               ],
           },
       },
   ]

   WSGI_APPLICATION = 'sosgui.wsgi.application'


   # Database
   # https://docs.djangoproject.com/en/1.8/ref/settings/#databases

   DATABASES = {
       'default': {
           'ENGINE': 'django.db.backends.sqlite3',
           'NAME': os.path.join(BASE_DIR, 'db.sqlite3'),
       }
   }

   LANGUAGE_CODE = 'en-us'

   TIME_ZONE = 'UTC'

   USE_I18N = True

   USE_L10N = True

   USE_TZ = True


   # Static files (CSS, JavaScript, Images)
   # https://docs.djangoproject.com/en/1.8/howto/static-files/

   STATIC_URL = '/static/'

   STATICFILES_DIRS = [
       '/opt/ovis/ui/static/',
   ]

   SESSION_EXPIRE_AT_BROWSER_CLOSE = True
   SOS_ROOT = "/store/"
   DSOS_ROOT = "/store/"
   DSOS_CONF = "/opt/ovis/etc/dsosd.conf"
   LOG_FILE = "/var/log/sosgui/sosgui.log"
   LOG_DATE_FMT = "%F %T"
   ODS_LOG_FILE = "/var/log/sosgui/ods.log"
   ODS_LOG_MASK = "255"
   ODS_GC_TIMEOUT = 10
   BSTORE_PLUGIN="bstore_sos"
   os.environ.setdefault("BSTORE_PLUGIN_PATH", "/opt/ovis/lib64")
   os.environ.setdefault("SET_POS_KEEP_TIME", "3600")


   try:
       import ldms_cfg
       LDMS_CFG = ldms_cfg.aggregators
   except Exception as e:
       log.write(repr(e)+'\n')
       LDMS_CFG = { "aggregators" : [] }

   try:
       import syslog
       SYSLOG_CFG = syslog.syslog
   except Exception as e:
       log.write('SYSLOG_SETTINGS ERR '+repr(e)+'\n')
       SYSLOG_CFG = { "stores" : [] }

LDMS Sampler Container
----------------------

.. code:: sh

   # SYNOPSIS
   $ docker run -d --name=<CONTAINER_NAME> --network=host --pid=host --privileged
            -e COMPID=<NUMBER> # set COMPID environment variable
            [ -v /run/munge:/run/munge:ro ] # expose host's munge to the container
            [ -v /on-host/munge.key:/etc/munge/munge.key:ro ] # use container's munged with custom key
            ovishpc/ldms-samp # the image name
                 -x <XPRT>:<PORT>  # transport, listening port
                 [ -a munge ] # use munge authentication
                 [ OTHER LDMSD OPTIONS ] # e.g. -v INFO

``ovishpc/ldms-samp`` entrypoint executes ``ldmsd -F``, making it the
leader process of the container. Users can append ``[OPTIONS]`` and they
will be passed to ``ldmsd -F`` CLI. If ``-a munge`` is given, the
entrypoint script will check if ``/run/munge`` is a bind-mount from the
host. If so, munge encoding/decoding is done through ``munged`` on the
host via the bind-mounged ``/run/munge`` -- no need to run ``munged``
inside the container. Otherwise, in the case that ``-a munge`` is given
and ``/run/munge`` is not host-bind-mounted, the entrypoint script runs
``munged`` and tests it BEFORE ``ldmsd``.

Usage examples:

.. code:: sh

   ## On a compute node

   # Pull the container image
   $ docker pull ovishpc/ldms-samp

   # Start ldmsd container, with host network namespace and host PID namespace;
   # - COMPID env var is HOSTNAME without the non-numeric prefixes and the leading
   #   zeroes (e.g. nid00100 => 100, nid10000 => 10000). Note that this uses
   #   bash(1) Parameter Expansion and Pattern Matching features.
   #
   # - serving on socket transport port 411 with munge authentication
   #
   # - using host munge
   $ docker run -d --name=samp --network=host --pid=host --privileged \
            -e COMPID=${HOSTNAME##*([^1-9])} \
            -v /run/munge:/run/munge:ro \
            ovishpc/ldms-samp -x sock:411 -a munge

We encourage to use ``maestro`` to configure a cluster of ``ldmsd``.
However, if there is a need to configure ``ldmsd`` manually, one can do
from within the container. In this case:

.. code:: sh

   $ docker exec samp /bin/bash
   (samp) $ ldmsd_controller --xprt sock --port 411 --host localhost --auth munge
   LDMSD_CONTROLLER_PROMPT>

LDMS Aggregator Container
-------------------------

.. code:: sh

   # SYNOPSIS
   $ docker run -d --name=<CONTAINER_NAME> --network=host --pid=host --privileged
            -e COMPID=<NUMBER> # set COMPID environment variable
            [ -v /on-host/storage:/storage:rw ] # bind 'storage/'. Could be any path, depending on ldmsd configuration
            [ -v /on-host/dsosd.json:/etc/dsosd.json:ro ] # bind dsosd.json configuration, if using dsosd to export SOS data
            [ -v /run/munge:/run/munge:ro ] # expose host's munge to the container
            [ -v /on-host/munge.key:/etc/munge/munge.key:ro ] # use container's munged with custom key
            ovishpc/ldms-samp # the image name
                 -x <XPRT>:<PORT>  # transport, listening port
                 [ -a munge ] # use munge authentication
                 [ OTHER LDMSD OPTIONS ]
   # dsosd to export SOS data
   $ docker exec -it <CONTAINER_NAME> /bin/bash
   (<CONTAINER_NAME>) $ rpcbind
   (<CONTAINER_NAME>) $ export DSOSD_DIRECTORY=/etc/dsosd.json
   (<CONTAINER_NAME>) $ dsosd >/var/log/dsosd.log 2>&1 &
   (<CONTAINER_NAME>) $ exit

``ovishpc/ldms-agg`` entrypoint executes ``ldmsd -F``, making it the
leader process of the container. It also handles ``-a munge`` the same
way that ``ovishpc/ldms-samp`` does. In the case of exporting SOS data
through ``dsosd``, the daemon is required to execute after the container
is up.

Example usage:

.. code:: sh

   ## On a service node

   # Pull the container image
   $ docker pull ovishpc/ldms-agg

   # Start ldmsd container, using host network namespace and host PID namespace;
   # - with host munge
   # - serving port 411
   # - The `-v  /on-host/storage:/storage:rw` option is to map on-host storage
   #   location `/on-host/storage` to `/storage` location in the container. The
   #   data written to `/storage/` in the container will persist in
   #   `/on-host/storage/` on the host.
   $ docker run -d --name=agg --network=host --privileged \
            -v /run/munge:/run/munge:ro \
	 -v /on-host/storage:/storage:rw \
            ovishpc/ldms-agg -x sock:411 -a munge

   # Start dsosd service for remote SOS container access (e.g. by UI), by first
   # bring up a shell inside the container, then start rpcbind and dsosd.
   $ docker exec agg /bin/bash
   (agg) $ rpcbind
   (agg) $ export DSOSD_DIRECTORY=/etc/dsosd.json
   (agg) $ dsosd >/var/log/dsosd.log 2>&1 &
   (agg) $ exit

``dsosd.json`` contains a collection of ``container_name`` - ``path``
mappings for each host. For example:

.. code:: json

   {
     "host1": {
       "dsos_cont":"/storage/cont_host1",
       "tmp_cont":"/tmp/ram_cont"
     },
     "host2": {
       "dsos_cont":"/storage/cont_host2",
       "tmp_cont":"/tmp/ram_cont"
     }
   }

Maestro Container
-----------------

.. code:: sh

   # SYNOPSIS
   $ docker run -d --name=<CONTAINER_NAME> --network=host --privileged
            [ -v /run/munge:/run/munge:ro ] # expose host's munge to the container
            [ -v /on-host/munge.key:/etc/munge/munge.key:ro ] # use container's munged with custom key
            -v /on-host/ldms_cfg.yaml:/etc/ldms_cfg.yaml:ro # bind ldms_cfg.yaml, used by maestro_ctrl
            ovishpc/ldms-maestro # the image name

``ovishpc/ldms-maestro`` containers will run at the least two daemons:
``etcd`` and ``maestro``. It may also run ``munged`` if host's munge is
not used (i.e. ``-v /run/munge:/run/munge:ro`` is not given to
``docker run``). The entrypoint script does the following:

#. starts ``etcd``
#. starts ``munged`` if host's munge is not used.
#. execute ``maestro_ctrl`` with ``--ldms_config /etc/ldms_cfg.yaml``.
   Notice that the ``ldms_cfg.yaml`` file is given by the user by the
   ``-v`` option.
#. execute ``maestro`` process. ``maestro`` will periodically connect to
   all ``ldmsd`` specified by ``ldms_cfg.yaml`` and send the
   corresponding configuration.

REMARK: For now, the ``etcd`` and ``maestro`` processes in the
``ovishpc/ldms-maestro`` container run as stand-alone processes. We will
support a cluster of ``ovishpc/ldms-maestro`` containers in the future.

Example usage:

.. code:: sh

   ## On a service node

   # Pull the container image
   $ docker pull ovishpc/ldms-maestro

   # Start maestro container, using host network namespace, and using host's munge
   $ docker run -d --network=host --privileged \
            -v /run/munge:/run/munge:ro \
	 -v /my/ldms_cfg.yaml:/etc/ldms_cfg.yaml:rw \
            ovishpc/ldms-maestro

Please see `ldms_cfg.yaml <test/test-maestro/files/ldms_cfg.yaml>`__ for
an example.

LDMS UI Back-End Container
--------------------------

.. code:: sh

   # SYNOPSIS
   $ docker run -d --name=<CONTAINER_NAME> --network=host --privileged
            -v /on-host/dsosd.conf:/opt/ovis/etc/dsosd.conf # dsosd.conf file, required to connect to dsosd
            -v /on-host/settings.py:/opt/ovis/ui/sosgui/settings.py # sosdb-ui Django setting file
            ovishpc/ldms-ui # the image name
                [ --http-socket=<ADDR>:<PORT> ] # addr:port to serve, ":80" by default
                [ OTHER uWSGI OPTIONS ]

``ovishpc/ldms-ui`` execute ``uwsgi`` process with ``sosgui`` (the
back-end GUI WSGI module) application module. It is the only process in
the container. The ``uwsgi`` in this container by default will listen to
port 80. The ``--http-socket=ADDR:PORT`` will override this behavior.
Other options given to ``docker run`` will also be passed to the
``uwsgi`` command as well.

The ``sosgui`` WSGI application requires two configuration files:

#. ``dsosd.conf``: containing a list of hostnames of dsosd, one per
   line. See `here <https://github.com/ovis-hpc/ldms-containers/blob/master/test/test-maestro/files/dsosd.conf>`__ for an
   example.
#. ``settings.py``: containing a WSGI application settings. Please pay
   attention to DSOS_ROOT and DSOS_CONF. See
   `here <https://github.com/ovis-hpc/ldms-containers/blob/master/test/test-maestro/files/settings.py>`__ for an example.

Usage example:

.. code:: sh

   ## On a service node

   # Pull the container image
   $ docker pull ovishpc/ldms-ui

   # Start ldms-ui container, using host network namespace
   $ docker run -d --name=ui --network=host --privileged \
	   -v /HOST/dsosd.conf:/opt/ovis/etc/dsosd.conf \
	   -v /HOST/settings.py:/opt/ovis/ui/sosgui/settings.py \
            ovishpc/ldms-ui

LDMS-Grafana Container
----------------------

.. code:: sh

   # SYNOPSIS
   $ docker run -d --name=<CONTAINER_NAME> --network=host --privileged
            [ -v /on-host/grafana.ini:/etc/grafana/grafana.ini:ro ] # custom grafana config
            [ -e GF_SERVER_HTTP_ADDR=<ADDR> ] # env var to override Grafana IP address binding (default: all addresses)
            [ -e GF_SERVER_HTTP_PORT=<PORT> ] # env var to override Grafana port binding (default: 3000)
            ovishpc/ldms-grafana # the image name
                 [ OTHER GRAFANA-SERVER OPTIONS ] # other options to grafana-server

``ovishpc/ldms-grafana`` is based on
`grafana/grafana-oss:9.1.0-ubuntu <https://hub.docker.com/layers/grafana/grafana/grafana/9.1.0-ubuntu/images/sha256-39ea2186a2a5f04d808342400fe667678fd02632e62f2c36efa58c27a435d31d?context=explore>`__
with Sos data source plugin to access distributed-SOS data. The grafana
server listens to port 3000 by default. The options specified at the
``docker run`` CLI will be passed to the ``grafana-server`` command.

.. code:: sh

   ## On a service node

   # Pull the container image
   $ docker pull ovishpc/ldms-grafana

   # Start ldms-grafana container, this will use port 3000
   $ docker run -d --name=grafana --privileged --network=host ovishpc/ldms-grafana

   # Use a web browser to navigate to http://HOSTNAME:3000 to access grafana

SSH port forwarding to grafana
------------------------------

In the case that the grafana server cannot be accessed directly, use SSH
port forwarding as follows:

.. code:: sh

   (laptop) $ ssh -L 127.0.0.1:3000:127.0.0.1:3000 LOGIN_NODE
   (LOGIN_HODE) $ ssh -L 127.0.0.1:3000:127.0.0.1:3000 G_HOST
   # Assuming that the ldms-grafana container is running on G_HOST.

Then, you should be able to access the grafana web server via
``http://127.0.0.1:3000/`` on your laptop.

Building Containers
-------------------

TL;DR: edit `config.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/config.sh>`__, customize the ``*_REPO``,
``*_BRANCH`` and ``*_OPTIONS``, then run ``./scripts/build-all.sh``.

The following steps describe the building process executed by the
`scripts/build-all.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/scripts/build-all.sh>`__ script:

#. Build ``ovishpc/ldms-dev`` docker image. This "development" image
   contains development programs and libraries for building
   ``/opt/ovis`` binaries and ``dsosds``.

   -  See
      `recipes/ldms-dev/docker-build.sh <https://github.com/ovis-hpc/ldms-containers/blob/master/recipes/ldms-dev/docker-build.sh>`__
      and `recipes/ldms-dev/Dockerfile <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-dev/Dockerfile>`__.

#. Build ``/opt/ovis`` binaries with
   `scripts/build-ovis-binaries.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/scripts/build-ovis-binaries.sh>`__
   script. The environment variables specified in
   `config.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/config.sh>`__ file inform the build script which
   reposositories or branches to check out and build. The variables
   categorized by the components are as follows:

   -  ovis: the main component of OVIS project (``ldmsd`` and LDMS
      python)

      -  ``OVIS_REPO``
      -  ``OVIS_BRANCH``

   -  sos: the Scalable Object Storage technology

      -  ``SOS_REPO``
      -  ``SOS_BRANCH``

   -  maestro: the ``ldmsd`` cluster configurator

      -  ``MAESTRO_REPO``
      -  ``MAESTRO_BRANCH``

   -  numsos:

      -  ``NUMSOS_REPO``
      -  ``NUMSOS_BRANCH``

   -  sosdb-ui:

      -  ``SOSDBUI_REPO``
      -  ``SOSDBUI_BRANCH``

   -  sosdb-grafana:

      -  ``SOSDBGRAFANA_REPO``
      -  ``SOSDBGRAFANA_BRANCH`` The binaries output directory
         (absolute, or relative to the top source directory) is
         specified by the ``OVIS`` variable in
         `config.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/config.sh>`__.

#. Build ``dsosds`` grafana data source plugin for SOS data access with
   `scripts/build-dsosds.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/scripts/build-dsosds.sh>`__. The following
   envronment variables in `config.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/config.sh>`__ determine which
   repository and branch to check the code out for building ``dsosds``:

   -  ``DSOSDS_REPO``
   -  ``DSOSDS_BRANCH`` The ``dsosds`` output directory (absolute, or
      relative to the top source directory) is specified by ``DSOSDS``
      variable in `config.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/config.sh>`__.

#. Build ``ovishpc/ldms-samp`` image using the ``ovis`` binaries built
   in step 2. The LDMS Sampler Image contains only ``ldmsd``, the
   sampler plugins and their dependencies. The storage plugins are not
   included.

   -  See
      `recipes/ldms-samp/docker-build.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-samp/docker-build.sh>`__
      and
      `recipes/ldms-samp/Dockerfile <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-samp/Dockerfile>`__.
   -  Also see ``OVIS_OPTIONS`` in `config.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/config.sh>`__ for the
      build options that enable/disable plugins.

#. Build ``ovishpc/ldms-agg`` image using the ``ovis`` binaries built in
   step 2. The LDMS Aggregator Image contains SOS, ``ldmsd`` and all
   plugins (both samplers and stores).

   -  See
      `recipes/ldms-agg/docker-build.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-agg/docker-build.sh>`__
      and `recipes/ldms-agg/Dockerfile <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-agg/Dockerfile>`__.
   -  Also see ``OVIS_OPTIONS`` in `config.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/config.sh>`__ for the
      build options that enable/disable plugins.

#. Build ``ovishpc/ldms-maestro`` image using the maestro binaries from
   ``ovis`` binaries built in step 2. This image also includes ``etcd``,
   a dependency of ``maestro``.

   -  See
      `recipes/ldms-maestro/docker-build.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-maestro/docker-build.sh>`__
      and
      `recipes/ldms-maestro/Dockerfile <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-maestro/Dockerfile>`__.

#. Build ``ovishpc/ldms-ui`` image using the UI components from ``ovis``
   binaries built in step 2 (``ovis/ui/``). The image includes ``uwsgi``
   web server that is used to serve ``sosdb-ui`` Django application,
   providing SOS data access over HTTP.

   -  See
      `recipes/ldms-ui/docker-build.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-ui/docker-build.sh>`__
      and `recipes/ldms-ui/Dockerfile <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-ui/Dockerfile>`__.

#. Build ``ovishpc/ldms-grafana`` image based on ``grafana`` image and
   include ``dsosds`` grafana data source plugin built in step 3. A
   container that instantiates from this image is bacially a grafana
   server with ``dsosds`` data source plugin pre-installed.

   -  See
      `recipes/ldms-grafana/docker-build.sh <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-grafana/docker-build.sh>`__
      and
      `recipes/ldms-grafana/Dockerfile <https://github.com/ovis-hpc/ldms-containers/tree/master/recipes/ldms-grafana/Dockerfile>`__.

Note that many of the ``docker-build.sh`` scripts use ``tar`` to create
docker build context (a set of files / directories for Docker Build
process to ADD) instead of using the working directory that contains
``Dockerfile``. This is so that we don't have to copy the selected files
from ``ovis`` into each of the ``Dockerfile`` directories.

It is also possible to manually run an ``ovishpc/ldms-dev`` container
and build your version of ``ovis`` (e.g. creating a new plugin) and
package a custom ``ovishpc/ldms-samp`` with
``recipes/ldms-samp/docker-buildingn.sh`` because the
``docker-building.sh`` script uses whatever binaries available in the
``ovis`` directory.
