Documentation
=============

Any new documentation will be formatted in ``.rst`` and added to this directory (with the exception of "man pages" explained below).

To enable RST linking, add the following target at the top of the file:
   - ``.. _<file-name>:``
   - Refer to any existing ``.rst`` files for formatting examples.

Man Pages
---------

.RST File Formatting And Editing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For any code or plugin-specific documentation (e.g., manual pages), please use the ``.rst`` format.

1. To edit existing man page ``.rst`` files, make changes within the ``ldms/`` directory where the corresponding code resides.
   - **Do not** edit or add files directly in the ``docs/rst_man/`` directory. This directory is where symlinks of the ``.rst`` man files under ``ldms/`` are created when building Read the Docs.
3. When creating a new man page, use the following naming convention: ``<group/category>-<plugin-type>_<plugin-name>.rst``
   - Example: ``ldms-sampler_my-plugin.rst`` OR ``ldmsd-store_my-plugin.rst``
   - For non-plugin files, include the type (e.g., ``ldms_/ldmsd_``) at the beginning of the filename.
4. To enable RST linking, add the following target at the top of the file:
   - ``.. _my-plugin:``
   - Refer to existing man page ``.rst`` files for formatting examples.
5. Please follow the standard man page ``.rst`` formatting by including the following information at the top:

::
        .. _my-plugin:                                  <-------- Document target. Usually name of plugin or functionality. Same as MY-PLUGIN.

        ==========
        MY-PLUGIN                                       <-------- Title of document and name of plugin or functionality. Same as .._my-plugin:
        ==========

        -------------------------
        Description of my-plugin                        <-------- Brief description of plugin with minus signs (-) above and below to create a subsection header.
        -------------------------

        :Date: YYYY MM DD                               <-------- date of plugin release
        :Version: vX.X                                  <-------- (optional) version being documented
        :Manual section: X                              <-------- Unix man page section document belongs to
        :Manual group: LDMS/LDMSD <plugin-type>
               ^                        ^
               |                        |
               |                        |
        Grouping label to indicate    Category of plugin
        doc falls under LDMS or LDMSD

Generation (via rst2man)
^^^^^^^^^^^^^^^^^^^^^^^^^

During the build process, LDMS uses the ``rst2man`` program to convert ``.rst`` man page files into ``.man`` files.

This conversion is currently required for LDMS to build successfully. If ``rst2man`` is not installed or cannot be found, the build will fail with an error similar to:

::

    rst2man not found. Please install python3-docutils package.

At this time, there is **no flag to disable man page generation**. However, you can bypass the error and still build LDMS by setting ``RST2MAN`` to a no-op command:

::

    RST2MAN=: ./configure

This command tells the build system to treat ``rst2man`` as a do-nothing placeholder. The build will proceed, but the resulting ``.man`` pages will be **empty**.

.. note::

   This is a temporary workaround. In the future, a ``--disable-docs`` or similar flag may be added to provide more control over documentation generation.

Test Your Changes
-----------------

There are multiple ways to preview your documentation changes before merging them. We recommend using a **personal Read the Docs account** for testing, as it allows you to view the rendered results without affecting the main project. However, testing through a **pull request** is useful when you want to see a side-by-side comparison using Read the Docs' "diff" feature.

Personal Readthedocs Account
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To visualize and test your documentation changes on the offical `OVIS LDMS Documentation page <https://ovis-hpc.readthedocs.io/projects/ldms/en/latest/>`_, you can set up your own RTD account.

1. **Fork the repository**: Fork the repository where the docs are located to your own GitHub account.
2. **Create a Read the Docs account**: Sign up for a free Read the Docs account at `readthedocs.org <https://readthedocs.org/>`_.
3. **Link the repository**:
    - After logging in, go to the **"Import a Project"** page on `Read the Docs <https://readthedocs.org/projects/>`_.
    - Choose **"GitHub"** as the source and authorize Read the Docs to access your GitHub account.
    - Select your forked repository from the list.
4. **Specify the correct branch**:
    - In the project settings, you will be asked to specify the branch of your repository. Select the branch that contains the changes you've made to the documentation.
5. **Set the location of ``.readthedocs.yml``**:
    - Read the Docs will need to know where your ``.readthedocs.yml`` file is located. In the project settings, ensure that you specify the **current directory** (`./`) as the location of the ``.readthedocs.yml`` file.
    - The ``.readthedocs.yml`` file controls the configuration of your documentation build process on Read the Docs (including things like Python dependencies, versioning, and build steps).
6. **Build the documentation**: After linking the project and selecting the correct branch, Read the Docs will automatically build the documentation. You can view the generated docs at the provided URL.

This will allow you to see your changes in real-time as you push updates to the branch.

For a full walkthrough with screenshots, visit the `Read the Docs Tutorial <https://docs.readthedocs.com/platform/stable/tutorial/index.html#creating-a-read-the-docs-account>`_.

Pull Request Build Preview
^^^^^^^^^^^^^^^^^^^^^^^^^^^

To preview documentation changes through RTD's PR build preview feature:

1. **Submit a PR** with your changes to the official repository.
2. Read the Docs will automatically detect the pull request and generate a preview.
3. On the PR page, a comment from Read the Docs will appear with a link to the **preview documentation build**.
4. Click the link to review the rendered documentation, including a **side-by-side comparison** ("View docs as diff") between the current version and your proposed changes.

For more details, please visit the `Read the Docs PR Preview Documentation <https://docs.readthedocs.com/platform/stable/pull-requests.html#features>`_.

This is especially useful for maintainers to verify how changes will appear before merging.

Using Local Machine
^^^^^^^^^^^^^^^^^^^

The documentation can be built with ``make html`` or ``make man`` and the generated files will be found in the ``_build`` directory.
If you instead want to test a build on your local machine, please install the following depencencies before running ``make html``.

``` shell
pip install -r docs/requirements.txt
sphinx-build -b html docs/ docs/_build
```

or, if you are using make:

``` shell
make html
```

.. note::

        Even if the documentation builds successfully, the final appearance (especially images, plots, or custom themes) may differ from the online version. Always check the rendered result on Read the Docs when possible.


Edits To Other Files
--------------------

If your changes affect the documentation build process itself (e.g. editing ``conf.py``, ``.readthedocs.yml``, or ``requirements.txt``), please submit a PR as usual. An admin or maintainer will review the changes to ensure compatibility with the build system.
