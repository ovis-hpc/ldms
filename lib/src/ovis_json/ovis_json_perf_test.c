#include <coll/rbt.h>
#include <sys/time.h>
#include "ovis_json.h"
#include "stdio.h"

typedef struct darshanConnector {
	int to;
	int64_t rank;
	uint64_t record_id;
	char *exename;
	const char* env_ldms_stream;
	int server_rc;
	int64_t jobid;
	int64_t uid;
	struct ldms_sps *ln ;
	void * ldms_darsh[2];
	int64_t hdf5_data[5];
	int64_t open_count;
	const char *filename;
	const char *data_set;
	int conn_sem;
	int conn_status;
	int recv_sem;
	struct timespec ts;
} darshanConnector;

static darshanConnector dC = {
	.to = 12345,
	.rank = 12345121345,
	.record_id = 123123123123,
	.exename = "/usr/bin/ultimate/slowness/test",
	.env_ldms_stream = "darshan-stream",
	.server_rc = 3,
	.jobid = 321321321,
	.uid = 12345,
	.hdf5_data = { 123, 12345, 54321, 321, 2 },
	.open_count = 456,
	.filename = "/out/to/someplace/not/real",
	.data_set = "/out/to/someplace/not/real/data_set"
};

char * hname = "myhostname";

int make_string_sprintf(int iter, char * jb11,
	int64_t record_count, char *rwo, int64_t offset, int64_t length, int64_t max_byte, int64_t rw_switch, int64_t flushes,  double start_time, double end_time, struct timespec tspec_start, struct timespec tspec_end, double total_time, char *mod_name, char *data_type)
{
	uint64_t micro_s = tspec_end.tv_nsec/1.0e3;

	sprintf(jb11, "{\"uid\":%" PRId64 ",\"exe\":\"%s\",\"job_id\":%" PRId64
		",\"rank\":%" PRIu64 ",\"ProducerName\":\"%s\",\"file\":\"%s\",\"record_id\":%"
		PRIu64",\"module\":\"%s\",\"type\":\"%s\",\"max_byte\":%" PRId64
		",\"switches\":%" PRId64 ",\"flushes\":%" PRId64 ",\"cnt\":%" PRId64
		",\"op\":\"%s\",\"seg\":[{\"data_set\":\"%s\",\"pt_sel\":%" PRId64
		",\"irreg_hslab\":%" PRId64 ",\"reg_hslab\":%" PRId64 ",\"ndims\":%" PRId64
		",\"npoints\":%" PRId64 ",\"off\":%" PRId64 ",\"len\":%" PRId64
		",\"dur\":%0.6f,\"timestamp\":%lu.%06lu}]}",
	dC.uid,
	dC.exename,
	dC.jobid,
	dC.rank,
	hname,
	dC.filename,
	dC.record_id,
	mod_name,
	data_type,
	max_byte,
	rw_switch,
	flushes,
	record_count,
	rwo,
	dC.data_set,
	dC.hdf5_data[0],
	dC.hdf5_data[1],
	dC.hdf5_data[2],
	dC.hdf5_data[3],
	dC.hdf5_data[4],
	offset,
	length,
	total_time,
	tspec_end.tv_sec,
	micro_s);
	if (!iter)
		printf("%s\n", jb11);
	return 0;
}

/* use elementwise append */
int make_string_jbuf(int iter,
	int64_t record_count, char *rwo, int64_t offset, int64_t length, int64_t max_byte, int64_t rw_switch, int64_t flushes,  double start_time, double end_time, struct timespec tspec_start, struct timespec tspec_end, double total_time, char *mod_name, char *data_type)
{
	uint64_t micro_s = tspec_end.tv_nsec/1.0e3;
	jbuf_t jb, jbd;
	jbd = jb = jbuf_new(); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "uid", "%d,", dC.uid); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "exe", "\"%s\",", dC.exename); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "job_id", "%d,", dC.jobid); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "rank", "%" PRIu64 ",", dC.rank); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "ProducerName", "\"%s\",", hname); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "file", "\"%s\",", dC.filename); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "record_id","%"PRIu64",", dC.record_id); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "module", "\"%s\",", mod_name); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "type","\"%s\",", data_type); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "max_byte", "%lld,", max_byte);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "switches", "%d,", rw_switch);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "flushes", "%d,", flushes);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "cnt", "%d,", record_count); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "op","\"%s\",", rwo); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "seg", "[{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "data_set", "\"%s\",", dC.data_set); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "pt_sel", "%lld,", dC.hdf5_data[0]);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "irreg_hslab", "%lld,", dC.hdf5_data[1]);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "reg_hslab", "%lld,", dC.hdf5_data[2]);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "ndims", "%lld,", dC.hdf5_data[3]);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "npoints", "%lld,", dC.hdf5_data[4]);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "off", "%lld,", offset);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "len", "%lld,", length);if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "dur", "%0.6f,", total_time); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "timestamp", "%lu.%0.6lu", tspec_end.tv_sec, micro_s); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "}]}"); if (!jb) goto out_1;
	if (!iter)
		printf("%s\n", jbd->buf);
	jbuf_free(jbd);
	return 0;
out_1:
	return 1;

}

/* use recycled string and bulk formatted append with escape sequences
 * for quotes. split the format by items with lines for readability and visual matching
 * to correct args. */
int make_string_jbuf_append_recycle(jbuf_t jb, int iter,
	int64_t record_count, char *rwo, int64_t offset, int64_t length, int64_t max_byte, int64_t rw_switch, int64_t flushes,  double start_time, double end_time, struct timespec tspec_start, struct timespec tspec_end, double total_time, char *mod_name, char *data_type)
{
	uint64_t micro_s = tspec_end.tv_nsec/1.0e3;
	jbuf_t jbd = jb;
	if (!jb) goto out_1;
	jbuf_reset(jb);
	jb = jbuf_append_str(jbd,
		"{"
		"\"uid\":%" PRId64 ","
		"\"exe\":\"%s\","
		"\"job_id\":%" PRId64 ","
		"\"rank\":%" PRIu64 ","
		"\"ProducerName\":\"%s\","
		"\"file\":\"%s\","
		"\"record_id\":%" PRIu64 ","
		"\"module\":\"%s\","
		"\"type\":\"%s\","
		"\"max_byte\":%lld,"
		"\"switches\":%d,"
		"\"flushes\":%d,"
		"\"cnt\":%d,"
		"\"op\":\"%s\","
		"\"seg\":[{"
			"\"data_set\":\"%s\","
			"\"pt_sel\":%lld,"
			"\"irreg_hslab\":%lld,"
			"\"reg_hslab\":%lld,"
			"\"ndims\":%lld,"
			"\"npoints\":%lld,"
			"\"off\":%lld,"
			"\"len\":%lld,"
			"\"dur\":%0.6f,"
			"\"timestamp\":%lu.%0.6lu"
			"}]"
		"}",
		dC.uid,
		dC.exename,
		dC.jobid,
		dC.rank,
		hname,
		dC.filename,
		dC.record_id,
		mod_name,
		data_type,
		max_byte,
		rw_switch,
		flushes,
		record_count,
		rwo,
			dC.data_set,
			dC.hdf5_data[0],
			dC.hdf5_data[1],
			dC.hdf5_data[2],
			dC.hdf5_data[3],
			dC.hdf5_data[4],
			offset,
			length,
			total_time,
			tspec_end.tv_sec, micro_s
		);
	if (!iter)
		printf("%s\n", jbd->buf);
	return 0;
out_1:
	return 1;

}

double ldmsd_timeval_diff(struct timeval *start, struct timeval *end)
{
        return (end->tv_sec-start->tv_sec)*1000000.0 + (end->tv_usec-start->tv_usec);
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("%s: need input count\n", argv[0]);
		return 1;
	}
	int count = atoi(argv[1]);
	if (count < 1) {
		printf("%s: need int input\n", argv[0]);
		return 1;
	}
	int i;
	int64_t record_count = 765;
	char *rwo = "rwo_what";
	int64_t offset = 32;
	int64_t length = 512;
	int64_t max_byte = 1024;
	int64_t rw_switch = 1;
	int64_t flushes = 987;
	double start_time = 1.234567;
	double end_time = 3.456789;
	struct timespec tspec_start = { 1000, 100};
	struct timespec tspec_end = { 1001, 101 };
	double total_time = 1.2345;
	char *mod_name ="mod_name";
	char *data_type = "datatype";
	char buf[2048];
	struct timeval tv1, tv2, tv3, tv4;

	gettimeofday(&tv1, NULL);
	for (i = 0; i< count; i++) {
		make_string_sprintf(i, buf,
			record_count, rwo, offset, length, max_byte, rw_switch, flushes, start_time, end_time, tspec_start, tspec_end, total_time, mod_name, data_type);
	}

	gettimeofday(&tv2, NULL);
	for (i = 0; i< count; i++) {
		make_string_jbuf(i,
			record_count, rwo, offset, length, max_byte, rw_switch, flushes, start_time, end_time, tspec_start, tspec_end, total_time, mod_name, data_type);
	}

	gettimeofday(&tv3, NULL);
	jbuf_t jb = jbuf_new();
	if (jb) {
		for (i = 0; i< count; i++) {
			make_string_jbuf_append_recycle(jb, i,
				record_count, rwo, offset, length, max_byte, rw_switch, flushes, start_time, end_time, tspec_start, tspec_end, total_time, mod_name, data_type);
		}
	}
	jbuf_free(jb);

	gettimeofday(&tv4, NULL);
	printf("%d sprintf time us %g\n",count, ldmsd_timeval_diff(&tv1, &tv2));
	printf("%d jbuf elements time us    %g\n",count, ldmsd_timeval_diff(&tv2, &tv3));
	printf("%d jbuf fmt time us    %g\n",count, ldmsd_timeval_diff(&tv3, &tv4));
	return 0;
}
