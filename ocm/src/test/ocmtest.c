#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <assert.h>
#include "ocm.h"

const char *short_opt = "?h:p:P:x:";
struct option long_opt[] = {
	{"host", 1, 0, 'h'},
	{"port", 1, 0, 'p'},
	{"PORT", 1, 0, 'P'},
	{"xprt", 1, 0, 'x'},
	{0, 0, 0, 0}
};

char *host = NULL;
uint16_t port = 0;
uint16_t PORT = 54321;
char *xprt = "sock";

void print_usage()
{
	printf(
"Usage: ocmtest [-h HOST] [-p port] [-P listen_port] [-x Transport]\n"
	);
}

void handle_arg(int argc, char **argv)
{
	char c;
loop:
	c = getopt(argc, argv, short_opt);
	switch (c) {
	case 'h':
		host = optarg;
		break;
	case 'p':
		port = atoi(optarg);
		break;
	case 'P':
		PORT = atoi(optarg);
		break;
	case 'x':
		xprt = optarg;
		break;
	case -1:
		return;
		break;
	default:
		print_usage();
		exit(-1);
	}
	goto loop;
}

int req_cb(struct ocm_event *e)
{
	const char *key = ocm_cfg_req_key(e->req);
	if (!host) {
		/* I am a client, reject everything */
		ocm_event_resp_err(e, -1, key, "I am a receiver.");
		return 0;
	}
	if (strcmp(key, "config_me")) {
		ocm_event_resp_err(e, -1, key, "REJECTED!!!");
		return 0;
	}
	struct ocm_cfg_buff *buff = ocm_cfg_buff_new(4096, key);
	char _buff[4096];
	struct ocm_value *v = (void*)_buff;

	/* first test */
	ocm_cfg_buff_add_verb(buff, "add_host");

	ocm_value_set_s(v, "abc.def.ghi");
	ocm_cfg_buff_add_av(buff, "host", v);

	v->type = OCM_VALUE_UINT16;
	v->u16 = 12345;
	ocm_cfg_buff_add_av(buff, "port", v);

	/* another test */
	ocm_cfg_buff_add_verb(buff, "test_values");

	ocm_value_set(v, OCM_VALUE_INT8, INT8_MAX);
	ocm_cfg_buff_add_av(buff, "int8", v);
	ocm_value_set(v, OCM_VALUE_INT16, INT16_MAX);
	ocm_cfg_buff_add_av(buff, "int16", v);
	ocm_value_set(v, OCM_VALUE_INT32, INT32_MAX);
	ocm_cfg_buff_add_av(buff, "int32", v);
	ocm_value_set(v, OCM_VALUE_INT64, INT64_MAX);
	ocm_cfg_buff_add_av(buff, "int64", v);
	ocm_value_set(v, OCM_VALUE_UINT8, UINT8_MAX);
	ocm_cfg_buff_add_av(buff, "uint8", v);
	ocm_value_set(v, OCM_VALUE_UINT16, UINT16_MAX);
	ocm_cfg_buff_add_av(buff, "uint16", v);
	ocm_value_set(v, OCM_VALUE_UINT32, UINT32_MAX);
	ocm_cfg_buff_add_av(buff, "uint32", v);
	ocm_value_set(v, OCM_VALUE_UINT64, UINT64_MAX);
	ocm_cfg_buff_add_av(buff, "uint64", v);
	ocm_value_set(v, OCM_VALUE_FLOAT, 0.55);
	ocm_cfg_buff_add_av(buff, "float", v);
	ocm_value_set(v, OCM_VALUE_DOUBLE, 0.5555);
	ocm_cfg_buff_add_av(buff, "double", v);
	ocm_value_set(v, OCM_VALUE_STR, "test test test");
	ocm_cfg_buff_add_av(buff, "str", v);

	/* Nested test */
	ocm_cfg_buff_add_verb(buff, "nested");

	ocm_value_set_s(v, "Nested attributes.");
	ocm_cfg_buff_add_av(buff, "desc", v);

		/* buff2 is for constructing nested cmd */
		/* intentionally indented to make it clear */
		struct ocm_cfg_buff *buff2 = ocm_cfg_buff_new(4096, "");
		ocm_cfg_buff_add_verb(buff2, ""); /* no real verb */

		ocm_value_set(v, OCM_VALUE_UINT64, 1000000);
		ocm_cfg_buff_add_av(buff2, "name1", v);
		ocm_value_set(v, OCM_VALUE_UINT64, 2000000);
		ocm_cfg_buff_add_av(buff2, "name2", v);
		ocm_value_set(v, OCM_VALUE_UINT64, 3000000);
		ocm_cfg_buff_add_av(buff2, "name3", v);

	ocm_cfg_buff_add_cmd_as_av(buff, "metric_ids", buff2->current_cmd);
	ocm_cfg_buff_free(buff2);

#if 0
	/* alternative to using ocm_value_set function */
	v->type = OCM_VALUE_INT8;
	v->i8 = 127;
	ocm_cfg_buff_add_av(buff, "int8", v);

	v->type = OCM_VALUE_INT16;
	v->i16 = 32767;
	ocm_cfg_buff_add_av(buff, "int16", v);

	v->type = OCM_VALUE_INT32;
	v->i32 = 2147483647;
	ocm_cfg_buff_add_av(buff, "int32", v);

	v->type = OCM_VALUE_INT64;
	v->i64 = INT64_MAX;
	ocm_cfg_buff_add_av(buff, "int64", v);

	v->type = OCM_VALUE_UINT8;
	v->u8 = 255;
	ocm_cfg_buff_add_av(buff, "uint8", v);

	v->type = OCM_VALUE_UINT16;
	v->u16 = 65535;
	ocm_cfg_buff_add_av(buff, "uint16", v);

	v->type = OCM_VALUE_UINT32;
	v->u32 = 4294967295;
	ocm_cfg_buff_add_av(buff, "uint32", v);

	v->type = OCM_VALUE_UINT64;
	v->u64 = UINT64_MAX;
	ocm_cfg_buff_add_av(buff, "uint64", v);

	v->type = OCM_VALUE_FLOAT;
	v->f = 0.5;
	ocm_cfg_buff_add_av(buff, "float", v);

	v->type = OCM_VALUE_DOUBLE;
	v->d = 0.55;
	ocm_cfg_buff_add_av(buff, "double", v);

	ocm_value_set_s(v, "Test string");
	ocm_cfg_buff_add_av(buff, "string", v);
#endif

	ocm_event_resp_cfg(e, buff->buff);

	ocm_cfg_buff_free(buff);
}

void print_cmd(ocm_cfg_cmd_t cmd, int level);

void print_ocm_value(const struct ocm_value *v, int level)
{
	switch (v->type) {
	case OCM_VALUE_INT8:
		printf("%"PRIi8, v->i8);
		break;
	case OCM_VALUE_INT16:
		printf("%"PRIi16, v->i16);
		break;
	case OCM_VALUE_INT32:
		printf("%"PRIi32, v->i32);
		break;
	case OCM_VALUE_INT64:
		printf("%"PRIi64, v->i64);
		break;
	case OCM_VALUE_UINT8:
		printf("%"PRIu8, v->u8);
		break;
	case OCM_VALUE_UINT16:
		printf("%"PRIu16, v->u16);
		break;
	case OCM_VALUE_UINT32:
		printf("%"PRIu32, v->u32);
		break;
	case OCM_VALUE_UINT64:
		printf("%"PRIu64, v->u64);
		break;
	case OCM_VALUE_FLOAT:
		printf("%f", v->f);
		break;
	case OCM_VALUE_DOUBLE:
		printf("%lf", v->d);
		break;
	case OCM_VALUE_STR:
		printf("%s", v->s.str);
		break;
	case OCM_VALUE_CMD:
		printf("\n");
		print_cmd(v->cmd, level);
		break;
	}
}

void indent(int level)
{
	int i;
	for (i = 0; i < level; i++)
		printf("\t");
}

void print_cmd(ocm_cfg_cmd_t cmd, int level)
{
	struct ocm_av_iter av_iter;
	ocm_av_iter_init(&av_iter, cmd);
	const char *attr;
	const struct ocm_value *value;
	const char *verb = ocm_cfg_cmd_verb(cmd);
	if (verb[0]) {
		indent(level);
		printf("verb: %s\n", verb);
	}
	while (ocm_av_iter_next(&av_iter, &attr, &value) == 0) {
		indent(level+1);
		printf("%s: ", attr);
		print_ocm_value(value, level+1);
		printf("\n");
	}
}

void cfg_received(struct ocm_event *e)
{
	printf("Receiving configuration: %s\n", ocm_cfg_key(e->cfg));
	struct ocm_cfg_cmd_iter cmd_iter;
	ocm_cfg_cmd_iter_init(&cmd_iter, e->cfg);
	ocm_cfg_cmd_t cmd;
	while (ocm_cfg_cmd_iter_next(&cmd_iter, &cmd) == 0) {
		print_cmd(cmd, 1);
	}
	printf("End of Configuration: %s\n", ocm_cfg_key(e->cfg));
}

void err_received(struct ocm_event *e) {
	printf("Receiving error, key: %s msg: %s\n", ocm_err_key(e->err),
			ocm_err_msg(e->err));
}

int cfg_cb(struct ocm_event *e)
{
	switch (e->type) {
	case OCM_EVENT_ERROR:
		err_received(e);
		break;
	case OCM_EVENT_CFG_RECEIVED:
		cfg_received(e);
		break;
	default:
		printf("Unhandled event: %d\n", e->type);
	}
}

int main(int argc, char **argv)
{
	int rc;
	struct addrinfo *ai;
	char p[16];

	handle_arg(argc, argv);
	ocm_t ocm = ocm_create(xprt, PORT, req_cb, NULL);
	if (!ocm) {
		printf("Cannot create ocm.\n");
		exit(-1);
	}

	if (host && port)
		goto server;
client:
	rc = ocm_register(ocm, "config_me", cfg_cb);
	assert(rc == 0);
	rc = ocm_register(ocm, "reject_me", cfg_cb);
	assert(rc == 0);

	goto infinite_loop;

server:
	sprintf(p, "%u", port);
	rc = getaddrinfo(host, p, NULL, &ai);
	if (rc) {
		printf("getaddrinfo error %d\n", rc);
		exit(-1);
	}
	rc = ocm_add_receiver(ocm, ai->ai_addr, ai->ai_addrlen);
	assert(rc == 0);
	freeaddrinfo(ai);

infinite_loop:

	ocm_enable(ocm);

	while (1) {
		sleep(10);
	}

	return 0;
}
