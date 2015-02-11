#ifndef _LDMS_AUTH_H_
#define _LDMS_AUTH_H_

#include <stdint.h>

/** convert network order ints back to 64 bit. */
uint64_t ldms_unpack_challenge(uint32_t chi, uint32_t clo);

/** generate the challenge number. */
uint64_t ldms_get_challenge();

#endif /* _LDMS_AUTH_H_ */
