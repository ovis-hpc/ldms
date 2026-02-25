.. _ldmsd_communicator:

=========================
ldmsd_communicator Module
=========================


--------------------------------------------------------------
A python program to communicator via Python with a LDMS daemon
--------------------------------------------------------------

:Date: 2/24/2026
:Manual section: 8
:Manual group: LDMSD

SYNOPSIS
========

This module provides a high-level client interface for communicating
with an ``ldmsd`` daemon.

Communicator
============

Implements an interface between a client and an instance of
an ``ldmsd`` daemon.

__init__(xprt, host, port, auth=None, auth_opt=None, recv_timeout=5)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Create a communicator interface to an LDMS Daemon.

:Parameters:
    **xprt** (str)
        Transport name (e.g., ``"sock"``, ``"rdma"``, etc.).

    **host** (str)
        Daemon hostname to connect to.

    **port** (int)
        Daemon port number.

    **auth** (str, optional)
        Name of authentication plugin.

    **auth_opt** (dict, optional)
        Options for the authentication plugin.

    **recv_timeout** (int)
        Timeout for receiving a response (microseconds).

connect(timeout=None)
^^^^^^^^^^^^^^^^^^^^^

Connect to the configured daemon.

:Parameters:
    **timeout** (int, optional)
        Maximum time (microseconds) to wait for connect.

:Returns:
    Tuple ``(status, message)`` indicating success or error.

reconnect(timeout=None)
^^^^^^^^^^^^^^^^^^^^^^^

Reconnect by closing and reinitializing the transport.

:Parameters:
    **timeout** (int, optional)
        Maximum time (microseconds) to wait for reconnect.

:Returns:
    Tuple ``(status, message)``.

getState()
^^^^^^^^^^

Return the communicator’s current connection state.

:Returns:
    Status code int.

getMaxRecvLen()
^^^^^^^^^^^^^^^

Return the maximum supported receive buffer length.

:Returns:
    Integer length.

getHost()
^^^^^^^^^

Return the configured host name.

:Returns:
    Hostname (str).

getPort()
^^^^^^^^^

Return the configured port number.

:Returns:
    Port (int).

send_command(cmd)
^^^^^^^^^^^^^^^^^

Send a raw LDMSD request to the daemon.

:Parameters:
    **cmd**
        An ``LDMSD_Request`` object representing the request.

:Returns:
    Tuple ``(status, message)``.

receive_response()
^^^^^^^^^^^^^^^^^^

Receive and parse a response from the daemon.

:Returns:
    Tuple ``(status, response_data)``.

greeting(name=None)
^^^^^^^^^^^^^^^^^^^

Send a greeting request.

:Parameters:
    **name** (str, optional)
        Name to identify the client.

:Returns:
    Tuple ``(status, greeting_info)``.

dump_cfg(path)
^^^^^^^^^^^^^^

Request the daemon to write its configuration to a file.

:Parameters:
    **path** (str)
        Path on the remote host where config is to be dumped.

:Returns:
    Tuple ``(status, result_message)``.

auth_add(name, plugin, auth_opt=None)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Add an authentication domain.

:Parameters:
    **name** (str)
        Authentication domain name.

    **plugin** (str)
        Plugin implementing authentication.

    **auth_opt** (dict, optional)
        Plugin options.

:Returns:
    Tuple ``(status, message)``.

daemon_exit()
^^^^^^^^^^^^^

Tell the daemon to cleanly exit.

:Returns:
    Tuple ``(status, message)``.

failover_config(primary=None, secondary=None, timeout=None)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Configure failover endpoints.

:Parameters:
    **primary** (str, optional)
        Primary daemon endpoint.

    **secondary** (str, optional)
        Secondary daemon endpoint.

    **timeout** (int, optional)
        Timeout for failover coordination.

:Returns:
    Tuple ``(status, message)``.

failover_start()
^^^^^^^^^^^^^^^^

Begin failover.

:Returns:
    Tuple ``(status, message)``.

failover_stop()
^^^^^^^^^^^^^^^

Stop failover.

:Returns:
    Tuple ``(status, message)``.

failover_status()
^^^^^^^^^^^^^^^^^

Get current failover status.

:Returns:
    Tuple ``(status, status_info)``.

failover_peercfg_start()
^^^^^^^^^^^^^^^^^^^^^^^^

Start peer configuration replication.

:Returns:
    Tuple ``(status, message)``.

failover_peercfg_stop()
^^^^^^^^^^^^^^^^^^^^^^^

Stop peer configuration replication.

:Returns:
    Tuple ``(status, message)``.

setgroup_add(setgroup, setlist, expire=None)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Add a new set group.

:Parameters:
    **setgroup** (str)
        Name of the set group.

    **setlist** (list)
        List of set names to include.

    **expire** (int, optional)
        Expiration time of the group.

:Returns:
    Tuple ``(status, message)``.

setgroup_mod(setgroup, add=None, rm=None, expire=None)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Modify an existing set group.

:Parameters:
    **setgroup** (str)
        Group name.

    **add** (list, optional)
        Sets to add.

    **rm** (list, optional)
        Sets to remove.

    **expire** (int, optional)
        New expiration.

:Returns:
    Tuple ``(status, message)``.

setgroup_del(setgroup)
^^^^^^^^^^^^^^^^^^^^^^

Delete a set group.

:Parameters:
    **setgroup** (str)
        Group to delete.

:Returns:
    Tuple ``(status, message)``.

setgroup_ins(setgroup, setname)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Insert a set into a group.

:Parameters:
    **setgroup** (str)
        Group name.

    **setname** (str)
        Set to insert.

:Returns:
    Tuple ``(status, message)``.

setgroup_rm(setgroup, setname)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Remove a set from a group.

:Parameters:
    **setgroup** (str)
        Group name.

    **setname** (str)
        Set to remove.

:Returns:
    Tuple ``(status, message)``.

stream_client_dump()
^^^^^^^^^^^^^^^^^^^^

Retrieve stream client information.

:Returns:
    Tuple ``(status, dump_data)``.

stream_status(reset=False)
^^^^^^^^^^^^^^^^^^^^^^^^^^

Get current stream status.

:Parameters:
    **reset** (bool)
        Whether to reset statistics.

:Returns:
    Tuple ``(status, status_data)``.

stream_disable(name=None)
^^^^^^^^^^^^^^^^^^^^^^^^^

Disable a stream.

:Parameters:
    **name** (str, optional)
        Name of stream to disable.

:Returns:
    Tuple ``(status, message)``.

stream_enable(name=None)
^^^^^^^^^^^^^^^^^^^^^^^^

Enable a stream.

:Parameters:
    **name** (str, optional)
        Name of stream to enable.

:Returns:
    Tuple ``(status, message)``.

msg_stats(regex=None, stream=None, reset=False)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Gather message stats.

:Parameters:
    **regex** (str)
        Regex filter for messages.

    **stream** (str)
        Stream name to filter.

    **reset** (bool)
        Reset stats if True.

:Returns:
    Tuple ``(status, stats_data)``.

msg_client_stats(reset=False)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Retrieve client message stats.

:Parameters:
    **reset** (bool)
        Whether to reset statistics.

:Returns:
    Tuple ``(status, data)``.

msg_disable()
^^^^^^^^^^^^^

Disable messaging subsystem.

:Returns:
    Tuple ``(status, message)``.

msg_enable()
^^^^^^^^^^^^

Enable messaging subsystem.

:Returns:
    Tuple ``(status, message)``.

listen(xprt, port, host=None, auth=None, quota=None, rx_limit=None)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Open a transport listener endpoint.

:Parameters:
    **xprt** (str)
        Transport type.

    **port** (int)
        Port to listen on.

    **host** (str, optional)
        Interface to bind.

    **auth** (str, optional)
        Auth plugin for incoming connections.

    **quota** (int, optional)
        Connection quota.

    **rx_limit** (int, optional)
        Receive rate limit.

:Returns:
    Tuple ``(status, listener_info)``.

metric_sets_default_authz(uid=None, gid=None, perm=None)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Set default authorization for metric sets.

:Parameters:
    **uid** (int, optional)
        Default user ID.

    **gid** (int, optional)
        Default group ID.

    **perm** (str, optional)
        Permissions (e.g., ``"rw"``).

:Returns:
    Tuple ``(status, info)``.

dir_list()
^^^^^^^^^^

Retrieve a directory listing from the daemon.

:Returns:
    Tuple ``(status, listing)``.

store_time_stats(name=None, reset=False)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Retrieve or reset store timing statistics.

:Parameters:
    **name** (str, optional)
        Store name to filter.

    **reset** (bool)
        Reset stats if True.

:Returns:
    Tuple ``(status, stats)``.

plugn_load(name, plugin)
^^^^^^^^^^^^^^^^^^^^^^^^

Load a plugin into the daemon.

:Parameters:
    **name** (str)
        Plugin instance name.

    **plugin** (str)
        Plugin file or identifier.

:Returns:
    Tuple ``(status, message)``.

plugn_term(name)
^^^^^^^^^^^^^^^^

Terminate a plugin.

:Parameters:
    **name** (str)
        Plugin instance name.

:Returns:
    Tuple ``(status, message)``.

plugn_config(name, cfg_str)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Configure a running plugin.

:Parameters:
    **name** (str)
        Plugin name.

    **cfg_str** (str)
        Configuration options string.

:Returns:
    Tuple ``(status, message)``.

plugn_stop(name)
^^^^^^^^^^^^^^^^

Stop a plugin.

:Parameters:
    **name** (str)
        Plugin name.

:Returns:
    Tuple ``(status, message)``.

plugn_status(name)
^^^^^^^^^^^^^^^^^^

Get plugin status.

:Parameters:
    **name** (str)
        Plugin name.

:Returns:
    Tuple ``(status, status_data)``.

plugn_sets(name)
^^^^^^^^^^^^^^^^

List sets associated with a plugin.

:Parameters:
    **name** (str)
        Plugin name.

:Returns:
    Tuple ``(status, set_list)``.
