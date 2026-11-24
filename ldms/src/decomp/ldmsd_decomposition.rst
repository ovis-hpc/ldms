.. _ldmsd_decomposition:

===================
ldmsd_decomposition
===================

------------------------------
Manual for LDMSD decomposition
------------------------------

:Date: 2026-11-18
:Version: v5
:Manual section: 7
:Manual group: LDMSD Decomposition man page

DESCRIPTION
===========

A decomposition is a routine that converts LDMS sets into one or more
rows before feeding them to storage. Decompositions provide flexible
data transformation and restructuring capabilities for efficient storage
and analysis.

**Supported Stores:** Currently, **store_sos**, **store_csv**, and
**store_kafka** support decomposition.

**Usage:** Specify **decomposition=**\ *CONFIG_FILE* in the
**strgp_add** command. The configuration file can be either YAML or JSON
format (YAML is recommended for better readability and maintainability).

Decomposition Types
-------------------

LDMS provides three decomposition types:

**static**
  Statically and strictly converts LDMS sets according to explicit
  definitions. Use this when you need precise control over the output
  schema, column mapping, and data transformation.

**as_is**
  Dynamically converts all metrics as-is into rows without explicit
  definitions. Use this when you want automatic schema discovery and
  don't need complex transformations.

**flex**
  Flexibly applies different decompositions based on schema digest
  mapping or pattern matching. Use this when you have multiple schema
  types that need different decomposition strategies.

The decomposition mechanism is pluggable. See the implementation in
``ldms/src/decomp/`` directory in the source tree for more information.


CONFIGURATION FILE FORMAT
==========================

Both YAML and JSON formats are supported. YAML is recommended for its
readability, support for comments, and multi-line strings.

**YAML Configuration:**

.. code-block:: yaml

   type: static  # or as_is, or flex
   # ... type-specific configuration ...

**JSON Configuration (equivalent):**

.. code-block:: json

   {
     "type": "static",
     ...
   }

All examples below use YAML format. To convert to JSON, follow standard
YAML-to-JSON mapping rules.

Configuration Parsers
---------------------

ldmsd can be built with up to three configuration file parsers, depending
on which libraries were found at build time:

**libfyaml**
  YAML and JSON. Used by default when available.

**libyaml**
  YAML and JSON. Used by default when libfyaml is not available.

**jansson**
  JSON only. Used by default only when neither YAML parser is
  available. Error messages about configuration values cannot include a
  line and column with this parser.

The **LDMSD_DECOMP_PARSER** environment variable of ldmsd selects a
specific parser by name (``libfyaml``, ``libyaml``, or ``jansson``). This
is mostly useful for testing. If it names a parser that was not built,
decomposition configuration fails with an error listing the available
parsers.


STATIC DECOMPOSITION
====================

The **static** decomposition provides precise control over how LDMS sets
are converted into rows. It supports:

- Multiple output schemas from a single input
- Column selection, renaming, and type specification
- Record member access for list/array metrics
- Fill values for optional metrics
- Operations (diff, min, max, mean) on column values
- Custom indices for storage optimization
- Row grouping for aggregate operations

Configuration Structure
-----------------------

.. code-block:: yaml

   type: static
   rows:
     - schema: OUTPUT_SCHEMA_NAME
       cols:
         - src: LDMS_METRIC_NAME
           dst: OUTPUT_COLUMN_NAME      # optional
           type: DATA_TYPE               # optional
           rec_member: RECORD_MEMBER     # for list/record metrics
           fill: DEFAULT_VALUE           # optional
           array_len: ARRAY_LENGTH       # for char_array
           op: OPERATION                 # optional: diff, min, max, mean
       indices:                          # optional
         - name: INDEX_NAME
           cols: [COL1, COL2, ...]
       group:                            # required if using op
         limit: ROW_LIMIT
         index: [GROUP_COL1, ...]
         order: [ORDER_COL1, ...]
         timeout: TIME_STRING

Column Attributes
-----------------

**src** (required)
  The source metric name from the LDMS set. Can use special syntax:

  - Simple metric: ``"MemFree"``
  - Record member: ``"netdev_list[rx_bytes]"``
  - Alternative: ``"netdev_list"`` with ``rec_member: "rx_bytes"``

  **Special source names** for set metadata:

  - ``timestamp`` - sampling timestamp
  - ``producer`` - producer name
  - ``instance`` - instance name
  - ``component_id`` - component identifier
  - ``M_card`` - set cardinality
  - ``M_digest`` - schema digest string
  - ``M_duration`` - sampling duration
  - ``M_gid`` - owner GID
  - ``M_perm`` - permission value
  - ``M_schema`` - schema name
  - ``M_uid`` - owner UID

**dst** (optional)
  Output column name. If omitted, uses the ``src`` name.

**type** (optional, required with fill)
  Output column data type. Types include: ``ts``, ``u64``, ``s64``,
  ``u32``, ``s32``, ``u16``, ``s16``, ``u8``, ``s8``, ``float``,
  ``double``, ``d64``, ``char``, ``char_array``.

**rec_member** (optional)
  For list or array metrics, specifies which record member to extract.
  Alternative to bracket notation in ``src``.

**fill** (optional)
  Default value when the source metric is not found. Can be scalar or
  array. Required metrics without fill will cause the set to be skipped.

**array_len** (optional)
  For ``char_array`` types, specifies the maximum string length.

**op** (optional)
  Operation to perform on the column. Supported: ``diff`` (difference),
  ``min`` (minimum), ``max`` (maximum), ``mean`` (average). Requires
  ``group`` configuration.

Index Attributes
----------------

**name** (required)
  Name of the index.

**cols** (required)
  List of output column names comprising the index.

Group Attributes
----------------

Required when using ``op`` on any column.

**index** (required)
  List of columns defining group identity. Rows with identical values
  in these columns belong to the same group.

**order** (required)
  List of columns for ordering rows within each group (descending).

**limit** (required)
  Maximum number of rows cached per group. Keeps the first N rows after
  ordering.

**timeout** (optional)
  Time string (e.g., ``"60s"``, ``"5m"``) for group inactivity before
  cleanup. Units: ``s`` (seconds), ``ms`` (milliseconds), ``us``
  (microseconds), ``ns`` (nanoseconds).

Static Decomposition Examples
------------------------------

**Example 1: Basic column selection with fill values**

This example shows filtering meminfo metrics and using fill values for
architecture-specific metrics.

.. code-block:: yaml

   type: static
   rows:
     # Filter specific memory metrics
     - schema: meminfo_filter
       cols:
         - src: timestamp
           dst: ts
           type: ts
         - src: producer
           dst: prdcr
           type: char_array
           array_len: 64
         - src: instance
           dst: inst
           type: char_array
           array_len: 64
         - src: component_id
           dst: comp_id
           type: u64
         - src: MemFree
           dst: free
           type: u64
         - src: MemActive
           dst: active
           type: u64
       indices:
         - name: time_comp
           cols: [ts, comp_id]
         - name: time
           cols: [ts]

     # DirectMap metrics with fill (architecture-specific)
     - schema: meminfo_directmap
       cols:
         - src: timestamp
           dst: ts
           type: ts
         - src: component_id
           dst: comp_id
           type: u64
         - src: DirectMap4k
           dst: directmap4k
           type: u64
           fill: 0  # default if not present
         - src: DirectMap2M
           dst: directmap2M
           type: u64
           fill: 0
         - src: DirectMap4M
           dst: directmap4M
           type: u64
           fill: 0
         - src: DirectMap1G
           dst: directmap1G
           type: u64
           fill: 0
       indices:
         - name: time_comp
           cols: [ts, comp_id]

**Example 2: Record member access with operations**

This example demonstrates accessing list record members and computing
difference operations.

.. code-block:: yaml

   type: static
   rows:
     - schema: netdev_stats
       cols:
         # Metadata columns
         - src: timestamp
           dst: ts
           type: ts
         - src: producer
           dst: prdcr
           type: char_array
           array_len: 64
         - src: component_id
           dst: comp_id
           type: u64

         # Record members - various notations
         - src: netdev_list
           rec_member: name
           dst: device_name
           type: char_array
           array_len: 16

         # Bracket notation (equivalent to above)
         - src: netdev_list[rx_bytes]
           dst: rx_bytes
           type: u64

         - src: netdev_list[tx_bytes]
           dst: tx_bytes
           type: u64

         # Compute rate of change (diff operation)
         - src: netdev_list[rx_bytes]
           dst: rx_bytes_rate
           type: u64
           op: diff

         - src: netdev_list[tx_bytes]
           dst: tx_bytes_rate
           type: u64
           op: diff

       indices:
         - name: time_comp_dev
           cols: [ts, comp_id, device_name]

       # Grouping for diff operation
       group:
         limit: 2          # keep current + previous row
         index: [comp_id, device_name]  # group by node and device
         order: [ts]       # order by timestamp
         timeout: 60s      # cleanup after 60s inactivity


AS_IS DECOMPOSITION
===================

The **as_is** decomposition automatically converts all metrics in an
LDMS set to row columns without requiring explicit definitions. This is
useful for:

- Rapid deployment without writing configurations
- Dynamic schemas that change over time
- Exploratory data analysis
- Simple pass-through storage

**Schema Naming:** To avoid conflicts from heterogeneous sources,
as_is decomposition appends a 7-character schema digest to the row
schema name. Example: ``meminfo`` becomes ``meminfo_8d2b8bd``.

**List Expansion:** When encountering list metrics:

- **List of primitives:** Expands into multiple rows (non-list metrics
  repeated)
- **List of records:** Expands rows and flattens record members into
  columns named ``LIST_NAME.MEMBER_NAME``

**Automatic Columns:** The ``timestamp`` column is automatically
prepended to all output rows.

Configuration Structure
-----------------------

.. code-block:: yaml

   type: as_is
   indices:
     - name: INDEX_NAME
       cols: [COLUMN_NAME, ...]

As_Is Decomposition Example
----------------------------

.. code-block:: yaml

   type: as_is
   indices:
     - name: time
       cols: [timestamp]
     - name: time_comp
       cols: [timestamp, component_id]

This configuration will:

1. Convert all metrics to columns automatically
2. Expand any list metrics into multiple rows
3. Flatten record members into dot-notation columns
4. Create two indices for efficient querying


FLEX DECOMPOSITION
==================

The **flex** decomposition provides intelligent routing to apply
different decompositions based on:

- Schema digest mapping (exact schema matching)
- Regular expression patterns (schema and/or instance name)
- Default fallback for unmatched sets

This is essential for heterogeneous environments where different LDMS
schemas need different handling.

Configuration Structure
-----------------------

.. code-block:: yaml

   type: flex

   # Define all decompositions used
   decomposition:
     decomp_name_1:
       type: static  # or as_is
       # ... decomposition config ...
     decomp_name_2:
       type: as_is
       # ... decomposition config ...

   # Option 1: Map by exact schema digest
   digest:
     FULL_SHA256_DIGEST_1: decomp_name_1
     FULL_SHA256_DIGEST_2: [decomp_name_1, decomp_name_2]
     "*": default_decomp  # catch-all (optional)

   # Option 2: Map by pattern matching
   matches:
     - schema: REGEX_PATTERN      # match schema name
       instance: REGEX_PATTERN    # match instance name (optional)
       apply: decomp_name          # single decomposition
     - schema: ANOTHER_PATTERN
       apply: [decomp_a, decomp_b]  # multiple decompositions

   # Default for unmatched sets (optional)
   default: decomp_name

Notes:

- Use either ``digest`` or ``matches`` or both
- With ``digest``: checks exact SHA256 schema digest first
- With ``matches``: checks patterns in order, first match wins
- ``default``: applies when nothing matches
- Multiple decompositions can be applied to a single set

Flex Decomposition Examples
----------------------------

**Example 1: Digest-based routing**

Map specific schemas by their digest to appropriate decompositions:

.. code-block:: yaml

   type: flex

   decomposition:
     # Static decomposition for meminfo
     meminfo_filter:
       type: static
       rows:
         - schema: meminfo_core
           cols:
             - src: timestamp
               dst: ts
               type: ts
             - src: component_id
               dst: comp_id
               type: u64
             - src: MemFree
               dst: free
               type: u64
             - src: MemAvailable
               dst: available
               type: u64
           indices:
             - name: time_comp
               cols: [ts, comp_id]

     # Static decomposition for network stats
     netdev_stats:
       type: static
       rows:
         - schema: network_io
           cols:
             - src: timestamp
               dst: ts
               type: ts
             - src: component_id
               dst: comp_id
               type: u64
             - src: netdev_list[name]
               dst: device
               type: char_array
               array_len: 16
             - src: netdev_list[rx_bytes]
               dst: rx_bytes
               type: u64
             - src: netdev_list[tx_bytes]
               dst: tx_bytes
               type: u64
           indices:
             - name: time_comp_dev
               cols: [ts, comp_id, device]

     # Catch-all as_is decomposition
     default_asis:
       type: as_is
       indices:
         - name: time
           cols: [timestamp]
         - name: time_comp
           cols: [timestamp, component_id]

   # Map digests to decompositions
   digest:
     # Two different meminfo schemas (different architectures)
     71B03E47E7C9033E359DB5225BC6314A589D8772F4BC0866B6E79A698C8799C0: meminfo_filter
     59DD05D768CFF8F175496848486275822A6A9795286FD9B534FDB9434EAF4D50: meminfo_filter

     # Network stats with both specific and generic decomposition
     E8B9CC8D83FB4E5B779071E801CA351B69DCB9E9CE2601A0B127A2977F11C62A: [netdev_stats, default_asis]

     # Everything else uses as_is
     "*": default_asis

**Example 2: Pattern-based routing**

Use regex patterns instead of digests for more flexible matching:

.. code-block:: yaml

   type: flex

   decomposition:
     # Same decompositions as Example 1
     meminfo_filter:
       type: static
       rows:
         - schema: meminfo_core
           cols:
             - {src: timestamp, dst: ts, type: ts}
             - {src: component_id, dst: comp_id, type: u64}
             - {src: MemFree, dst: free, type: u64}
             - {src: MemAvailable, dst: available, type: u64}
           indices:
             - {name: time_comp, cols: [ts, comp_id]}

     netdev_stats:
       type: static
       rows:
         - schema: network_io
           cols:
             - {src: timestamp, dst: ts, type: ts}
             - {src: component_id, dst: comp_id, type: u64}
             - {src: "netdev_list[name]", dst: device, type: char_array, array_len: 16}
             - {src: "netdev_list[rx_bytes]", dst: rx_bytes, type: u64}
             - {src: "netdev_list[tx_bytes]", dst: tx_bytes, type: u64}
           indices:
             - {name: time_comp_dev, cols: [ts, comp_id, device]}

     cpu_stats:
       type: static
       rows:
         - schema: cpu_usage
           cols:
             - {src: timestamp, dst: ts, type: ts}
             - {src: component_id, dst: comp_id, type: u64}
             - {src: user, type: u64}
             - {src: system, type: u64}
             - {src: idle, type: u64}
           indices:
             - {name: time_comp, cols: [ts, comp_id]}

     default_asis:
       type: as_is
       indices:
         - {name: time, cols: [timestamp]}

   # Pattern-based matching (checked in order)
   matches:
     # Match any schema starting with "meminfo"
     - schema: "^meminfo"
       apply: meminfo_filter

     # Match any schema starting with "proc" AND instance containing "cpu"
     - schema: "^proc.*"
       instance: ".*cpu.*"
       apply: cpu_stats

     # Match exact schema name
     - schema: procnetdev2
       apply: [netdev_stats, default_asis]

     # Match any network-related schema
     - schema: "^(net|eth|ib).*"
       apply: netdev_stats

   # Default for non-matching schemas
   default: default_asis

**Example 3: Combined digest and pattern matching**

Use both methods for maximum flexibility:

.. code-block:: yaml

   type: flex

   decomposition:
     specific_decomp:
       type: static
       rows:
         - schema: specific_schema
           cols:
             - {src: timestamp, dst: ts, type: ts}
             - {src: value, type: u64}

     pattern_decomp:
       type: static
       rows:
         - schema: pattern_schema
           cols:
             - {src: timestamp, dst: ts, type: ts}
             - {src: data, type: char_array, array_len: 128}

     fallback:
       type: as_is
       indices:
         - {name: time, cols: [timestamp]}

   # Digest checked first (fastest, exact match)
   digest:
     KNOWN_DIGEST_1234: specific_decomp
     KNOWN_DIGEST_5678: specific_decomp

   # Patterns checked second (flexible, for unknowns)
   matches:
     - schema: "^test_.*"
       apply: pattern_decomp

   # Default checked last
   default: fallback

Processing order: digest → matches → default


YAML vs JSON
============

Both formats are supported, but YAML is recommended:

**YAML Advantages:**

- Human-readable with less punctuation
- Supports comments for documentation
- Multi-line strings without escaping
- More maintainable for complex configurations

**JSON Advantages:**

- Programmatically generated configurations
- Strict parsing (catches some errors YAML doesn't)
- Broader tool support

**Converting between formats:**

YAML is a superset of JSON, so valid JSON is valid YAML. To convert
YAML to JSON, use standard conversion tools:

.. code-block:: bash

   # Using Python
   python3 -c 'import yaml, json, sys; \
     print(json.dumps(yaml.safe_load(sys.stdin), indent=2))' \
     < config.yaml > config.json

   # Using yq (YAML processor)
   yq eval -o=json config.yaml > config.json


BEST PRACTICES
==============

1. **Use YAML for maintainability**

   - Add comments explaining non-obvious configurations
   - Use consistent indentation (2 spaces recommended)
   - Group related configurations together

2. **Design efficient schemas**

   - Create appropriate indices for common queries
   - Use fill values for optional metrics
   - Consider storage backend capabilities

3. **Test incrementally**

   - Start with simple configurations
   - Add complexity after verifying basic functionality
   - Use as_is for rapid prototyping

4. **Handle heterogeneity**

   - Use flex decomposition for multiple schema types
   - Provide sensible defaults
   - Use fill values for architecture-specific metrics

5. **Document custom configurations**

   - Add comments explaining the purpose
   - Document expected LDMS schemas
   - Note any special requirements or assumptions

6. **Monitor performance**

   - Watch for row explosion with lists
   - Limit group cache size appropriately
   - Set reasonable timeouts for cleanup


TROUBLESHOOTING
===============

**Sets are skipped:**
  Check that all required columns have either a source metric or a fill
  value. Sets without required metrics are skipped.

**Schema conflicts:**
  Use as_is decomposition (adds digest suffix) or ensure flex
  decomposition maps different schemas to different output names.

**Memory growth:**
  Verify group timeout settings are appropriate. Set limits on group
  cache size. Check for list explosion creating too many rows.

**Missing columns:**
  Ensure dst names match index column names. Verify source metric names
  are correct. Check for typos in configuration.

**Performance issues:**
  Create indices on frequently queried columns. Reduce row expansion
  from lists. Simplify complex operations.


SEE ALSO
========

**ldmsd(8)**, **ldmsctl(8)**, **ldmsd_strgp_add(8)**

**Store plugins:** :ref:`store_sos(7) <store_sos>`,
:ref:`store_csv(7) <store_csv>`, :ref:`store_kafka(7) <store_kafka>`

**Source code:** ``ldms/src/decomp/`` in the OVIS-HPC/ovis repository
