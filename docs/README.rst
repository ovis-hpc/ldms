Documentation
=============

Any new documentation will be formatted in ``.rst`` and added to this directory (with the exception of "man pages" explained below).

To enable RST linking, add the following directive at the top of the file:
   - ``.. _<file-name>``
   - Refer to any existing ``.rst`` files for formatting examples.

Man Page .RST File Formatting And Editing
-----------------------------------------

For any code or plugin-specific documentation (e.g., manual pages), please use the ``.rst`` format.

1. To edit existing man page ``.rst`` files, make changes within the ``ldms/`` directory where the corresponding code resides.
   - **Do not** edit or add files directly in the ``docs/rst_man/`` directory. This directory is automatically populated with ``.rst`` files when building Read the Docs.
3. When creating a new man page, use the following naming convention: ``<group/category>-<plugin-type>_<plugin-name>.rst``
   - Example: ``ldms-sampler_my-plugin.rst`` OR ``ldmsd-store_my-plugin.rst``
   - For non-plugin files, include the type (e.g., ``ldms/ldmsd``) at the beginning of the filename.
4. To enable RST linking, add the following directive at the top of the file:
   - ``.. _my-plugin``
   - Refer to existing man page ``.rst`` files for formatting examples.
5. Please follow the standard man page ``.rst`` formatting by including the following information at the top:
        =========
        LDMS MAN
        =========

        ---------------------------------------------
        Description of my-plugin
        ---------------------------------------------

        :Date: YYYY-MM-DD
        :Version: vX.X
        :Manual section: X
        :Manual group: LDMS/LDMSD <plugin-type>

Using Readthedocs
---------------------

To visualize and test your documentation changes on the offical [OVIS LDMS Documentation page](https://ovis-hpc.readthedocs.io/projects/ldms/en/latest/) before submitting a PR, please follow the steps below.

1. **Fork the repository**: Fork the repository where the docs are located to your own GitHub account.
2. **Create a Read the Docs account**: Sign up for a free Read the Docs account at [readthedocs.org](https://readthedocs.org/).
3. **Link the repository**:
    - After logging in, go to the **"Import a Project"** page on [Read the Docs](https://readthedocs.org/projects/).
    - Choose **"GitHub"** as the source and authorize Read the Docs to access your GitHub account.
    - Select your forked repository from the list.
4. **Specify the correct branch**:
    - In the project settings, you will be asked to specify the branch of your repository. Select the branch that contains the changes you've made to the documentation.
5. **Set the location of `.readthedocs.yml`**:
    - Read the Docs will need to know where your `.readthedocs.yml` file is located. In the project settings, ensure that you specify the **current directory** (`./`) as the location of the `.readthedocs.yml` file.
    - The `.readthedocs.yml` file controls the configuration of your documentation build process on Read the Docs (including things like Python dependencies, versioning, and build steps).
6. **Build the documentation**: After linking the project and selecting the correct branch, Read the Docs will automatically build the documentation. You can view the generated docs at the provided URL.

This will allow you to see your changes in real-time as you push updates to the branch.

Edits To Other Files
--------------------

If you are making changes to the project configurations (i.e. conf.py or requirements.txt), submit the PR and one of the admins will review the changes.

Using Local Machine
-----------------------

The documentation can be built with `make html` or `make man` and the generated files will be found in the `_build` directory.
If you instead want to test a build on your local machine, please install the following depencencies before running `make html`.

``` shell
pip install -r requirements.txt
pip install sphinx sphinx-rtd-theme
```

Optionally, you can run the following to build the documents:

``` shell
sphinx-build -b html docs/ docs/_build
```

Users may want to install these packages into a [Python Virtual Environment](https://docs.python.org/3/tutorial/venv.html)
