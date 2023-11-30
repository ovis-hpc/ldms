# SYNOPSIS
#
#   CYTHON_CHECK([CYTHON_MIN_VERSION], [PYTHON_VERSION], [action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   Look for variations on the cython binary name, including ones
#   that contain the PYTHON_VESRION parameter. For instance,
#   if PYTHON_VERSION is 3.6, look for cython-3.6, cython3.6, cython3, and cython
#   in that order. Each found binary is feature checked to determine
#   if its Cython version is greater than or equal to the supplied
#   CYTHON_MIN_VESRION parameter. The serach stops on the first binary
#   that meets this critia.
#
#   If this macro is successful, It will AC_SUBST the variable CYTHON
#   with the full path to the discovered binary. Also, "action-if-found"
#   is executed if supplied. If the macro fails to find cython,
#   "action-if-not-found" is executed if supplied.

AC_DEFUN([CYTHON_CHECK],[
 cython_min_version=$1
 python_version=$2
 found=untested
 python_major_version=$( echo ${python_version} | sed 's/\..*//' )
 AC_CACHE_CHECK([for a version of Cython >= ${cython_min_version}],
               [ac_cv_path_CYTHON],
               [AC_PATH_PROGS_FEATURE_CHECK([CYTHON],
                       [cython-${python_version} cython${python_version} cython${python_major_version} cython],
                       [cython_version=$(${ac_path_CYTHON} --version 2>&1 | sed -e 's/.*\s//')
                        AX_COMPARE_VERSION([${cython_version}],[ge],[$cython_min_version],
                       [ac_cv_path_CYTHON=$ac_path_CYTHON ac_path_CYTHON_found=:
		        found=true])],
                       [found=false])])
 # When CYTHON is specified (e.g. './configure CYTHON=/opt/cython/bin/cython3'),
 # 'ac_cv_path_CYTHON' variable is set to that value and the
 # AC_PATH_PROGS_FEATURE_CHECK skips the 'feature-test' part, which contains
 # version-checking logic. Hence, we handle that case here.
 AS_IF([ test "$found" = "untested" && test -n "$ac_cv_path_CYTHON" ], [
	 cython_version=$(${ac_cv_path_CYTHON} --version 2>&1 | sed -e 's/.*\s//')
	 AX_COMPARE_VERSION([${cython_version}],[ge],[$cython_min_version],
		 [found=true])
 ])
 AS_IF([test "$found" = "true"],
	[AC_SUBST([CYTHON], [$ac_cv_path_CYTHON])
	 m4_ifnblank([$3],[$3],[[:]])],
	[m4_ifnblank([$4],[$4],[[:]])])
])
