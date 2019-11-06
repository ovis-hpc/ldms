# Why this directory?

The preferred location of LDMS contributions is in the ldms/src/contrib where
contributions are directly integrated with the OVIS autotools build. 

That approach may not work for some, particularly developers
wanting or needing to maintain their own separate build system for legal or practical reasons.
For those developers and for other seeking a template of a project
that is dependent on but not part of LDMS, the third-plugins directory is provided.

The third-plugins directory provides a location where copies of third
party plugins with independent build systems can be dropped and loosely
integrated with the LDMS build by a shell script hook, ldms_build.sh.

The default example third-plugins/my_plugin provides a project template
for aspiring plugin writers that do not want to put their code in the tree just yet.

# How to contribute to the third-plugins directory.

* Open an issue on our web git and to discuss the plugin you want to create. Make sure there is not already a work in progress that you could help with or influence with your requirements.
* Document your plugin. This includes a man page, possibly a markdown file documenting design and other considerations outside the scope of a man page, and a test case script set for use on a single node with ldms-static-test.sh.
* Put it in a new subdirectory of ldms/src/third-plugins. 
  * Share it with us via a pull request when you have it working.
  * Or contact us through your issue page about other means for us to obtain and incorporate your code such as a git submodule.

# LDMS third-plugin contribution guidelines

* Names of contributions cannot contain a comma, but may contain a / that maps to a directory. Our plugin name-space is flat. If you are creating, for example, an improved meminfo plugin please call it meminfo_better or some such. If you work for yoyo, perhaps call it yoyo_meminfo.
* The LDMS developers are the final arbiters of what your directory and plugin will be named if there is a naming conflict with another plugin.
* Contributions must have a compatible open-source license and it must be located in a file named LICENSE at the top of your plugin directory. If your contribution incorporates source from other open-source libraries, the names of the libraries used and the locations of their open-source licenses must be listed a file LICENSES_THIRD_PARTY.
* Our top level build system will enable LDMS users and packagers to automatically build your contribution with the rest of LDMS only if your work uses the build approach documented below, implementing ldms_build.sh. 
* Contributions which do not provide build support that integrates with our build may still be included in our repository at our discretion, but the LDMS team will provide no support to users that want to build your plugin. Document your unsupported build process well. LDMS installs libraries and headers that can be used like most other third party libraries.

# Build Approaches

Our build system is managed with autotools (autoconf/automake/libtool).
We provide configuration hooks that enable users to build LDMSD with third party plugins in the third-plugins subdirectories by naming them at configure time. Your build will be invoked in an environment with variables indicating where LDMS is installed. Make sure your ldms_build.sh script uses these variables and does not assume LDMS is installed in standard system locations such as /usr/include or /usr/lib.

For example, if there are contributed plugins in directories named 'a', 'b/p1', and 'b/p2', the user can
configure ldms with "--enable-third-plugins=a,b/p2" to request the 'a' and 'p2' plugins.

## The bash build approach for my_plugin

This approach is for those using autotools, cmake, or other scripted systems.

You provide a script in a file, for example, in ldms/src/third-plugins/my_plugin/ldms_build.sh. 
After our build and install, we switch to your directory third-plugins/my_plugin and invoke './ldms_build.sh'. That script should do everything needed to configure, build, and install your plugin. The my_plugin example ldms_build.sh can be used as a template for other plugins.

The final exit code of your script should be 0 if installation succeeded and otherwise an error code. Your script should also display a prominent failure message explaining your failure and what the user should do about it (install a needed package, for example).

You can run your build entirely independently of our build if you have an already installed ldms (including development headers and scripts) and its bin/ directory is in your PATH. You can also build independently with an off-path ldms build if you define the pointers to the *-configvars.sh scripts:

  LDMS_CONFIG_SH=$libdir/ovis-ldms-configvars.sh LIB_CONFIG_SH=$libdir/ovis-lib-configvars.sh ./my_build.sh

# Build environment variables

LDMS exports its configuration information in C header format. The list of configure options is provided in the definition 'OVIS_LDMS_LDMS_CONFIG_ARGS' in $includedir/ovis-ldms-config.h. If your plugin needs raw configuration arguments, extract them from this definition. LDMS ignores any arguments it does not use.

LDMS exports its installation information in bash environment variable format.
The variable files are installed in $libdir/ovis-lib-configvars.sh and $libdir/ovis-ldms-configvars.sh. Examples of using them are included in the example contributed plugins. You can load the values in your GNU make file with include statements

  include $(LIB_CONFIG_SH)
  include $(LDMS_CONFIG_SH)

or you can load them in your bash scripts with the 'source' or '.' commands

  . $LIB_CONFIG_SH
  . $LDMS_CONFIG_SH

The environment and flags passed to LDMS configure are available in alternate useful
formats in $ovis_ldms_pkglibdir/ovis-ldms-configure-args and $ovis_ldms_pkglibdir/ovis-ldms-configure-env.

# Examples

The ovis/ldms/src/third-plugins contains an example of simple plugin building in my_plugin/.

To copy and modify it, you will need to change the lines (and similar ones) listed below to put in your plugin name.
Some files will need to be renamed so that my_plugin is replaced with the real name of $your_plugin.

  $your_plugin/src/$your_plugin.c:  #define SAMP "your_plugin"
  $your_plugin/src/Plugin_your_plugin.man:  .\" Manpage for Plugin_your_plugin
  $your_plugin/src/Makefile.am:  libyour_plugin_la_SOURCES = your_plugin.c
  $your_plugin/test/$your_plugin:  export plugname=$your_plugin
  $your_plugin/test/Makefile.am:  PLUGIN_NAME=$your_plugin
  $your_plugin/configure.ac:  AC_INIT($your_plugin, 1.0.0, foo@bar.com)
  $your_plugin/configure.ac:  AC_CONFIG_SRCDIR([m4/$your_plugin_top.m4])
  $your_plugin/configure.ac:  AX_PREFIX_CONFIG_H_G($your_plugin-config.h)
  $your_plugin/configure.ac:  OPTION_DEFAULT_ENABLE([$your_plugin], [ENABLE_MY_PLUGIN])
  $your_plugin/autogen.sh:  required=src/$your_plugin.c
  $your_plugin/Makefile.am:  EXTRA_DIST= m4/$your_plugin_top.m4



