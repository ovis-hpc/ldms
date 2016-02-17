/**
 * \file bsos_msg.h
 * \author Narate Taerat (narate at ogc dot us)
 */
#ifndef __BSOS_MSG_H
#define __BSOS_MSG_H

#include "endian.h"
#include "sos/sos.h"

/* little endian */
#define BSOS_MSG_PTN_ID  0
#define BSOS_MSG_SEC     1
#define BSOS_MSG_USEC    2
#define BSOS_MSG_COMP_ID 3
#define BSOS_MSG_ARGV_0  4

#define BSOS_MSG_IDX_PTH_NAME "index_pth"
#define BSOS_MSG_IDX_TH_NAME "index_th"

/*
 * The key in the sos index is big endian, because key_UINT96 uses memcmp() to
 * compare two keys.
 */
struct __attribute__ ((__packed__)) bsos_msg_key_ptc {
	/* lower bytes, more precedence */
	uint32_t ptn_id;
	uint32_t sec;
	uint32_t comp_id;
	/* higher bytes */
};

static inline
void bsos_msg_key_ptc_set_ptn_id(struct bsos_msg_key_ptc *k, uint32_t ptn_id)
{
	k->ptn_id = htobe32(ptn_id);
}

static inline
void bsos_msg_key_ptc_set_comp_id(struct bsos_msg_key_ptc *k, uint32_t comp_id)
{
	k->comp_id = htobe32(comp_id);
}

static inline
void bsos_msg_key_ptc_set_sec(struct bsos_msg_key_ptc *k, uint32_t sec)
{
	k->sec = htobe32(sec);
}

static inline
void bsos_msg_key_ptc_htobe(struct bsos_msg_key_ptc *k)
{
	k->ptn_id = htobe32(k->ptn_id);
	k->comp_id = htobe32(k->comp_id);
	k->sec = htobe32(k->sec);
}

struct __attribute__ ((__packed__)) bsos_msg_key_tc {
	/* this structure is uint64_t, lower byte has less precedence */
	/* NOTE: We don't care about big-endian machine at the moment */
	uint32_t comp_id;
	uint32_t sec;
};

static inline
void bsos_msg_key_tc_set_comp_id(struct bsos_msg_key_tc *k, uint32_t comp_id)
{
	k->comp_id = comp_id;
}

static inline
void bsos_msg_key_tc_set_sec(struct bsos_msg_key_tc *k, uint32_t sec)
{
	k->sec = sec;
}

/**
 * \brief Message SOS wrapper
 */
struct bsos_msg {
	sos_t sos;
	sos_index_t index_ptc; /* PTH: PatternID, Timestamp, CompID */
	sos_index_t index_tc; /* TH: Timestamp, CompID */
};

typedef struct bsos_msg *bsos_msg_t;

/**
 * \brief Open Baler SOS Image store.
 *
 * \retval handle The store handle, if success.
 * \retval NULL if failed. The \c errno is also set.
 */
bsos_msg_t bsos_msg_open(const char *path, int create);

/**
 * \brief Close Baler SOS Image store.
 *
 * \param bsos_msg The store handle.
 * \param commit The SOS commit type.
 *
 * \note After the store is closed, application must not use \c handle again.
 */
void bsos_msg_close(bsos_msg_t bsos_msg, sos_commit_t commit);

#endif
