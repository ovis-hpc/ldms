/*
 * POWERNV debug  perf driver to export inband occ_sensors
 *
 * (C) Copyright IBM 2015
 *
 * Author: Shilpasri G Bhat <shilpa.bhat@linux.vnet.ibm.com>
 *
 * Usage:
 * Build this driver against your kernel and load the module.
 */
/*
 * LDMS Modifications: 
 *
 * (C) Copyright 2017 Open Grid Computing, Inc.
 *
 * Author: Tom TUcker <tom@ogc.us>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/io.h>
#include <linux/perf_event.h>

#include <asm/cputhreads.h>
#include "kldms.h"

#define BE(x, s)	be_to_cpu(x, s)

typedef struct sensor {
	char name[30];
	int metric_id;
	const char *unit;
	u64 paddr;
	u64 vaddr;
	u32 size;
	struct device_attribute attr;
} sensor_t;

typedef struct core {
	int pir;
	sensor_t *sensors;
} core_t;

struct chip {
	int id;
	char name[30];
	int nr_cores;
	u64 pbase;
	u64 vbase;
	sensor_t *sensors;
	core_t	*cores;
} *chips;

sensor_t *system_sensors;

static unsigned int nr_chips, nr_system_sensors, nr_chip_sensors;
static unsigned int nr_cores_sensors;
static unsigned int nr_metrics;
static u64 power_addr;
static u64 chip_power_addr;
static u64 chip_energy_addr;
static u64 system_energy_addr;
static u64 count_addr;

static struct attribute **system_attrs;
static struct attribute ***chip_attrs;

static struct attribute_group system_attr_group = {
	.name = "system",
};

static struct attribute_group **chip_attr_group;
struct kobject *occ_sensor_kobj;

static kldms_set_t occ_sensor_set;

unsigned long be_to_cpu(u64 addr, u32 size)
{
	switch (size) {
	case 16:
		return __be16_to_cpu(*(u16 *)addr);
	case 32:
		return __be32_to_cpu(*(u32 *)addr);
	case 64:
		return __be64_to_cpu(*(u64 *)addr);
	}
	return 0;
}

static ssize_t sensor_attr_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	sensor_t *sensor = container_of(attr, sensor_t, attr);

	return sprintf(buf, "%lu %s\n", BE(sensor->vaddr, sensor->size),
		       sensor->unit);
}

#define add_sensor(node, var, len, reg)					   \
do {									   \
	if (of_property_read_u64(node, "reg", &var.paddr)) {		   \
		pr_info("%s node cannot read reg property\n", node->name); \
		continue;						   \
	}								   \
	reg = of_get_property(node, "reg", &len);			   \
	var.size = of_read_number(reg, 3) * 8;				   \
	if (of_property_read_string(node, "unit", &var.unit)) {		   \
		pr_info("%s node cannot read unit\n", node->name);	   \
	}								   \
	var.vaddr = (u64)phys_to_virt(var.paddr);			   \
	pr_info("Sensor : %s *(%lx)[%d] = %lu\n", node->name,		   \
		(unsigned long)var.vaddr, var.size, BE(var.vaddr, var.size)); \
	var.attr.attr.mode = S_IRUGO;					   \
	var.attr.show = sensor_attr_show;				   \
	var.attr.store = NULL;						   \
} while (0)

static int add_system_sensor(struct device_node *snode)
{
	struct device_node *node;
	const __be32 *reg;
	int len, i = 0;

	for_each_child_of_node(snode, node) {
		add_sensor(node, system_sensors[i], len, reg);
		sprintf(system_sensors[i].name, "%s", node->name);
		system_sensors[i].attr.attr.name = system_sensors[i].name;
		if (!strcmp(node->name, "power"))
			power_addr = system_sensors[i].vaddr;

		if (!strcmp(node->name, "count"))
			count_addr = system_sensors[i].vaddr;

		if (!strcmp(node->name, "system-energy"))
			system_energy_addr = system_sensors[i].vaddr;
		i++;
	}

	return 0;
}

static int add_core_sensor(struct device_node *cnode, core_t *core, int cid)
{
	const __be32 *reg;
	struct device_node *node;
	int i = 0, len;

	if (of_property_read_u32(cnode, "ibm,core-id", &core->pir)) {
		pr_info("Core_id not found");
		return -EINVAL;
	}

	for_each_child_of_node(cnode, node) {
		add_sensor(node, core->sensors[i], len, reg);
		sprintf(core->sensors[i].name, "core%d-%s", cid + 1,
			node->name);
		core->sensors[i].attr.attr.name = core->sensors[i].name;
		i++;
	}

	return 0;
}

static int add_chip_sensor(struct device_node *chip_node, struct chip *chip)
{
	const __be32 *reg;
	u32 len;
	struct device_node *node;
	int j, k;

	if (of_property_read_u32(chip_node, "ibm,chip-id", &chip->id)) {
		pr_err("Chip id not found\n");
		return -ENODEV;
	}

	if (of_property_read_u64(chip_node, "reg", &chip->pbase)) {
		pr_err("Chip Homer sensor offset not found\n");
		return -ENODEV;
	}

	chip->vbase = (u64)phys_to_virt(chip->pbase);
	pr_info("Chip %d sensor pbase= %lx, vbase = %lx (%lx)\n", chip->id,
		(unsigned long)chip->pbase, (unsigned long)chip->vbase,
		BE(chip->vbase + 4, 16));

	j = k = 0;
	for_each_child_of_node(chip_node, node) {
		if (!strcmp(node->name, "core")) {
			add_core_sensor(node, &chip->cores[k], k);
			k++;
			continue;
		}
		add_sensor(node, chip->sensors[j], len, reg);
		sprintf(chip->sensors[j].name, "%s", node->name);
		chip->sensors[j].attr.attr.name = chip->sensors[j].name;
		if (!strcmp(node->name, "power"))
			chip_power_addr = chip->sensors[j].vaddr;
		if (!strcmp(node->name, "chip-energy"))
			chip_energy_addr = chip->sensors[j].vaddr;
		j++;
	}

	return 0;
}


static int init_chip(void)
{
	unsigned int i, j, k, l;
	struct device_node *sensor_node, *node;
	int rc = -EINVAL;

	sensor_node = of_find_node_by_path("/occ_sensors");
	if (!sensor_node) {
		pr_info("Node occ_sensors not found\n");
		return rc;
	}

	if (of_property_read_u32(sensor_node, "nr_system_sensors",
				 &nr_system_sensors)) {
		pr_info("nr_system_sensors not found\n");
		goto out;
	}

	if (of_property_read_u32(sensor_node, "nr_chip_sensors",
				 &nr_chip_sensors)) {
		pr_info("nr_chip_sensors not found\n");
		goto out;
	}

	if (of_property_read_u32(sensor_node, "nr_core_sensors",
				 &nr_cores_sensors)) {
		pr_info("nr_core_sensors not found\n");
		goto out;
	}

	for_each_child_of_node(sensor_node, node)
		if (!strcmp(node->name, "chip"))
			nr_chips++;

	pr_info("nr_chips %d\n", nr_chips);
	chips = kcalloc(nr_chips, sizeof(struct chip), GFP_KERNEL);
	if (!chips) {
		pr_info("Out of memory\n");
		rc = -ENOMEM;
		goto out;
	}

	i = 0;
	for_each_child_of_node(sensor_node, node) {
		if (!strcmp(node->name, "chip")) {
			struct device_node *cnode;

			for_each_child_of_node(node, cnode) {
				if (!strcmp(cnode->name, "core"))
					chips[i].nr_cores++;
			}
			i++;
		}
	}

	system_sensors = kcalloc(nr_system_sensors, sizeof(sensor_t),
				 GFP_KERNEL);
	for (i = 0; i < nr_chips; i++) {
		chips[i].sensors = kcalloc(nr_chip_sensors, sizeof(sensor_t),
					   GFP_KERNEL);
		chips[i].cores = kcalloc(chips[i].nr_cores, sizeof(core_t),
					 GFP_KERNEL);
		for (j = 0; j < chips[i].nr_cores; j++)
			chips[i].cores[j].sensors = kcalloc(nr_cores_sensors,
							    sizeof(sensor_t),
							    GFP_KERNEL);
	}

	i = 0;
	for_each_child_of_node(sensor_node, node) {
		if (!strcmp(node->name, "chip")) {
			rc = add_chip_sensor(node, &chips[i]);
			i++;
		} else {
			rc = add_system_sensor(node);
		}
		if (rc)
			goto out;
	}

	system_attrs = kcalloc(nr_system_sensors + 1, sizeof(struct attribute *),
			       GFP_KERNEL);
	chip_attrs = kcalloc(nr_chips + 1, sizeof(struct attribute **),
			     GFP_KERNEL);

	for (i = 0; i < nr_chips; i++) {
		int size = nr_chip_sensors +
			   nr_cores_sensors * chips[i].nr_cores + 1;

		chip_attrs[i] = kcalloc(size, sizeof(struct attribute *),
					GFP_KERNEL);
	}

	for (i = 0; i < nr_system_sensors; i++)
		system_attrs[i] = &system_sensors[i].attr.attr;
	system_attrs[i] = NULL;

	for (j = 0; j < nr_chips; j++) {
		i = 0;
		for (k = 0; k < nr_chip_sensors; k++)
			chip_attrs[j][i++] = &chips[j].sensors[k].attr.attr;
		for (k = 0; k < chips[j].nr_cores; k++)
			for (l = 0; l < nr_cores_sensors; l++)
				chip_attrs[j][i++] =
					&chips[j].cores[k].sensors[l].attr.attr;
		chip_attrs[j][i] = NULL;
	}
	chip_attrs[j] = NULL;

	chip_attr_group = kcalloc(nr_chips + 1, sizeof(struct attribute_group *),
				  GFP_KERNEL);
	for (i = 0; i < nr_chips; i++) {
		chip_attr_group[i] = kzalloc(sizeof(struct attribute_group),
					     GFP_KERNEL);
		sprintf(chips[i].name, "chip%d", chips[i].id);
		chip_attr_group[i]->name = chips[i].name;
		chip_attr_group[i]->attrs = chip_attrs[i];
	}
	chip_attr_group[i] = NULL;
	system_attr_group.attrs = system_attrs;

out:
	of_node_put(sensor_node);
	return rc;
}

static void clear_chips(void)
{
	int i, j;

	kfree(system_attrs);
	kfree(chip_attrs);
	kfree(system_sensors);
	for (i = 0; i < nr_chips; i++) {
		kfree(chip_attr_group[i]);
		kfree(chips[i].sensors);
		kfree(chips[i].cores);
		for (j = 0; j < chips[i].nr_cores; j++)
			kfree(chips[i].cores[j].sensors);
	}
	kfree(chip_attr_group);
	kfree(chips);

}

static enum ldms_value_type bits_to_type(size_t bits)
{
	switch (bits) {
	case 8:
		return LDMS_V_U8;
	case 16:
		return LDMS_V_U16;
	case 32:
		return LDMS_V_U32;
	case 64:
		return LDMS_V_U64;
	default:
		return LDMS_V_U64;
	}
}

static int create_metric_set(void)
{
	int i, j, k, rc = 0;
	kldms_schema_t schema;
	char mn[256];

#if 0
	schema = kldms_schema_find("occ_sensors");
	if (schema)
		kldms_schema_delete(schema);
#endif
	schema = kldms_schema_new("occ_sensors");
	if (!schema) {
		pr_err("The schema could not be created.\n");
		return ENOMEM;
	}
	pr_info("create metric set %p\n", schema);
	for (i = 0; i < nr_system_sensors; i++) {
		snprintf(mn, sizeof(mn), "system/%s", system_sensors[i].name);
		rc = kldms_schema_metric_add(schema, mn,
					     bits_to_type(system_sensors[i].size),
					     system_sensors[i].unit);
		if (rc < 0) {
			pr_err("Error %d creating the metric %s\n", rc, mn);
			goto err;
		}
		system_sensors[i].metric_id = rc;
		pr_info("Creating metric id %d name %s\n", rc, mn);
	}
	for (i = 0; i < nr_chips; i++) {
		for (j = 0; j < nr_chip_sensors; j++) {
			snprintf(mn, sizeof(mn), "%s/%s",
				 chips[i].name, chips[i].sensors[j].name);
			rc = kldms_schema_metric_add(schema, mn,
						     bits_to_type(chips[i].sensors[j].size),
						     chips[i].sensors[j].unit);
			if (rc < 0) {
				pr_err("Error %d creating the metric %s\n", rc, mn);
				goto err;
			}
			chips[i].sensors[j].metric_id = rc;
			pr_info("Creating metric id %d name %s\n", rc, mn);
		}
		for (j = 0; j < chips[i].nr_cores; j++) {
			for (k = 0; k < nr_cores_sensors; k++) {
				snprintf(mn, sizeof(mn), "%s/%s",
					 chips[i].name, chips[i].cores[j].sensors[k].name);
				rc = kldms_schema_metric_add(schema, mn,
							     bits_to_type(chips[i].cores[j].sensors[k].size),
							     chips[i].cores[j].sensors[k].unit);
				if (rc < 0) {
					pr_err("Error %d creating the metric %s\n", rc, mn);
					goto err;
				}
				chips[i].cores[j].sensors[k].metric_id = rc;
				pr_info("Creating metric id %d name %s\n", rc, mn);
			}
		}
	}
	nr_metrics = rc;
	occ_sensor_set = kldms_set_new("occ_sensors", schema);
	return 0;
 err:
	kldms_schema_delete(schema);
	return rc;
}

static void sample(void)
{
	int i, j, k;
	union ldms_value v;

	if (!occ_sensor_set)
		return;

	kldms_transaction_begin(occ_sensor_set);
	pr_info("sampling metric set %p\n", occ_sensor_set);

	for (i = 0; i < nr_system_sensors; i++) {
		sensor_t *s = &system_sensors[i];
		v.v_u64 = BE(s->vaddr, s->size);
		kldms_metric_set(occ_sensor_set, s->metric_id, &v);
	}

	for (i = 0; i < nr_chips; i++) {
		for (j = 0; j < nr_chip_sensors; j++) {
			sensor_t *s = &chips[i].sensors[j];
			v.v_u64 = BE(s->vaddr, s->size);
			kldms_metric_set(occ_sensor_set, s->metric_id, &v);
		}
		for (j = 0; j < chips[i].nr_cores; j++) {
			for (k = 0; k < nr_cores_sensors; k++) {
				sensor_t *s = &chips[i].cores[j].sensors[k];
				v.v_u64 = BE(s->vaddr, s->size);
				kldms_metric_set(occ_sensor_set, s->metric_id, &v);
			}
		}
	}
	kldms_transaction_end(occ_sensor_set);
}

static int sensor_init(void)
{
	int rc, i;
	rc = init_chip();
	if (rc)
		goto out;

	occ_sensor_kobj = kobject_create_and_add("occ_sensors",
						 &cpu_subsys.dev_root->kobj);
	rc = sysfs_create_group(occ_sensor_kobj, &system_attr_group);
	if (rc) {
		pr_info("Failed to create system attribute group\n");
		goto out_free_chips;
	}

	for (i = 0; i < nr_chips; i++) {
		rc = sysfs_create_group(occ_sensor_kobj, chip_attr_group[i]);
		if (rc) {
			pr_info("Chip %d failed to create chip_attr_group\n",
				chips[i].id);
			goto out_clean_kobj;
		}
	}
	if (create_metric_set())
		goto out_clean_kobj;
	else
		sample();
	return rc;

out_clean_kobj:
	kobject_put(occ_sensor_kobj);
out_free_chips:
	clear_chips();
out:
	return rc;
}

static void sensor_exit(void)
{
	kldms_schema_t schema = kldms_schema_find("occ_sensors");;
	kldms_schema_delete(schema);

	kobject_put(occ_sensor_kobj);

	if (occ_sensor_set) {
		kldms_set_delete(occ_sensor_set);
		occ_sensor_set = NULL;
	}
	clear_chips();
	pr_info("kldms_ibm_pwr unloading...\n");
}

module_init(sensor_init);
module_exit(sensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shilpasri G Bhat <shilpa.bhat at linux.vnet.ibm.com>");
