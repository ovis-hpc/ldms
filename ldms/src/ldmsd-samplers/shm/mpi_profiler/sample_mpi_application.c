/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "mpi.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define  MASTER		0

#define RESULT_FILE_NAME "result.txt"

#define DDOT_MAX_MEMORY 1024*1024*100
#define DDOT_LENGTH_IN_LOOP_BODY 2560
static double *waste_v;
static double *waste_u;
static int waste_n;

static double getWallTime()
{
	struct timeval time;

	if(gettimeofday(&time, NULL))
		return 0;

	return (double)time.tv_sec * 1000000 + (double)time.tv_usec;
}

static void init_ddot()
{
	waste_n = ((DDOT_MAX_MEMORY) / 2) / sizeof(double);
	waste_v = calloc(waste_n, sizeof(double));
	waste_u = calloc(waste_n, sizeof(double));
	if (!waste_v || !waste_u) {
		waste_n = 0;
		printf("unable to allocate ddot memory\n");
	}
	int i = 0;
	for(i = 0; i < waste_n; i++) {
		waste_v[i] = i % sizeof(double);
		waste_u[i] = (waste_n - i) % sizeof(double);
	}
}
static double ddot(int n)
{
	if(n > waste_n)
		n = waste_n;
	int i = 0;
	double result = 0.0;
	for(i = 0; i < n; i++)
		result += waste_v[i] * waste_u[i];
	return result;
}

static void print_results(int rtaskid, const char* config, double time_spent)
{
	FILE * fp;
	fp = fopen(RESULT_FILE_NAME, "a");
	fprintf(fp, "%s, %d,%f\n", config, rtaskid, time_spent);
	fclose(fp);
}

static int taskid = 0;
static int numtasks = 0;
static int sample_int_array[5] = {17, 22, 25, 30, 11};

static void* buff = NULL;

static void loop_body()
{
	int partner = 0, message = 0;
	MPI_Status status;
	if(taskid < numtasks / 2) {
		partner = numtasks / 2 + taskid;
		/* for variant messeage size uncomments this */
		/*		MPI_Send(sample_int_array+3, 2, MPI_INT, partner, 1,
		 MPI_COMM_WORLD);
		 MPI_Recv(buff, 1, MPI_INT, partner, 1,
		 MPI_COMM_WORLD, &status);
		 MPI_Send(sample_int_array, 5, MPI_INT, partner, 1,
		 MPI_COMM_WORLD);
		 MPI_Recv(buff, 1, MPI_INT, partner, 1,
		 MPI_COMM_WORLD, &status);
		 MPI_Send(sample_int_array+1, 4, MPI_INT, partner, 1,
		 MPI_COMM_WORLD);
		 MPI_Recv(buff, 1, MPI_INT, partner, 1,
		 MPI_COMM_WORLD, &status);
		 MPI_Send(sample_int_array+2, 3, MPI_INT, partner, 1,
		 MPI_COMM_WORLD);*/

		MPI_Send(&taskid, 1, MPI_INT, partner, 1, MPI_COMM_WORLD);
		MPI_Recv(&message, 1, MPI_INT, partner, 1, MPI_COMM_WORLD,
				&status);
		MPI_Send(&taskid, 1, MPI_INT, partner, 1, MPI_COMM_WORLD);
		MPI_Recv(&message, 1, MPI_INT, partner, 1, MPI_COMM_WORLD,
				&status);
		MPI_Send(&taskid, 1, MPI_INT, partner, 1, MPI_COMM_WORLD);
		MPI_Recv(&message, 1, MPI_INT, partner, 1, MPI_COMM_WORLD,
				&status);
		MPI_Send(&taskid, 1, MPI_INT, partner, 1, MPI_COMM_WORLD);
		MPI_Recv(&message, 1, MPI_INT, partner, 1, MPI_COMM_WORLD,
				&status);
	} else if(taskid >= numtasks / 2) {
		partner = taskid - numtasks / 2;
		/* for variant messeage size uncomments this */
		/*		MPI_Recv(buff, 5, MPI_INT, partner, 1,
		 MPI_COMM_WORLD, &status);
		 MPI_Send(&taskid, 1, MPI_INT, partner, 1,
		 MPI_COMM_WORLD);
		 MPI_Recv(buff, 5, MPI_INT, partner, 1,
		 MPI_COMM_WORLD, &status);
		 MPI_Send(&taskid, 1, MPI_INT, partner, 1,
		 MPI_COMM_WORLD);
		 MPI_Recv(buff, 5, MPI_INT, partner, 1,
		 MPI_COMM_WORLD, &status);
		 MPI_Send(&taskid, 1, MPI_INT, partner, 1,
		 MPI_COMM_WORLD);
		 MPI_Recv(buff, 5, MPI_INT, partner, 1,
		 MPI_COMM_WORLD, &status);*/

		MPI_Recv(&message, 1, MPI_INT, partner, 1, MPI_COMM_WORLD,
				&status);
		MPI_Send(&taskid, 1, MPI_INT, partner, 1, MPI_COMM_WORLD);
		MPI_Recv(&message, 1, MPI_INT, partner, 1, MPI_COMM_WORLD,
				&status);
		MPI_Send(&taskid, 1, MPI_INT, partner, 1, MPI_COMM_WORLD);
		MPI_Recv(&message, 1, MPI_INT, partner, 1, MPI_COMM_WORLD,
				&status);
		MPI_Send(&taskid, 1, MPI_INT, partner, 1, MPI_COMM_WORLD);
		MPI_Recv(&message, 1, MPI_INT, partner, 1, MPI_COMM_WORLD,
				&status);
		MPI_Send(&taskid, 1, MPI_INT, partner, 1, MPI_COMM_WORLD);
	}
}

int main(int argc, char *argv[])
{
	int len = 0;
	double start = 0.0, end = 0.0;
	char hostname[MPI_MAX_PROCESSOR_NAME];
	init_ddot();
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &taskid);
	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
	if(argc < 2) {
		printf("%s needs loop length argument\n", argv[0]);
		goto out;
	}
	int maxiter = atoi(argv[1]);
	if(maxiter < 1) {
		printf("%s needs loop length >= 1\n", argv[0]);
		goto out;
	}

	char *config = "test";
	if(argc > 2) {
		config = strdup(argv[2]);
		if(config == NULL) {
			printf("%d: app config decription is null\n\r", taskid);
			config = "test";
		}
	}
	buff = malloc(10 * sizeof(int));
	/* need an even number of tasks  */
	if(numtasks % 2 != 0) {
		if(taskid == MASTER)
			printf(
					"Quitting. Need an even number of tasks: numtasks=%d\n",
					numtasks);
	}

	else {
		if(taskid == MASTER)
			printf("MASTER: Number of MPI tasks is: %d\n",
					numtasks);

		MPI_Get_processor_name(hostname, &len);
		printf(
				"Task %d has been started on %s! with DDOT of size (%d)\n",
				taskid, hostname, DDOT_LENGTH_IN_LOOP_BODY);

		/* warm up the network */
		loop_body();
		/* determine partner and then send/receive with partner */
		start = getWallTime();
		while(maxiter > 0) {
			maxiter--;
			loop_body();
			ddot(DDOT_LENGTH_IN_LOOP_BODY);
		}
		end = getWallTime();

		print_results(taskid, config, (end - start));
		/* print partner info and exit*/
		printf("Task %d has been finished\n", taskid);
	}
	out: free(waste_v);
	free(waste_u);
	MPI_Finalize();
	return 0;
}
