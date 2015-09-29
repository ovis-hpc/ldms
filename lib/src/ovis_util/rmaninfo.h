#ifndef rmaninfo_h_seen
#define rmaninfo_h_seen
#include <time.h>
#include <stdint.h>
#include <ovis_util/util.h>
#include <pthread.h>
/*
Singleton(ish) manager of resource_info objects, so that plugins
can share metrics of global interest, e.g. to avoid redundant collection work.
Only one plugin needs to know how to update it, but all can read/update if they 
known the name.
*/

typedef void * resource_info_manager;

resource_info_manager create_resource_info_manager();

enum rim_task {
	rim_init, // tinfo is avl; setup info object.
	rim_update, // update data. tinfo is in self->updateData
	rim_final,  // clear the data. tinfo is in self->updateData
};

struct resource_info {
	char *name; // registered name
	char *type; // resource type name is attached to, e.g. node
	uint64_t generation; // number of updates made.
	union {
		int32_t i32;
		uint32_t u32;
		int64_t i64;
		uint64_t u64;
		float	f32;
		double	f64;
		char *str;
	} v; // would be nice to unify with ldms.h v_* union in v3
	void *data;
};

/*
 Callback signature for shared metric implementer.
 Task t indicates what callee should do:
 switch (t) :
 case rim_init:
	cast tinfo to struct attr_value_list pointer config_args,
	allocate, init, and set self->data.
	return 0 if ok, or an errno value if not.
 case rim_update:
	cast self->data to private type pointer, cast tinfo to
	struct timespec* and perform update if needed.
	Expect calls more frequently than desired sampling
	rate and reuse prior results accordingly.
	self->generation++ when value is reread from source.
	return 0 if ok, or an errno value if not.
 case rim_final:
	clear/deallocate self->data and expect no more calls
	unless a cal with rim_init happens.
	return 0.
*/
typedef int (*rim_updatef)(struct resource_info *self,
		enum rim_task t, void * tinfo);

int register_resource_info(resource_info_manager rim, 
	const char *resource_name,
	const char *type,
	struct attr_value_list *config_args,
	rim_updatef update,
	void *updateData);


/* releases manager memory. outstanding resource_info 
 references will still be valid */
void clear_resource_info_manager(resource_info_manager rim);

/* Fetch a counted reference to the resource named. call release_resource_info(ri) when done with it. May be cached and released when convenient.
*/
struct resource_info *get_resource_info(resource_info_manager rim, const char *resource_name);

/* releaase the counted ri */
void release_resource_info(struct resource_info *ri);

/* update ri->v (if needed). update calls will be serialized.  */
int update_resource_info(struct resource_info *ri);

#endif
