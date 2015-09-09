/**
 *  The Gemini Performance Counter driver.
 *
 *  Copyright (C) 2013 Cray Inc. All Rights Reserved.
 *
 */
#ifndef _GPCD_KAPI_H_
#define _GPCD_KAPI_H_

#include <linux/types.h>

int gpcd_invalid_read_registers(uint32_t num_mmrs, uint64_t *mmr_addrs);
int gpcd_read_registers(uint32_t num_mmrs,
			uint64_t *mmr_addrs, uint64_t *mmr_data);


#endif /* _GPCD_KAPI_H_ */
