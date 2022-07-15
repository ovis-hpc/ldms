/*
 * parse csv files with starting columns:
	#Time,Time_usec,ProducerName,component_id
 * and find holes in the grid (interval X component_id)
 * Duplicate times reported later are listed.
 * Missing times (assuming a detected interval and 0 offset) are listed.
 */
// g++ -Wall -DPRINT_RANGE csv_time_drops.cxx -lboost_system -g -o csv_time_drops_range
// g++ -Wall csv_time_drops.cxx -lboost_system -g -o csv_time_drops

#include <map>
#include <set>
#include <iomanip>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/program_options.hpp>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <inttypes.h>


using namespace std;
using namespace boost;
namespace po = boost::program_options;

class time_v {
public:
	time_v(uint32_t s, uint32_t us) : sec(s), usec(us) {}
	time_v() : sec(0), usec(0) {}
	uint32_t sec;
	uint32_t usec;
	bool operator <(const time_v *p) {
		return (sec < p->sec || (sec == p->sec && usec < p->usec));
	}
	bool operator <(const time_v &p) const {
		return (sec < p.sec || (sec == p.sec && usec < p.usec));
	}
	double operator -(const time_v &rt) const {
		double ds =
		(double)((int64_t)sec - (int64_t)rt.sec) +
		((int64_t)usec - (int64_t)rt.usec)*1e-6;
		return ds;
        }
};

class time_stamp {
public:
	time_v t;
	time_v latest;
	bool operator<(const time_stamp *p) {
		return (t < (p->t) );
	}
	bool operator<(const time_stamp &p) const {
		return (t < (p.t) );
	}
};

class file_info {
public:
	file_info(uint32_t tmin) : oldest(tmin, 0), newest(0, 0), interval(0), lines(0) {}
	time_v oldest;
	time_v newest;
	uint32_t interval;
	uint64_t lines;
};

class host {
public:
	string producer;
	uint64_t comp_id;
	uint32_t interval_most; /*< rounded interval seen most */
	uint32_t interval_most_count; /*< how many times interval_most is seen */
	multiset< time_stamp > times;
	void add(time_stamp & t, string producer, string compid) {
		times.insert(t);
		this->producer = producer;
		stringstream ss(compid);
		ss >> this->comp_id;
	}
	/* find interval_most and interval_most_count */
	void compute_interval() {
		interval_most = 0;
		map<uint32_t, uint32_t> interval;
		map<uint32_t, uint32_t>::iterator it;
		multiset< time_stamp >::iterator mit;
		time_v last;
		mit=times.begin();
		++mit;
		last = mit->t;
		for ( ; mit != times.end(); ++mit) {
			double d = mit->t - last;
			if (d < 0) {
				continue;
			}
			int32_t bucket = int(round(d));
			it = interval.find(bucket);
			if (it != interval.end())
				interval[bucket]++;
			else
				interval[bucket] = 1;
		}
		for (it = interval.begin(); it != interval.end(); it++) {
			if (it->second > interval_most)
				interval_most = it->first;
				interval_most_count = it->second;
		}
	}
};

void parse_time(string &tsec, string &usec, time_v &t)
{
	sscanf(tsec.c_str(), "%" SCNu32 ".", &t.sec);
	sscanf(usec.c_str(), "%" SCNu32 ".", &t.usec);
}

void parse_file(map <string, host> &m, string fname, map<string, uint32_t> &idata)
{
	boost::filesystem::ifstream fh(fname);
	if (!fh.is_open()) {
		cout << fname << " not opened." << endl;
		return;
	}
	string line, producer, comp_id;
	file_info fi(2000000000);
	while (getline(fh, line)) {
		// format #Time,Time_usec,ProducerName,component_id
		if (line[0] == '#')
			continue;
		istringstream ss(line);
		string token;
		int word = 0;
		time_stamp st;
		string tsec;
		while (std::getline(ss, token, ',') && word < 4) {
			switch (word) {
			case 0:
				tsec = token;
				break;
			case 1:
				parse_time(tsec, token, st.t);
				break;
			case 2:
				producer = token;
				break;
			case 3:
				comp_id = token;
				break;
			}
			word++;
		}
		if (word < 4)
			break; // skip incomplete lines
		if (st.t < fi.oldest)
			fi.oldest = st.t;
		if (fi.newest < st.t)
			fi.newest = st.t;
		st.latest = fi.newest;
		m[producer].add(st, producer, comp_id);
		fi.lines++;
	}
	map <string, host>::iterator it;
	for (it=m.begin(); it!=m.end(); ++it)
		it->second.compute_interval();
	uint32_t imax = 0, imin = 0;
	for (it=m.begin(); it!=m.end(); ++it) {
		if (it->second.interval_most < imin)
			imin = it->second.interval_most;
		if (it->second.interval_most > imax)
			imax = it->second.interval_most;
	}

	cout << "lines " << fi.lines << endl;
	cout << "oldest " << fi.oldest.sec << "." << fi.oldest.usec << endl;
	cout << "newest " << fi.newest.sec << "." << fi.newest.usec << endl;
	fi.interval = imax;
	cout << "interval " << imax  << " seconds" << endl;
	if (imin) {
		cout << "short_interval " << imin  << " seconds" << endl;
	}
	idata[fname] = fi.interval;
}

int main( int argc, char *argv[])
{
	/*  parse csv into time list per host */
	map< string, host> m;
	map<string, uint32_t> idata;
	for (int i = 1; i < argc; i++) {
		parse_file(m, argv[i], idata);
	}
	map< string, uint32_t>::iterator idit;
	uint32_t interval = 0;
	for (idit = idata.begin(); idit != idata.end(); ++idit) {
		if (!interval) {
			interval = idit->second;
			continue;
		}
		if (interval != idit->second) {
			cerr << "Gaps will not be computed. Found " << interval <<
				" and " << idit->second << " intervals." << endl;
			interval = 0;
			break;
		}
	}
	double deltamax = 0.4 + interval;
	map< string, host>::iterator it;
	for (it=m.begin(); it!=m.end(); ++it) {
		time_v last;
		multiset< time_stamp >::iterator mit;
		for (mit=it->second.times.begin(); mit!=it->second.times.end(); ++mit) {
			// std::cout << it->first << " " << mit->t.sec << "." << setw(6) << setfill('0') << mit->t.usec << endl ;
			if (mit->t.sec < mit->latest.sec) {
				std::cout << it->first << " " << mit->t.sec << "." << setw(6) << setfill('0') << mit->t.usec ;
				std::cout << " written again at " << mit->latest.sec <<
					"." << setw(6) << setfill('0') << mit->latest.usec;
				std::cout << endl;
			}
			double delta = mit->t - last;
			if (delta > deltamax && last.sec > 0 && interval) {
#ifdef PRINT_RANGE
				std::cout << it->first << " is missing " << round(delta / interval) - 1 <<
					" steps between \n    " <<
					last.sec << "." << setw(6) << setfill('0') << last.usec
					<< "\nand " <<
					mit->t.sec << "." << setw(6) << setfill('0') << mit->t.usec
					<< endl;
#else
				for (int64_t miss = last.sec + interval; miss < mit->t.sec; miss += interval) {
					std::cout << it->first << " missing " << miss << endl;
				}
#endif
			}
			last = mit->t;
		}
	}
	return 0;
}
