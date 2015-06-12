#include <gendersplusplus.hpp>
#include <map>
#include <set>
#include <sstream>
//#include <boost/algorithm/string_regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <unistd.h>
#include <cstdlib>


/*

queries we need:
LDMSAGGD_DBG= nodeattr $NODEATTRFILE -v $host ldmsaggd_dbg
LDMSAGGD_STORES= nodeattr $NODEATTRFILE -v $host ldmsd_stores
LDMSAGGD_INTERVAL_DEFAULT= nodeattr $NODEATTRFILE -v $host ldmsaggd_interval_default
LDMSAGGD_OFFSET_DEFAULT= nodeattr $NODEATTRFILE -v $host ldmsaggd_offset_default

for i in $(echo $STORES | tr ":;'\`" "\n"); do
	configtmp=`nodeattr $NODEATTRFILE -v $host ldmsaggd_${i}`
	intervaltmp=`nodeattr $NODEATTRFILE -v $host ldmsaggd_interval_${i}`
	offsettmp=`nodeattr $NODEATTRFILE -v $host ldmsaggd_offset_${i}`
done

*/

using namespace std;
using namespace boost;
using namespace Gendersplusplus;
namespace po = boost::program_options;


void usage(const char *ex)
{
	std::cerr << ex << ": usage: " <<
		ex << " <genders file> <host name> <task>" <<endl;
}

void printvec( const vector< pair< string, string > >& all)
{
	for (int i = 0; i < all.size(); i++) {
		cerr << i<< ": " << all[i].first << " = " <<  all[i].second <<endl;
	}
}

void printvec1( const vector<  string >& all)
{
	for (int i = 0; i < all.size(); i++) {
		cerr << i<< ": " << all[i] <<endl;
	}
}

void printset(const  set<string>& m) {
	for(set<string>::const_iterator it = m.begin(); it != m.end(); ++it)
	{
	    cerr << *it << "\n";
	}
}

void printmap(const  map<string, string>& m) {
	for(map<string, string >::const_iterator it = m.begin();
	    it != m.end(); ++it)
	{
	    cerr << it->first << " " << it->second << "\n";
	}
}

#if 0
// c list_attr_val equivalent
void
list_attr_val(Genders& g, const string &attr, bool Uopt, vector<string> & sets_list)
{
	vector<string> nodes = g.getnodes(attr);
	vector<string> all;
	for (int i = 0; i < nodes.size(); i++) {
		string val;
		bool ret = g.testattr(attr,val,nodes[i]);
		if (ret && val.size() >0) {
			all.push_back(val);
		}
	}
	if (Uopt) {
		set<string> set(all.begin(),all.end());
		sets_list.assign(set.begin(),set.end());
	} else {
		sets_list.assign(all.begin(),all.end());
	}
}
#endif

class trans {
public:
	string host;	// ldms[agg]d_host
	string xprt;	// ldms[agg]d_xprt
	string port;	// ldms[agg]d_port
};
// may soon want to wrap this around some other database.
class ldms_config_info {
public:
	virtual ~ldms_config_info(){}
	// tell all known about host
	virtual void dump_host_info(const string& host) = 0;
	// is ldmsd collector
	virtual bool is_colld(const string& host) = 0;
	// is ldmsaggd daemon. if true, val should be non-empty
	virtual bool is_aggd(const string& host, string& val) = 0;
	// get bootnode=host matches. may be empty result.
	virtual void bootnodes(const string& host, vector<string>& bnl) = 0;
	// get list of local data collector. may be empty result.
	virtual void collectornodes(vector<string>& bnl) = 0;
	// collect *_port,_xprt,_hostname values for t and host. host may be empty results.
	virtual void get_trans(const string& host, trans& t, string prefix) = 0;
	// get fail over host if host goes down. may be empty result.
	virtual bool has_backup(const string& host, string& val) = 0;
	// get local ldmsd_metric_sets on host. may be empty result.
	virtual void get_metric_sets(const string& host, string& sets) = 0;
	// get local ldmsaggd_exclude_sets on. may be empty result.
	virtual void get_exclude_sets(const string& host, string& sets) = 0;
	// get the collectors host aggregates. may be empty result.
	virtual void get_clientof(const string& host, vector<string>& clientof) = 0;
	// get the aggregators host aggregates. may be empty result.
	virtual void get_aggclientof(const string& host, vector<string>& aggclientof) = 0;

};

#define LDMS_PORT_DEFAULT "411"

// genders implementation
class genders_api : public virtual ldms_config_info {
private:
	Genders& g;
	bool has_property(const string& host, const string& prop, string& val) {
		try {
			if (g.testattr(prop, val, host)) {
				return true;
			}
			return false;
		} catch ( GendersException ge) {
			return false;
		}
	}
public:
	// given g must outlive this object.
	genders_api(Genders& g) : g(g) {}
	virtual void dump_host_info(const string& hostname) {
		vector< pair< string, string > >  all = g.getattr(hostname);
		map<string, string> m(all.begin(),all.end());
	}

	virtual bool is_colld(const string& host) {
		string dummy;
		return has_property(host, "ldmsd", dummy);
	}
	virtual bool is_aggd(const string& host, string& val) {
		return has_property(host, "ldmsaggd", val);
	}
	virtual void bootnodes(const string& host, vector<string>& bnl) {
		bnl = g.getnodes("bootnode",host);
	}
	virtual void collectornodes(vector<string>& bnl) {
		bnl = g.getnodes("ldmsd");
	}
	virtual void get_trans(const string& host, trans& t, string prefix) {
		string ports = prefix + "_port";
		string hosts = prefix + "_host";
		string xprts = prefix + "_xprt";
		string tmp;
		if (has_property(host, ports, tmp)) {
			istringstream ss(tmp);
			int chk;
			ss >> chk;
			if (chk < 1) {
				cerr << ports << " of " << tmp << " bogus for " << host <<endl;
			} else {
				t.port = tmp;
			}
		} else {
			t.port = LDMS_PORT_DEFAULT;
		}
		if (! has_property(host, hosts, t.host) ) {
			t.host = host;
		}
		if (! has_property(host, xprts, t.xprt) ) {
			t.xprt = "sock"; 
		}
	}
	virtual bool has_backup(const string& host, string& val) {
		return has_property(host, "ldmsaggd_fail", val);
	}
	virtual void get_metric_sets(const string& host, string &sets) {
		has_property(host,"ldmsd_metric_sets", sets);
	}
	virtual void get_exclude_sets(const string& host, string &sets) {
		has_property(host,"ldmsaggd_exclude_sets", sets);
	}
	virtual void get_clientof(const string& host, vector<string>& clientof) {
		clientof = g.getnodes("ldmsd_clientof",host);
	}
	virtual void get_aggclientof(const string& host, vector<string>& aggclientof) {
		aggclientof = g.getnodes("ldmsaggd_clientof",host);
	}
};


/* 
	Summary data class for a nodes roles.
*/
class hdata {
private:
	ldms_config_info* in;
public:

	hdata(const string& host, ldms_config_info* in) : in(in),hostname(host) {
		isd = in->is_colld(hostname);
		isaggd = in->is_aggd(hostname,ldmsaggd);
		hasbackup = in->has_backup(host,aggbackup);
		in->get_trans(host, dt,"ldmsd");
		in->get_trans(host, aggdt,"ldmsaggd");
		in->get_metric_sets(host,metricsets);
		in->get_exclude_sets(host,excludesets);
	}

	void set_schedule(const string& interval, const string& offset) {
		agg_collection_interval = interval;
		agg_collection_offset = offset;
	}

	string hostname;	// genders/default hostname
	bool isd;		// ldmsd present
	bool isaggd;		// ldmsaggd present
	string ldmsaggd;	// ldmsaggd
	bool hasbackup;		// ldmsaggd_fail defined
	string aggbackup;	// ldmsaggd_fail
	trans dt;		// ldmsd_host/xprt/port
	trans aggdt;		// ldmsaggd_host/xprt/port
	string metricsets;	// ldmsd_metric_sets
	string excludesets;	// ldmsaggd_exclude_sets

private:
	// only topmost agg needs these; use set_schedule before calling get_sets
	string agg_collection_interval;
	string agg_collection_offset;
	vector<string> adds;	// add host lines computed for topmost hdata only.

	// append local sets of client to out as elements "clienthame/setname".
	void format_local_sets(const string& client, vector<string>& out, const set<string>& ban, set<string>& sets_seen) {
		string lsets;
		in->get_metric_sets(client,lsets);
		vector<string> names;
		split(names,lsets,is_any_of(":"), boost::token_compress_on);
		for (int i = 0; i < names.size(); i++) {
			if (ban.find(names[i]) == ban.end()) {
				out.push_back(client+"/"+names[i]);
				if (sets_seen.find(names[i]) == sets_seen.end()) {
					sets_seen.insert(names[i]);
				}
			}
			
		}
		if (names.size()==0) {
			cerr << "Node " << client << " has no metric sets defined." << endl;
		}
	}
		
	void add_collectors(int level, vector<string>& node_list, vector<string>& out, const set<string>& ban, set<string>& sets_seen) {
		for (int j = 0; j < node_list.size(); j++) {
			if (level) {
				format_local_sets(node_list[j], out, ban, sets_seen);
			} else {
				vector<string> collsets;
				format_local_sets(node_list[j], collsets, ban, sets_seen);
				trans t;
				in->get_trans(node_list[j], t, "ldmsd");
				string sets = join(collsets,",");
				ostringstream oss;
				oss <<
				"add host=" << t.host <<
				" type=active";
				if (agg_collection_interval.size() > 0) {
					oss << " interval=" << agg_collection_interval;
				}
				if (agg_collection_offset.size() > 0) {
					oss << " offset=" << agg_collection_offset;
				}
				oss << " xprt=" << t.xprt <<
				" port=" << t.port <<
				" sets=" << sets;
				adds.push_back(oss.str());
			}
		}
	}

public:
	/** Expand the subaggregators under this one.
	 Does not need to expand collector-only nodes as hdata.
	 sets_seen: globally unique metric set names, sans host context.
	 ban: copy in, else lower level bans may propagate too far.
	 aggs_seen: global repeat prevention.
	 out: set names with node scoping.
	*/
	void get_sets(int level, vector<string>& out, set<string> ban, set<string>& aggs_seen , set<string>& sets_seen) {
		/// cerr << "get_sets on " << hostname <<endl;

		if (aggs_seen.find(hostname) != aggs_seen.end()) {
			cerr << "Recursion on " << hostname << " cut in get_sets." << endl;
			return;
		}
		aggs_seen.insert(hostname);

		vector<string> banlist;
		split(banlist,excludesets,is_any_of(":"), boost::token_compress_on);
		ban.insert(banlist.begin(),banlist.end());


		if (!isaggd) {
			// unexpected in ldms v2.
			// may need adjustment when aggs see their own metric sets.
			format_local_sets(hostname, out, ban, sets_seen);
			return;
		}
		vector<string> parts;
		split(parts,ldmsaggd,is_any_of(":"), boost::token_compress_on);
		bool didbootnode, didclientof, didaggclientof, didldmsdall;
		didbootnode = didclientof = didaggclientof = didldmsdall = false;
		for (int i = 0; i < parts.size(); i++) {
			if (parts[i] == "BOOTNODELIST" && ! didbootnode) {
				didbootnode = true;
				vector<string> bootnode_list;
				in->bootnodes(hostname,bootnode_list);
				add_collectors(level, bootnode_list, out, ban, sets_seen);
				continue;
			}
			if (parts[i] == "LDMSDALL" && ! didldmsdall) {
				didldmsdall = true;
				vector<string> node_list;
				in->collectornodes(node_list);
				add_collectors(level, node_list, out, ban, sets_seen);
				continue;
			}
			if (parts[i] == "CLIENTOFLIST" && !didclientof) {
				didclientof = true;
				vector<string> clientof_list;
				in->get_clientof(hostname,clientof_list);
				add_collectors(level, clientof_list, out, ban, sets_seen);
				continue;
			}
			if (parts[i] == "AGGCLIENTOFLIST" && !didaggclientof) {
				vector<string> aggclientof_list;
				in->get_aggclientof(hostname,aggclientof_list);
				for (int j = 0; j < aggclientof_list.size(); j++) {
					hdata sub(aggclientof_list[j],in);
					if (! sub.isaggd) {
						cerr << "Ignoring ldmsaggd_clientof=" <<
						hostname << " on node not aggregating: "
						<< aggclientof_list[j] << endl;
						continue;
					}
					if (level) {
						// recurse down nested agg subtree
						sub.get_sets(level+1, out, ban, aggs_seen, sets_seen);
					} else {
						// begin recursion
						vector<string> aggsets;
						sub.get_sets(level+1, aggsets, ban, aggs_seen, sets_seen);
						trans t;
						in->get_trans(aggclientof_list[j], t, "ldmsaggd");
						string sets = join(aggsets,",");
						ostringstream oss;
						oss <<
						"add host=" << t.host <<
						" type=active";
						if (agg_collection_interval.size() > 0) {
							oss << " interval=" << agg_collection_interval;
						}
						if (agg_collection_offset.size() > 0) {
							oss << " offset=" << agg_collection_offset;
						}
						oss << " xprt=" << t.xprt <<
						" port=" << t.port <<
						" sets=" << sets;
						adds.push_back(oss.str());
					}
				}
				continue;
			}
			// else it's a hostname or a typo in LDMSDALL.
			vector<string> singleton;
			singleton.push_back(parts[i]);
			add_collectors(level, singleton, out, ban, sets_seen);
		}
	}

	// 
	void print_add_hosts(string NODELIST) {
		for(int i = 0; i < adds.size(); i++) {
			cout << adds[i] << endl;
		}

	}
};	

class ctloptions {

public:
	bool ok; // if !true, options parsing failed.
	int log_level; // higher is louder
	string genders;
	string task;
	string hostname; 
	string outname; 
	bool useoutname;

	ctloptions(int argc, char **argv) :
		ok(true),
		log_level(0),
		genders("/etc/genders") ,
		task("host-list"),
		useoutname(false)
	{
#if 1
		char hostbuf[HOST_NAME_MAX+1];
		if (!gethostname(hostbuf,sizeof(hostbuf)))
			hostname = hostbuf;
		else {
			cout << argv[0] << ": gethostname failed"<< endl;
			ok = false;
			return;
		}
		char *envgenders = getenv("LDMS_GENDERS");
		if (envgenders != NULL) {
			genders = envgenders;
		}
		
		po::options_description desc("Allowed options");
		desc.add_options()
		("genders,g", po::value <  string  >(&genders),
		 "genders input file name (default env(LDMS_GENDERS) else /etc/genders)")
		("node,n", po::value <  string  >(&hostname),
		 "network node the query will describe (default from gethostname())")
		("task,t", po::value <  string  >(&task),
		 "the type of output wanted (default add host lines)")
		("output,o", po::value <  string  >(&outname),
		 "where to send output (default stdout)")
		("verbose,v", po::value < int >(&log_level),
		 "enable verbosity (higher is louder; default 0)")
		("help,h", "produce help message")
		;
		po::variables_map vm;
		po::store(parse_command_line(argc,argv,desc),vm);
		po::notify(vm);
		if (vm.count("help")) {
			cout << desc << "\n";
		}
		if (vm.count("output")) {
			useoutname = true;
		}
#endif
	}

};

int main(int argc, char **argv)
{

#if 0
	string filename(argv[1]);
	string hostname(argv[2]);
	string task(argv[3]);
#endif
	ctloptions opt(argc,argv);
	if (!opt.ok) {
		usage(argv[0]);
		return 1;
	}

	try {
		Genders g0(opt.genders);
		genders_api gapi(g0);
		ldms_config_info *info = &gapi;

		set<string> ban, hosts_seen, sets_seen;
		vector<string> out;
		hdata top(opt.hostname, info);
		top.set_schedule("1000000","10000");

		top.get_sets(0, out, ban, hosts_seen, sets_seen);

		string storesets = join(sets_seen,",");
		if (opt.task == "store-list") {
			cout << storesets << endl;
		}
		if (opt.task == "host-list") {
			top.print_add_hosts("");
		}
	} catch ( GendersException ge) {
		cerr << argv[0] << ": Error parsing " << opt.genders <<
			": " << ge.errormsg() << endl;
	}

	return 0;
}
