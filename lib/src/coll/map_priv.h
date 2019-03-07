#ifndef __MAP_PRIV_H_
#define __MAP_PRIV_H_

#include <sos/sos.h>

struct map_s {
	sos_t sos;
	sos_schema_t schema;
	sos_attr_t src_attr;
	sos_attr_t tgt_attr;
	sos_iter_t src_iter;
	sos_iter_t tgt_iter;
};

#endif
