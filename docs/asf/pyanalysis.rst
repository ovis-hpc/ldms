Python Analysis Creation
========================

Analysis I/O
-----------
An analysis module is a python script and has a general template. There is a class, which must be called the same name as the python script itself, and two class functions: ``__init__`` and ``get_data``. The module is first initialized and then ``get_data`` is called. This should return a pandas DataFrame or a NumSOS DataSet (preferably the former if you are using python3). Below are the variables passed from the Grafana interface to these class functions.

``__init__``
  * ``cont`` - A Sos.Container object which contains the path information to the SOS container specified in the Grafana query
  * ``start`` - The beginning of the time range of the Grafana query (in epoch time).
  * ``end`` - The end of the time range of the Grafana query (in epoch time).
  * ``schema`` - The LDMS schema specified by the Grafana query (e.g. meminfo).
  * ``maxDataPoints`` - the maximum number of points that Grafana can display on the user's screen.

``get_data``
  * ``metrics`` - a python list of metrics specified by the Grafana query (e.g. ['Active','MemFree']).
  * ``job_id`` - a string of the job_id specified by the Grafana query.
  * ``user_name`` - a string of the user name specified by the Grafana query.
  * ``params`` - a string of the extra parameters specified by the Grafana query (e.g. 'threshold = 10').
  * ``filters`` - a python list of filter strings for the DSOS query (e.g. ['job_id == 30','component_id < 604']).
/t

Example Analysis Module
-------------------------------------

Below is a basic analysis that simply queries the database and returns the DataFrame of the metrics passed in along with the timestamp, component_id, and job_id for each metric.

.. code-block:: python

    import os, sys, traceback
    import datetime as dt
    from graf_analysis.grafanaAnalysis import Analysis
    from sosdb import Sos
    import pandas as pd
    import numpy as np
    class dsosTemplate(Analysis):
        def __init__(self, cont, start, end, schema='job_id', maxDataPoints=4096):
            super().__init__(cont, start, end, schema, 1000000)

        def get_data(self, metrics, filters=[],params=None):
            try:
                sel = f'select {",".join(metrics)} from {self.schema}'
                where_clause = self.get_where(filters)
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

In the ``__init__`` function, most things are set to be self variables to access them later in the ``get_data`` using the ``super()`` function. The ``super()`` function also sets up a variable called ``self.query`` which is a ``Sos.SqlQuery`` object. The 1000000 in the ``super()`` function sets the block size for this ``self.query`` object. An optimal block size is dependent on the query, however 1 million has been sufficiently performant to this point.

In the ``get_data`` function we create a select clause for the DSOS query by joining the metrics and schema variables. The ``self.get_where`` is a graf_analysis class function which takes filter parameters and makes an SQL-like where clause string with ``self.start`` and ``self.end`` as timestamp boundaries. There is also the orderby variable which we are setting as ``time_job_comp`` here. This refers to what index we should use when querying the database. Our SOS databases are setup to use permutations of ``timestamp``, ``job ID``, and ``component ID`` as multi-indices. Depending on your filter, you may want to use a different multi-index.

The ``self.get_all_data`` takes the Sos.SqlQuery object, ``self.query``, and calls ``self.query.next``. This returns a block size number of records that match the query from database defined by the cont variable. If there are more than a block size number of records, it continues calling ``self.query.next`` and appending the results to a pandas DataFrame until all data is returned.

Additional analysis can be added where the "Fun stuff here!" comment is.

With the example parameters specified in the last section, our select statement here would be ``select Active,MemFree from meminfo where timestamp > start and timestamp < end and job_id == 30 and component_id < 604 order_by time_job_comp``.

.. note::

  ``job_id`` and ``user_name`` must exist in the schema passed in for this command to work.

Testing an Analysis Module
--------------------------
This section goes over how to test your python analysis module as a user.

You do not need to query from the Grafana interface to test your module. Below is a simple code which mimics the Grafana pipeline and prints the JSON returned to Grafana.

.. note::

	**If Grafana and SOS are already installed on your system then please skip the `Required Scripts`_ section** and ask your system administrator where these scripts reside on the system so that you may copy all necessary python scripts and modules to your home directory, edit/modify exisiting python analysis modules and create new ones.


.. code-block:: bash

    export PYTHONPATH=/usr/bin/python:/<INSTALL_PATH>/lib/python<PYTHON_VERSION>/site-packages/
    export PATH=/usr/bin:/<INSTALL_PATH>/bin:/<INSTALL_PATH>/sbin::$PATH

Then you can imitate the Grafana query to call your analysis module using a python script such as:

.. code-block:: python

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

    x = model.get_data(['Active'])

    #fmt = table_formatter(x)
    fmt = time_series_formatter(x)
    x = fmt.ret_json()
    print(x)

* The ``model.get_data`` is where you can define the type of metrics to collect (in this case it is "Active"), what filters and extra parameters you want to add to your query. The syntax is as follows: ``(['<metric>'], filters=['job_id>0'], params='<variable>')``

* If you would like to query all metrics then replace ``Active`` with ``*``.
* To query a specific job_id: set ``job_id`` to you job_id with ``==``.
* To query from a specific time range: update the start time, ``time.time()-300`` and end time, ``time.time()`` to an epoch timestamp.
* To add a string metric, filter or parameter, you must include a double quote, ``"``, before and after the string (i.e. ``filters=['user=="myusername"']``)

.. note::

	The ``params`` can be any number or string that you want to define in your analysis module to better manage, output or analyze the data. For example, you can program your module to return specific analyses such as the average with ``params='analysis=average'`` by parsing the arguement, using ``if`` statements to determine what analysis to apply to the data and, to make things cleaner, a function to perform these calculations in.
/t

Required Scripts
*****************
The following scripts are needed to run the python analysis module. If these python scripts or modules **do not exist on your system and you have no way of accessing them** then please continue. Otherwise, you can skip this section

**If you do not have access to these existing scripts** then please create them in the same directory as your python analysis module.

.. note::

  If Grafana and SOS are installed on your system then please ask your system administator where these files reside on the system so that you can copy them to your home directory.

grafanaFormatter:

.. code-block:: python

  from sosdb import Sos
  from sosdb.DataSet import DataSet
  import numpy as np
  import pandas as pd
  import copy

  class RowIter(object):
      def __init__(self, dataSet):
          self.dset = dataSet
          self.limit = dataSet.get_series_size()
          self.row_no = 0

      def __iter__(self):
          return self

      def cvt(self, value):
          if type(value) == np.datetime64:
              return [ value.astype(np.int64) / 1000 ]
          return value

      def __next__(self):
          if self.row_no >= self.limit:
              raise StopIteration
          res = [ self.cvt(self.dset[[col, self.row_no]]) for col in range(0, self.dset.series_count) ]
          self.row_no += 1
          return res

  class DataFormatter(object):
      def __init__(self, data):
           self.result = []
           self.data = data
           self.fmt = type(self.data).__module__
           self.fmt_data = {
               'sosdb.DataSet' : self.fmt_dataset,
               'pandas.core.frame' : self.fmt_dataframe,
               'builtins' : self.fmt_builtins
           }

      def ret_json(self):
           return self.fmt_data[self.fmt]()

      def fmt_dataset(self):
          pass

      def fmt_dataframe(self):
          pass

      def fmt_builtins(self):
          pass

table_formatter:

.. code-block:: python

  from graf_analysis.grafanaFormatter import DataFormatter, RowIter
  from sosdb.DataSet import DataSet
  from sosdb import Sos
  import numpy as np
  import pandas as pd
  import copy

  class table_formatter(DataFormatter):
      def fmt_dataset(self):
          # Format data from sosdb DataSet object
          if self.data is None:
              return {"columns" : [{ "text" : "No papi jobs in time range" }] }

          self.result = { "type" : "table" }
          self.result["columns"] = [ { "text" : colName } for colName in self.data.series ]
          rows = []
          for row in RowIter(self.data):
              rows.append(row)
          self.result["rows"] = rows
          return self.result

      def fmt_dataframe(self):
          if self.data is None:
              return {"columns" : [{ "text" : "No papi jobs in time range" }] }

          self.result = { "type" : "table" }
          self.result["columns"] = [ { "text" : colName } for colName in self.data.columns ]
          self.result["rows"] = self.data.to_numpy()
          return self.result

      def fmt_builtins(self):
          if self.data is None:
              return { "columns" : [], "rows" : [], "type" : "table" }
          else:
              return self.data

time_series_formatter:

.. code-block:: python

  from graf_analysis.grafanaFormatter import DataFormatter
  from sosdb.DataSet import DataSet
  from sosdb import Sos
  import numpy as np
  import pandas as pd
  import copy

  class time_series_formatter(DataFormatter):
      def fmt_dataset(self):
          # timestamp is always last series
          if self.data is None:
              return [ { "target" : "", "datapoints" : [] } ]

          for series in self.data.series:
              if series == 'timestamp':
                  continue
              ds = DataSet()
              ds.append_series(self.data, series_list=[series, 'timestamp'])
              plt_dict = { "target" : series }
              plt_dict['datapoints'] = ds.tolist()
              self.result.append(plt_dict)
              del ds
          return self.result

      def fmt_dataframe(self):
          if self.data is None:
              return [ { "target" : "", "datapoints" : [] } ]

          for series in self.data.columns:
              if series == 'timestamp':
                  continue
              plt_dict = { "target" : series }
              plt_dict['datapoints'] = self.fmt_datapoints([series, 'timestamp'])
              self.result.append(plt_dict)
          return self.result

      def fmt_datapoints(self, series):
          ''' Format dataframe to output expected by grafana '''
          aSet = []
          for row_no in range(0, len(self.data)):
              aRow = []
              for col in series:
                  v = self.data[col].values[row_no]
                  typ = type(v)
                  if typ.__module__ == 'builtins':
                      pass
                  elif typ == np.ndarray or typ == np.string_ or typ == np.str_:
                      v = str(v)
                  elif typ == np.float32 or typ == np.float64:
                      v = float(v)
                  elif typ == np.int64 or typ == np.uint64:
                      v = int(v)
                  elif typ == np.int32 or typ == np.uint32:
                      v = int(v)
                  elif typ == np.int16 or typ == np.uint16:
                      v = int(v)
                  elif typ == np.datetime64:
                      # convert to milliseconds from microseconds
                      v = v.astype(np.int64) / int(1e6)
                  else:
                      raise ValueError("Unrecognized numpy type {0}".format(typ))
                  aRow.append(v)
              aSet.append(aRow)
          return aSet

      def fmt_builtins(self):
          if self.data is None:
              return [ { "target" : "", "datapoints" : [] } ]
          else:
              return self.data
