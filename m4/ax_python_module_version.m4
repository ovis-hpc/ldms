# ============================================================================
#  http://www.gnu.org/software/autoconf-archive/ax_python_module_version.html
# ============================================================================
#
# SYNOPSIS
#
#   AX_PYTHON_MODULE_VERSION(modname, min_version[, python])
#
# DESCRIPTION
#
#   Checks for Python module with at least the given version.
#
#   Triggers an error if module is absent or present but at a lower version.
#   The third parameter can either be "python" for Python 2 or "python3" for
#   Python 3; defaults to Python 3.
#
# LICENSE
#
#   Copyright (c) 2015 Endless Mobile, Inc.; contributed by Philip Chimento <philip@endlessm.com> and Kurt von Laven <kurt@endlessm.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 2

AC_DEFUN([AX_PYTHON_MODULE_VERSION], [
    AX_PYTHON_MODULE([$1], [required], [$3])
    AC_MSG_CHECKING([for version $2 or higher of $1])

    python_fullversion=`${PYTHON} -c "import sys; print(sys.version)" | sed q`
    python_majorversion=`echo "$python_fullversion" | sed '[s/^\([0-9]*\).*/\1/]'`
    python_minorversion=`echo "$python_fullversion" | sed '[s/^[0-9]*\.\([0-9]*\).*/\1/]'`
    python_version=`echo "$python_fullversion" | sed '[s/^\([0-9]*\.[0-9]*\).*/\1/]'`

    # Reject unsupported Python versions as soon as practical.
    if test "$python_majorversion" -lt 3; then
      AC_MSG_ERROR([Python version $python_version is too old (version 3 or later is required)])
    fi
    # Determine which version class to use based on Python version
    if test "$python_majorversion" -gt 3 || (test "$python_majorversion" -eq 3 && test "$python_minorversion" -ge 12); then
        VERSION_CLASS="packaging.version"
        VERSION_MODULE="Version"
    else
        VERSION_CLASS="distutils.version"
        VERSION_MODULE="StrictVersion"
    fi

    # Check the module version using the selected version class
    mod_ver=`$PYTHON -c "import sys, $1; from $VERSION_CLASS import $VERSION_MODULE; sys.exit(print($VERSION_MODULE($1.__version__)))"`
    $PYTHON -c "import sys, $1; from $VERSION_CLASS import $VERSION_MODULE; sys.exit($VERSION_MODULE($1.__version__) < $VERSION_MODULE('$2'))" 2> /dev/null
    AS_IF([test $? -eq 0], [], [
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([You need at least version $2 of the $1 Python module. Version detected is $mod_ver.])
    ])
    AC_MSG_RESULT([yes])
])
