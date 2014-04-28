#!/usr/bin/env python

## @package komondor
#  Python package to communicate with Komondor.
#
#  If this module is executed via CLI, CONTINUE HERE.

from ovis_lib import zap
import errno
import logging
import os
import struct
import time
from array import *

## komondor
#
#  This is a komondor class. To use it, create an object with \c host and \c
#  port. Then, use  komondor::kmd_msg() to send an event message to Komondor.
class komondor:
    ## Init function.
    #
    #  \param host The komondor host to connect to.
    #  \param port The komondor port.
    def __init__(self, host, port):
        logging.basicConfig()
        self.logger = logging.getLogger()
        self.s = zap.zap_GET("sock")
        self.ep = zap.zap_NEW(self.s)
        if (not self.ep):
            self.rc = errno.ENOMEM
            self.logger.error("Cannot create zap endpoint")
            return
        self.rc = zap.zap_connect_ez(self.ep, str(host)+":"+str(port))
        if (self.rc):
            self.logger.error("zap_connect_ez rc: %s", errno.errorcode[self.rc])

    def __del__(self):
        if (self.ep):
            zap.zap_close(self.ep);

    def log(self, msg):
        self.logger.log(msg)

    ## Sending an event message.
    #
    #  \param model_id The model ID.
    #  \param comp_id The component ID.
    #  \param metric_type_id The metric type ID.
    #  \param level The severity level.
    #  \param sec The number of second since epoc (unix time stamp). 0 means
    #             current time.
    #  \ param usec The microsecond part of the time stamp. If \c sec is 0, this
    #               parameter is not used.
    def kmd_msg(self, model_id, comp_id, metric_type_id, level, sec, usec):
        s = ''
        if self.rc:
            return errno.ENOTCONN

        if (not sec):
            t = time.time()
            sec = int(t)
            usec = int(round((t - sec)*1000000))

        msg = struct.pack('>HxxLLBxxxLL', model_id, comp_id, metric_type_id,
                                                            level, sec, usec)
        msg = array('B', msg)
        return zap.zap_send(self.ep, msg, len(msg))


# The following is the MAIN code, in the case that the script is invoked from a
# command-line interface.
if __name__ == "__main__":
    import sys
    import argparse
    parser = argparse.ArgumentParser(description='Sending an event to komondor');
    parser.add_argument('-H', '--host', help='Komondor host',
                        default='localhost')
    parser.add_argument('-P', '--port', help='Komondor port', type=int,
                        default=55555)
    parser.add_argument('-m', '--model_id', help='model ID', type=int,
                        default=65535)
    parser.add_argument('-C', '--comp_id', type=int,
                        help='Component ID', required=True)
    parser.add_argument('-M', '--metric_type_id', type=int,
                        help='metric type ID', required=True)
    parser.add_argument('-l', '--level', type=int,
                        help='severity level (0: nominal, 1: info, '
                        '2: warning, 3: critical)',
                        default=1, choices=range(0,3))
    parser.add_argument('-s', '--sec', type=int,
                        help='Seconds since epoc (unix timestamp), 0 means'
                        ' now.', default=0)
    parser.add_argument('-u', '--usec', type=int,
                        help='The micro second part of the timestamp.',
                        default=0)
    args = parser.parse_args()
    k = komondor(args.host, args.port)
    rc = k.kmd_msg(args.model_id, args.comp_id, args.metric_type_id,
            int(args.level),
            int(args.sec),
            int(args.usec))
