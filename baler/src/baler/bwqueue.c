#include "bwqueue.h"

void bwq_init(struct bwq *q)
{
	TAILQ_INIT(&q->head);
	pthread_mutex_init(&q->qmutex, NULL);
	pthread_mutex_init(&q->avail_cond_mutex, NULL);
	pthread_cond_init(&q->avail_cond, NULL);
}

struct bwq* bwq_alloc()
{
	struct bwq *q = (struct bwq*) malloc(sizeof(*q));
	return q;
}

struct bwq* bwq_alloci()
{
	struct bwq *q = bwq_alloc();
	if (q)
		bwq_init(q);
	return q;
}
