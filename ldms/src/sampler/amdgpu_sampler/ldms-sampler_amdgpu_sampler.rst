===========================
ldms-sampler_amdgpu_sampler
===========================

:Date: 2026-11-17
:Version: OVIS-4
:Manual section: 7
:Manual group: LDMS Sampler Plugins

SYNOPSIS
=========

Within ldmsd_controller or in a configuration file:

| **load** name=amdgpu_sampler
| **config** name=amdgpu_sampler [ <attr> = <value> ... ]
| **start** name=amdgpu_sampler interval=<int> [ offset=<int> ]

DESCRIPTION
============

The **amdgpu_sampler** plugin collects metrics from AMD GPUs using the AMD SMI (System Management Interface) library.
This provides comprehensive monitoring of AMD GPU devices with support for ROCm >= 7.14.

The sampler automatically detects all AMD GPUs on the system and creates one LDMS set per GPU device.
Users can configure which specific AMD SMI fields to monitor by specifying a comma-separated list of field names or using convenient field aliases.

The plugin supports over 126 different GPU metrics organized by category including temperatures, power consumption,
clock frequencies, memory usage, utilization, PCIe bandwidth, XGMI interconnect metrics, and advanced MI300-specific fields.

CONFIGURATION ATTRIBUTE SYNTAX
================================

**name** =<sampler_name>
   |
   | Sampler instance name. Must be "amdgpu_sampler".

**schema** =<schema_name>
   |
   | Base name for the schema. Default: "amdgpu_sampler"

**instance** =<instance_name>
   |
   | Instance name prefix for the metric sets. Each GPU will create a set named
   | "<instance_name>/gpu<N>" where N is the GPU index. Required.

**producer** =<producer_name>
   |
   | Producer name. Required.

**component_id** =<component_id>
   |
   | Numeric component identifier. Required.

**fields** =<field_list>
   |
   | Comma-separated list of AMD SMI field names or field aliases. If not specified,
   | a default set of common fields is used (equivalent to "base" alias).
   |
   | See AVAILABLE FIELDS section below for the complete list of field names.
   | See FIELD ALIASES section below for convenient preset field groups.

**uid** =<user_id>
   |
   | Owner user ID for the metric sets.

**gid** =<group_id>
   |
   | Owner group ID for the metric sets.

**perm** =<permissions>
   |
   | Permission mode in octal (e.g., 0755) for the metric sets.

**job_set** =<job_set_name>
   |
   | Name of the job_set to use for associating job information.

**use_schema_hash** =<0|1>
   |
   | If set to 1, append a hash suffix to the schema name based on the selected fields.
   | This ensures schema uniqueness when different field configurations are used.
   | Default: 0 (no hash suffix)

AVAILABLE FIELDS
=================

The plugin supports 126+ AMD GPU metrics organized by category. Fields prefixed with
**derived:** are derived values calculated from native AMD SMI metrics rather than
direct hardware readings.

**Temperature Fields (°C):**
   - **temperature_edge** - GPU edge temperature
   - **temperature_hotspot** - GPU hotspot/junction temperature
   - **temperature_mem** - GPU memory temperature
   - **temperature_vrgfx** - Voltage regulator (GFX) temperature
   - **temperature_vrsoc** - Voltage regulator (SOC) temperature
   - **temperature_vrmem** - Voltage regulator (MEM) temperature
   - **temperature_hbm_0** through **temperature_hbm_3** - HBM memory temperatures (4 sensors)
   - **temperature_hbm_stack_0** through **temperature_hbm_stack_7** - MI300 HBM stack temperatures (8 stacks)
   - **temperature_mid_0** through **temperature_mid_3** - MI300 MID (Memory I/O Die) temperatures (4 dies)
   - **temperature_aid_0** through **temperature_aid_3** - MI300 AID (Accelerator I/O Die) temperatures (4 dies)

**Power Fields (Watts):**
   - **power_socket** - Average socket power consumption
   - **power_gfx** - Graphics engine power (APU only)
   - **power_soc** - SOC power consumption (APU only)
   - **current_socket_power** - Current socket power consumption

**Energy Fields:**
   - **energy_accumulator** - Raw energy accumulator tick count (ticks)
   - **derived:gpu_energy** - GPU energy consumption in microjoules (uses hardware-specific counter_resolution)

**Clock Frequency Fields - Current (MHz):**
   - **clock_gfxclk** - Graphics engine clock frequency
   - **clock_socclk** - SOC clock frequency
   - **clock_uclk** - Memory/UCLK clock frequency
   - **clock_vclk0** - Video decode clock 0
   - **clock_dclk0** - Display clock 0
   - **clock_vclk1** - Video decode clock 1
   - **clock_dclk1** - Display clock 1
   - **clock_fclk** - Fabric/Infinity Fabric clock (APU only)

**Clock Frequency Fields - Average (MHz):**
   - **avg_clock_gfxclk** - Average graphics clock frequency
   - **avg_clock_socclk** - Average SOC clock frequency
   - **avg_clock_uclk** - Average memory clock frequency
   - **avg_clock_vclk0** - Average video decode clock 0
   - **avg_clock_dclk0** - Average display clock 0
   - **avg_clock_vclk1** - Average video decode clock 1
   - **avg_clock_dclk1** - Average display clock 1

**MI300-Specific Clock Fields (MHz):**
   - **uclk_aid_0** through **uclk_aid_3** - Per-AID memory clocks (4 AIDs)
   - **socclk_mid_0** through **socclk_mid_3** - Per-MID SOC clocks (4 MIDs)

**Utilization Fields (%):**
   - **utilization_gfx** - Graphics engine utilization percentage
   - **utilization_mem** - Memory controller utilization percentage
   - **utilization_mm** - Multimedia engine utilization percentage

**Memory Fields:**
   - **memory_usage** - Used GPU memory (bytes)
   - **memory_total** - Total GPU memory (bytes)

**Voltage Fields (millivolts):**
   - **voltage_vddgfx** - VDDGFX voltage rail
   - **voltage_soc** - SOC voltage rail
   - **voltage_mem** - Memory voltage rail

**PCIe Link Fields:**
   - **pcie_link_width** - PCIe link width (lanes)
   - **pcie_link_speed** - PCIe link speed (0.1 GT/s units)
   - **pcie_bandwidth** - PCIe bandwidth utilization (bytes/sec)
   - **pcie_throughput_tx** - PCIe transmit throughput (bytes/sec)
   - **pcie_throughput_rx** - PCIe receive throughput (bytes/sec)
   - **pcie_bandwidth_acc** - Accumulated PCIe bandwidth (GB/s)
   - **pcie_bandwidth_inst** - Instantaneous PCIe bandwidth (GB/s)

**PCIe Error Counters:**
   - **pcie_l0_to_recov_count** - PCIe L0 to recovery transition count
   - **pcie_replay_count** - PCIe replay count
   - **pcie_replay_rover_count** - PCIe replay rollover count
   - **pcie_nak_sent_count** - PCIe NAK packets sent
   - **pcie_nak_rcvd_count** - PCIe NAK packets received
   - **pcie_lc_perf_other_end_recovery** - PCIe lane recovery counter

**XGMI/Infinity Fabric Fields:**
   - **xgmi_link_width** - XGMI link width (GB/s)
   - **xgmi_link_speed** - XGMI link speed (GB/s)
   - **xgmi_read_data_0** through **xgmi_read_data_7** - Per-link read data (KB, 8 links)
   - **xgmi_write_data_0** through **xgmi_write_data_7** - Per-link write data (KB, 8 links)
   - **xgmi_link_status_0** through **xgmi_link_status_7** - Per-link status (8 links)

**Video/Multimedia Activity Fields (%):**
   - **vcn_activity_0** through **vcn_activity_3** - VCN (video decoder/encoder) activity (4 instances)
   - **jpeg_activity_0** through **jpeg_activity_3** - JPEG decoder activity (4 instances)

**Activity Accumulators:**
   - **gfx_activity_acc** - Accumulated graphics activity counter
   - **mem_activity_acc** - Accumulated memory activity counter

**Throttling Status Fields:**
   - **throttle_status** - Throttling status bitmask
   - **indep_throttle_status** - Independent throttle status
   - **accumulation_counter** - Throttle accumulation counter
   - **prochot_residency** - PROCHOT throttle residency counter
   - **ppt_residency** - Power throttle residency counter
   - **socket_thm_residency** - Socket thermal throttle residency
   - **vr_thm_residency** - VR thermal throttle residency
   - **hbm_thm_residency** - HBM thermal throttle residency

**Other Fields:**
   - **fan_speed** - Fan speed (RPM)
   - **gfxclk_lock_status** - Graphics clock lock status
   - **firmware_timestamp** - Firmware timestamp (10 nanosecond units)
   - **system_clock_counter** - System clock counter (nanoseconds)
   - **num_partition** - Number of GPU partitions
   - **vram_max_bandwidth** - Maximum VRAM bandwidth (GB/s)

FIELD ALIASES
==============

For convenience, the plugin provides predefined field aliases that expand to commonly used field groups.
All aliases must be prefixed with **alias:** to differentiate them from regular field names.

**alias:base** (default if no fields specified)
   | temperature_edge, temperature_hotspot, temperature_mem,
   | power_socket, clock_gfxclk, clock_uclk,
   | utilization_gfx, memory_usage, memory_total

**alias:all_temps**
   | temperature_edge, temperature_hotspot, temperature_mem,
   | temperature_vrgfx, temperature_vrsoc, temperature_vrmem

**alias:all_clocks**
   | clock_gfxclk, clock_socclk, clock_uclk,
   | clock_vclk0, clock_dclk0, clock_vclk1, clock_dclk1, clock_fclk

**alias:avg_clocks**
   | avg_clock_gfxclk, avg_clock_socclk, avg_clock_uclk,
   | avg_clock_vclk0, avg_clock_dclk0, avg_clock_vclk1, avg_clock_dclk1

**alias:all_power**
   | power_socket, power_gfx, power_soc, energy_accumulator, current_socket_power

**alias:all_voltage**
   | voltage_vddgfx, voltage_soc, voltage_mem

**alias:all_util**
   | utilization_gfx, utilization_mem, utilization_mm

**alias:hbm_temps**
   | temperature_hbm_0, temperature_hbm_1, temperature_hbm_2, temperature_hbm_3

**alias:xgmi_read**
   | xgmi_read_data_0 through xgmi_read_data_7 (all 8 XGMI read data counters)

**alias:xgmi_write**
   | xgmi_write_data_0 through xgmi_write_data_7 (all 8 XGMI write data counters)

**alias:xgmi_all**
   | xgmi_link_width, xgmi_link_speed, plus all alias:xgmi_read and alias:xgmi_write fields

**alias:xgmi_link_status**
   | xgmi_link_status_0 through xgmi_link_status_7 (all 8 XGMI link status fields)

**alias:vcn_activity**
   | vcn_activity_0, vcn_activity_1, vcn_activity_2, vcn_activity_3

**alias:pcie_counters**
   | pcie_bandwidth_acc, pcie_bandwidth_inst, pcie_l0_to_recov_count,
   | pcie_replay_count, pcie_replay_rover_count, pcie_nak_sent_count, pcie_nak_rcvd_count

**alias:throttle**
   | throttle_status, indep_throttle_status, accumulation_counter,
   | prochot_residency, ppt_residency, socket_thm_residency,
   | vr_thm_residency, hbm_thm_residency

**alias:mi300_temps**
   | All MI300-specific temperature sensors: HBM stacks (8), MIDs (4), AIDs (4)

**alias:mi300_clocks**
   | All MI300-specific clock frequencies: per-AID UClks (4), per-MID SOCClks (4)

EXAMPLES
=========

Example with default fields::

  load name=amdgpu_sampler
  config name=amdgpu_sampler producer=node1 instance=node1/amdsmi component_id=1
  start name=amdgpu_sampler interval=1000000

Example with specific fields::

  load name=amdgpu_sampler
  config name=amdgpu_sampler producer=node1 instance=node1/amdsmi component_id=1 \\
         fields=temperature_hotspot,power_socket,clock_gfxclk,utilization_gfx,memory_usage
  start name=amdgpu_sampler interval=1000000

Example using field aliases::

  load name=amdgpu_sampler
  config name=amdgpu_sampler producer=node1 instance=node1/amdsmi component_id=1 \\
         fields=alias:all_temps,alias:all_power
  start name=amdgpu_sampler interval=1000000

Example combining aliases and individual fields::

  load name=amdgpu_sampler
  config name=amdgpu_sampler producer=node1 instance=node1/amdsmi component_id=1 \\
         fields=alias:base,pcie_throughput_tx,pcie_throughput_rx,fan_speed
  start name=amdgpu_sampler interval=1000000

Example for MI300 monitoring::

  load name=amdgpu_sampler
  config name=amdgpu_sampler producer=node1 instance=node1/mi300 component_id=1 \\
         fields=alias:base,alias:mi300_temps,alias:mi300_clocks,alias:xgmi_all
  start name=amdgpu_sampler interval=1000000

Example with schema hash for unique schemas::

  load name=amdgpu_sampler
  config name=amdgpu_sampler producer=node1 instance=node1/amdsmi component_id=1 \\
         fields=temperature_hotspot,power_socket use_schema_hash=1
  start name=amdgpu_sampler interval=1000000

SCHEMA
=======

The schema contains:

- **device** (char[24]) - GPU identifier (e.g., "gpu0", "gpu1")
- One U64 metric for each configured field, using the field name as the metric name

By default, the schema name is exactly as specified in the **schema** parameter.
If **use_schema_hash=1** is set, a hash suffix based on the configured fields is
appended. For example, if schema="amdgpu_sampler", fields="base", and
use_schema_hash=1, the actual schema name might be "amdgpu_sampler_a1b2c3d4".

The hash ensures that different field configurations result in different schema names,
preventing schema conflicts when multiple instances monitor different metrics.

SCHEMA NAME COMPUTATION
========================

When **use_schema_hash=1** is set, the schema name includes a hash suffix to ensure
uniqueness based on the selected fields. You can precompute the schema name using the
**ldms_amdgpu_schema_name** utility::

  ldms_amdgpu_schema_name schema=amdgpu_sampler fields=temperature_hotspot,power_socket use_schema_hash=1

This is useful for configuring storage plugins that need to know the exact schema name.
See **ldms_amdgpu_schema_name(1)** for more details.

MULTIPLE INSTANCES
===================

The plugin supports running multiple instances concurrently with different configurations.
Each instance maintains independent state and can monitor different field sets with
different sampling intervals.

This allows for:

- Different sampling intervals for different metric sets
- Separate monitoring of critical metrics vs. comprehensive diagnostics
- Independent instance lifecycle management (start/stop/reconfigure separately)
- Flexible schema organization with different field groupings

Example with multiple instances::

  # Fast sampling of critical thermal and power metrics
  load name=amdgpu_fast
  config name=amdgpu_fast producer=node1 instance=node1/fast component_id=1 \\
         fields=temperature_hotspot,power_socket,throttle_status
  start name=amdgpu_fast interval=100000

  # Comprehensive sampling at lower frequency
  load name=amdgpu_detailed
  config name=amdgpu_detailed producer=node1 instance=node1/detailed component_id=2 \\
         fields=alias:all_temps,alias:all_clocks,alias:all_power,alias:all_util,alias:xgmi_all use_schema_hash=1
  start name=amdgpu_detailed interval=1000000

  # PCIe bandwidth monitoring
  load name=amdgpu_pcie
  config name=amdgpu_pcie producer=node1 instance=node1/pcie component_id=3 \\
         fields=pcie_throughput_tx,pcie_throughput_rx,alias:pcie_counters
  start name=amdgpu_pcie interval=500000

Each instance can be started, stopped, and reconfigured independently without affecting
the other instances. All instances can monitor the same GPU devices simultaneously.

NOTES
======

- The sampler requires the AMD SMI library (libamd_smi) to be installed
- Requires ROCm >=7.14 AMD SMI library
- Root or appropriate permissions may be required to access GPU metrics
- The sampler automatically detects all AMD GPUs on the system
- Each GPU gets its own LDMS set with the same schema
- Metric collection is synchronous (no background threads)
- If a field is not supported by the hardware, it will return 0 or ENOTSUP
- Not all fields are available on all GPU models:
  - APU-only fields: power_gfx, power_soc, clock_fclk
  - MI300-specific fields: HBM stack temps, MID/AID temps and clocks
  - HBM-specific fields require HBM memory
  - XGMI fields require multi-GPU systems with Infinity Fabric links
- Multiple instances can monitor the same GPUs with different field configurations
- Field selection affects schema structure - use schema hashing when needed

BUGS
=====

No known bugs.

SEE ALSO
=========

ldmsd(8), ldms_quickstart(7), ldms_amdgpu_schema_name(1), ldms-sampler_rdc_sampler(7)
