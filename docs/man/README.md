# Creating Man Pages with .rst Files

This repository contains reStructuredText (.rst) files that are meant to be used as man pages. These files should be structured in a way that allows them to be included in the generated documentation as part of the project.

## Key Points for Creating Man Pages

### 1. Adding a Reference at the Top of Each File

Each man page `.rst` file **must** start with a reference to that file. This reference allows the man page to be correctly identified and linked to throughout the documentation. The reference is placed at the top of the file in the following format:

```rst
.. _<filename>:
```

For example, if you have a file called example.rst, the top of the file should contain:
```rst
.. _example:
```
The reference at the top of the file (.. _<filename>:) is used by Sphinx and other documentation generators to build the proper internal links between files. Without this reference, the man page would not be able to link to it properly, and navigation within the documentation would break. This ensures consistency and accessibility when working with multiple files and links.

### 2. Using :ref: for Cross-Referencing
In order to link to a man page from other .rst files or documentation, you will use the :ref: directive. The syntax for this directive is:
```rst
:ref:`name <filename>`
```
Where:
 - **`name`** is the anchor name you want to display (this can be different from the filename).
 - **`filename`** is the identifier of the target file, which is the part after the underscore (_) in the reference (i.e., from .. _<filename>:).
For example, to link to the example.rst file, you would write:
```rst
See the :ref:`example <example>` man page for more details.
```
This creates a hyperlink to the example.rst file and ensures that the correct page is linked when generating the final output.

For more information on cross-referencing in reStructuredText, you can visit the official Read the Docs documentation [here](https://docs.readthedocs.com/platform/stable/guides/cross-referencing-with-sphinx.html).
