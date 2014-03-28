/*
 * ctrl.h
 *
 *  Created on: Jul 9, 2013
 *      Author: nichamon
 */

#ifndef CTRL_H_
#define CTRL_H_

#include <netinet/in.h>
#include <sys/un.h>
#include "../ovis_util/util.h"

struct ctrlsock {
	int sock;
	struct sockaddr *sa;
	size_t sa_len;
	struct sockaddr_in sin;
	struct sockaddr_un rem_sun;
	struct sockaddr_un lcl_sun;
};

int setup_control(char *sockname);
struct ctrlsock *ctrl_connect(char *my_name, char *sockname,
					const char *sock_envpath);
struct ctrlsock *ctrl_inet_connect(struct sockaddr_in *sin);
int ctrl_request(struct ctrlsock *sock, int cmd_id,
		 struct attr_value_list *avl, char *err_str);
void ctrl_close(struct ctrlsock *sock);

#endif /* CTRL_H_ */
