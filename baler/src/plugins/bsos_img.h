/**
 * \file bsos_img.h
 * \author Narate Taerat (narate at ogc dot us)
 *
 * \brief SOS wrapper for Baler Image data
 */
#ifndef __BSOS_IMG_H
#define __BSOS_IMG_H

#include "endian.h"
#include "sos/sos.h"

/*
 * The key in the sos index is big endian.
 */
struct __attribute__ ((__packed__)) bsos_img_key {
	/* lower bytes */
	uint32_t ptn_id;
	uint32_t ts;
	uint32_t comp_id;
	/* higher bytes */
};

static inline
void bsos_img_key_htobe(struct bsos_img_key *k)
{
	k->ptn_id = htobe32(k->ptn_id);
	k->ts = htobe32(k->ts);
	k->comp_id = htobe32(k->comp_id);
}

/*
 * These are the array index to access data in the array.
 */
#define BSOS_IMG_PTN_ID		0
#define BSOS_IMG_SEC		1
#define BSOS_IMG_COMP_ID	2
#define BSOS_IMG_COUNT		3

/**
 * \brief Image SOS wrapper
 */
struct bsos_img {
	sos_t sos;
	sos_index_t index;
};

typedef struct bsos_img *bsos_img_t;

/**
 * \brief Open Baler SOS Image store.
 *
 * \param path The path to the store.
 * \param create A \c create boolean flag. If set to 1, a new store will be
 *               created, if it does not exist.
 *
 * \retval handle The store handle, if success.
 * \retval NULL if failed. The \c errno is also set.
 */
bsos_img_t bsos_img_open(const char *path, int create);

/**
 * \brief Close Baler SOS Image store.
 *
 * \param bsos_img The \c bsos_img_t handle.
 * \param commit SOS commit type.
 *
 * \note After the store is closed, application must not use \c handle again.
 */
void bsos_img_close(bsos_img_t bsos_img, sos_commit_t commit);

#endif
