#!/usr/bin/env python
'''
oconverter_sqlite -- Convert an OVIS configuration database back to text files

oconverter_sqlite is a converter that converts an OVIS configuration sqlite database
back to the text file format.

The result of this program preserves the comp_ids of components and
the metric_ids of metrics.

@author:     Nichamon Naksinehaboon

@contact:    nichamon@opengridcomputing.com
'''

import sys, os, logging, sqlite3
from argparse import ArgumentParser

from oconf_sqlite import osqlite_util, oconvert_sqlite
import traceback

logging.basicConfig(format = '%(message)s')
log = logging.getLogger()

__all__ = []
__version__ = 0.1
__date__ = '2014-10-22'
__updated__ = '2014-10-28'

def main(argv=None): # IGNORE:C0111
    '''Command line options.'''

    if argv is None:
        argv = sys.argv
    else:
        sys.argv.extend(argv)

    program_name = os.path.basename(sys.argv[0])
    program_version = "v%s" % __version__
    program_build_date = str(__updated__)
    program_version_message = '%%(prog)s %s (%s)' % (program_version, program_build_date)
    program_shortdesc = __import__('__main__').__doc__.split("\n")[1]

    conn = None

    try:
        # Setup argument parser
        parser = ArgumentParser()
        parser.add_argument('-V', '--version', action='version', version=program_version_message)
        parser.add_argument('--conf_db', help="Full path to the OVIS configuration " \
                   "database. If it is given, the default path is " \
                   "$OVIS_RUN_CONF. (See /etc/profile.d/set-ovis-variables.sh.)")
        parser.add_argument('--path', help="Path to create the OVIS configuration text files",
                            default="/etc/ovis/conf")
        parser.add_argument('--components', help="Create components.conf from the database", \
                            default = False, action = "store_true")
        parser.add_argument('--metrics', help="Create metrics.conf from the databse", \
                            default = False, action = "store_true")
        parser.add_argument('--model_events', help="Create model_events.conf from the database", \
                            default = False, action = "store_true")
        parser.add_argument('--services', help="Create services.conf from the database", \
                            default = False, action = "store_true")
        parser.add_argument('--cables', help="Create cables.conf from the database", \
                            default = False, action = "store_true")
        parser.add_argument('--log_level', help="The verbosity of the program. " \
                   "The default is ERROR.", choices=['DEBUG', 'INFO', 'ERROR'],\
                   default = 'ERROR')

        # Process arguments
        args = parser.parse_args()
        log.setLevel(logging._levelNames[args.log_level])

        if args.conf_db is None:
            args.conf_db = osqlite_util.get_ovis_conf_db()

        db_path = args.conf_db

        if not os.path.isfile(db_path):
            sys.exit(db_path + " doesn't exist.")

        osqlite_util.print_line()
        osqlite_util.print_ovis_db_path(db_path)
        osqlite_util.print_line()

        if not os.path.isdir(args.path):
            sys.exit(args.path + " does not exist.")

        conn = sqlite3.connect(db_path)
        if conn is None:
            sys.exit("Failed to connect to the OVIS conf. database")

        cursor = conn.cursor()
        if cursor is None:
            conn.close()
            sys.exit("Failed to create a cursor to the OVIS conf. database")

        if not (args.components or args.metrics or args.model_events or args.services or args.cables):
            args.components = True
            args.metrics = True
            args.model_events = True
            args.services = True
            args.cables = True

        if args.components:
            '''
            Write components.conf
            '''
            oconvert_sqlite.write_component_conf(cursor, args.path)

        if args.metrics:
            '''
            Write metrics.conf
            '''
            oconvert_sqlite.write_metric_conf(cursor, args.path)

        if args.model_events:
            '''
            Write model_events.conf
            '''
            oconvert_sqlite.write_model_events_conf(cursor, args.path)

        if args.services:
            '''
            Write services.conf
            '''
            oconvert_sqlite.write_services_conf(cursor, args.path)

        if args.cables:
            '''
            Write cables.conf
            '''
            oconvert_sqlite.write_cables_conf(cursor, args.path)

        conn.close()
        print "DONE"
        return 0
    except KeyboardInterrupt:
        ### handle keyboard interrupt ###
        return 0
    except Exception:
        traceback.print_exc()
        return 2
    finally:
        if conn:
            conn.close()

if __name__ == "__main__":
    sys.exit(main())
