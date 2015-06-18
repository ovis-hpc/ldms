#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <rmaninfo.h>

struct priv {
	int tnext;
	int tinc;
};

// this assumes data allocation is managed by registrant.
int rim_update_test(struct resource_info *self, enum rim_task t, void * tinfo)
{
	struct priv *data;
	struct attr_value_list *config_args;
	switch (t) {
	case rim_init:
		printf("rim_init\n");
		config_args = tinfo;
		char *value = av_value(config_args, "interval");
		if (!value)
			return EINVAL;
		int delay = atoi(value);
		((struct priv*)self->data)->tnext = 0;
		((struct priv*)self->data)->tinc = delay;
		self->v.i64 = 0;
		return 0;
	case rim_update: {
		struct timespec *tp = tinfo;
		printf("rim_update(%d)\n",(int)tp->tv_sec);
		data = self->data;
		if (!self->generation || ! data->tnext) {
			data->tnext = tp->tv_sec;
		}
		if (tp->tv_sec < data->tnext)
			return 0;
		self->v.i64 += 10;
		data->tnext += data->tinc; 
		self->generation++;
		return 0;
	}
	case rim_final:
		printf("rim_final\n");
		self->data = NULL;
		break;
	default:
		return EINVAL;
	}
	return 0;
}

int main(int argc, char **argv)
{

	struct attr_value_list *av_list, *kw_list;
	av_list = av_new(128);
	kw_list = av_new(128);
	char *s=strdup("interval=3");
	int rc = tokenize(s, kw_list, av_list);

	if (rc) {
		printf("failed tokenize\n");
		free(s);
		exit(EINVAL);
	}

	int i = 0;
	resource_info_manager rim = create_resource_info_manager();
	if (!rim)
		exit(ENOMEM);
	struct priv *dp, p;
	dp = &p;
	int err;
	err = register_resource_info(rim, "test", "node", av_list, rim_update_test, dp);
	if (err)
		exit(EINVAL);

	struct resource_info *testri = get_resource_info(rim, "test");
	if (!testri)
		exit(EINVAL);

	clear_resource_info_manager(rim);

	while (i<12) {
		int err =  update_resource_info(testri);
		if (err) {
			return err;
		}
		printf("%d %" PRIu64 " %" PRId64 "\n", p.tnext,
			testri->generation, testri->v.i64);
		sleep(1);
		i++;
	}
	
	release_resource_info(testri);

	free(s);
	free(av_list);
	free(kw_list);

	return 0;
}
