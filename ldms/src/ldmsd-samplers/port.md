Sampler Porting Status
======================

This is a list of all v4 samplers and their v5 porting status. The checked items
are ported and run-time tested, unless specified otherwise. All samplers that
depend on `/proc` or `/sys` files are accompanied with their Python unittest
file that uses `chroot` to emulate + test the parsing.

- [x] all_example
- [x] appinfo: only build test
  - [x] appinfo_lib: only build test
- [x] aries_mmr
- [x] aries_linkstatus
- [x] array_example: DEPRECATED by all_example.
- [x] clock
- [x] cray_power_sampler
- [x] cray_system_sampler
  - [x] dvs_sampler
  - [x] cray_gemini_r_sampler
  - [x] cray_aries_r_sampler
- [x] dstat
- [x] filesingle
- [x] fptrans
- [x] generic_sampler
- [x] grptest
- [ ] ~~hadoop~~ DEPRECATED
- [ ] ~~hfclock~~ DEPRECATED
- [ ] hweventpapi: TOM?
- [x] job_info
  - [x] job_info_slurm
- [x] kgnilnd
- [ ] kokkos: TOM?
- [ ] ~~ldms_jobid~~  DEPRECATED
- [x] llnl
  - [x] edac
- [x] lnet_stats
- [x] lustre
- [x] meminfo
- [x] msr_interlagos
- [x] opa2: need RUNTIME TEST
- [ ] ~~papi~~ deprecated by hweventpapi
- [x] perfevent
- [ ] ~~procdiskstats~~ deprecated by sysclassblock
- [x] sysclassblock
- [x] procinterrupts
- [x] procnetdev
- [x] procnfs
- [x] procsensors: need RUMTIME TEST
- [x] procstat
- [ ] ~~rapl~~ deprecated by hweventpapi
- [x] sampler_atasmart
- [x] shm: need RUNTIME TEST
- [x] slurm
  - [x] spank
- [x] switchx: only compile-test
- [ ] ~~switchx_eth~~ deprecated by switchx
- [x] synthetic
- [x] sysclassib
- [x] test_sampler
- [ ] ~~timer_base~~ DEPRECATED
- [ ] ~~tsampler~~ DEPRECATED
- [x] variable
- [x] vmstat
- [ ] ipmireader: NEW
- [ ] ipmisensors: NEW
- [ ] ibm_occ: NEW
