#ifndef __EV_H_
#define __EV_H_
#include <time.h>
#include <inttypes.h>
/**
 * Events (ev_t) are exchanged between workers (ev_worker_t) to do
 * work. Workers that are interested in events register to receive
 * them. Workers that wish to communicate with other workers do so by
 * generating events.
 *
 * There are two fundamental kinds of events: directed, multicast. A
 * directed event is sent directly from one worker to another. A
 * multicast event is sent to any worker that has registered to
 * receive it.
 *
 * ev_worker_t timer = ev_worker_get("TIMER");
 * ev_type_t to = ev_type_get("TIMEOUT");
 * ev_t to_ev = ev_new(to, my_worker, &timeout);
 *
 * ev_send(my_worker, timer, to_ev);
 *
 */

typedef struct ev_worker_s *ev_worker_t;
typedef struct ev_type_s *ev_type_t;
typedef struct ev_s {
	ev_type_t e_type;
	uint32_t e_refcount;
	int e_posted;
	uint8_t e_data[0];
} *ev_t;

/**
 * \brief The worker's event callback function
 *
 * \param evw The worker handle
 * \param ev The event handle
 * \retval The result of handling the event
 */
typedef int (*ev_actor_t)(ev_worker_t src, ev_worker_t dst, ev_t ev);

ev_t ev_new(ev_type_t evt);
ev_t ev_get(ev_t ev);
void ev_put(ev_t ev);

/**
 * \brief Creates a worker
 *
 * \param name The worker name
 * \param actor_fn A pointer to the worker's actor function
 * \retval 0 The worker was created
 * \retval EINVAL An invalid argument was specified
 * \retval ENOMEM Insufficient resources
 * \retval EEXIST A worker named \c name already exists
 */
ev_worker_t ev_worker_new(const char *name, ev_actor_t actor_fn);
const char *ev_worker_name(ev_worker_t w);

/**
 * \brief Get the worker handle
 *
 * \param name The worker name
 * \returns The worker handle or NULL if the worker is not found
 */
ev_worker_t ev_worker_get(const char *name);
ev_type_t ev_type_new(const char *name, size_t size);
ev_type_t ev_type_get(const char *name);

/**
 * \brief Get the event type
 *
 * Workers will register for occurrances of this event type by
 * name.
 *
 * \param name The unique event name
 * \returns An event type handle or NULL if there are insufficient resources
 */
ev_type_t ev_type_get(const char *name);

/**
 * \brief Listen for multi-cast events
 *
 * \param evw The event worker handle
 * \param evt The event type handle
 */
int ev_listen(ev_worker_t evw, ev_type_t evt);

/**
 * \brief Post an event to a worker
 *
 * \param src The source worker
 * \param dst The destination worker
 * \param ev The event
 * \param to The scheduled event deliver time (null == now)
 *
 * \retval 0 Event posted
 * \retval EBUSY The event is already posted
 */
int ev_post(ev_worker_t src, ev_worker_t dst, ev_t ev, struct timespec *to);

/**
 * \brief Flush all events queued to a worker
 *
 * The actor passed to this function _cannot_ make calls that affect
 * the worker \c w from the \c actor_fn or a deadlock will occur.
 *
 * \param w The worker handle
 * \param actor_fn The function to call with each flushed event.
 */
void ev_flush(ev_worker_t w, ev_actor_t actor_fn);

/**
 * \brief Convenience function to schedule a timespec in the future
 */
void ev_sched_to(struct timespec *to, time_t secs, int nsecs);

#define EV_DATA(_ptr_, _type_) ((_type_ *)&(_ptr_)->e_data[0])

uint32_t ev_type_id(ev_type_t t);
const char *ev_type_name(ev_type_t t);
ev_type_t ev_type(ev_t ev);

int ev_dispatch(ev_worker_t w, ev_type_t t, ev_actor_t fn);

/**
 * \brief Returns 1 if the event is posted
 *
 * \retval 1 Event is posted
 * \retval 0 Event is not posted
 */
int ev_posted(ev_t ev);

/**
* \brief Compare two timespec values
*
* Compares two timespace values *tsa and *tsb and returns:
* -1 if tsa < tsb, 0 if tsa == tsb, and 1 if tsa > tsb.
*
* \param tsa Pointer to timespec structure
* \param tsb Pointer to timespec structure
* \retval -1 *tsa < *tsb
* \retval 0 *tsa == *tsb
* \retval 1 *tsa > *tsb
*/
int ev_time_cmp(struct timespec *tsa, const struct timespec *tsb);

#endif
