.. _appsysfusion:

.. image:: ../images/appsysfusion.png
   :width: 600
   :height: 250
   :align: center

AppSysFusion
============

AppSysFusion provides analysis and visualization capabilities aimed at serving insights from HPC monitoring data gathered with LDMS, though could be generalized outside of that scope.
It combines a Grafana front-end with a Django back-end to perform in-query analyses on raw data and return transformed information back to the end user.
By performing in-query analyses, only data of interest to the end-user is operated on rather than the entirety of the dataset for all analyses for all time.
This saves significant computation and storage resources with the penalty of slightly higher query times.
These analyses are modular python scripts that can be easily added or changed to suit evolving needs.
The current implementation is aimed at querying DSOS databases containing LDMS data, though efforts are in progress to abstract this functionality out to other databases and datatypes.

.. toctree::
   :maxdepth: 2

   asf-quickstart
   asf-tutorial
   grafanapanel
   grafanause
   pyanalysis
