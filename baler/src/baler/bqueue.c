#include "bqueue.h"

struct bqueue *bqueue_new()
{
	struct bqueue *q = malloc(sizeof(*q));
	if (!q)
		return NULL;
	TAILQ_INIT(&q->head);
	pthread_mutex_init(&q->mutex, NULL);
	pthread_cond_init(&q->cond, NULL);
	q->state = BQUEUE_STATE_ACTIVE;
	return q;
}

void bqueue_nq(struct bqueue *q, struct bqueue_entry *ent)
{
	pthread_mutex_lock(&q->mutex);
	TAILQ_INSERT_TAIL(&q->head, ent, link);
	pthread_cond_broadcast(&q->cond);
	pthread_mutex_unlock(&q->mutex);
}

struct bqueue_entry *bqueue_dq(struct bqueue *q)
{
	struct bqueue_entry *ent = NULL;
	pthread_mutex_lock(&q->mutex);

	while (q->state == BQUEUE_STATE_ACTIVE &&
			(ent = TAILQ_FIRST(&q->head)) == NULL) {
		pthread_cond_wait(&q->cond, &q->mutex);
	}

	if (ent)
		TAILQ_REMOVE(&q->head, ent, link);

	pthread_mutex_unlock(&q->mutex);
	return ent;
}

void bqueue_free(struct bqueue *q)
{
	pthread_cond_destroy(&q->cond);
	pthread_mutex_destroy(&q->mutex);
	free(q);
}

void bqueue_term(struct bqueue *q)
{
	pthread_mutex_lock(&q->mutex);
	q->state = BQUEUE_STATE_TERM;
	pthread_cond_broadcast(&q->cond);
	pthread_mutex_unlock(&q->mutex);
}
