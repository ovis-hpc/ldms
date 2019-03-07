#ifndef _IBM_OCC_H_
#define _IBM_OCC_H_
#include <inttypes.h>

#define MAX_OCCS               8
#define MAX_CHARS_SENSOR_NAME 16
#define MAX_CHARS_SENSOR_UNIT  4

#define OCC_SENSOR_DATA_BLOCK_OFFSET 0x00580000
#define OCC_SENSOR_DATA_BLOCK_SIZE   0x00025800

enum occ_sensor_type {
    OCC_SENSOR_TYPE_GENERIC        = 0x0001,
    OCC_SENSOR_TYPE_CURRENT        = 0x0002,
    OCC_SENSOR_TYPE_VOLTAGE        = 0x0004,
    OCC_SENSOR_TYPE_TEMPERATURE    = 0x0008,
    OCC_SENSOR_TYPE_UTILIZATION    = 0x0010,
    OCC_SENSOR_TYPE_TIME           = 0x0020,
    OCC_SENSOR_TYPE_FREQUENCY      = 0x0040,
    OCC_SENSOR_TYPE_POWER          = 0x0080,
    OCC_SENSOR_TYPE_PERFORMANCE    = 0x0200,
};

enum occ_sensor_location {
    OCC_SENSOR_LOC_SYSTEM       = 0x0001,
    OCC_SENSOR_LOC_PROCESSOR    = 0x0002,
    OCC_SENSOR_LOC_PARTITION    = 0x0004,
    OCC_SENSOR_LOC_MEMORY       = 0x0008,
    OCC_SENSOR_LOC_VRM          = 0x0010,
    OCC_SENSOR_LOC_OCC          = 0x0020,
    OCC_SENSOR_LOC_CORE         = 0x0040,
    OCC_SENSOR_LOC_GPU          = 0x0080,
    OCC_SENSOR_LOC_QUAD         = 0x0100,
};

enum sensor_struct_type {
	OCC_SENSOR_RECORD   = 1,
	OCC_SENSOR_COUNTER
};

enum sensor_id{
	PWRSYS       = 158, // Power Usage System
	PROCPWRTHROT = 301  // Power Cap
};

struct occ_sensor_data_header {
	uint8_t  valid;                 /* File contents are valid */
	uint8_t  hdr_version;           /* File format version */
	uint16_t nr_sensors;            /* Count of sensors */
	uint8_t  buffer_format;		/* Format version of the Ping/Pong buffer. */
	uint8_t  pad[3];
	uint32_t names_offset;          /* Offset to the location of names buffer. */
	uint8_t  names_version;         /* Format version of names buffer. */
	uint8_t  name_length;           /* Length of each sensor in names buffer. */
	uint16_t reserved;
	uint32_t ping_offset;           /* Ping buffer offset */
	uint32_t pong_offset;           /* Pong buffer offset */

	struct occ_sensor_name *names;
} __attribute__((__packed__));

struct occ_sensor_name {
	char name[MAX_CHARS_SENSOR_NAME];
	char units[MAX_CHARS_SENSOR_UNIT];
	uint16_t gsid;              /* Global sensor id */
	uint32_t freq;              /* Update frequency */
	uint32_t scale_factor;
	uint16_t type;              /* Sensor type, enum occ_sensor_type */
	uint16_t location;          /* Sensor location, enum occ_sensor_location */
	uint8_t format;             /* Sensor format OCC_SENSOR_RECORD or OCC_SENSOR_COUNTER */
	uint32_t offset;            /* Offset from the start of the ping/pong buffers */
	uint8_t sensor_info;
	uint8_t pad[8];
} __attribute__((__packed__));

struct occ_sensor_header {
	uint16_t gsid;              /* Global sensor id (OCC) */
	uint64_t timestamp;         /* Time base counter value at time last update */
} __attribute__((__packed__));

struct occ_sensor_record {
	uint16_t sample;
	uint16_t sample_min;        /* Minimum value since OCC reset */
	uint16_t sample_max;        /* Maximum value since OCC reset */
	uint16_t csm_min;           /* Minimum value since reset by CSM */
	uint16_t csm_max;           /* Maximum value since reset by CSM */
	uint16_t profiler_min;      /* Minimum value since reset by profiler */
	uint16_t profiler_max;      /* Maximum value since reset by profiler */
	uint16_t job_scheduler_min; /* Minimum value since reset by job scheduler */
	uint16_t job_scheduler_max; /* Maximum value since reset by job scheduler */
	uint64_t accumulator;       /* Accumulator for this sensor */
	uint32_t update_tag;        /* Count of ticks that have passed between updates. */
} __attribute__((__packed__));

struct occ_sensor_counter {
	uint64_t accumulator;
	uint8_t sample;
} __attribute__((__packed__));

struct occ_sensor {
	struct occ_sensor_header hdr;
	union {
		struct occ_sensor_record record;
		struct occ_sensor_counter counter;
	};
} __attribute__((__packed__));

#endif
