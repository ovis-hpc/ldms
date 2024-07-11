OVIS-HPC LDMS Documentation
########################

This repository hosts all LDMS related documentation such as how-to tutorials, getting started with LDMS, docker-hub links, API's and much more. Documentation webpage can be found in the `LDMS readthedocs webpage <https://ovis-hpc.readthedocs.io/projects/ldms/en/latest/>`_

Contributing to ReadTheDocs
############################
Instructions and documentation on how to use ReadTheDocs can be found here:
`readthedocs Help Guide <https://sublime-and-sphinx-guide.readthedocs.io/en/latest/images.html>`_


* Clone the repository:

.. code-block:: RST

  > git clone git@github.com:<current-repo>/ovis-docs.git

* Add any existing file name(s) you will be editing to paper.lock

.. code-block:: RST

  > vi paper.lock
  <add Name | Date | File(s)>
  <username> | mm/dd | <filename>

* Make necessary changes, update paper.lock file and push to repo.

.. code-block:: RST

  > vi paper.lock
  <add Name | Date | File(s)>
  ## remove line
  > git add <files>
  > git commit -m "add message"
  > git push
  
Adding A New File 
******************
For any new RST files created, please include them in rtd/docs/src/index.rst under their corresponding sections. All RST files not included in index.rst will not populate on the offical webpage (e.g. readthedocs).

Paper Lock
************
This is for claiming any sections you are working on so there is no overlap.
Please USE paper.lock to indicate if you are editing an existing RST file.  


