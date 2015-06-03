#!/usr/bin/python
import cmd
import argparse
import socket
import sys
import os

LDMSCTL_LIST_PLUGINS =	0
LDMSCTL_LOAD_PLUGIN =	1
LDMSCTL_TERM_PLUGIN =	2
LDMSCTL_CFG_PLUGIN =	3
LDMSCTL_START_SAMPLER =	4
LDMSCTL_STOP_SAMPLER =	5
LDMSCTL_ADD_HOST =	6
LDMSCTL_REM_HOST =	7
LDMSCTL_STORE =		8
LDMSCTL_INFO_DAEMON =	9
LDMSCTL_SET_UDATA =	10
LDMSCTL_EXIT_DAEMON =	11
LDMSCTL_UPDATE_STANDBY = 12
LDMSCTL_ONESHOT_SAMPLE = 13

LDMSCTL_PRDCR_ADD	= 20
LDMSCTL_PRDCR_DEL	= 21
LDMSCTL_PRDCR_START	= 22
LDMSCTL_PRDCR_STOP	= 23
LDMSCTL_PRDCR_START_REGEX	= 24
LDMSCTL_PRDCR_STOP_REGEX	= 25

LDMSCTL_UPDTR_ADD	= 30
LDMSCTL_UPDTR_DEL	= 31
LDMSCTL_UPDTR_MATCH_ADD = 32
LDMSCTL_UPDTR_MATCH_DEL = 33
LDMSCTL_UPDTR_PRDCR_ADD = 34
LDMSCTL_UPDTR_PRDCR_DEL = 35
LDMSCTL_UPDTR_START	= 38
LDMSCTL_UPDTR_STOP	= 39

LDMSCTL_STRGP_ADD		= 40
LDMSCTL_STRGP_DEL		= 41
LDMSCTL_STRGP_PRDCR_ADD	= 42
LDMSCTL_STRGP_PRDCR_DEL	= 43
LDMSCTL_STRGP_METRIC_ADD	= 44
LDMSCTL_STRGP_METRIC_DEL	= 45
LDMSCTL_STRGP_START	= 48
LDMSCTL_STRGP_STOP	= 49

class LdmsdCmdParser(cmd.Cmd):
    def __init__(self, sockname, infile=None):
        self.prompt = "ldmsd> "
        self.myname = "{0}/ldmsd_cfg_socket_{1}".format(os.getcwd(), os.getpid())
        try:
            os.unlink(self.myname)
        except:
            pass
            self.sockname = sockname
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
                self.sock.bind(self.myname)
            except IOError:
                print("Cannot open the socket {0}".format(sockname))

        self.prompt = os.path.basename(sockname) + '> '
        self.load_kw = [ 'name=' ]
        self.config_kw = self.load_kw
        self.start_kw = [ 'name=', 'interval=' ]
        self.stop_kw = [ 'name=', 'interval=' ]
        self.set_udata_kw = [ 'name=', 'metric=', 'udata=' ]

        self.prdcr_add_kw = [ 'name=', 'xprt=', 'host=', 'port=', 'type=', 'interval=' ]
        self.prdcr_del_kw = [ 'name=' ]
        self.prdcr_start_kw = [ 'name=', 'interval=' ]
        self.prdcr_start_regex_kw = [ 'regex=', 'interval=' ]
        self.prdcr_stop_kw = [ 'name=' ]
        self.prdcr_stop_regex_kw = [ 'regex=' ]

        self.updtr_add_kw = [ 'name=', 'interval=', 'offset=' ]
        self.updtr_del_kw = [ 'name=' ]
        self.updtr_start_kw = [ 'name=' ]
        self.updtr_stop_kw = [ 'name=' ]
        self.updtr_match_add_kw = [ 'name=', 'match=', 'regex=' ]
        self.updtr_match_del_kw = self.updtr_match_add_kw
        self.updtr_prdcr_add_kw = [ 'name=', 'regex=' ]
        self.updtr_prdcr_del_kw = self.updtr_prdcr_add_kw

        self.strgp_add_kw = [ 'name=', 'plugin=', 'container=', 'rotate=', 'schema=' ]
        self.strgp_del_kw = [ 'name=' ]
        self.strgp_prdcr_add_kw = [ 'name=', 'regex=' ]
        self.strgp_prdcr_del_kw = self.strgp_prdcr_add_kw
        self.strgp_metric_add_kw = [ 'name=', 'metric=' ]
        self.strgp_metric_del_kw = [ 'name=', 'metric=' ]
        self.strgp_start_kw = [ 'name=' ]
        self.strgp_stop_kw = self.strgp_start_kw
        if infile:
            cmd.Cmd.__init__(self, stdin=infile)
        else:
            cmd.Cmd.__init__(self)

    def emptyline(self):
        pass

    def do_shell(self, args):
        """
        Execute a shell command
        """
        os.system(args)

    def do_say(self, args):
        """
        Print a message to the console
        """
        print(args)

    def precmd(self, line):
        if line[0:1] == '#':
            return ''
        return line

    def handle_request(self, cmd, args):
        try:
            req = "{0} {1}\n".format(cmd, args)
            self.sock.sendto(req, self.sockname)
        except:
            return False
        return True

    def handle_response(self):
        try:
            (rsp, addr) = self.sock.recvfrom(1024)
            if rsp[0:1] != '0':
                print("Error: '{0}'".format(rsp))
        except:
            return False
        return True

    def handle(self, cmd, args):
        if self.handle_request(cmd, args):
            self.handle_response()
        else:
            print("Error sending request to {0}".format(self.sockname))

    def do_source(self, arg):
        """
        Parse commands from the specified file as if they were entered
        on the console.
        """
        try:
            script = open(arg, 'r')
            for cmd in script:
                self.onecmd(cmd)
            script.close()
        except Exception as e:
            print(str(e))

    def do_sockname(self, arg):
        """
        Set the socket name of the server with which to communicate
        """
        self.sockname = arg
        self.prompt = os.path.basename(self.sockname) + '> '

    def do_load(self, arg):
        """
        Load a plugin at the Aggregator/Producer
        Parameters:
        name=     The plugin name
        """
        self.handle(LDMSCTL_LOAD_PLUGIN, arg)

    def complete_load(self, text, line, begidx, endidx):
        return [ name for name in self.load_kw if name.startswith(text) ]

    def do_prdcr_add(self, arg):
        """
        Add an LDMS Producer to the Aggregator
        Parameters:
        name=     A unique name for this Producer
        xprt=     The transport name [sock, rdma, ugni]
        host=     The hostname of the host
        port=     The port number on which the LDMS is listening
        type=     The connection type [active, passive]
        interval= The connection retry interval (us)
        """
        self.handle(LDMSCTL_PRDCR_ADD, arg)

    def complete_prdcr_add(self, text, line, begidx, endidx):
        return [ name for name in self.prdcr_add_kw if name.startswith(text) ]

    def do_prdcr_del(self, arg):
        """
        Delete an LDMS Producer from the Aggregator. The producer
        cannot be in use or running.
        Parameters:
        name=    The Producer name
        """
        self.handle(LDMSCTL_PRDCR_DEL, arg)

    def complete_prdcr_del(self, text, line, begidx, endidx):
        return [ name for name in self.prdcr_add_kw if name.startswith(text) ]

    def do_prdcr_start(self, arg):
        """
        Start the specified producer.
        Parameters:
        name=     The name of the producer
        interval= The connection retry interval in micro-seconds. If this is not
                  specified, the previously configured value will be used.
        """
        self.handle(LDMSCTL_PRDCR_START, arg);

    def complete_prdcr_start(self, text, line, begidx, endidx):
        return [ name for name in self.prdcr_start_kw if name.startswith(text) ]

    def do_prdcr_start_regex(self, arg):
        """
        Start all producers matching a regular expression.
        Parameters:
        regex=     A regular expression
        interval=  The connection retry interval in micro-seconds. If this is not
                   specified, the previously configured value will be used.
        """
        self.handle(LDMSCTL_PRDCR_START_REGEX, arg);

    def complete_prdcr_start_regex(self, text, line, begidx, endidx):
        return [ name for name in self.prdcr_start_regex_kw if name.startswith(text) ]

    def do_prdcr_stop(self, arg):
        """
        Stop the specified Producer.
        Parameters:
        name=  The producer name
        """
        self.handle(LDMSCTL_PRDCR_STOP, arg);

    def complete_prdcr_stop(self, text, line, begidx, endidx):
        return [ name for name in self.prdcr_stop_kw if name.startswith(text) ]

    def do_prdcr_stop_regex(self, arg):
        """
        Stop all producers matching a regular expression.
        Parameters:
        regex=   The regular expression
        """
        self.handle(LDMSCTL_PRDCR_STOP_REGEX, arg);

    def complete_prdcr_stop_regex(self, text, line, begidx, endidx):
        return [ name for name in self.prdcr_stop_regex_kw if name.startswith(text) ]

    def do_updtr_add(self, arg):
        """
        Add an updater process that will periodically sample
        Producer metric sets
        """
        self.handle(LDMSCTL_UPDTR_ADD, arg)

    def complete_updtr_add(self, text, line, begidx, endidx):
        return [ name for name in self.updtr_add_kw if name.startswith(text) ]

    def do_updtr_del(self, arg):
        """
        Remove an updater from the configuration
        """
        self.handle(LDMSCTL_UPDTR_DEL, arg)

    def complete_updtr_del(self, text, line, begidx, endidx):
        return [ name for name in self.updtr_del_kw if name.startswith(text) ]

    def do_updtr_match_add(self, arg):
        """
        Add a match condition that specifies the sets to update. The
        parameters are as follows:
        name=   The Update Policy name
        regex=  The regular expression string
        match=  The value with which to compare; if match=inst,
                the expression will match the set's instance name, if
                match=schema, the expression will match the set's
                schema name.
        """
        self.handle(LDMSCTL_UPDTR_MATCH_ADD, arg)

    def complete_updtr_match_add(self, text, line, begidx, endidx):
        return [ name for name in self.updtr_match_add_kw if name.startswith(text) ]

    def do_updtr_match_del(self, arg):
        """
        Remove a match condition from the Updater. The
        parameters are as follows:
        name=   The Update Policy name
        regex=  The regular expression string
        match=  The value with which to compare; if match=inst,
                the expression will match the set's instance name, if
                match=schema, the expression will match the set's
                schema name.
        """
        self.handle(LDMSCTL_UPDTR_MATCH_DEL, arg)

    def complete_updtr_match_del(self, text, line, begidx, endidx):
        return [ name for name in self.updtr_match_del_kw if name.startswith(text) ]

    def do_updtr_prdcr_add(self, arg):
        """
        Add matching Producers to an Updater policy. The parameters are as
        follows:
        name=   The name of the Updater
        regex=  A regular expression matching zero or more producers
        """
        self.handle(LDMSCTL_UPDTR_PRDCR_ADD, arg)

    def complete_updtr_prdcr_add(self, text, line, begidx, endidx):
        return [ name for name in self.updtr_prdcr_add_kw if name.startswith(text) ]

    def do_updtr_prdcr_del(self, arg):
        """
        Remove matching Producers from an Updater policy. The parameters are as
        follows:
        name=    The name of the Updater
        regex=   A regular expression matching zero or more producers
        """
        self.handle(LDMSCTL_UPDTR_PRDCR_DEL)

    def complete_updtr_prdcr_del(self, text, line, begidx, endidx):
        return [ name for name in self.updtr_prdcr_del_kw if name.startswith(text) ]

    def do_updtr_start(self, arg):
        """
        Start updaters. The parameters to the commands are as
        follows:
        regex=    A regular expression specifying which updaters to start
        interval= The update interval in micro-seconds. If this is not
                  specified, the previously configured value will be used.
        """
        self.handle(LDMSCTL_UPDTR_START, arg);

    def complete_updtr_start(self, text, line, begidx, endidx):
        return [ name for name in self.updtr_start_kw if name.startswith(text) ]

    def do_updtr_stop(self, arg):
        """
        Stop the Updater. The Updater must be stopped in order to
        change it's configuration.
        Paramaeters:
        regex=   A regular expression specifying which updaters to stop
        """
        self.handle(LDMSCTL_UPDTR_STOP, arg);

    def complete_updtr_stop(self, text, line, begidx, endidx):
        return [ name for name in self.updtr_stop_kw if name.startswith(text) ]

    def do_strgp_add(self, arg):
        """
        Create a Storage Policy and open/create the storage instance.
        Parameters:
        name=      The unique storage policy name.
        plugin=    The name of the storage backend.
        container= The storage backend container name.
        rotate=    The time period stored in a single container. The format is
                   <number><units> where <units> is one of 'h' or 'd', for
                   hours and days respectively. For example "rotate=2d",
                   means that a container will contain two days of data before
                   rotation to a new container. If rotate is ommitted, container
                   rotation is disabled.
        schema=    The schema name of the metric set to store.
        ...        All other values will be passed to the plugin as
                   configure options
        """
        self.handle(LDMSCTL_STRGP_ADD, arg)

    def complete_strgp_add(self, text, line, begidx, endidx):
        return [ name for name in self.strgp_add_kw if name.startswith(text) ]

    def do_strgp_del(self, arg):
        """
        Remove a Storage Policy. All updaters must be stopped in order for
        a storage policy to be deleted.
        Parameters:
        name=   The storage policy name
        """
        self.handle(LDMSCTL_STRGP_DEL, arg)

    def complete_strgp_del(self, text, line, begidx, endidx):
        return [ name for name in self.strgp_del_kw if name.startswith(text) ]

    def do_strgp_prdcr_add(self, arg):
        """
        Add a regular expression used to identify the producers this
        storage policy will apply to.
        Parameters:
        name=   The storage policy name
        regex=  A regular expression matching metric set producers
        """
        self.handle(LDMSCTL_STRGP_PRDCR_ADD, arg)

    def complete_strgp_prdcr_add(self, text, line, begidx, endidx):
        return [ name for name in self.strgp_prdcr_add_kw if name.startswith(text) ]

    def do_strgp_prdcr_del(self, arg):
        """
        Remove a regular expression from the producer match list.
        Parameters:
        name=   The storage policy name
        regex=  The regular expression to remove
        """
        self.handle(LDMSCTL_STRGP_PRDCR_DEL, arg)

    def complete_strgp_prdcr_del(self, text, line, begidx, endidx):
        return [ name for name in self.strgp_prdcr_del_kw if name.startswith(text) ]

    def do_strgp_metric_add(self, arg):
        """
        Add the name of a metric to store. If the metric list is NULL,
        all metrics in the metric set will be stored.
        Parameters:
        name=   The storage policy name
        metric= The metric name
        """
        self.handle(LDMSCTL_STRGP_METRIC_ADD, arg)

    def complete_strgp_metric_add(self, text, line, begidx, endidx):
        return [ name for name in self.strgp_metric_add_kw if name.startswith(text) ]

    def do_strgp_metric_del(self, arg):
        """
        Remove a metric from the set of stored metrics.
        Parameters:
        name=   The storage policy name
        metric= The metric to remove
        """
        self.handle(LDMSCTL_STRGP_METRIC_DEL, arg)

    def complete_strgp_set_del(self, text, line, begidx, endidx):
        return [ name for name in self.strgp_set_del_kw if name.startswith(text) ]

    def do_strgp_start(self, arg):
        """
        Start storage policies.
        regex=    A regular expression specifying which storage policies to start
        """
        self.handle(LDMSCTL_STRGP_START, arg);

    def complete_strgp_start(self, text, line, begidx, endidx):
        return [ name for name in self.strgp_start_kw if name.startswith(text) ]

    def do_strgp_stop(self, arg):
        """
        Stop storage policies. A storage policy must be stopped in order to
        change it's configuration.
        Paramaeters:
        regex=   A regular expression specifying which storage policies to stop
        """
        self.handle(LDMSCTL_STRGP_STOP, arg);

    #
    # Backward compatibility commands
    #
    def do_info(self, arg):
        """
        Tell the daemon to dump it's internal state to the log file.
        """
        self.handle(LDMSCTL_INFO_DAEMON, arg)

    def do_config(self, arg):
        """
        Send a configuration command to the specified plugin.
        Parameters:
        name=   The plugin name
        ...     Plugin specific attr=value tuples
        """
        self.handle(LDMSCTL_CFG_PLUGIN, arg)

    def complete_config(self, text, line, begidx, endidx):
        return [ name for name in self.prdcr_config_kw if name.startswith(text) ]

    def do_start(self, arg):
        """
        Start a sampler plugin
        Parameters:
        name=     The plugin name
        interval= The sample interval in microseconds
        """
        self.handle_request(LDMSCTL_START_SAMPLER, arg)
        self.handle_response()

    def complete_start(self, text, line, begidx, endidx):
        return [ name for name in self.prdcr_start_kw if name.startswith(text) ]

    def do_stop(self, arg):
        """
        Stop a sampler plugin
        Parameters:
        name=     The plugin name
        """
        self.handle(LDMSCTL_STOP_SAMPLER, arg)

    def complete_stop(self, text, line, begidx, endidx):
        return [ name for name in self.prdcr_stop_kw if name.startswith(text) ]

    def do_set_udata(self, arg):
        """
        Set the user data value for a metric in a metric set. This is typically used to
        convey the Component Id to the Aggregator.
        Parameters:
        name=   The sampler plug-in name
        metric= The metric name
        udata=  The desired user-data. This is a 64b unsigned integer.
        """
        self.handle(LDMSCTL_SET_UDATA, arg)

    def complete_set_udata(self, text, line, begidx, endidx):
        return [ name for name in self.set_udata_kw if name.startswith(text) ]

    def do_list(self, arg):
        """List the plugins loaded on the server."""
        self.handle(LDMSCTL_LIST_PLUGINS, arg)

    def do_EOF(self, arg):
        """
        Ctrl-D will exit the shell
        """
        self.do_quit(self)

    def do_quit(self, arg):
        """
        Quit the LDMSD shell
        """
        os.unlink(self.myname)
        sys.exit(0)

    def complete_quit(self, arg):
        return [ 'quit' ]

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Configure an LDMS Daemon")
    parser.add_argument("--sockname",
                        default="/var/ldmsd/control",
                        help="Specify the UNIX socket used to communicate with LDMSD.")
    args = parser.parse_args()
    sockname = args.sockname
    cmdParser = LdmsdCmdParser(sockname)
    cmdParser.cmdloop("Welcome to the LDMSD control processor")
