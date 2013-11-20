/**
 * \file bmlist.h
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 21, 2013
 *
 * \defgroup bmlist Baler MMapped List for Mapped Memory.
 * \{
 * \brief Baler MMaped List is designed to be a linked list in mmap area.
 * It supports only insertion for now (its current use need no deletion).
 */

#ifndef _BMLIST_H
#define _BMLIST_H

#include "bcommon.h"

/**
 * Link structure for Baler MMapped List.
 */
struct bmlist_link {
	int64_t next; /**< offset to the next list entry. */
};

/**
 * \brief Baler MMapped List node that store uint32_t.
 */
struct __attribute__((packed)) bmlnode_u32 {
	uint32_t data;
	struct bmlist_link link;
};

/**
 * \brief A utility macro to get the next element in the list.
 */
#define BMLIST_NEXT(elm, field, bmem) BMPTR(bmem, elm->field.next)

/**
 * \brief Insert head.
 * This macro assumes that \a elm is allocated in the mmapped region.
 * \param head_off_var The offset variable. This variable will also be
 * 		changed to point (in offset sense) to \a elm.
 * \param elm The list element (e.g. node) to be inserted into the list.
 * \param field The field of type ::bmlist_link to access the link of the list.
 * \param bmem The pointer to bmem structure that stores the list.
 */
#define BMLIST_INSERT_HEAD(head_off_var, elm, field, bmem) do { \
	(elm)->field.next = (head_off_var); \
	(head_off_var) = BMOFF(bmem, elm); \
} while (0)

/**
 * \brief For each iteration for Baler Offset List.
 * \param var The variable expression for the iterator.
 * \param head_off The head of the list (being offset relative to \a relm).
 * \param field The field name to access list elements.
 * \param bmem The pointer to bmem structure that stores the list.
 */
#define BMLIST_FOREACH(var, head_off, field, bmem) \
	for ((var) = BMPTR(bmem, head_off);\
		(var); \
		(var) = BMLIST_NEXT(var, field, bmem))

/**\}*/
#endif // _BMLIST_H
