/* -*- c-basic-offset: 8 -*- */
/* Copyright 2026 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

/**
 * amdgpu_sampler - AMD GPU monitoring via AMD SMI library
 *
 * This sampler collects GPU metrics from AMD devices using the AMD System
 * Management Interface (SMI) library. Users configure which metrics to monitor,
 * and the plugin automatically detects all AMD GPUs and creates one LDMS set
 * per GPU.
 *
 * Key features:
 * - Configurable field selection (temperature, power, clocks, memory, etc.)
 * - Multi-instance support (multiple configs with different sampling rates)
 * - Per-GPU metric sets
 * - Synchronous sampling (no background threads)
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"
#include "ldmsd_plugattr.h"
#include "coll/fnv_hash.h"
#include "ovis_util/dstring.h"
#include <amd_smi/amdsmi.h>

/* ========================================================================
 * Constants and Configuration Limits
 * ======================================================================== */

#define SAMP "amdgpu_sampler"
#define INST_LOG(inst, lvl, fmt, ...) do { \
        ovis_log(inst->mylog, (lvl), fmt, ##__VA_ARGS__); \
} while (0)

enum {
        AMDGPU_MAX_SCHEMA_NAME_BASE = 32,
        AMDGPU_MAX_GPU_DEVICES = 32,
        AMDGPU_MAX_CONFIGURED_FIELDS = 128,
        AMDGPU_DEVICE_NAME_LENGTH = 24
};

/* ========================================================================
 * Field Definitions and Descriptors
 * ======================================================================== */

/**
 * enum amdgpu_field_id - AMD SMI metric field identifiers
 *
 * Each field corresponds to a specific GPU metric that can be sampled.
 * Fields are organized by gpu_metrics version where they were introduced.
 */
enum amdgpu_field_id {
        /* v1.0 Base - Temperature fields (C) */
        AMDGPU_FIELD_TEMPERATURE_EDGE = 0,
        AMDGPU_FIELD_TEMPERATURE_HOTSPOT,
        AMDGPU_FIELD_TEMPERATURE_MEM,
        AMDGPU_FIELD_TEMPERATURE_VRGFX,
        AMDGPU_FIELD_TEMPERATURE_VRSOC,
        AMDGPU_FIELD_TEMPERATURE_VRMEM,

        /* v1.0 Base - Power fields (W) and Energy (15.259uJ per 1ns) */
        AMDGPU_FIELD_POWER_SOCKET,
        AMDGPU_FIELD_POWER_GFX,
        AMDGPU_FIELD_POWER_SOC,
        AMDGPU_FIELD_ENERGY_ACCUMULATOR,

        /* v1.0 Base - Current clock fields (MHz) */
        AMDGPU_FIELD_CLOCK_GFXCLK,
        AMDGPU_FIELD_CLOCK_SOCCLK,
        AMDGPU_FIELD_CLOCK_UCLK,
        AMDGPU_FIELD_CLOCK_VCLK0,
        AMDGPU_FIELD_CLOCK_DCLK0,
        AMDGPU_FIELD_CLOCK_VCLK1,
        AMDGPU_FIELD_CLOCK_DCLK1,
        AMDGPU_FIELD_CLOCK_FCLK,

        /* v1.0 Base - Average clock fields (MHz) */
        AMDGPU_FIELD_AVG_CLOCK_GFXCLK,
        AMDGPU_FIELD_AVG_CLOCK_SOCCLK,
        AMDGPU_FIELD_AVG_CLOCK_UCLK,
        AMDGPU_FIELD_AVG_CLOCK_VCLK0,
        AMDGPU_FIELD_AVG_CLOCK_DCLK0,
        AMDGPU_FIELD_AVG_CLOCK_VCLK1,
        AMDGPU_FIELD_AVG_CLOCK_DCLK1,

        /* v1.0 Base - Utilization fields (%) */
        AMDGPU_FIELD_UTILIZATION_GFX,
        AMDGPU_FIELD_UTILIZATION_MEM,
        AMDGPU_FIELD_UTILIZATION_MM,

        /* v1.0 Base - PCIe link info */
        AMDGPU_FIELD_PCIE_LINK_WIDTH,
        AMDGPU_FIELD_PCIE_LINK_SPEED,

        /* v1.0 Base - Other */
        AMDGPU_FIELD_THROTTLE_STATUS,
        AMDGPU_FIELD_FAN_SPEED,
        AMDGPU_FIELD_VOLTAGE_VDDGFX,

        /* v1.1 - Accumulated activity */
        AMDGPU_FIELD_GFX_ACTIVITY_ACC,
        AMDGPU_FIELD_MEM_ACTIVITY_ACC,

        /* v1.1 - HBM temperatures (C) - 4 instances */
        AMDGPU_FIELD_TEMPERATURE_HBM_0,
        AMDGPU_FIELD_TEMPERATURE_HBM_1,
        AMDGPU_FIELD_TEMPERATURE_HBM_2,
        AMDGPU_FIELD_TEMPERATURE_HBM_3,

        /* v1.2 - Timestamps */
        AMDGPU_FIELD_FIRMWARE_TIMESTAMP,
        AMDGPU_FIELD_SYSTEM_CLOCK_COUNTER,

        /* v1.3 - Additional voltages (mV) */
        AMDGPU_FIELD_VOLTAGE_SOC,
        AMDGPU_FIELD_VOLTAGE_MEM,
        AMDGPU_FIELD_INDEP_THROTTLE_STATUS,

        /* v1.4 - Power */
        AMDGPU_FIELD_CURRENT_SOCKET_POWER,

        /* v1.4 - VCN activity (%) - 4 instances */
        AMDGPU_FIELD_VCN_ACTIVITY_0,
        AMDGPU_FIELD_VCN_ACTIVITY_1,
        AMDGPU_FIELD_VCN_ACTIVITY_2,
        AMDGPU_FIELD_VCN_ACTIVITY_3,

        /* v1.4 - Clock lock status */
        AMDGPU_FIELD_GFXCLK_LOCK_STATUS,

        /* v1.4 - XGMI link info */
        AMDGPU_FIELD_XGMI_LINK_WIDTH,
        AMDGPU_FIELD_XGMI_LINK_SPEED,

        /* v1.4 - PCIe bandwidth and counters */
        AMDGPU_FIELD_PCIE_BANDWIDTH_ACC,
        AMDGPU_FIELD_PCIE_BANDWIDTH_INST,
        AMDGPU_FIELD_PCIE_L0_TO_RECOV_COUNT,
        AMDGPU_FIELD_PCIE_REPLAY_COUNT,
        AMDGPU_FIELD_PCIE_REPLAY_ROVER_COUNT,

        /* v1.4 - XGMI read data (KB) - 8 links */
        AMDGPU_FIELD_XGMI_READ_DATA_0,
        AMDGPU_FIELD_XGMI_READ_DATA_1,
        AMDGPU_FIELD_XGMI_READ_DATA_2,
        AMDGPU_FIELD_XGMI_READ_DATA_3,
        AMDGPU_FIELD_XGMI_READ_DATA_4,
        AMDGPU_FIELD_XGMI_READ_DATA_5,
        AMDGPU_FIELD_XGMI_READ_DATA_6,
        AMDGPU_FIELD_XGMI_READ_DATA_7,

        /* v1.4 - XGMI write data (KB) - 8 links */
        AMDGPU_FIELD_XGMI_WRITE_DATA_0,
        AMDGPU_FIELD_XGMI_WRITE_DATA_1,
        AMDGPU_FIELD_XGMI_WRITE_DATA_2,
        AMDGPU_FIELD_XGMI_WRITE_DATA_3,
        AMDGPU_FIELD_XGMI_WRITE_DATA_4,
        AMDGPU_FIELD_XGMI_WRITE_DATA_5,
        AMDGPU_FIELD_XGMI_WRITE_DATA_6,
        AMDGPU_FIELD_XGMI_WRITE_DATA_7,

        /* v1.5 - JPEG activity (%) - first 4 of 32 instances (most common) */
        AMDGPU_FIELD_JPEG_ACTIVITY_0,
        AMDGPU_FIELD_JPEG_ACTIVITY_1,
        AMDGPU_FIELD_JPEG_ACTIVITY_2,
        AMDGPU_FIELD_JPEG_ACTIVITY_3,

        /* v1.5 - PCIe NAK counters */
        AMDGPU_FIELD_PCIE_NAK_SENT_COUNT,
        AMDGPU_FIELD_PCIE_NAK_RCVD_COUNT,

        /* v1.6 - Throttle residency accumulators */
        AMDGPU_FIELD_ACCUMULATION_COUNTER,
        AMDGPU_FIELD_PROCHOT_RESIDENCY,
        AMDGPU_FIELD_PPT_RESIDENCY,
        AMDGPU_FIELD_SOCKET_THM_RESIDENCY,
        AMDGPU_FIELD_VR_THM_RESIDENCY,
        AMDGPU_FIELD_HBM_THM_RESIDENCY,

        /* v1.6 - Other */
        AMDGPU_FIELD_NUM_PARTITION,
        AMDGPU_FIELD_PCIE_LC_PERF_OTHER_END_RECOVERY,

        /* v1.7 - VRAM bandwidth */
        AMDGPU_FIELD_VRAM_MAX_BANDWIDTH,

        /* v1.7 - XGMI link status - 8 links */
        AMDGPU_FIELD_XGMI_LINK_STATUS_0,
        AMDGPU_FIELD_XGMI_LINK_STATUS_1,
        AMDGPU_FIELD_XGMI_LINK_STATUS_2,
        AMDGPU_FIELD_XGMI_LINK_STATUS_3,
        AMDGPU_FIELD_XGMI_LINK_STATUS_4,
        AMDGPU_FIELD_XGMI_LINK_STATUS_5,
        AMDGPU_FIELD_XGMI_LINK_STATUS_6,
        AMDGPU_FIELD_XGMI_LINK_STATUS_7,

        /* v1.9 - MI300 HBM stack temperatures (C) - 8 stacks */
        AMDGPU_FIELD_TEMPERATURE_HBM_STACK_0,
        AMDGPU_FIELD_TEMPERATURE_HBM_STACK_1,
        AMDGPU_FIELD_TEMPERATURE_HBM_STACK_2,
        AMDGPU_FIELD_TEMPERATURE_HBM_STACK_3,
        AMDGPU_FIELD_TEMPERATURE_HBM_STACK_4,
        AMDGPU_FIELD_TEMPERATURE_HBM_STACK_5,
        AMDGPU_FIELD_TEMPERATURE_HBM_STACK_6,
        AMDGPU_FIELD_TEMPERATURE_HBM_STACK_7,

        /* v1.9 - MI300 MID temperatures (C) - 4 MIDs */
        AMDGPU_FIELD_TEMPERATURE_MID_0,
        AMDGPU_FIELD_TEMPERATURE_MID_1,
        AMDGPU_FIELD_TEMPERATURE_MID_2,
        AMDGPU_FIELD_TEMPERATURE_MID_3,

        /* v1.9 - MI300 AID temperatures (C) - 4 AIDs */
        AMDGPU_FIELD_TEMPERATURE_AID_0,
        AMDGPU_FIELD_TEMPERATURE_AID_1,
        AMDGPU_FIELD_TEMPERATURE_AID_2,
        AMDGPU_FIELD_TEMPERATURE_AID_3,

        /* v1.9 - MI300 per-AID memory clocks (MHz) - 4 AIDs */
        AMDGPU_FIELD_UCLK_AID_0,
        AMDGPU_FIELD_UCLK_AID_1,
        AMDGPU_FIELD_UCLK_AID_2,
        AMDGPU_FIELD_UCLK_AID_3,

        /* v1.9 - MI300 per-MID SOC clocks (MHz) - 4 MIDs */
        AMDGPU_FIELD_SOCCLK_MID_0,
        AMDGPU_FIELD_SOCCLK_MID_1,
        AMDGPU_FIELD_SOCCLK_MID_2,
        AMDGPU_FIELD_SOCCLK_MID_3,

        /* Separate API fields (not from gpu_metrics) */
        AMDGPU_FIELD_MEMORY_USAGE,
        AMDGPU_FIELD_MEMORY_TOTAL,
        AMDGPU_FIELD_PCIE_BANDWIDTH,
        AMDGPU_FIELD_PCIE_THROUGHPUT_TX,
        AMDGPU_FIELD_PCIE_THROUGHPUT_RX,

        /* Computed fields */
        AMDGPU_FIELD_GPU_ENERGY,  /* energy_accumulator converted to microjoules */

        AMDGPU_FIELD_MAX
};

/**
 * struct amdgpu_field_desc - Field descriptor
 */
struct amdgpu_field_desc {
        enum amdgpu_field_id id;
        const char *name;
        const char *unit;
        enum ldms_value_type type;
};

/**
 * struct amdgpu_inst - Plugin instance state
 */
struct amdgpu_inst {
        ovis_log_t mylog;
        pthread_mutex_t lock;

        /* LDMS integration */
        base_data_t base;
        char schema_name_base[AMDGPU_MAX_SCHEMA_NAME_BASE];
        char *schema_name;
        int first_index;
        int metric_offset;

        /* AMD SMI state */
        uint32_t meta_done;
        uint32_t num_devices;
        amdsmi_processor_handle device_handles[AMDGPU_MAX_GPU_DEVICES];
        ldms_set_t devset[AMDGPU_MAX_GPU_DEVICES];
        uint32_t num_sets;

        /* Per-GPU sampling cache - fetched once per sample cycle */
        amdsmi_gpu_metrics_t gpu_metrics_cache[AMDGPU_MAX_GPU_DEVICES];
        uint8_t gpu_metrics_valid[AMDGPU_MAX_GPU_DEVICES];  /* 1 if cache is valid for this cycle */

        /* PCIe throughput cache */
        uint64_t pcie_tx_cache[AMDGPU_MAX_GPU_DEVICES];
        uint64_t pcie_rx_cache[AMDGPU_MAX_GPU_DEVICES];
        uint8_t pcie_throughput_valid[AMDGPU_MAX_GPU_DEVICES];  /* 1 if cache is valid */

        /* Field configuration */
        enum amdgpu_field_id field_ids[AMDGPU_MAX_CONFIGURED_FIELDS];
        uint32_t num_fields;
        int use_schema_hash;  /* Append hash to schema name (default: 0) */

        /* Sampling optimization flags - computed at configuration time */
        bool needs_gpu_metrics;       /* At least one field from gpu_metrics API */
        bool needs_pcie_throughput;   /* At least one TX/RX field */
};

typedef struct amdgpu_inst *amdgpu_inst_t;

/* Forward declarations */
static amdgpu_inst_t inst_new(ovis_log_t mylog);
static void inst_delete(amdgpu_inst_t inst);

/* Field descriptor table */
static const struct amdgpu_field_desc field_descriptors[] = {
        /* v1.0 - Temperature fields (C) */
        { AMDGPU_FIELD_TEMPERATURE_EDGE, "temperature_edge", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HOTSPOT, "temperature_hotspot", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_MEM, "temperature_mem", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_VRGFX, "temperature_vrgfx", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_VRSOC, "temperature_vrsoc", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_VRMEM, "temperature_vrmem", "C", LDMS_V_U64 },

        /* v1.0 - Power fields (W) and energy */
        { AMDGPU_FIELD_POWER_SOCKET, "power_socket", "W", LDMS_V_U64 },
        { AMDGPU_FIELD_POWER_GFX, "power_gfx", "W", LDMS_V_U64 },
        { AMDGPU_FIELD_POWER_SOC, "power_soc", "W", LDMS_V_U64 },
        { AMDGPU_FIELD_ENERGY_ACCUMULATOR, "energy_accumulator", "ticks", LDMS_V_U64 },

        /* v1.0 - Current clock fields (MHz) */
        { AMDGPU_FIELD_CLOCK_GFXCLK, "clock_gfxclk", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_CLOCK_SOCCLK, "clock_socclk", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_CLOCK_UCLK, "clock_uclk", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_CLOCK_VCLK0, "clock_vclk0", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_CLOCK_DCLK0, "clock_dclk0", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_CLOCK_VCLK1, "clock_vclk1", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_CLOCK_DCLK1, "clock_dclk1", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_CLOCK_FCLK, "clock_fclk", "MHz", LDMS_V_U64 },

        /* v1.0 - Average clock fields (MHz) */
        { AMDGPU_FIELD_AVG_CLOCK_GFXCLK, "avg_clock_gfxclk", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_AVG_CLOCK_SOCCLK, "avg_clock_socclk", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_AVG_CLOCK_UCLK, "avg_clock_uclk", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_AVG_CLOCK_VCLK0, "avg_clock_vclk0", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_AVG_CLOCK_DCLK0, "avg_clock_dclk0", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_AVG_CLOCK_VCLK1, "avg_clock_vclk1", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_AVG_CLOCK_DCLK1, "avg_clock_dclk1", "MHz", LDMS_V_U64 },

        /* v1.0 - Utilization fields (%) */
        { AMDGPU_FIELD_UTILIZATION_GFX, "utilization_gfx", "%", LDMS_V_U64 },
        { AMDGPU_FIELD_UTILIZATION_MEM, "utilization_mem", "%", LDMS_V_U64 },
        { AMDGPU_FIELD_UTILIZATION_MM, "utilization_mm", "%", LDMS_V_U64 },

        /* v1.0 - PCIe link info */
        { AMDGPU_FIELD_PCIE_LINK_WIDTH, "pcie_link_width", "lanes", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_LINK_SPEED, "pcie_link_speed", "0.1GT/s", LDMS_V_U64 },

        /* v1.0 - Other */
        { AMDGPU_FIELD_THROTTLE_STATUS, "throttle_status", "", LDMS_V_U64 },
        { AMDGPU_FIELD_FAN_SPEED, "fan_speed", "RPM", LDMS_V_U64 },
        { AMDGPU_FIELD_VOLTAGE_VDDGFX, "voltage_vddgfx", "mV", LDMS_V_U64 },

        /* v1.1 - Accumulated activity */
        { AMDGPU_FIELD_GFX_ACTIVITY_ACC, "gfx_activity_acc", "", LDMS_V_U64 },
        { AMDGPU_FIELD_MEM_ACTIVITY_ACC, "mem_activity_acc", "", LDMS_V_U64 },

        /* v1.1 - HBM temperatures (C) */
        { AMDGPU_FIELD_TEMPERATURE_HBM_0, "temperature_hbm_0", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_1, "temperature_hbm_1", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_2, "temperature_hbm_2", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_3, "temperature_hbm_3", "C", LDMS_V_U64 },

        /* v1.2 - Timestamps */
        { AMDGPU_FIELD_FIRMWARE_TIMESTAMP, "firmware_timestamp", "10ns", LDMS_V_U64 },
        { AMDGPU_FIELD_SYSTEM_CLOCK_COUNTER, "system_clock_counter", "ns", LDMS_V_U64 },

        /* v1.3 - Additional voltages (mV) */
        { AMDGPU_FIELD_VOLTAGE_SOC, "voltage_soc", "mV", LDMS_V_U64 },
        { AMDGPU_FIELD_VOLTAGE_MEM, "voltage_mem", "mV", LDMS_V_U64 },
        { AMDGPU_FIELD_INDEP_THROTTLE_STATUS, "indep_throttle_status", "", LDMS_V_U64 },

        /* v1.4 - Power */
        { AMDGPU_FIELD_CURRENT_SOCKET_POWER, "current_socket_power", "W", LDMS_V_U64 },

        /* v1.4 - VCN activity (%) */
        { AMDGPU_FIELD_VCN_ACTIVITY_0, "vcn_activity_0", "%", LDMS_V_U64 },
        { AMDGPU_FIELD_VCN_ACTIVITY_1, "vcn_activity_1", "%", LDMS_V_U64 },
        { AMDGPU_FIELD_VCN_ACTIVITY_2, "vcn_activity_2", "%", LDMS_V_U64 },
        { AMDGPU_FIELD_VCN_ACTIVITY_3, "vcn_activity_3", "%", LDMS_V_U64 },

        /* v1.4 - Clock lock status */
        { AMDGPU_FIELD_GFXCLK_LOCK_STATUS, "gfxclk_lock_status", "", LDMS_V_U64 },

        /* v1.4 - XGMI link info */
        { AMDGPU_FIELD_XGMI_LINK_WIDTH, "xgmi_link_width", "GB/s", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_LINK_SPEED, "xgmi_link_speed", "GB/s", LDMS_V_U64 },

        /* v1.4 - PCIe bandwidth and counters */
        { AMDGPU_FIELD_PCIE_BANDWIDTH_ACC, "pcie_bandwidth_acc", "GB/s", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_BANDWIDTH_INST, "pcie_bandwidth_inst", "GB/s", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_L0_TO_RECOV_COUNT, "pcie_l0_to_recov_count", "", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_REPLAY_COUNT, "pcie_replay_count", "", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_REPLAY_ROVER_COUNT, "pcie_replay_rover_count", "", LDMS_V_U64 },

        /* v1.4 - XGMI read data (KB) */
        { AMDGPU_FIELD_XGMI_READ_DATA_0, "xgmi_read_data_0", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_READ_DATA_1, "xgmi_read_data_1", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_READ_DATA_2, "xgmi_read_data_2", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_READ_DATA_3, "xgmi_read_data_3", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_READ_DATA_4, "xgmi_read_data_4", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_READ_DATA_5, "xgmi_read_data_5", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_READ_DATA_6, "xgmi_read_data_6", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_READ_DATA_7, "xgmi_read_data_7", "KB", LDMS_V_U64 },

        /* v1.4 - XGMI write data (KB) */
        { AMDGPU_FIELD_XGMI_WRITE_DATA_0, "xgmi_write_data_0", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_WRITE_DATA_1, "xgmi_write_data_1", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_WRITE_DATA_2, "xgmi_write_data_2", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_WRITE_DATA_3, "xgmi_write_data_3", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_WRITE_DATA_4, "xgmi_write_data_4", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_WRITE_DATA_5, "xgmi_write_data_5", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_WRITE_DATA_6, "xgmi_write_data_6", "KB", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_WRITE_DATA_7, "xgmi_write_data_7", "KB", LDMS_V_U64 },

        /* v1.5 - JPEG activity (%) */
        { AMDGPU_FIELD_JPEG_ACTIVITY_0, "jpeg_activity_0", "%", LDMS_V_U64 },
        { AMDGPU_FIELD_JPEG_ACTIVITY_1, "jpeg_activity_1", "%", LDMS_V_U64 },
        { AMDGPU_FIELD_JPEG_ACTIVITY_2, "jpeg_activity_2", "%", LDMS_V_U64 },
        { AMDGPU_FIELD_JPEG_ACTIVITY_3, "jpeg_activity_3", "%", LDMS_V_U64 },

        /* v1.5 - PCIe NAK counters */
        { AMDGPU_FIELD_PCIE_NAK_SENT_COUNT, "pcie_nak_sent_count", "", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_NAK_RCVD_COUNT, "pcie_nak_rcvd_count", "", LDMS_V_U64 },

        /* v1.6 - Throttle residency accumulators */
        { AMDGPU_FIELD_ACCUMULATION_COUNTER, "accumulation_counter", "", LDMS_V_U64 },
        { AMDGPU_FIELD_PROCHOT_RESIDENCY, "prochot_residency", "", LDMS_V_U64 },
        { AMDGPU_FIELD_PPT_RESIDENCY, "ppt_residency", "", LDMS_V_U64 },
        { AMDGPU_FIELD_SOCKET_THM_RESIDENCY, "socket_thm_residency", "", LDMS_V_U64 },
        { AMDGPU_FIELD_VR_THM_RESIDENCY, "vr_thm_residency", "", LDMS_V_U64 },
        { AMDGPU_FIELD_HBM_THM_RESIDENCY, "hbm_thm_residency", "", LDMS_V_U64 },

        /* v1.6 - Other */
        { AMDGPU_FIELD_NUM_PARTITION, "num_partition", "", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_LC_PERF_OTHER_END_RECOVERY, "pcie_lc_perf_other_end_recovery", "", LDMS_V_U64 },

        /* v1.7 - VRAM bandwidth */
        { AMDGPU_FIELD_VRAM_MAX_BANDWIDTH, "vram_max_bandwidth", "GB/s", LDMS_V_U64 },

        /* v1.7 - XGMI link status */
        { AMDGPU_FIELD_XGMI_LINK_STATUS_0, "xgmi_link_status_0", "", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_LINK_STATUS_1, "xgmi_link_status_1", "", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_LINK_STATUS_2, "xgmi_link_status_2", "", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_LINK_STATUS_3, "xgmi_link_status_3", "", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_LINK_STATUS_4, "xgmi_link_status_4", "", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_LINK_STATUS_5, "xgmi_link_status_5", "", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_LINK_STATUS_6, "xgmi_link_status_6", "", LDMS_V_U64 },
        { AMDGPU_FIELD_XGMI_LINK_STATUS_7, "xgmi_link_status_7", "", LDMS_V_U64 },

        /* v1.9 - MI300 HBM stack temperatures (C) */
        { AMDGPU_FIELD_TEMPERATURE_HBM_STACK_0, "temperature_hbm_stack_0", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_STACK_1, "temperature_hbm_stack_1", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_STACK_2, "temperature_hbm_stack_2", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_STACK_3, "temperature_hbm_stack_3", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_STACK_4, "temperature_hbm_stack_4", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_STACK_5, "temperature_hbm_stack_5", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_STACK_6, "temperature_hbm_stack_6", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_HBM_STACK_7, "temperature_hbm_stack_7", "C", LDMS_V_U64 },

        /* v1.9 - MI300 MID temperatures (C) */
        { AMDGPU_FIELD_TEMPERATURE_MID_0, "temperature_mid_0", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_MID_1, "temperature_mid_1", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_MID_2, "temperature_mid_2", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_MID_3, "temperature_mid_3", "C", LDMS_V_U64 },

        /* v1.9 - MI300 AID temperatures (C) */
        { AMDGPU_FIELD_TEMPERATURE_AID_0, "temperature_aid_0", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_AID_1, "temperature_aid_1", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_AID_2, "temperature_aid_2", "C", LDMS_V_U64 },
        { AMDGPU_FIELD_TEMPERATURE_AID_3, "temperature_aid_3", "C", LDMS_V_U64 },

        /* v1.9 - MI300 per-AID memory clocks (MHz) */
        { AMDGPU_FIELD_UCLK_AID_0, "uclk_aid_0", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_UCLK_AID_1, "uclk_aid_1", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_UCLK_AID_2, "uclk_aid_2", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_UCLK_AID_3, "uclk_aid_3", "MHz", LDMS_V_U64 },

        /* v1.9 - MI300 per-MID SOC clocks (MHz) */
        { AMDGPU_FIELD_SOCCLK_MID_0, "socclk_mid_0", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_SOCCLK_MID_1, "socclk_mid_1", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_SOCCLK_MID_2, "socclk_mid_2", "MHz", LDMS_V_U64 },
        { AMDGPU_FIELD_SOCCLK_MID_3, "socclk_mid_3", "MHz", LDMS_V_U64 },

        /* Separate API fields (not from gpu_metrics) */
        { AMDGPU_FIELD_MEMORY_USAGE, "memory_usage", "bytes", LDMS_V_U64 },
        { AMDGPU_FIELD_MEMORY_TOTAL, "memory_total", "bytes", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_BANDWIDTH, "pcie_bandwidth", "bytes/s", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_THROUGHPUT_TX, "pcie_throughput_tx", "bytes/s", LDMS_V_U64 },
        { AMDGPU_FIELD_PCIE_THROUGHPUT_RX, "pcie_throughput_rx", "bytes/s", LDMS_V_U64 },

        /* Computed fields */
        { AMDGPU_FIELD_GPU_ENERGY, "derived:gpu_energy", "uJ", LDMS_V_U64 },

        /* Terminator */
        { AMDGPU_FIELD_MAX, NULL, NULL, LDMS_V_NONE }
};

/* Default field set */
static const char *default_fields[] = {
        "temperature_edge",
        "temperature_hotspot",
        "temperature_mem",
        "power_socket",
        "clock_gfxclk",
        "clock_uclk",
        "utilization_gfx",
        "memory_usage",
        "memory_total",
        NULL
};

/* Field aliases - commonly used field groups */
#define ALIAS_BASE_FIELDS \
        "temperature_edge,temperature_hotspot,temperature_mem," \
        "power_socket,clock_gfxclk,clock_uclk," \
        "utilization_gfx,memory_usage,memory_total"

#define ALIAS_ALL_TEMPS \
        "temperature_edge,temperature_hotspot,temperature_mem," \
        "temperature_vrgfx,temperature_vrsoc,temperature_vrmem"

#define ALIAS_ALL_CLOCKS \
        "clock_gfxclk,clock_socclk,clock_uclk," \
        "clock_vclk0,clock_dclk0,clock_vclk1,clock_dclk1,clock_fclk"

#define ALIAS_AVG_CLOCKS \
        "avg_clock_gfxclk,avg_clock_socclk,avg_clock_uclk," \
        "avg_clock_vclk0,avg_clock_dclk0,avg_clock_vclk1,avg_clock_dclk1"

#define ALIAS_ALL_POWER \
        "power_socket,power_gfx,power_soc,energy_accumulator,current_socket_power"

#define ALIAS_ALL_VOLTAGE \
        "voltage_vddgfx,voltage_soc,voltage_mem"

#define ALIAS_ALL_UTIL \
        "utilization_gfx,utilization_mem,utilization_mm"

#define ALIAS_HBM_TEMPS \
        "temperature_hbm_0,temperature_hbm_1,temperature_hbm_2,temperature_hbm_3"

#define ALIAS_XGMI_READ \
        "xgmi_read_data_0,xgmi_read_data_1,xgmi_read_data_2,xgmi_read_data_3," \
        "xgmi_read_data_4,xgmi_read_data_5,xgmi_read_data_6,xgmi_read_data_7"

#define ALIAS_XGMI_WRITE \
        "xgmi_write_data_0,xgmi_write_data_1,xgmi_write_data_2,xgmi_write_data_3," \
        "xgmi_write_data_4,xgmi_write_data_5,xgmi_write_data_6,xgmi_write_data_7"

#define ALIAS_XGMI_ALL \
        "xgmi_link_width,xgmi_link_speed," \
        ALIAS_XGMI_READ "," ALIAS_XGMI_WRITE

#define ALIAS_XGMI_LINK_STATUS \
        "xgmi_link_status_0,xgmi_link_status_1,xgmi_link_status_2,xgmi_link_status_3," \
        "xgmi_link_status_4,xgmi_link_status_5,xgmi_link_status_6,xgmi_link_status_7"

#define ALIAS_VCN_ACTIVITY \
        "vcn_activity_0,vcn_activity_1,vcn_activity_2,vcn_activity_3"

#define ALIAS_PCIE_COUNTERS \
        "pcie_bandwidth_acc,pcie_bandwidth_inst,pcie_l0_to_recov_count," \
        "pcie_replay_count,pcie_replay_rover_count,pcie_nak_sent_count,pcie_nak_rcvd_count"

#define ALIAS_THROTTLE \
        "throttle_status,indep_throttle_status," \
        "accumulation_counter,prochot_residency,ppt_residency," \
        "socket_thm_residency,vr_thm_residency,hbm_thm_residency"

#define ALIAS_MI300_TEMPS \
        "temperature_hbm_stack_0,temperature_hbm_stack_1,temperature_hbm_stack_2,temperature_hbm_stack_3," \
        "temperature_hbm_stack_4,temperature_hbm_stack_5,temperature_hbm_stack_6,temperature_hbm_stack_7," \
        "temperature_mid_0,temperature_mid_1,temperature_mid_2,temperature_mid_3," \
        "temperature_aid_0,temperature_aid_1,temperature_aid_2,temperature_aid_3"

#define ALIAS_MI300_CLOCKS \
        "uclk_aid_0,uclk_aid_1,uclk_aid_2,uclk_aid_3," \
        "socclk_mid_0,socclk_mid_1,socclk_mid_2,socclk_mid_3"

struct field_alias {
        const char *key;
        const char *value;
};

static const struct field_alias field_aliases[] = {
        { "alias:base", ALIAS_BASE_FIELDS },
        { "alias:all_temps", ALIAS_ALL_TEMPS },
        { "alias:all_clocks", ALIAS_ALL_CLOCKS },
        { "alias:avg_clocks", ALIAS_AVG_CLOCKS },
        { "alias:all_power", ALIAS_ALL_POWER },
        { "alias:all_voltage", ALIAS_ALL_VOLTAGE },
        { "alias:all_util", ALIAS_ALL_UTIL },
        { "alias:hbm_temps", ALIAS_HBM_TEMPS },
        { "alias:xgmi_read", ALIAS_XGMI_READ },
        { "alias:xgmi_write", ALIAS_XGMI_WRITE },
        { "alias:xgmi_all", ALIAS_XGMI_ALL },
        { "alias:xgmi_link_status", ALIAS_XGMI_LINK_STATUS },
        { "alias:vcn_activity", ALIAS_VCN_ACTIVITY },
        { "alias:pcie_counters", ALIAS_PCIE_COUNTERS },
        { "alias:throttle", ALIAS_THROTTLE },
        { "alias:mi300_temps", ALIAS_MI300_TEMPS },
        { "alias:mi300_clocks", ALIAS_MI300_CLOCKS },
        { NULL, NULL }
};

/* ========================================================================
 * Field Lookup and Resolution
 * ======================================================================== */

static const char *resolve_field_alias(const char *key)
{
        const struct field_alias *alias = field_aliases;
        while (alias->key) {
                if (strcmp(alias->key, key) == 0)
                        return alias->value;
                alias++;
        }
        return NULL;
}

static const struct amdgpu_field_desc *find_field_by_name(const char *name)
{
        const struct amdgpu_field_desc *fd = field_descriptors;
        while (fd->name) {
                if (strcmp(fd->name, name) == 0)
                        return fd;
                fd++;
        }
        return NULL;
}

static const struct amdgpu_field_desc *find_field_by_id(enum amdgpu_field_id id)
{
        const struct amdgpu_field_desc *fd = field_descriptors;
        while (fd->name) {
                if (fd->id == id)
                        return fd;
                fd++;
        }
        return NULL;
}

static int parse_field_list(const char *fields_str, enum amdgpu_field_id *field_ids,
                            uint32_t *num_fields, uint32_t max_fields, ovis_log_t mylog)
{
        char *fields_copy = strdup(fields_str);
        if (!fields_copy)
                return ENOMEM;

        *num_fields = 0;
        char *token, *saveptr;
        token = strtok_r(fields_copy, ",", &saveptr);
        while (token) {
                /* Trim whitespace */
                while (*token == ' ' || *token == '\t')
                        token++;
                char *end = token + strlen(token) - 1;
                while (end > token && (*end == ' ' || *end == '\t'))
                        *end-- = '\0';

                /* Check if this token is an alias */
                const char *alias_expansion = resolve_field_alias(token);
                if (alias_expansion) {
                        /* Recursively parse the expanded alias */
                        uint32_t alias_num_fields = 0;
                        enum amdgpu_field_id alias_fields[AMDGPU_MAX_CONFIGURED_FIELDS];
                        int rc = parse_field_list(alias_expansion, alias_fields,
                                                 &alias_num_fields, AMDGPU_MAX_CONFIGURED_FIELDS, mylog);
                        if (rc) {
                                free(fields_copy);
                                return rc;
                        }
                        /* Add expanded fields to result (skip duplicates) */
                        for (uint32_t i = 0; i < alias_num_fields; i++) {
                                /* Check if already added */
                                bool duplicate = false;
                                for (uint32_t j = 0; j < *num_fields; j++) {
                                        if (field_ids[j] == alias_fields[i]) {
                                                duplicate = true;
                                                break;
                                        }
                                }
                                if (duplicate)
                                        continue;

                                if (*num_fields >= max_fields) {
                                        ovis_log(mylog, OVIS_LERROR,
                                                SAMP ": too many fields (max %d)\n", max_fields);
                                        free(fields_copy);
                                        return EINVAL;
                                }
                                field_ids[(*num_fields)++] = alias_fields[i];
                        }
                } else {
                        /* Not an alias, treat as field name */
                        const struct amdgpu_field_desc *fd = find_field_by_name(token);
                        if (!fd) {
                                ovis_log(mylog, OVIS_LERROR, SAMP ": unknown field '%s'\n", token);
                                free(fields_copy);
                                return EINVAL;
                        }

                        /* Check if already added */
                        bool duplicate = false;
                        for (uint32_t j = 0; j < *num_fields; j++) {
                                if (field_ids[j] == fd->id) {
                                        duplicate = true;
                                        break;
                                }
                        }
                        if (duplicate) {
                                token = strtok_r(NULL, ",", &saveptr);
                                continue;
                        }

                        if (*num_fields >= max_fields) {
                                ovis_log(mylog, OVIS_LERROR, SAMP ": too many fields (max %d)\n",
                                        max_fields);
                                free(fields_copy);
                                return EINVAL;
                        }

                        field_ids[(*num_fields)++] = fd->id;
                }

                token = strtok_r(NULL, ",", &saveptr);
        }

        free(fields_copy);
        return 0;
}

/* ========================================================================
 * Schema Generation
 * ======================================================================== */

static uint32_t compute_schema_hash(amdgpu_inst_t inst)
{
        dstring_t ds;
        dstr_init2(&ds, 256);

        for (uint32_t i = 0; i < inst->num_fields; i++) {
                const struct amdgpu_field_desc *fd = find_field_by_id(inst->field_ids[i]);
                if (fd) {
                        dstrcat(&ds, fd->name, DSTRING_ALL);
                        dstrcat(&ds, ",", 1);
                }
        }

        uint32_t hash = fnv_hash_a1_32(dstrval(&ds), ds.length, 0);
        dstr_free(&ds);
        return hash;
}

static int generate_schema_name(amdgpu_inst_t inst)
{
        inst->schema_name = malloc(AMDGPU_MAX_SCHEMA_NAME_BASE + 16);
        if (!inst->schema_name) {
                INST_LOG(inst, OVIS_LERROR, "Failed to allocate schema name\n");
                return ENOMEM;
        }

        if (inst->use_schema_hash) {
                uint32_t hash = compute_schema_hash(inst);
                snprintf(inst->schema_name, AMDGPU_MAX_SCHEMA_NAME_BASE + 16,
                        "%s_%08x", inst->schema_name_base, hash);
        } else {
                snprintf(inst->schema_name, AMDGPU_MAX_SCHEMA_NAME_BASE + 16,
                        "%s", inst->schema_name_base);
        }
        return 0;
}

/* ========================================================================
 * AMD SMI Hardware Initialization
 * ======================================================================== */

static const char *amdsmi_status_string(amdsmi_status_t status)
{
        const char *status_string = NULL;
        amdsmi_status_t result = amdsmi_status_code_to_string(status, &status_string);

        if (result == AMDSMI_STATUS_SUCCESS && status_string) {
                return status_string;
        }

        /* Fallback if the library function fails */
        return "UNKNOWN";
}

static int init_amdsmi_library(amdgpu_inst_t inst)
{
        amdsmi_status_t result = amdsmi_init(AMDSMI_INIT_AMD_GPUS);
        if (result != AMDSMI_STATUS_SUCCESS) {
                INST_LOG(inst, OVIS_LERROR, "Failed to initialize AMD SMI: %s\n",
                        amdsmi_status_string(result));
                return EINVAL;
        }
        return 0;
}

static int enumerate_gpu_sockets(amdgpu_inst_t inst,
                                amdsmi_socket_handle **handles, uint32_t *count)
{
        amdsmi_status_t result;

        result = amdsmi_get_socket_handles(count, NULL);
        if (result != AMDSMI_STATUS_SUCCESS || *count == 0) {
                INST_LOG(inst, OVIS_LERROR, "Failed to enumerate sockets: %d\n", result);
                return ENODEV;
        }

        *handles = calloc(*count, sizeof(amdsmi_socket_handle));
        if (!*handles) {
                INST_LOG(inst, OVIS_LERROR, "Failed to allocate socket handle array\n");
                return ENOMEM;
        }

        result = amdsmi_get_socket_handles(count, *handles);
        if (result != AMDSMI_STATUS_SUCCESS) {
                INST_LOG(inst, OVIS_LERROR, "Failed to get socket handles: %d\n", result);
                free(*handles);
                *handles = NULL;
                return EINVAL;
        }

        return 0;
}

static int enumerate_gpus_on_sockets(amdgpu_inst_t inst,
                                    amdsmi_socket_handle *sockets,
                                    uint32_t socket_count)
{
        amdsmi_status_t result;

        inst->num_devices = 0;
        for (uint32_t i = 0; i < socket_count && inst->num_devices < AMDGPU_MAX_GPU_DEVICES; i++) {
                uint32_t proc_count = AMDGPU_MAX_GPU_DEVICES - inst->num_devices;
                amdsmi_processor_handle *proc_handles = &inst->device_handles[inst->num_devices];

                result = amdsmi_get_processor_handles_by_type(
                        sockets[i],
                        AMDSMI_PROCESSOR_TYPE_AMD_GPU,
                        proc_handles,
                        &proc_count
                );
                if (result == AMDSMI_STATUS_SUCCESS) {
                        inst->num_devices += proc_count;
                }
        }

        if (inst->num_devices == 0) {
                INST_LOG(inst, OVIS_LERROR, "No AMD GPUs found\n");
                return ENODEV;
        }

        INST_LOG(inst, OVIS_LINFO, "Found %d AMD GPU(s)\n", inst->num_devices);

        /* Test which GPUs support metrics to provide better diagnostics */
        for (uint32_t i = 0; i < inst->num_devices; i++) {
                amdsmi_gpu_metrics_t metrics;
                amdsmi_status_t result = amdsmi_get_gpu_metrics_info(inst->device_handles[i], &metrics);
                if (result != AMDSMI_STATUS_SUCCESS) {
                        INST_LOG(inst, OVIS_LWARNING,
                                "GPU %d does not support gpu_metrics (%s) - metrics will be zero\n",
                                i, amdsmi_status_string(result));
                }
        }

        return 0;
}

static int hardware_init(amdgpu_inst_t inst)
{
        int rc;
        amdsmi_socket_handle *sockets = NULL;
        uint32_t socket_count = 0;

        rc = init_amdsmi_library(inst);
        if (rc)
                return rc;

        rc = enumerate_gpu_sockets(inst, &sockets, &socket_count);
        if (rc)
                goto cleanup;

        rc = enumerate_gpus_on_sockets(inst, sockets, socket_count);

cleanup:
        free(sockets);
        if (rc) {
                amdsmi_shut_down();
                inst->num_devices = 0;
        }
        return rc;
}

/* ========================================================================
 * Field Sampling
 * ======================================================================== */

static bool is_gpu_metrics_field(enum amdgpu_field_id field_id)
{
        /* Only fields NOT from gpu_metrics structure (separate APIs or computed) */
        switch (field_id) {
        case AMDGPU_FIELD_MEMORY_USAGE:
        case AMDGPU_FIELD_MEMORY_TOTAL:
        case AMDGPU_FIELD_PCIE_BANDWIDTH:
        case AMDGPU_FIELD_PCIE_THROUGHPUT_TX:
        case AMDGPU_FIELD_PCIE_THROUGHPUT_RX:
        case AMDGPU_FIELD_GPU_ENERGY:
                return false;
        default:
                return true;  /* All other fields come from gpu_metrics */
        }
}

static int extract_gpu_metrics_field(const amdsmi_gpu_metrics_t *metrics,
                                    enum amdgpu_field_id field_id, uint64_t *value)
{
        switch (field_id) {
        /* v1.0 - Temperature fields */
        case AMDGPU_FIELD_TEMPERATURE_EDGE:      *value = metrics->temperature_edge; break;
        case AMDGPU_FIELD_TEMPERATURE_HOTSPOT:   *value = metrics->temperature_hotspot; break;
        case AMDGPU_FIELD_TEMPERATURE_MEM:       *value = metrics->temperature_mem; break;
        case AMDGPU_FIELD_TEMPERATURE_VRGFX:     *value = metrics->temperature_vrgfx; break;
        case AMDGPU_FIELD_TEMPERATURE_VRSOC:     *value = metrics->temperature_vrsoc; break;
        case AMDGPU_FIELD_TEMPERATURE_VRMEM:     *value = metrics->temperature_vrmem; break;

        /* v1.0 - Power fields */
        case AMDGPU_FIELD_POWER_SOCKET:          *value = metrics->average_socket_power; break;
        case AMDGPU_FIELD_POWER_GFX:             /* APU-only field - not in base gpu_metrics */ return ENOTSUP;
        case AMDGPU_FIELD_POWER_SOC:             /* APU-only field - not in base gpu_metrics */ return ENOTSUP;
        case AMDGPU_FIELD_ENERGY_ACCUMULATOR:    *value = metrics->energy_accumulator; break;

        /* v1.0 - Current clocks */
        case AMDGPU_FIELD_CLOCK_GFXCLK:          *value = metrics->current_gfxclk; break;
        case AMDGPU_FIELD_CLOCK_SOCCLK:          *value = metrics->current_socclk; break;
        case AMDGPU_FIELD_CLOCK_UCLK:            *value = metrics->current_uclk; break;
        case AMDGPU_FIELD_CLOCK_VCLK0:           *value = metrics->current_vclk0; break;
        case AMDGPU_FIELD_CLOCK_DCLK0:           *value = metrics->current_dclk0; break;
        case AMDGPU_FIELD_CLOCK_VCLK1:           *value = metrics->current_vclk1; break;
        case AMDGPU_FIELD_CLOCK_DCLK1:           *value = metrics->current_dclk1; break;
        case AMDGPU_FIELD_CLOCK_FCLK:            /* APU-only field - not in base gpu_metrics */ return ENOTSUP;

        /* v1.0 - Average clocks */
        case AMDGPU_FIELD_AVG_CLOCK_GFXCLK:      *value = metrics->average_gfxclk_frequency; break;
        case AMDGPU_FIELD_AVG_CLOCK_SOCCLK:      *value = metrics->average_socclk_frequency; break;
        case AMDGPU_FIELD_AVG_CLOCK_UCLK:        *value = metrics->average_uclk_frequency; break;
        case AMDGPU_FIELD_AVG_CLOCK_VCLK0:       *value = metrics->average_vclk0_frequency; break;
        case AMDGPU_FIELD_AVG_CLOCK_DCLK0:       *value = metrics->average_dclk0_frequency; break;
        case AMDGPU_FIELD_AVG_CLOCK_VCLK1:       *value = metrics->average_vclk1_frequency; break;
        case AMDGPU_FIELD_AVG_CLOCK_DCLK1:       *value = metrics->average_dclk1_frequency; break;

        /* v1.0 - Utilization */
        case AMDGPU_FIELD_UTILIZATION_GFX:       *value = metrics->average_gfx_activity; break;
        case AMDGPU_FIELD_UTILIZATION_MEM:       *value = metrics->average_umc_activity; break;
        case AMDGPU_FIELD_UTILIZATION_MM:        *value = metrics->average_mm_activity; break;

        /* v1.0 - PCIe link */
        case AMDGPU_FIELD_PCIE_LINK_WIDTH:       *value = metrics->pcie_link_width; break;
        case AMDGPU_FIELD_PCIE_LINK_SPEED:       *value = metrics->pcie_link_speed; break;

        /* v1.0 - Other */
        case AMDGPU_FIELD_THROTTLE_STATUS:       *value = metrics->throttle_status; break;
        case AMDGPU_FIELD_FAN_SPEED:             *value = metrics->current_fan_speed; break;
        case AMDGPU_FIELD_VOLTAGE_VDDGFX:        *value = metrics->voltage_gfx; break;

        /* v1.1 - Accumulated activity */
        case AMDGPU_FIELD_GFX_ACTIVITY_ACC:      *value = metrics->gfx_activity_acc; break;
        case AMDGPU_FIELD_MEM_ACTIVITY_ACC:      *value = metrics->mem_activity_acc; break;

        /* v1.1 - HBM temperatures */
        case AMDGPU_FIELD_TEMPERATURE_HBM_0:     *value = metrics->temperature_hbm[0]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_1:     *value = metrics->temperature_hbm[1]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_2:     *value = metrics->temperature_hbm[2]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_3:     *value = metrics->temperature_hbm[3]; break;

        /* v1.2 - Timestamps */
        case AMDGPU_FIELD_FIRMWARE_TIMESTAMP:    *value = metrics->firmware_timestamp; break;
        case AMDGPU_FIELD_SYSTEM_CLOCK_COUNTER:  *value = metrics->system_clock_counter; break;

        /* v1.3 - Additional voltages */
        case AMDGPU_FIELD_VOLTAGE_SOC:           *value = metrics->voltage_soc; break;
        case AMDGPU_FIELD_VOLTAGE_MEM:           *value = metrics->voltage_mem; break;
        case AMDGPU_FIELD_INDEP_THROTTLE_STATUS: *value = metrics->indep_throttle_status; break;

        /* v1.4 - Power */
        case AMDGPU_FIELD_CURRENT_SOCKET_POWER:  *value = metrics->current_socket_power; break;

        /* v1.4 - VCN activity */
        case AMDGPU_FIELD_VCN_ACTIVITY_0:        *value = metrics->vcn_activity[0]; break;
        case AMDGPU_FIELD_VCN_ACTIVITY_1:        *value = metrics->vcn_activity[1]; break;
        case AMDGPU_FIELD_VCN_ACTIVITY_2:        *value = metrics->vcn_activity[2]; break;
        case AMDGPU_FIELD_VCN_ACTIVITY_3:        *value = metrics->vcn_activity[3]; break;

        /* v1.4 - Clock lock status */
        case AMDGPU_FIELD_GFXCLK_LOCK_STATUS:    *value = metrics->gfxclk_lock_status; break;

        /* v1.4 - XGMI link */
        case AMDGPU_FIELD_XGMI_LINK_WIDTH:       *value = metrics->xgmi_link_width; break;
        case AMDGPU_FIELD_XGMI_LINK_SPEED:       *value = metrics->xgmi_link_speed; break;

        /* v1.4 - PCIe bandwidth and counters */
        case AMDGPU_FIELD_PCIE_BANDWIDTH_ACC:    *value = metrics->pcie_bandwidth_acc; break;
        case AMDGPU_FIELD_PCIE_BANDWIDTH_INST:   *value = metrics->pcie_bandwidth_inst; break;
        case AMDGPU_FIELD_PCIE_L0_TO_RECOV_COUNT: *value = metrics->pcie_l0_to_recov_count_acc; break;
        case AMDGPU_FIELD_PCIE_REPLAY_COUNT:     *value = metrics->pcie_replay_count_acc; break;
        case AMDGPU_FIELD_PCIE_REPLAY_ROVER_COUNT: *value = metrics->pcie_replay_rover_count_acc; break;

        /* v1.4 - XGMI read data */
        case AMDGPU_FIELD_XGMI_READ_DATA_0:      *value = metrics->xgmi_read_data_acc[0]; break;
        case AMDGPU_FIELD_XGMI_READ_DATA_1:      *value = metrics->xgmi_read_data_acc[1]; break;
        case AMDGPU_FIELD_XGMI_READ_DATA_2:      *value = metrics->xgmi_read_data_acc[2]; break;
        case AMDGPU_FIELD_XGMI_READ_DATA_3:      *value = metrics->xgmi_read_data_acc[3]; break;
        case AMDGPU_FIELD_XGMI_READ_DATA_4:      *value = metrics->xgmi_read_data_acc[4]; break;
        case AMDGPU_FIELD_XGMI_READ_DATA_5:      *value = metrics->xgmi_read_data_acc[5]; break;
        case AMDGPU_FIELD_XGMI_READ_DATA_6:      *value = metrics->xgmi_read_data_acc[6]; break;
        case AMDGPU_FIELD_XGMI_READ_DATA_7:      *value = metrics->xgmi_read_data_acc[7]; break;

        /* v1.4 - XGMI write data */
        case AMDGPU_FIELD_XGMI_WRITE_DATA_0:     *value = metrics->xgmi_write_data_acc[0]; break;
        case AMDGPU_FIELD_XGMI_WRITE_DATA_1:     *value = metrics->xgmi_write_data_acc[1]; break;
        case AMDGPU_FIELD_XGMI_WRITE_DATA_2:     *value = metrics->xgmi_write_data_acc[2]; break;
        case AMDGPU_FIELD_XGMI_WRITE_DATA_3:     *value = metrics->xgmi_write_data_acc[3]; break;
        case AMDGPU_FIELD_XGMI_WRITE_DATA_4:     *value = metrics->xgmi_write_data_acc[4]; break;
        case AMDGPU_FIELD_XGMI_WRITE_DATA_5:     *value = metrics->xgmi_write_data_acc[5]; break;
        case AMDGPU_FIELD_XGMI_WRITE_DATA_6:     *value = metrics->xgmi_write_data_acc[6]; break;
        case AMDGPU_FIELD_XGMI_WRITE_DATA_7:     *value = metrics->xgmi_write_data_acc[7]; break;

        /* v1.5 - JPEG activity */
        case AMDGPU_FIELD_JPEG_ACTIVITY_0:       *value = metrics->jpeg_activity[0]; break;
        case AMDGPU_FIELD_JPEG_ACTIVITY_1:       *value = metrics->jpeg_activity[1]; break;
        case AMDGPU_FIELD_JPEG_ACTIVITY_2:       *value = metrics->jpeg_activity[2]; break;
        case AMDGPU_FIELD_JPEG_ACTIVITY_3:       *value = metrics->jpeg_activity[3]; break;

        /* v1.5 - PCIe NAK counters */
        case AMDGPU_FIELD_PCIE_NAK_SENT_COUNT:   *value = metrics->pcie_nak_sent_count_acc; break;
        case AMDGPU_FIELD_PCIE_NAK_RCVD_COUNT:   *value = metrics->pcie_nak_rcvd_count_acc; break;

        /* v1.6 - Throttle residency */
        case AMDGPU_FIELD_ACCUMULATION_COUNTER:  *value = metrics->accumulation_counter; break;
        case AMDGPU_FIELD_PROCHOT_RESIDENCY:     *value = metrics->prochot_residency_acc; break;
        case AMDGPU_FIELD_PPT_RESIDENCY:         *value = metrics->ppt_residency_acc; break;
        case AMDGPU_FIELD_SOCKET_THM_RESIDENCY:  *value = metrics->socket_thm_residency_acc; break;
        case AMDGPU_FIELD_VR_THM_RESIDENCY:      *value = metrics->vr_thm_residency_acc; break;
        case AMDGPU_FIELD_HBM_THM_RESIDENCY:     *value = metrics->hbm_thm_residency_acc; break;

        /* v1.6 - Other */
        case AMDGPU_FIELD_NUM_PARTITION:         *value = metrics->num_partition; break;
        case AMDGPU_FIELD_PCIE_LC_PERF_OTHER_END_RECOVERY: *value = metrics->pcie_lc_perf_other_end_recovery; break;

        /* v1.7 - VRAM bandwidth */
        case AMDGPU_FIELD_VRAM_MAX_BANDWIDTH:    *value = metrics->vram_max_bandwidth; break;

        /* v1.7 - XGMI link status */
        case AMDGPU_FIELD_XGMI_LINK_STATUS_0:    *value = metrics->xgmi_link_status[0]; break;
        case AMDGPU_FIELD_XGMI_LINK_STATUS_1:    *value = metrics->xgmi_link_status[1]; break;
        case AMDGPU_FIELD_XGMI_LINK_STATUS_2:    *value = metrics->xgmi_link_status[2]; break;
        case AMDGPU_FIELD_XGMI_LINK_STATUS_3:    *value = metrics->xgmi_link_status[3]; break;
        case AMDGPU_FIELD_XGMI_LINK_STATUS_4:    *value = metrics->xgmi_link_status[4]; break;
        case AMDGPU_FIELD_XGMI_LINK_STATUS_5:    *value = metrics->xgmi_link_status[5]; break;
        case AMDGPU_FIELD_XGMI_LINK_STATUS_6:    *value = metrics->xgmi_link_status[6]; break;
        case AMDGPU_FIELD_XGMI_LINK_STATUS_7:    *value = metrics->xgmi_link_status[7]; break;

        /* v1.9 - MI300 HBM stack temperatures */
        case AMDGPU_FIELD_TEMPERATURE_HBM_STACK_0: *value = metrics->temperature_hbm_stacks[0]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_STACK_1: *value = metrics->temperature_hbm_stacks[1]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_STACK_2: *value = metrics->temperature_hbm_stacks[2]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_STACK_3: *value = metrics->temperature_hbm_stacks[3]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_STACK_4: *value = metrics->temperature_hbm_stacks[4]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_STACK_5: *value = metrics->temperature_hbm_stacks[5]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_STACK_6: *value = metrics->temperature_hbm_stacks[6]; break;
        case AMDGPU_FIELD_TEMPERATURE_HBM_STACK_7: *value = metrics->temperature_hbm_stacks[7]; break;

        /* v1.9 - MI300 MID temperatures */
        case AMDGPU_FIELD_TEMPERATURE_MID_0:     *value = metrics->temperature_mid[0]; break;
        case AMDGPU_FIELD_TEMPERATURE_MID_1:     *value = metrics->temperature_mid[1]; break;
        case AMDGPU_FIELD_TEMPERATURE_MID_2:     *value = metrics->temperature_mid[2]; break;
        case AMDGPU_FIELD_TEMPERATURE_MID_3:     *value = metrics->temperature_mid[3]; break;

        /* v1.9 - MI300 AID temperatures */
        case AMDGPU_FIELD_TEMPERATURE_AID_0:     *value = metrics->temperature_aid[0]; break;
        case AMDGPU_FIELD_TEMPERATURE_AID_1:     *value = metrics->temperature_aid[1]; break;
        case AMDGPU_FIELD_TEMPERATURE_AID_2:     *value = metrics->temperature_aid[2]; break;
        case AMDGPU_FIELD_TEMPERATURE_AID_3:     *value = metrics->temperature_aid[3]; break;

        /* v1.9 - MI300 per-AID memory clocks */
        case AMDGPU_FIELD_UCLK_AID_0:            *value = metrics->current_uclk_aid[0]; break;
        case AMDGPU_FIELD_UCLK_AID_1:            *value = metrics->current_uclk_aid[1]; break;
        case AMDGPU_FIELD_UCLK_AID_2:            *value = metrics->current_uclk_aid[2]; break;
        case AMDGPU_FIELD_UCLK_AID_3:            *value = metrics->current_uclk_aid[3]; break;

        /* v1.9 - MI300 per-MID SOC clocks */
        case AMDGPU_FIELD_SOCCLK_MID_0:          *value = metrics->current_socclks_mid[0]; break;
        case AMDGPU_FIELD_SOCCLK_MID_1:          *value = metrics->current_socclks_mid[1]; break;
        case AMDGPU_FIELD_SOCCLK_MID_2:          *value = metrics->current_socclks_mid[2]; break;
        case AMDGPU_FIELD_SOCCLK_MID_3:          *value = metrics->current_socclks_mid[3]; break;

        default:                                  return EINVAL;
        }
        return 0;
}

/* Fetch and cache gpu_metrics for a single GPU - called once per sample cycle */
static int fetch_gpu_metrics_cache(amdgpu_inst_t inst, uint32_t dev_idx)
{
        amdsmi_status_t result;

        result = amdsmi_get_gpu_metrics_info(inst->device_handles[dev_idx],
                                            &inst->gpu_metrics_cache[dev_idx]);
        if (result != AMDSMI_STATUS_SUCCESS) {
                INST_LOG(inst, OVIS_LDEBUG,
                        "Failed to get gpu_metrics for GPU %d: %s\n",
                        dev_idx, amdsmi_status_string(result));
                inst->gpu_metrics_valid[dev_idx] = 0;
                return EINVAL;
        }

        inst->gpu_metrics_valid[dev_idx] = 1;
        return 0;
}

/* Fetch and cache PCIe throughput for a single GPU - called once per sample cycle */
static int fetch_pcie_throughput_cache(amdgpu_inst_t inst, uint32_t dev_idx)
{
        amdsmi_status_t result;
        uint64_t max_pkt_sz;

        result = amdsmi_get_gpu_pci_throughput(inst->device_handles[dev_idx],
                                               &inst->pcie_tx_cache[dev_idx],
                                               &inst->pcie_rx_cache[dev_idx],
                                               &max_pkt_sz);
        if (result != AMDSMI_STATUS_SUCCESS) {
                INST_LOG(inst, OVIS_LDEBUG,
                        "Failed to get PCIe throughput for GPU %d: %s\n",
                        dev_idx, amdsmi_status_string(result));
                inst->pcie_throughput_valid[dev_idx] = 0;
                return EINVAL;
        }

        inst->pcie_throughput_valid[dev_idx] = 1;
        return 0;
}

static int sample_gpu_metrics_field(amdgpu_inst_t inst,
                                   uint32_t dev_idx,
                                   enum amdgpu_field_id field_id,
                                   uint64_t *value)
{
        /* Use cached gpu_metrics - should have been fetched at start of sample cycle */
        if (!inst->gpu_metrics_valid[dev_idx]) {
                INST_LOG(inst, OVIS_LERROR,
                        "BUG: gpu_metrics not cached for GPU %d\n", dev_idx);
                return EINVAL;
        }

        return extract_gpu_metrics_field(&inst->gpu_metrics_cache[dev_idx], field_id, value);
}

static int sample_memory_field(amdgpu_inst_t inst,
                              amdsmi_processor_handle handle,
                              uint32_t dev_idx,
                              enum amdgpu_field_id field_id,
                              uint64_t *value)
{
        amdsmi_status_t result;

        if (field_id == AMDGPU_FIELD_MEMORY_USAGE) {
                result = amdsmi_get_gpu_memory_usage(handle, AMDSMI_MEM_TYPE_VRAM, value);
        } else if (field_id == AMDGPU_FIELD_MEMORY_TOTAL) {
                result = amdsmi_get_gpu_memory_total(handle, AMDSMI_MEM_TYPE_VRAM, value);
        } else {
                return EINVAL;
        }

        if (result != AMDSMI_STATUS_SUCCESS) {
                INST_LOG(inst, OVIS_LDEBUG,
                        "Failed to get memory info for GPU %d: %s\n",
                        dev_idx, amdsmi_status_string(result));
                return EINVAL;
        }

        return 0;
}

static int sample_pcie_field(amdgpu_inst_t inst,
                            amdsmi_processor_handle handle,
                            uint32_t dev_idx,
                            enum amdgpu_field_id field_id,
                            uint64_t *value)
{
        amdsmi_status_t result;

        switch (field_id) {
        case AMDGPU_FIELD_PCIE_BANDWIDTH: {
                amdsmi_pcie_info_t pcie_info;
                result = amdsmi_get_pcie_info(handle, &pcie_info);
                if (result != AMDSMI_STATUS_SUCCESS) {
                        INST_LOG(inst, OVIS_LDEBUG,
                                "Failed to get PCIe info for GPU %d: %s\n",
                                dev_idx, amdsmi_status_string(result));
                        return EINVAL;
                }
                *value = pcie_info.pcie_metric.pcie_bandwidth;
                break;
        }
        case AMDGPU_FIELD_PCIE_THROUGHPUT_TX:
        case AMDGPU_FIELD_PCIE_THROUGHPUT_RX: {
                /* Use cached PCIe throughput - should have been fetched at start of sample cycle */
                if (!inst->pcie_throughput_valid[dev_idx]) {
                        INST_LOG(inst, OVIS_LERROR,
                                "BUG: PCIe throughput not cached for GPU %d\n", dev_idx);
                        return EINVAL;
                }
                *value = (field_id == AMDGPU_FIELD_PCIE_THROUGHPUT_TX) ?
                         inst->pcie_tx_cache[dev_idx] : inst->pcie_rx_cache[dev_idx];
                break;
        }
        default:
                return EINVAL;
        }

        return 0;
}

static int sample_field(amdgpu_inst_t inst, uint32_t dev_idx,
                       enum amdgpu_field_id field_id, uint64_t *value)
{
        amdsmi_processor_handle handle = inst->device_handles[dev_idx];
        amdsmi_status_t result;

        /* Most fields from gpu_metrics structure (uses cached data) */
        if (is_gpu_metrics_field(field_id)) {
                return sample_gpu_metrics_field(inst, dev_idx, field_id, value);
        }

        /* Special case fields requiring separate API calls */
        switch (field_id) {
        case AMDGPU_FIELD_MEMORY_USAGE:
        case AMDGPU_FIELD_MEMORY_TOTAL:
                return sample_memory_field(inst, handle, dev_idx, field_id, value);

        case AMDGPU_FIELD_PCIE_BANDWIDTH:
        case AMDGPU_FIELD_PCIE_THROUGHPUT_TX:
        case AMDGPU_FIELD_PCIE_THROUGHPUT_RX:
                return sample_pcie_field(inst, handle, dev_idx, field_id, value);

        case AMDGPU_FIELD_GPU_ENERGY: {
                /* Computed field: convert energy_accumulator to microjoules
                 * using the counter_resolution from AMD SMI API
                 */
                uint64_t energy_acc;
                float counter_resolution;  /* microjoules per tick */
                uint64_t timestamp;

                result = amdsmi_get_energy_count(handle, &energy_acc, &counter_resolution, &timestamp);
                if (result != AMDSMI_STATUS_SUCCESS) {
                        INST_LOG(inst, OVIS_LERROR,
                                "Failed to get energy count for GPU %d: %s\n",
                                dev_idx, amdsmi_status_string(result));
                        return EIO;
                }

                /* Convert: ticks * resolution = microjoules
                 * Use long double to avoid precision loss for large energy_acc values
                 * (double only has 53-bit mantissa, can't exactly represent uint64_t > 2^53;
                 *  long double has 64-bit mantissa on x86-64, handles full uint64_t range)
                 *
                 * Rounding: Adding 0.5L before casting to uint64_t implements round-half-up.
                 * C truncates when casting float-to-int, so:
                 *   123.3 * res + 0.5 = 123.8 -> (uint64_t) -> 123  (rounds down)
                 *   123.7 * res + 0.5 = 124.2 -> (uint64_t) -> 124  (rounds up)
                 */
                *value = (uint64_t)((long double)energy_acc * counter_resolution + 0.5L);
                return 0;
        }

        default:
                INST_LOG(inst, OVIS_LERROR, "Unsupported field ID %d\n", field_id);
                return ENOTSUP;
        }
}

/* ========================================================================
 * Configuration and Schema Creation
 * ======================================================================== */

static const char *config_options[] = {
        "schema",
        "instance",
        "producer",
        "component_id",
        "uid",
        "gid",
        "perm",
        "job_set",
        "fields",
        "use_schema_hash",
        NULL
};

static int parse_config_options(amdgpu_inst_t inst, struct attr_value_list *avl)
{
        int rc;
        const char *val;

        rc = ldmsd_plugattr_config_check(config_options, NULL, avl, NULL, NULL, SAMP);
        if (rc)
                return rc;

        /* Parse schema base name */
        val = av_value(avl, "schema");
        if (val) {
                strncpy(inst->schema_name_base, val, AMDGPU_MAX_SCHEMA_NAME_BASE - 1);
                inst->schema_name_base[AMDGPU_MAX_SCHEMA_NAME_BASE - 1] = '\0';
        } else {
                strcpy(inst->schema_name_base, SAMP);
        }

        /* Parse fields */
        val = av_value(avl, "fields");
        if (val) {
                rc = parse_field_list(val, inst->field_ids, &inst->num_fields,
                                     AMDGPU_MAX_CONFIGURED_FIELDS, inst->mylog);
                if (rc)
                        return rc;
        } else {
                /* Use default fields */
                const char **def = default_fields;
                inst->num_fields = 0;
                while (*def && inst->num_fields < AMDGPU_MAX_CONFIGURED_FIELDS) {
                        const struct amdgpu_field_desc *fd = find_field_by_name(*def);
                        if (fd) {
                                inst->field_ids[inst->num_fields++] = fd->id;
                        }
                        def++;
                }
        }

        if (inst->num_fields == 0) {
                INST_LOG(inst, OVIS_LERROR, "No valid fields specified\n");
                return EINVAL;
        }

        /* Calculate which API caches we need - optimization for sampling loop */
        inst->needs_gpu_metrics = false;
        inst->needs_pcie_throughput = false;
        for (uint32_t i = 0; i < inst->num_fields; i++) {
                if (is_gpu_metrics_field(inst->field_ids[i])) {
                        inst->needs_gpu_metrics = true;
                }
                if (inst->field_ids[i] == AMDGPU_FIELD_PCIE_THROUGHPUT_TX ||
                    inst->field_ids[i] == AMDGPU_FIELD_PCIE_THROUGHPUT_RX) {
                        inst->needs_pcie_throughput = true;
                }
        }

        /* Parse use_schema_hash (default: 0 = disabled) */
        val = av_value(avl, "use_schema_hash");
        if (val) {
                inst->use_schema_hash = (atoi(val) != 0);
        } else {
                inst->use_schema_hash = 0;
        }

        return 0;
}

static ldms_schema_t create_ldms_schema(amdgpu_inst_t inst)
{
        int rc;

        ldms_schema_t schema = base_schema_new(inst->base);
        if (!schema) {
                INST_LOG(inst, OVIS_LERROR, "base_schema_new failed\n");
                return NULL;
        }

        /* Add device name metric */
        rc = ldms_schema_metric_array_add(schema, "device",
                                         LDMS_V_CHAR_ARRAY,
                                         AMDGPU_DEVICE_NAME_LENGTH);
        if (rc < 0) {
                INST_LOG(inst, OVIS_LERROR, "Failed to add device metric\n");
                return NULL;
        }
        inst->first_index = rc;
        inst->metric_offset = rc + 1;

        /* Add metrics for configured fields */
        for (uint32_t i = 0; i < inst->num_fields; i++) {
                const struct amdgpu_field_desc *fd = find_field_by_id(inst->field_ids[i]);
                if (fd) {
                        ldms_schema_metric_add(schema, fd->name, fd->type);
                }
        }

        return schema;
}

static int create_gpu_sets(amdgpu_inst_t inst, const char *instance_name)
{
        for (uint32_t i = 0; i < inst->num_devices; i++) {
                char set_name[256];
                snprintf(set_name, sizeof(set_name), "%s/gpu%d", instance_name, i);

                char *orig_name = inst->base->instance_name;
                inst->base->instance_name = set_name;

                inst->devset[i] = base_set_new(inst->base);

                inst->base->instance_name = orig_name;

                if (!inst->devset[i]) {
                        INST_LOG(inst, OVIS_LERROR,
                                "Failed to create set for GPU %d\n", i);
                        return errno ? errno : ENOMEM;
                }
                inst->num_sets++;
        }

        return 0;
}

static int create_schema_and_sets(amdgpu_inst_t inst, struct attr_value_list *avl,
                                  const char *instance_name)
{
        int rc;

        rc = generate_schema_name(inst);
        if (rc)
                return rc;

        inst->base = base_config(avl, instance_name, inst->schema_name, inst->mylog);
        if (!inst->base) {
                INST_LOG(inst, OVIS_LERROR, "base_config failed\n");
                return errno ? errno : EINVAL;
        }

        ldms_schema_t schema = create_ldms_schema(inst);
        if (!schema) {
                return errno ? errno : EINVAL;
        }

        rc = create_gpu_sets(inst, instance_name);
        if (rc)
                return rc;

        return 0;
}

/* ========================================================================
 * Instance Management
 * ======================================================================== */

#ifndef MAIN
/* Plugin functions - not compiled for standalone utility */

static int do_sample(amdgpu_inst_t inst)
{
        int rc = 0;

        for (uint32_t dev_idx = 0; dev_idx < inst->num_devices; dev_idx++) {
                inst->base->set = inst->devset[dev_idx];
                base_sample_begin(inst->base);

                /* Set device name metadata (once) */
                if (!inst->meta_done) {
                        char dev_name[AMDGPU_DEVICE_NAME_LENGTH];
                        snprintf(dev_name, sizeof(dev_name), "gpu%d", dev_idx);
                        ldms_metric_array_set_str(inst->devset[dev_idx],
                                                 inst->first_index, dev_name);
                }

                /* Fetch gpu_metrics once if needed - used by temperature, clock, power fields */
                if (inst->needs_gpu_metrics) {
                        fetch_gpu_metrics_cache(inst, dev_idx);
                }

                /* Fetch PCIe throughput once if needed - used by TX/RX fields */
                if (inst->needs_pcie_throughput) {
                        fetch_pcie_throughput_cache(inst, dev_idx);
                }

                /* Sample all configured fields */
                for (uint32_t i = 0; i < inst->num_fields; i++) {
                        uint64_t value = 0;
                        int field_rc = sample_field(inst, dev_idx, inst->field_ids[i], &value);
                        if (field_rc == 0) {
                                ldms_metric_set_u64(inst->devset[dev_idx],
                                                   inst->metric_offset + i, value);
                        } else {
                                ldms_metric_set_u64(inst->devset[dev_idx],
                                                   inst->metric_offset + i, 0);
                        }
                }

                base_sample_end(inst->base);
                inst->base->set = NULL;
        }

        inst->meta_done = 1;
        return rc;
}

static void do_reset(amdgpu_inst_t inst)
{
        /* Deregister and delete LDMS sets */
        for (uint32_t i = 0; i < inst->num_sets; i++) {
                if (inst->devset[i]) {
                        const char *name = ldms_set_instance_name_get(inst->devset[i]);
                        ldmsd_set_deregister(name, inst->base->cfg_name);
                        ldms_set_unpublish(inst->devset[i]);
                        ldms_set_delete(inst->devset[i]);
                        inst->devset[i] = NULL;
                }
        }
        inst->num_sets = 0;

        /* Shutdown AMD SMI */
        if (inst->num_devices > 0) {
                amdsmi_status_t result = amdsmi_shut_down();
                if (result != AMDSMI_STATUS_SUCCESS) {
                        INST_LOG(inst, OVIS_LWARNING,
                                "AMD SMI shutdown returned status %d\n", result);
                }
                inst->num_devices = 0;
        }

        /* Free base configuration */
        if (inst->base) {
                if (inst->base->instance_name) {
                        free(inst->base->instance_name);
                        inst->base->instance_name = NULL;
                }
                base_del(inst->base);
                inst->base = NULL;
        }

        /* Free schema name */
        free(inst->schema_name);
        inst->schema_name = NULL;

        inst->meta_done = 0;
        inst->num_fields = 0;
}

static int do_config(amdgpu_inst_t inst, struct attr_value_list *avl,
                    const char *instance_name)
{
        int rc;

        rc = parse_config_options(inst, avl);
        if (rc)
                return rc;

        rc = hardware_init(inst);
        if (rc)
                return rc;

        rc = create_schema_and_sets(inst, avl, instance_name);
        if (rc)
                goto err;

        return 0;

err:
        do_reset(inst);
        return rc;
}

static char *get_usage(void)
{
        char *buf = NULL;
        size_t len = 0;
        FILE *f = open_memstream(&buf, &len);
        if (!f)
                return NULL;

        fprintf(f,
                "Configuration options:\n"
                "  schema=<name>       Schema base name (default: amdgpu_sampler)\n"
                "  instance=<name>     Instance name prefix\n"
                "  producer=<name>     Producer name\n"
                "  component_id=<id>   Component identifier\n"
                "  fields=<list>       Comma-separated field names or alias\n"
                "                      (default: base set of common fields)\n"
                "  uid=<uid>           Owner UID\n"
                "  gid=<gid>           Owner GID\n"
                "  perm=<mode>         Permissions (octal)\n"
                "  job_set=<name>      Job set name\n"
                "\n"
                "Available fields:\n");

        const struct amdgpu_field_desc *fd = field_descriptors;
        while (fd->name) {
                fprintf(f, "  %-30s %s\n", fd->name, fd->unit);
                fd++;
        }

        fprintf(f,
                "\nField aliases:\n"
                "  base        - Common temperature, power, clock, memory, and utilization\n"
                "  all_temps   - All temperature sensors\n"
                "  all_clocks  - All clock domains\n"
                "  all_power   - All power metrics\n"
                "\n"
                "Examples:\n"
                "  config name=amdgpu_sampler producer=node1 instance=node1/amdsmi \\\n"
                "         component_id=1\n"
                "  config name=amdgpu_sampler producer=node1 instance=node1/amdsmi \\\n"
                "         component_id=1 fields=temperature_hotspot,power_socket\n"
        );

        fclose(f);
        return buf;
}

/* ========================================================================
 * LDMS Plugin Interface
 * ======================================================================== */

static int constructor(ldmsd_plug_handle_t handle)
{
        ovis_log_t mylog = ldmsd_plug_log_get(handle);
        amdgpu_inst_t inst = inst_new(mylog);
        if (!inst) {
                ovis_log(mylog, OVIS_LCRITICAL, SAMP ": failed to create instance\n");
                return ENOMEM;
        }
        ldmsd_plug_ctxt_set(handle, inst);
        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
        amdgpu_inst_t inst = ldmsd_plug_ctxt_get(handle);
        if (!inst)
                return;
        pthread_mutex_lock(&inst->lock);
        do_reset(inst);
        pthread_mutex_unlock(&inst->lock);
        inst_delete(inst);
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
                 struct attr_value_list *avl)
{
        int rc;
        amdgpu_inst_t inst = ldmsd_plug_ctxt_get(handle);
        if (!inst)
                return EINVAL;

        pthread_mutex_lock(&inst->lock);

        /* Allow reconfiguration - reset first if already configured */
        if (inst->base) {
                do_reset(inst);
        }

        rc = do_config(inst, avl, ldmsd_plug_cfg_name_get(handle));
        if (rc) {
                do_reset(inst);
                INST_LOG(inst, OVIS_LERROR, SAMP ": configuration failed\n");
        }

        pthread_mutex_unlock(&inst->lock);
        return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
        int rc;
        amdgpu_inst_t inst = ldmsd_plug_ctxt_get(handle);
        if (!inst)
                return EINVAL;

        pthread_mutex_lock(&inst->lock);

        if (!inst->base) {
                pthread_mutex_unlock(&inst->lock);
                return EINVAL;
        }

        rc = do_sample(inst);
        if (rc) {
                do_reset(inst);
                INST_LOG(inst, OVIS_LWARNING,
                        SAMP ": deconfigured due to sampling errors\n");
        }

        pthread_mutex_unlock(&inst->lock);
        return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
        return get_usage();
}

struct ldmsd_sampler ldmsd_plugin_interface = {
        .base = {
                .type = LDMSD_PLUGIN_SAMPLER,
                .flags = LDMSD_PLUGIN_MULTI_INSTANCE,
                .config = config,
                .usage = usage,
                .constructor = constructor,
                .destructor = destructor,
        },
        .sample = sample,
};

#endif /* !MAIN */

/* ========================================================================
 * Common Utility Functions (Used by plugin and standalone)
 * ======================================================================== */

static amdgpu_inst_t inst_new(ovis_log_t mylog)
{
        amdgpu_inst_t inst = calloc(1, sizeof(*inst));
        if (!inst)
                return NULL;

        inst->mylog = mylog;
        pthread_mutex_init(&inst->lock, NULL);

        return inst;
}

static void inst_delete(amdgpu_inst_t inst)
{
        if (!inst)
                return;

        pthread_mutex_destroy(&inst->lock);
        free(inst);
}

/* ========================================================================
 * Standalone Utility (Schema Name Calculator)
 * ======================================================================== */

#ifdef MAIN
int main(int argc, char **argv)
{
        ovis_log_t mylog = ovis_log_register("amdgpu_schema",
                                            "AMD GPU schema name utility");
        if (!mylog) {
                fprintf(stderr, "Failed to create logger\n");
                return 1;
        }

        amdgpu_inst_t inst = inst_new(mylog);
        if (!inst) {
                fprintf(stderr, "Failed to create instance\n");
                return 1;
        }

        struct attr_value_list *avl = av_new(argc);
        if (!avl) {
                fprintf(stderr, "Failed to create attr list\n");
                return 1;
        }

        for (int i = 1; i < argc; i++) {
                char *eq = strchr(argv[i], '=');
                if (eq) {
                        *eq = '\0';
                        char *name = argv[i];
                        char *value = eq + 1;
                        av_add(avl, name, value);
                }
        }

        const char *schema = av_value(avl, "schema");
        if (schema) {
                strncpy(inst->schema_name_base, schema, AMDGPU_MAX_SCHEMA_NAME_BASE - 1);
        } else {
                strcpy(inst->schema_name_base, SAMP);
        }

        const char *fields = av_value(avl, "fields");
        if (fields) {
                parse_field_list(fields, inst->field_ids, &inst->num_fields,
                                AMDGPU_MAX_CONFIGURED_FIELDS, mylog);
        } else {
                const char **def = default_fields;
                inst->num_fields = 0;
                while (*def && inst->num_fields < AMDGPU_MAX_CONFIGURED_FIELDS) {
                        const struct amdgpu_field_desc *fd = find_field_by_name(*def);
                        if (fd) {
                                inst->field_ids[inst->num_fields++] = fd->id;
                        }
                        def++;
                }
        }

        const char *use_hash = av_value(avl, "use_schema_hash");
        if (use_hash) {
                inst->use_schema_hash = (atoi(use_hash) != 0);
        } else {
                inst->use_schema_hash = 0;
        }

        if (inst->use_schema_hash) {
                uint32_t hash = compute_schema_hash(inst);
                printf("%s_%08x\n", inst->schema_name_base, hash);
        } else {
                printf("%s\n", inst->schema_name_base);
        }

        av_free(avl);
        inst_delete(inst);
        return 0;
}
#endif
