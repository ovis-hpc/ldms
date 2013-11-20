#ifndef __SOS_IMG_CLASS_DEF_H
#define __SOS_IMG_CLASS_DEF_H

/**
 * SOS Object definition for Image SOS.
 *
 * An object in Image SOS is a tuple of \<{ts, comp_id}*, ptn_id*, count \>.
 * The first field is a couple of timestamp (ts) and component ID (comp_id).
 * The second field represents pattern ID, and the third field is the count of
 * occurrences of such pattern at timestamp x component. The fields that marked
 * with '*' are indexed.
 */
static
SOS_OBJ_BEGIN(sos_img_class, "BalerSOSImageClass")
	SOS_OBJ_ATTR_WITH_KEY("time_comp_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR_WITH_KEY("ptn_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("count", SOS_TYPE_UINT32),
SOS_OBJ_END(3);

struct bout_sos_img_key {
	uint32_t ts;
	uint32_t comp_id;
};


#endif
