Quick Start
==================================================================

Create A Simple Analysis
------------------------
To start, please create a folder called ``graf_analysis`` in your home directory and copy the following contents to a python file called ``dsosTemplate.py``:

* This is a python analysis that queries the DSOS database and returns a DataFrame of the ``meminfo`` schema metrics along with the ``timestamp``, ``component_id`` and ``job_id``.

dsosTemplate.py:

.. code-block :: python

    import os, sys, traceback
    import datetime as dt
    from graf_analysis.grafanaAnalysis import Analysis
    from sosdb import Sos
    import pandas as pd
    import numpy as np
    class dsosTemplate(Analysis):
        def __init__(self, cont, start, end, schema='meminfo', maxDataPoints=4096):
            super().__init__(cont, start, end, schema, 1000000)

        def get_data(self, metrics, filters=[],params=None):
            try:
                self.sel = f'select {",".join(metrics)} from {self.schema}'
                where_clause = self.get_where(filters,res=FALSE)
                order = 'time_job_comp'
                orderby='order_by ' + order
                self.query.select(f'{sel} {where_clause} {orderby}')
                res = self.get_all_data(self.query)
                # Fun stuff here!
                print(res.head)
                return res
            except Exception as e:
                a, b, c = sys.exc_info()
                print(str(e)+' '+str(c.tb_lineno))

.. note::

  If you want to use this analysis module in a Grafana dashboard, you will need to ask your administrator to copy your new analysis module(s) into the directory that Grafana points to. This is because Grafana is setup to look at a specific path directory to query from.

Test Analysis via Terminal Window
----------------------------------
You can easily test your module without the Grafana interface by creating a python script that mimics the Grafana query and formats the returned JSON into a timeseries dataframe or table.

First, create the following file in the same directory as your python analysis (i.e. ``/user/home/graf_analysis/``) and label it ``testDSOSanalysis.py``.

* This python script imitates the Grafana query that calls your analysis module and will return a timeseries DataFrame of the ``Active`` and ``Inactive`` meminfo metrics.

.. code-block :: python

    #!/usr/bin/python3

    import time,sys
    from sosdb import Sos
    from grafanaFormatter import DataFormatter
    from table_formatter import table_formatter
    from time_series_formatter import time_series_formatter
    from dsosTemplate import dsosTemplate

    sess = Sos.Session("/<DSOS_CONFIG_PATH>/config/dsos.conf")
    cont = '<PATH_TO_DATABASE>'
    cont = sess.open(cont)

    model = dsosTemplate(cont, time.time()-300, time.time(), schema='meminfo', maxDataPoints=4096)

    x = model.get_data(['Active','Inactive'], filters=['job_id'], params='')

    #fmt = table_formatter(x)
    fmt = time_series_formatter(x)
    x = fmt.ret_json()
    print(x)

.. note::

  You will need to provide the path to the DSOS container and ``Sos.Session()`` configuration file in order to run this python script. Please see the `Python Analysis Creation <pyanalysis.rst>`_ for more details.

* Next, run the python module:

.. code-block :: bash

  python3 dsosTemplate.py

.. note::

    All imports are python scripts that need to reside in the same directory as the test analysis module in order for it to run successfully.

Then, run the python script with the current python verion installed. In this case it would be ``python3 <analysisTemplate.py>``

Expected Results & Output
+++++++++++++++++++++++++
The following is an example test of an analysis module that queries the ``meminfo`` schema an returns a timeseries dataframe of the ``Active`` and ``Inactive`` metrics:

.. image:: images/grafana/grafana_output.png

Test Analysis via Grafana Dashboard
-----------------------------------
You can optionally test the analysis in a grafana dashboard. This is not preferred because it is a bit more time consuming and, if there is a lot of data to query, there can be some additional wait time in that as well.

Create A New Dashboard
++++++++++++++++++++++++++
To create a new dashboard, click on the + sign on the left side of the home page and hit dashboard. This will create a blank dashboard with an empty panel in it. Hit the add query button on the panel to begin configuring the query to be sent to an analysis module. 

.. note::

  For more information on how to navigate around the Grafana dashboard and what the variables and advanced settings do, please see `Grafana Panel <grafanapanel>`_ and `Grafana Usage <grafanause>`_.

* Next, add your analysis by filling out the required fields shown below:

.. image:: ../images/grafana/grafana_query.png

* These fields are identical to the python script you can generate to test in your terminal window so please refer to :ref:`Test Analysis via Terminal Window` or `Grafana Panel <grafanapanel>`_ for more details.

* Now change the analysis to query from the last 5 minutes by selecting the down arrow in the top right of the panel and selecting "Last 5 minutes"

.. image:: ../images/grafana/grafana_time.png
    :height: 250
    :width: 50

* Then change the refresh rate to 5 seconds so that Grafana will automatically query the data every 5 seconds

.. image:: ../images/grafana/grafana_timerange.png

* Now you should be able to see a the "Active" and "Inactive" values for each job_id (as seen in `Expected Results & Output`_).
