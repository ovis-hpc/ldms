/**
 * \file sos_msg_class_def.h
 * \brief SOS definition for Baler messages.
 *
 * This header file contains the definition of SOS Object definition for
 * Baler message, encapsulated in SOS_TYPE_BLOB.
 */
#ifndef __SOS_MSG_CLASS_DEF_H
#define __SOS_MSG_CLASS_DEF_H

/**
 * Get key from the message.
 */
static
void sos_msg_get_key(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	struct bmsg *b = SOS_TYPE_BLOB__get_fn(attr, obj);
	*(typeof(&b->ptn_id))key->key = b->ptn_id;
	key->keylen = sizeof(b->ptn_id);
}

/**
 * Set \c key corresponding to the given \c value.
 */
static
void sos_msg_set_key(sos_attr_t attr, void *value, sos_key_t key)
{
	/*
	 * According to sos code, this function seems unnecessary.
	 */
	struct bmsg *b = value;
	key->keylen = sizeof(b->ptn_id);
	*(typeof(&b->ptn_id))key->key = b->ptn_id;
}

typedef enum {
	SOS_MSG_SEC=0,
	SOS_MSG_USEC,
	SOS_MSG_COMP_ID,
	SOS_MSG_MSG,
} sos_msg_class_attr_id_t;

/**
 * SOS Object definition for messages.
 *
 * sec - second
 * usec - microsecond
 * comp_id - component id
 * msg - the message (::bmsg)
 */
static
SOS_OBJ_BEGIN(sos_msg_class, "BalerSOSMessageClass")
	SOS_OBJ_ATTR_WITH_KEY("sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("comp_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_UKEY("msg", SOS_TYPE_BLOB,
			       sos_msg_get_key,
			       sos_msg_set_key),
SOS_OBJ_END(4);

#endif
