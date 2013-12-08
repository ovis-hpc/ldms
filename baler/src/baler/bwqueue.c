#include "bwqueue.h"

void bwq_init(struct bwq *q, size_t qsize)
{
	TAILQ_INIT(&q->head);
	pthread_mutex_init(&q->qmutex, NULL);
	sem_init(&q->nq_sem, 0, qsize);
	sem_init(&q->dq_sem, 0, 0);
}

struct bwq* bwq_alloc()
{
	struct bwq *q = (struct bwq*) malloc(sizeof(*q));
	return q;
}

struct bwq* bwq_alloci(size_t qsize)
{
	struct bwq *q = bwq_alloc();
	if (q)
		bwq_init(q, qsize);
	return q;
}
