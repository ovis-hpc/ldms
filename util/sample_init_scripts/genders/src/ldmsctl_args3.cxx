#include <gendersplusplus.hpp>
#include <map>
#include <set>
#include <sstream>
//#include <boost/algorithm/string_regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
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


static int dbg = 0;

void printvec( const vector< pair< string, string > >& all)
{
	for (vector< pair< string, string > >::size_type i = 0; i < all.size(); i++) {
		cerr << i<< ": " << all[i].first << " = " <<  all[i].second <<endl;
	}
}

void printvec1( const vector<  string >& all)
{
	for (vector< string >::size_type i = 0; i < all.size(); i++) {
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
	string producer;// ldmsd_producer
	string retry;	// ldmsaggd_conn_retry
	string host;	// ldms[agg]d_host
	string xprt;	// ldms[agg]d_xprt
	string port;	// ldms[agg]d_port
	string interval;// ldms[aggd]_interval_default
	string offset; 	// ldms[aggd]_offset_default
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
	// get local ldmsd_metric_plugins on host. may be empty result.
	virtual void get_metric_sets(const string& host, string& sets) = 0;
	// get local ldmsd_schemas_extra on host. may be empty result.
	virtual void get_schemas_extra(const string& host, string& schemas) = 0;
	// get local ldmsaggd_exclude_sets on. may be empty result.
	virtual void get_exclude_sets(const string& host, string& sets) = 0;
	// get exclude flag for host for $prefix_exclude_host
	virtual bool get_exclude_host(const string& host, string prefix, string& ehval) = 0;
	// get the collectors host aggregates. may be empty result.
	virtual void get_clientof(const string& host, vector<string>& clientof) = 0;
	// get the aggregators host aggregates. may be empty result.
	virtual void get_aggclientof(const string& host, vector<string>& aggclientof) = 0;
	// get the producer alias of host
	virtual void get_producer(const string& host, string& producer) = 0;
	// get the producer alias of host
	virtual void get_retry(const string& host, string& retry) = 0;

};

#define LDMS_PORT_DEFAULT "411"

// genders implementation
class genders_api : public virtual ldms_config_info {
private:
	Genders& g;
	// return true and set val to genders file value if prop present.
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

	/** Replace %#d %#m and %#u with corresponding portions of hostname
	 * in a gender value.
	 * \param host unqualified hostname as used by genders
	 * \param val gender value possibly containing %%#d %#m and %#u,
	 * where # is an integer index of the split name pieces.
	 *
	 * output: substituted genderval based on the following:
	 * replace %#d with the #'th integer substring in hostname
	 * replace %#D with the #'th integer substring in hostname 0 left stripped
	 * replace %#m with the #'th - separated substring in hostname
	 * replace %#u with the #'th _ separated substring in hostname
	 * Any case where # is greater than the number of such
	 * elements in the input is left unsubstituted without warning.
	 * Gender usage examples:
	 * Extract the number suffix and use in related names:
	 * :  map host to fast net name
	 * chama-login[1-8] ldmsd_host=chama-lsm%0d-ib0
	 * :  map host to associated rps node
	 * chama-login[1-8] ldmsd_clientof=chama-rps%0d
	 * :  map host to short producer name for splunk/csv.
	 * chama-login[1-8] ldmsd_producer=ln%0d
	 * Extract the suffix and use as producer name
	 * : map host chama-rps# to 'rps#'
	 * chama-rps[1-8] ldmsd_producer=%1m
	 */
	void gender_substitute(const string& host, string& val) {
		// try nothing if % is not in value someplace.
		std::size_t found = val.find_first_of("%");
		if (found == std::string::npos)
			return;

		string ghost(host);
		regex ire("[0-9]*");
		sregex_iterator i(ghost.begin(), ghost.end(), ire);
		sregex_iterator j(ghost.begin(), ghost.end(), ire);
		sregex_iterator end;

		int n = 0;
		// make number-based replacements %#D
		while ( j != end) {
			if ((*j).length()) {
				ostringstream oss;
				oss << "%" << n << "D";
				string sub = oss.str();
				string jval = j->str();
				string kval = jval.substr( jval.find_first_not_of( "0" ) );
				replace_all(val, sub, kval);
				n++;
			}
			++j;
		}

		n = 0;
		// make number-based replacements %#d
		while ( i != end) {
			if ((*i).length()) {
				ostringstream oss;
				oss << "%" << n << "d";
				string sub = oss.str();
				string ival = i->str();
				replace_all(val, sub, ival);
				n++;
			}
			++i;
		}

		// make - delimited part replacements %#m
		vector<string> mparts;
		split(mparts, host, is_any_of("-"), boost::token_compress_on);
		n = 0;
		for (vector<string>::size_type k = 0; k < mparts.size(); k++) {
			ostringstream oss;
			oss << "%" << k << "m";
			string sub = oss.str();
			string kval = mparts[k];
			replace_all(val, sub, kval);
			n++;
		}

		// make _ delimited part replacements %#u
		vector<string> uparts;
		split(uparts, host, is_any_of("_"), boost::token_compress_on);
		n = 0;
		for (vector<string>::size_type k = 0; k < uparts.size(); k++) {
			ostringstream oss;
			oss << "%" << k << "u";
			string sub = oss.str();
			string kval = uparts[k];
			replace_all(val, sub, kval);
			n++;
		}
	}

	vector< string > getnodes_subst(const string attr, const string val) {
		vector< string > full = g.getnodes(attr);
		vector< string > result;
		if (val.size() == 0) {
			return full;
		}
		for (vector<string>::size_type j = 0; j < full.size(); j++) {
			string host = full[j];
			string unsub;
			g.testattr(attr, unsub, host);
			string hsub = unsub;
			gender_substitute(host, hsub);
			if (hsub == val) {
				if (dbg > 0) {
					cerr << "subst '" << hsub <<"' matches " 
					<< val << " from " << unsub << " on " <<
					host << endl;
				}
				result.push_back(host);
			} else {
				if (dbg > 0) {
					cerr << "subst '" << hsub <<"' unmatches " 
					<< val << " from " << unsub << " on " <<
					host <<  endl;
				}
			}
			
		}
		return result;
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
		// 1st 3 no longer separated by prefix
		string ports = "ldmsd_port";
		string hosts = "ldmsd_host";
		string xprts = "ldmsd_xprt";
		string producer = prefix + "_producer";
		string retry = prefix + "_conn_retry";
		string intervals = prefix + "_interval_default";
		string offsets = prefix + "_offset_default";
		string tmp;
		if (has_property(host, ports, tmp)) {
			t.port = tmp;
			istringstream ss(tmp);
			int chk;
			ss >> chk;
			if (chk < 1) {
				cerr << "INFO: " << ports << " of " << tmp << " not integer for " << host <<endl;
			} else {
				if (dbg > 0) {
					cerr << "For port of " << prefix << " parsed " << tmp << endl;
				}
			}
		} else {
			t.port = LDMS_PORT_DEFAULT;
			if (dbg > 0) {
				cerr << "Using port default " << t.port <<  endl;
			}
		}
		if (! has_property(host, hosts, t.host) ) {
			t.host = host;
		} else {
			gender_substitute(host, t.host);
		}
		if (! has_property(host, producer, t.producer) ) {
			t.producer = host;
		} else {
			gender_substitute(host, t.producer);
		}
		if (! has_property(host, xprts, t.xprt) ) {
			t.xprt = "sock";
		}
		if (! has_property(host, retry, t.retry) ) {
			t.retry = "2000000"; // 2 sec
		}
		if (! has_property(host, intervals, t.interval) ) {
			t.interval = "10000000"; // 10 sec
		}
		if (! has_property(host, offsets, t.offset) ) {
			t.offset = "100000"; // 0.1 sec
		}
	}
	virtual bool has_backup(const string& host, string& val) {
		return has_property(host, "ldmsaggd_fail", val);
	}
	virtual void get_metric_sets(const string& host, string &sets) {
		has_property(host,"ldmsd_metric_plugins", sets);
	}
	virtual void get_schemas_extra(const string& host, string &sets) {
		has_property(host,"ldmsd_schemas_extra", sets);
	}
	virtual bool get_exclude_host(const string& host, string prefix, string &ehval) {
		string ehname = prefix + "_exclude_host";
		return has_property(host, ehname, ehval);
	}
	virtual void get_exclude_sets(const string& host, string &sets) {
		has_property(host,"ldmsaggd_exclude_sets", sets);
	}
	virtual void get_clientof(const string& host, vector<string>& clientof) {
		clientof = getnodes_subst("ldmsd_clientof",host);
	}
	virtual void get_aggclientof(const string& host, vector<string>& aggclientof) {
		aggclientof = getnodes_subst("ldmsaggd_clientof",host);
	}
	virtual void get_producer(const string& host, string& producer) {
		has_property(host, "ldmsd_producer", producer);
		gender_substitute(host, producer);
	}
	virtual void get_retry(const string& host, string& retry) {
		has_property(host, "ldmsaggd_conn_retry", retry);
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
		in->get_schemas_extra(host,schemasextra);
		in->get_exclude_sets(host,excludesets);
	}

	void set_offset(const string& offset) {
		aggdt.offset = offset;
	}

	void set_interval(const string& interval) {
		aggdt.interval = interval;
	}

	string hostname;	// genders/default hostname
	bool isd;		// ldmsd present
	bool isaggd;		// ldmsaggd present
	string ldmsaggd;	// ldmsaggd
	bool hasbackup;		// ldmsaggd_fail defined
	string aggbackup;	// ldmsaggd_fail
	trans dt;		// ldmsd_host/xprt/port/interval/offset
	trans aggdt;		// ldmsaggd_host/xprt/port/interval/offset
	string metricsets;	// ldmsd_metric_sets
	string schemasextra;	// ldmsd_metric_sets
	string excludesets;	// ldmsaggd_exclude_sets

private:
	vector<string> adds;	// add host lines computed for topmost hdata only.

	// append local sets of client to out as elements "clienthame/setname".
	void format_local_sets(const string& client, vector<string>& out, const set<string>& ban, set<string>& sets_seen) {
		string lsets;
		string extrasets;
		in->get_metric_sets(client,lsets);
		in->get_schemas_extra(client,extrasets);
		vector<string> names, extranames;
		size_t nn = 0;
		split(names,lsets,is_any_of(":"), boost::token_compress_on);
		nn += names.size();
		vector<string>::size_type i;
		for (i = 0; i < names.size(); i++) {
			if (ban.find(names[i]) == ban.end()) {
				out.push_back(client+"/"+names[i]);
				if (sets_seen.find(names[i]) == sets_seen.end()) {
					sets_seen.insert(names[i]);
				}
			}

		}
		split(extranames,extrasets,is_any_of(":"),
			boost::token_compress_on);
		nn += extranames.size();
		for (i = 0; i < extranames.size(); i++) {
			if (ban.find(extranames[i]) == ban.end()) {
				out.push_back(client+"/"+extranames[i]);
				if (sets_seen.find(extranames[i]) ==
					sets_seen.end()) {
					sets_seen.insert(extranames[i]);
				}
			}

		}
		if (nn == 0) {
			cerr << "Node " << client << " has no schemas defined." << endl;
		}
	}

	void add_collectors(int level, vector<string>& node_list, vector<string>& out, const set<string>& ban, set<string>& sets_seen) {
		ostringstream oss;
		oss << "# ac top\n";
		oss << "updtr_add name=all_" << hostname;
		oss << " interval=" << aggdt.interval;
		oss << " offset=" << aggdt.offset;
		oss << endl;
		string ehdummy;
		for (vector<string>::size_type j = 0; j < node_list.size(); j++) {
			if (in->get_exclude_host(node_list[j], "ldmsd", ehdummy)) {
				// do not aggregate from or collect on excluded hosts.
				continue;
			}
			if (level) {
				format_local_sets(node_list[j], out, ban, sets_seen);
			} else {
				vector<string> collsets;
				format_local_sets(node_list[j], collsets, ban, sets_seen);
				trans t;
				in->get_trans(node_list[j], t, "ldmsd");
				string sets = join(collsets,",");
				// oss << "# line 417" << endl;
				oss << "prdcr_add name=" << t.producer;
				oss << " host=" << t.host;
				oss << " type=active";
				oss << " xprt=" << t.xprt;
				oss << " port=" << t.port;
				oss << " interval=" << t.retry;
				oss << endl;
				oss << "prdcr_start name=" << t.producer;
				oss << endl;
				// ; sets=" << sets;
			}
		}
		oss << "updtr_prdcr_add name=all_" << hostname;
		oss << " regex=" << ".*";
		oss << endl;
		oss << "updtr_start name=all_" << hostname;
		adds.push_back(oss.str());
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
		for (vector<string>::size_type i = 0; i < parts.size(); i++) {
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

				ostringstream oss;
				oss << "# aggclient list\n";
				oss << "updtr_add name=all_" << hostname;
				oss << " interval=" << aggdt.interval;
				oss << " offset=" << aggdt.offset;
				oss << endl;
				for (vector<string>::size_type j = 0; j < aggclientof_list.size(); j++) {
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
						if (dbg) {
							cerr << "Pulling from aggregator " << aggclientof_list[j] << endl;
						}
						in->get_trans(aggclientof_list[j], t, "ldmsaggd");
						string sets = join(aggsets,",");

						// oss << "# line 522" << endl;
						oss << "prdcr_add name=" << t.producer;
						oss << " host=" << t.host;
						oss << " type=active";
						oss << " xprt=" << t.xprt;
						oss << " port=" << t.port;
						oss << " interval=" << t.retry;
						oss << endl;
						oss << "prdcr_start name=" << t.producer;
						oss << endl;
					}
				}
				oss << "updtr_prdcr_add name=all_" << hostname;
				oss << " regex=" << ".*";
				oss << endl;
				oss << "updtr_start name=all_" << hostname;
				// ; sets=" << sets;
				adds.push_back(oss.str());
				continue;
			}
			// else it's a hostname or a typo in LDMSDALL.
			vector<string> singleton;
			singleton.push_back(parts[i]);
			add_collectors(level, singleton, out, ban, sets_seen);
		}
	}

	// dump the constructed command list, removing duplicates other
	// than comments
	void print_add_hosts(string NODELIST) {
		(void) NODELIST;
		vector<string>keepers;
		set<string>uniques;
		for(vector<string>::size_type i = 0; i < adds.size(); i++) {
			stringstream sstrm(adds[i]);
			string elt;
			while (std::getline(sstrm, elt)) {
				if (elt.size() > 0 && (elt[0] == '#' ||
					uniques.find(elt) == uniques.end())) {
					uniques.insert(elt);
					keepers.push_back(elt);
				}
			}
		}
		for(vector<string>::size_type i = 0; i < keepers.size(); i++) {
			cout << keepers[i] << endl;
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
	string interval;
	bool useinterval;
	string offset;
	bool useoffset;

	void dump() {
		#define DUMP(x) cerr << #x << " is " << x << endl
		DUMP(log_level);
		DUMP(genders);
		DUMP(task);
		DUMP(hostname);
		DUMP(outname);
		DUMP(useoutname);
		DUMP(interval);
		DUMP(useinterval);
		DUMP(offset);
		DUMP(useoffset);
	}

	ctloptions(int argc, char **argv) :
		ok(true),
		log_level(0),
		genders("/etc/genders") ,
		task("host-list"),
		useoutname(false),
		useinterval(false),
		useoffset(false)
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
		("interval,i", po::value <  string  >(&interval),
		 "value overriding interval_default gender")
		("offset,o", po::value <  string  >(&offset),
		 "value overriding offset_default gender")
		("output,O", po::value <  string  >(&outname),
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
			ok = false;
		}
		if (vm.count("output")) {
			useoutname = true;
		}
		if (vm.count("offset")) {
			useoffset = true;
		}
		if (vm.count("interval")) {
			useinterval = true;
		}
#endif
	}

};

int main(int argc, char **argv)
{
	ctloptions opt(argc,argv);
	if (!opt.ok) {
		return 1;
	}
	if (opt.log_level > 0) {
		opt.dump();
		dbg = opt.log_level;
	}

	try {
		Genders g0(opt.genders);
		genders_api gapi(g0);
		ldms_config_info *info = &gapi;

		set<string> ban, hosts_seen, sets_seen;
		vector<string> out;
		hdata top(opt.hostname, info);

		if (opt.useinterval)
			top.set_interval(opt.interval);

		if (opt.useoffset)
			top.set_offset(opt.offset);

		top.get_sets(0, out, ban, hosts_seen, sets_seen);

		string storesets = join(sets_seen,",");
		if (opt.task == "store-list") {
			cout << storesets << endl;
			return 0;
		}
		if (opt.task == "host-list") {
			top.print_add_hosts("");
			return 0;
		}
		cerr << argv[0] << ": unexpected task " << opt.task << endl;
		return 1;
	} catch ( GendersException ge) {
		cerr << argv[0] << ": Error parsing " << opt.genders <<
			": " << ge.errormsg() << endl;
	}

	return 0;
}
