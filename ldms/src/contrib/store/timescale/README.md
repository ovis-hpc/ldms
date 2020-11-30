# timescaledb_store_plugin

To deploy this plugin, do the following:

```
cd ovis/ldms/src/contrib/store
git clone https://github.com/SJTU-HPC/timescaledb_store_plugin.git
mv timescaledb_store_plugin timescale
```

Add the following lines to the Makefile.am under ovis/ldms/src/contrib/store:

```
if ENABLE_TIMESCALE_STORE
    MAYBE_TIMESCALE_STORE = timescale
endif
SUBDIRS += $(MAYBE_TIMESCALE_STORE)
```

Add the following lines to configure.ac under ovis/:

```
OPTION_DEFAULT_ENABLE([timescale-store],[ENABLE_TIMESCALE_STORE])

ldms/src/contrib/store/timescale/Makefile
```
