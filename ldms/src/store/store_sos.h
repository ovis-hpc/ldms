#ifndef __STORE_SOS_H
#define __STORE_SOS_H
SOS_OBJ_BEGIN(ovis_metric_class_int32, "OvisMetric_int32")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("metric_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR("value", SOS_TYPE_INT32)
SOS_OBJ_END(4);

SOS_OBJ_BEGIN(ovis_metric_class_int64, "OvisMetric_int64")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("metric_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR("value", SOS_TYPE_INT64)
SOS_OBJ_END(4);

SOS_OBJ_BEGIN(ovis_metric_class_uint32, "OvisMetric_uint32")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("metric_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR("value", SOS_TYPE_UINT32)
SOS_OBJ_END(4);

SOS_OBJ_BEGIN(ovis_metric_class_uint64, "OvisMetric_uint64")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("metric_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR("value", SOS_TYPE_UINT64)
SOS_OBJ_END(4);

SOS_OBJ_BEGIN(ovis_metric_class_double, "OvisMetric_double")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("metric_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR("value", SOS_TYPE_DOUBLE)
SOS_OBJ_END(4);
#endif
