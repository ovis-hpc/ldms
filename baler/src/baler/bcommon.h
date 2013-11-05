/**
 * \file bcommon.h
 * \author Narate Taerat (narate@ogc.us)
 * \defgroup bcommon Baler Common Header.
 * \{
 */
#ifndef __BCOMMON_H
#define __BCOMMON_H

#include <string.h>

#define BTKN_SYMBOL_STR  ",.:;`'\"<>\\/|[]{}()+-*=~!@#$%^&?"

/**
 * \brief Baler's Offset to pointer macro.
 * \param relm Relative memory address.
 * \param off Offset value.
 * \return The pointer ((void*)relm)+off if \a off > 0.
 * \return NULL if \a off <= 0.
 */
#define BPTR(relm, off) ((off<=0)?(NULL):((((void*)relm)+(off))))

/**
 * \brief Baler's Pointer to Offset macro.
 * \param relm Relative memory address.
 * \param ptr The pointer.
 * \return Offset (in bytes).
 */
#define BOFF(relm, ptr) (((void*)ptr) - ((void*)relm))

/* ENUM with STRING helper */
#define BENUM(PREFIX, NAME) PREFIX ## NAME
#define BENUM_STR(PREFIX, NAME) #PREFIX #NAME

/*
 * Usage Example:
 * #define MY_ENUM_LIST(PREFIX, MACRO) \
 * 	MACRO(PREFIX, ONE), \
 * 	MACRO(PREFIX, TWO), \
 * 	MACRO(PREFIX, THREE),
 *
 * enum my_enum {
 * 	MY_ENUM_LIST(MY_ENUM_, BENUM)
 * };
 *
 * char *my_enum_str[] = {
 * 	MY_ENUM_LIST(, BENUM_STR) //PREFIX can be empty too
 * };
 *
 * This code will get expanded into the following:
 *
 * enum my_enum {
 * 	MY_ENUM_ONE,
 * 	MY_ENUM_TWO,
 * 	MY_ENUM_THREE,
 * };
 *
 * char *my_enum_str[] = {
 * 	"ONE",
 * 	"TWO",
 * 	"THREE",
 * };
 *
 */

/**
 * Find \c key in \c table and returns the index.
 *
 * '''IMPORTANT''' The strings in this function are compared with character case
 * ignored.
 * \param table Array of char*.
 * \param table_len The number of strings in the table.
 * \param key The element to look for.
 * \returns index of \c key in \c table.
 * \returns -1 if not found.
 */
static
int bget_str_idx(const char *table[], int table_len, const char *key)
{
	int i;
	for (i=0; i<table_len; i++) {
		if (strcasecmp(table[i], key) == 0)
			return i;
	}
	return table_len;
}

/**\}*/
#endif /* __BCOMMON_H */
