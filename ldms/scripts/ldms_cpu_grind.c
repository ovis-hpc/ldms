/**
 * This program provides a way to load a cpu with arithmetic of
 * various sizes and types. Run it with --help for options.
 * It provides a way of loading cores so that it may be
 * harder for ldmsd to get cycles on a regular schedule.
 * Then examine the spread of delays in the timestamps of
 * metric sets compared to their target sample time.
 *
 * Understanding the noise of a system, including the
 * overhead of adding ldmsd, requires a more complex
 * statistical benchmark such as psnap:
 * https://fossies.org/dox/cbench_release_1.3.0/parse__psnap_8pl_source.html
 *
 * The matrix size used is reported so that it can be related
 * to cache sizes reported by lscpu.
 *
 * The load (compiled as 'prog') can be pinned to a cpu C and socket memory S with:
 * numactl --physcpubind $C --membind=$S -- prog -n 600 -r 100
 */
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <getopt.h>
#include <sys/resource.h>


// Initialize the array a size nxn with random values
void initialize_square_array_with_random_values(int seed, int N, double **a) {
	srand(seed);
	int i;
	int j;
	for (i = 0; i < N; i++) {
		for ( j = 0; j < N; j++) {
			a[i][j] = (double)rand() / RAND_MAX;
		}
	}
}

/* return the bytes read by arithmetic, and update out.
 * coef: scalar
 * a: nxn
 * in, out: n long
 *
 * where bytes are read from varies with size n and hardware cache behavior.
 */
typedef int (*func_t)(int n, double *coef, double **a, double *in, double *out);

struct meth {
	const char *name;
	func_t f;
	const char *desc;
};

void v_set(int n, double *v, double x)
{
	int i = 0;
	for ( ; i < n; ) v[i++] = x;
}

double v_dot(int n, double *v, double *u)
{
	int i = 0;
	double sum = 0;
	for ( ; i < n; i++)
		sum += v[i]*u[i];
	return sum;
}

// out += in
void v_add(int n, double *in, double *out)
{
	int i = 0;
	for ( ; i < n; i++)
		out[i] += in[i];
}

void v_scale(int n, double coef, double *in)
{
	int i = 0;
	for ( ; i < n; i++)
		in[i] *= coef;
}

int f_add(int n, double *coef, double **a, double *in, double *out)
{
	int i;
	for (i = 0; i < n; i++)
		v_add(n, a[i], out);
	return 2*n*n + n;
}

int f_multiply(int n, double *coef, double **a, double *in, double *out)
{
	int i;
	for (i =0; i < n; i++)
		out[i] = v_dot(n, a[i], in);
	return n*n;
}

int f_scale(int n, double *coef, double **a, double *in, double *out)
{
	int i;
	v_set(n, out, 0);
	for (i = 0; i < n; i++)
		v_scale(n, *coef, a[i]);
	return n*n;
}

// a = exp((a+coef/2))
int f_exp(int n, double *coef, double **a, double *in, double *out)
{
	int i,j;
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			a[i][j] = exp((a[i][j]+*coef)/2);
	return n*n;
}

int f_cos(int n, double *coef, double **a, double *in, double *out)
{
	int i,j;
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			a[i][j] = cos(a[i][j]);
	return n*n;
}

struct meth methods[] = {
	{ "add", f_add, "sum the rows of a" },
	{ "multiply", f_multiply, "compute out = a dot in" },
	{ "scale", f_scale, "compute a *= coef" },
	{ "exp", f_exp, "compute a = exp( (a + coef)/2 ) as scalars" },
	{ "cos", f_cos, "compute a = cos(a) as scalars" },
	{ NULL, NULL, NULL}
};


void print_help() {
	printf("A program to keep cpus busy under ldmsd.\n");
	printf("Usage: program [options]\n");
	printf("Options:\n");
	printf("  -n, --n <int>              Set the value of vector size N\n");
	printf("  -m, --method <name>        Pick method to use.\n");
	printf("  -p, --pid <pathname>       Write process id to file pathname.\n");
	printf("                             No redundancy checking included.\n");
	printf("  -r, --repeat <int>         Set the repetition count. Use -1 for no limit.\n");
	printf("  -s, --seed <int>           Set the repetition count.\n");
	printf("  -c, --coefficient <double> Set the coefficient.\n");
	printf("  -h, --help                 Display this help message\n");
	printf("name is one of:\n");
	int k = 0;
	while (methods[k].name != NULL) {
		printf("    %s: %s\n", methods[k].name, methods[k].desc);
		k++;
	}
	printf("When the time to execute the test exceed 1-10 milliseconds,\n");
	printf("operating system noise can severely degrade results by\n");
	printf("introducing slow outliers.\n");

}


double *create_vector(int n)
{
	if (n >= 0)
		return malloc(n*sizeof(double));
	else
		return NULL;
}

void destroy_vector(double *v)
{
	free(v);
}

void destroy_array(double **a)
{
	if (!a)
		return;
	int i = 0;
	while (a[i] != NULL) {
		destroy_vector(a[i]);
		i++;
	}
	free(a);
	return;
}

double ** create_array(int n)
{
	if (n < 1)
		return NULL;
	int i;
	double **hdr = calloc(n+1, sizeof(double *));
	if (!hdr)
		return NULL;
	for (i = 0; i < n; i++) {
		hdr[i] = create_vector(n);
		if (!hdr[i]) {
			destroy_array(hdr);
			return NULL;
		}
	}
	return hdr;
}

// get timespec diff in ns
int64_t diff_timespec(struct timespec *start, struct timespec *end)
{
	struct timespec result = {0,0};
	result.tv_sec = end->tv_sec - start->tv_sec;
	result.tv_nsec = end->tv_nsec - start->tv_nsec;
	if (result.tv_nsec < 0) {
		--(result.tv_sec);
		result.tv_nsec += 1000000000;
	}
	int64_t diff = result.tv_nsec + result.tv_sec * 1000000000;
	return diff;
}

// get the timeval diff in ns
int64_t diff_timeval(struct timeval *start, struct timeval *end)
{
	struct timeval d = {0 ,0};
	timersub(end, start, &d);
	int64_t diff_ns = d.tv_usec*1000 + d.tv_sec *1000000000;
	return diff_ns;
}

#define oom_check(o, n, otype) _oom_check(o, n, #o, otype)

void _oom_check(void *o, int n, const char *oname, const char *otype)
{
	if (!o) {
		fprintf(stderr, "Out of memory creating %s %s of size %d\n", otype, oname, n);
		fflush(0);
		exit(ENOMEM);
	}
}

int main(int argc, char **argv) {
	int N = 8;
	int o;
	double c = 0.5;
	const char * m = NULL;
	const char * p = NULL;
	int r = 10;
	int s = 1;
	func_t f = NULL;
	const char *name = NULL;

	static struct option long_options[] = {
		{"n", required_argument, 0, 'n'},
		{"method", required_argument, 0, 'm'},
		{"pidfile", required_argument, 0, 'p'},
		{"repeat", required_argument, 0, 'r'},
		{"seed", required_argument, 0, 's'},
		{"coefficient", required_argument, 0, 'c'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	while ((o = getopt_long(argc, argv, "hn:r:s:m:p:c:", long_options, NULL)) != -1) {
		switch (o) {
		case 'n':
			N = atoi(optarg);
			if (N < 8 ) {
				fprintf(stderr,"n %d must be at least 8, not %s\n", N, optarg);
				return EINVAL;
			}
			break;
		case 'r':
			r = atoi(optarg);
			if (!r || r < -1) {
				fprintf(stderr,"n %d must be -1 or at least 1, not %s\n", N, optarg);
				return EINVAL;
			}
			break;
		case 's':
			s = atoi(optarg);
			if (!s)
				s = 1;
			break;
		case 'm':
			{
				int k = 0;
				m = optarg;
				while (methods[k].name != NULL) {
					if (!strcmp(m, methods[k].name)) {
						f = methods[k].f;
						name = methods[k].name;
						break;
					}
					k++;
				}
				if (!f) {
					fprintf(stderr,"Unknown method %s requested\n", m);
					return EINVAL;
				}
			}
			break;
		case 'p':
			{
				p = optarg;
				FILE *pf = fopen(p,"w");
				if (!pf) {
					fprintf(stderr,"Failed to open pidfile %s (%s)\n", p, strerror(errno));
					return errno;
				}
				fprintf(pf, "%" PRId64, (int64_t)getpid());
				fclose(pf);
			}
			break;
		case 'c':
			c = atof(optarg);
			if (!c) {
				fprintf(stderr,"coefficient must be nonzero, not %s\n", optarg);
				return EINVAL;
			}
			break;
		case 'h':
			print_help();
			exit(0);
		default:
			print_help();
			exit(1);
		}
	}

	if (!f) {
		f = methods[0].f;
		name = methods[0].name;
	}
	printf("n = %d\n", N);
	printf("repeat = %d\n", r);
	printf("seed = %d\n", s);
	printf("method: %s\n", name);
	printf("coefficient = %f\n", c);

	double **a = NULL;
	double *vin = NULL;
	double *vout = NULL;
	double *swap = NULL;
	vin = create_vector(N);
	oom_check(vin, N, "vector");

	vout = create_vector(N);
	oom_check(vout, N, "vector");

	a = create_array(N);
	oom_check(a, N, "array");

	initialize_square_array_with_random_values(1, N, a);
	size_t bytes = 0;
	struct timespec start, end;
	struct rusage ustart, uend;
	clock_gettime(CLOCK_MONOTONIC, &start);
	int i;
	if (r == -1) {
		while (1) {
			f(N, &c, a, vin, vout);
			swap = vout;
			vout = vin;
			vin = swap;
			swap = NULL;
		}
	} else {
		getrusage(RUSAGE_SELF, &ustart);
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (i = 0; i < r ; i++) {
			bytes += f(N, &c, a, vin, vout);
			swap = vout;
			vout = vin;
			vin = swap;
			swap = NULL;
		}
		getrusage(RUSAGE_SELF, &uend);
		clock_gettime(CLOCK_MONOTONIC, &end);

		int64_t dt_ns = diff_timespec(&start, &end);
		int64_t dtv_ns = diff_timeval(&ustart.ru_utime, &uend.ru_utime);
		int64_t sys_dtv_ns = diff_timeval(&ustart.ru_stime, &uend.ru_stime);
		printf("bytes = %" PRId64 "\n", bytes);
		printf("dtv_ns = %" PRId64 "\n", dtv_ns);
		printf("dt_user_s = %g\n", dtv_ns*1e-9);
		printf("dt_sys_s = %g\n", sys_dtv_ns*1e-9);
		printf("dt_clock_s = %g\n", dt_ns*1e-9);
		double rate = (double)bytes/1024/1024/1024 / (dt_ns / 1e9);
		double urate = (double)bytes/1024/1024/1024 / (dtv_ns / 1e9);
		printf("GB/s = %g\n", rate);
		printf("(u)GB/s = %g\n", urate);
	}
	printf("matrix size (kb) = %g\n", N*N*8.0 / 1024);
	destroy_vector(vin);
	destroy_vector(vout);
	destroy_array(a);
	return 0;
}
