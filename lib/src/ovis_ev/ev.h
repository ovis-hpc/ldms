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
	uint8_t e_data[0];
} *ev_t;

typedef enum ev_status_e {
	EV_OK = 0,
	EV_FLUSH,
} ev_status_t;

/**
 * \brief The worker's event callback function
 *
 * \param src Sending worker handle
 * \param dst Receiving worker handle
 * \param status EV_OK, or EV_FLUSH
 * \param ev The event handle
 * \retval The result of handling the event
 */
typedef int (*ev_actor_t)(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev);

ev_t ev_new(ev_type_t evt);
ev_t ev_get(ev_t ev);
void ev_put(ev_t ev);

/**
 * \brief Creates a worker
 *
 * A worker implements a single threaded execution context for
 * events. See the ev_post() documentation for information on
 * the ordering of event delivery.
 *
 * Workers must have a name that is unique in the process.
 *
 * The thread that implements the worker will be given the name with
 * the last 16 characters of the worker name as an aid to debugging.
 *
 * \param name The worker name
 * \param actor_fn A pointer to the worker's actor function
 * \retval 0 The worker was created
 * \retval EINVAL An invalid argument was specified
 * \retval ENOMEM Insufficient resources
 * \retval EEXIST A worker named \c name already exists
 */
ev_worker_t ev_worker_new(const char *name, ev_actor_t actor_fn);

/**
 * \brief Return the worker's name
 *
 * \param w The worker handle
 * \returns The worker name
 */
const char *ev_worker_name(ev_worker_t w);

/**
 * \brief Get the worker handle
 *
 * \param name The worker name
 * \returns The worker handle or NULL if the worker is not found
 */
ev_worker_t ev_worker_get(const char *name);

/**
 * \brief Create an event type
 *
 * Event types allow for workers to register for events and deliver
 * them to actors. Actors that handle multiple events may also use to
 * the event type to determine the actions to take.
 *
 * If the \c name already exists, \c errno is set to \c EEXIST and
 *  \c NULL is returned. If there is insufficient memory, \c errno is set
 * to ENOMEM and NULL is returned.
 *
 * \param name The unique event name.
 * \param size The number of bytes required for application data
 * \returns !NULL The event type handle or NULL on error.
 */
ev_type_t ev_type_new(const char *name, size_t size);

/**
 * \brief Get the event type
 *
 * Workers will register for occurrences of this event type by
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
 * An event can be posted only once before it is delivered. After it
 * is delivered, it can be posted again. The event actor can re-post
 * the event from inside the actor function.
 *
 * If multiple threads attempt to post the same event all but
 * one will receive EBUSY. If the event is already posted all
 * will receive EBUSY.
 *
 * Events posted with a NULL timeout will be delivered as soon as
 * possible in the order they were posted. If multiple threads are
 * posting events to the same worker, the delivery order is
 * unspecified.
 *
 * Events posted with a timeout will be delivered when the timeout
 * expires. If two events are posted with the same timeout, the order
 * in which they are delivered is undefined.
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
 * \brief Cancel a posted event
 *
 * A cancelled event is delivered as soon as possible to the
 * associated actor function with an event status of EV_FLUSH.
 *
 * Note that is not guaranteed that the event will be delivered with
 * EV_FLUSH if it is concurrently being delivered by the worker.
 *
 * The caller is only guaranteed that the event will be delivered
 * exactly once with either EV_OK, or EV_FLUSH.
 *
 * \param ev The event to cancel
 * \retval 0 The event was cancelled
 * \retval ENOENT The event was not posted
 */
int ev_cancel(ev_t ev);

/**
 * \brief Flush all events queued to a worker
 *
 * This function has the affect of calling ev_cancel() on all events
 * currently posted to the worker. The event actors will all be called
 * with a status of \c EV_FLUSH.
 *
 * \param w The worker handle
 */
void ev_flush(ev_worker_t w);

/**
 * \brief Convenience function to schedule a timespec in the future
 *
 * Gets the current time in \c to and then adds \c secs to \c tv_sec,
 * and \c nsecs to \c tv_nsec
 *
 * Calling ev_sched_to(&to, 0, 0) will return the current time in
 * \c to.
 *
 * \param to Pointer to a timespec structure
 * \param secs Seconds to add to to->tv_sec
 * \param nsecs Nanoseconds to add to to->tv_nsec
 */
void ev_sched_to(struct timespec *to, time_t secs, int nsecs);

/**
 * \brief Convince macro to access user data in the event
 *
 * \param _ptr_ ev_t pointer
 * \param _type_ The type to which the event data is cast
 * \retval The event data cast to _type_
 */
#define EV_DATA(_ptr_, _type_) ((_type_ *)(_ptr_))

/**
 * \brief Return an event type's internal event id.
 *
 * This can be useful for indexing application data by a unique event
 * number.
 *
 * \param t The event type handle
 * \returns The event type id
 */
uint32_t ev_type_id(ev_type_t t);

/**
 * \brief Return the event type name
 *
 * \param t The event type handle
 * \returns The event type name
 */
const char *ev_type_name(ev_type_t t);

/**
 * \brief Return an event's type
 *
 * \param ev The event handle
 * \returns The event type
 */
ev_type_t ev_type(ev_t ev);

/**
 * \brief Instruct the worker on how to dispatch events
 *
 * This function associates an event type with an event
 * actor. Whenever an event of the specified type is delivered by this
 * worker, it is passed to the specified \c fn.
 *
 * \param w The worker handle
 * \param t The event type handle
 * \param fn The actor function that handles this event type
 * \retval 0 Success
 * \retval EINVAL The \c w or \c t parameters are invalid.
 */
int ev_dispatch(ev_worker_t w, ev_type_t t, ev_actor_t fn);

/**
 * \brief Returns 1 if the event is posted
 *
 * \retval 1 Event is posted
 * \retval 0 Event is not posted
 */
int ev_posted(ev_t ev);

/**
 * \brief Returns 1 if the event is canceled
 *
 * \retval 1 Event is canceled
 * \retval 0 Event is not canceled
 */
int ev_canceled(ev_t ev);

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


/**
 * \brief Return the time difference in seconds
 *
 * Returns the difference in seconds of \c tsa - \c tsb
 *
 * \param tsa Pointer to timespec
 * \param tsb Pointer to timespec
 * \returns The difference in seconds
 */
double ev_time_diff(struct timespec *tsa, const struct timespec *tsb);

/**
 * \brief Return the number of pending events on the worker
 *
 * The returned number does not include the event that is
 * currently handled by the application.
 *
 * \param w Worker
 *
 * \return A non-negative number. 0 means no pending events.
 */
int ev_pending(ev_worker_t w);

#endif
