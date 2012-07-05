#!/usr/bin/perl
#
# given a compid (of the sedc reporting comp) and a metric name, returns the information
# necessary to build the add_metric line for the config. Some basic remote component
# naming conventions are used.
# 
# for example
# if the set name is c0-0c2s7/l0g34temps and the metricname is L0_T_G34_NODE0_PCB_TEMP and the
# compid (gotten from LDMS and consistent with that in the database) is (for example) 666 
# and the 0th node of that slot is (for example) compid 999 (in the database) and the 
# CompTypeShortName in the database of a node is "node" then:
#
# > ./mysqlsedcConfigurationBuilder -c 666 -m L0_T_G34_NODE0_PCB_TEMP
# > node 999 L0_T_G34_NODEX_PCB_TEMP
# 
# This info can then be used to build the line:
# config mysqlinsert add_metric=c0-0c2s7/l0g34temps&L0_T_G34_NODE0_PCB_TEMP&999&0&Node&L0_T_G34_NODEX_PCB_TEMP
#
# Some other examples:
# [brandt@fedora14 Ann]$ ./mysqlsedcConfigurationBuilder.pl -d Cray1Rack_LDMS -c 124 -m BLAH_NODE0_FOO
#node 93 BLAH_NODEX_FOO
#[brandt@fedora14 Ann]$ ./mysqlsedcConfigurationBuilder.pl -d Cray1Rack_LDMS -c 124 -m BLAH_NODE1_FOO
#node 94 BLAH_NODEX_FOO
#[brandt@fedora14 Ann]$ ./mysqlsedcConfigurationBuilder.pl -d Cray1Rack_LDMS -c 124 -m BLAH_NODE8_FOO
#-1 at ./mysqlsedcConfigurationBuilder.pl line 188.
#[brandt@fedora14 Ann]$ ./mysqlsedcConfigurationBuilder.pl -d Cray1Rack_LDMS -c 124 -m BLAH_NODE2_PROC0_FOO
#proc 313 BLAH_NODEX_PROCY_FOO
#[brandt@fedora14 Ann]$ ./mysqlsedcConfigurationBuilder.pl -d Cray1Rack_LDMS -c 124 -m BLAH_GEMINI_MEZZ_F00_G0
#gemini 1899 BLAH_GEMINI_MEZZ_F00_GX
#[brandt@fedora14 Ann]$ ./mysqlsedcConfigurationBuilder.pl -d Cray1Rack_LDMS -c 97 -m L1_CRAP
#rslone 1901 L1_CRAP
#
#
# Note: there are 3 mysql lookups for each call to this code.
#

use DBI;
use Getopt::Long;
use Scalar::Util qw(looks_like_number);

my $username = "ovis";
my $password = "";
my $hostname = "localhost";
my $dbname = "";
my $socket = "/var/lib/mysql/mysql.sock";
my $port = 3306;
my $ldmsmetricname = "";
my $ldmscompid = -1;
$help = '';
$verbose = '';

$result = GetOptions (
    'version' => sub {
	print  "mysqlsedcconverter.pl version 1.0\n";  exit;},
    'help' => \$help,
    'd=s' => \$dbname,
    's=s' => \$socket,
    'p=i' => \$port,
    'h=s' => \$hostname,
    'u=s' => \$username,
    'w=s' => \$password,
    'c=i' => \$ldmscompid,
    'm=s' => \$ldmsmetricname
);


if ($help){
    print "mysqlsedcconverter.pl\n";
    print "\t-d Database name\n";
    print "\t-h Host name (defaults to localhost)\n";
    print "\t-s Socket (defaults to /var/lib/mysql/mysql.sock)\n";
    print "\t-p Port (defaults to 3306)\n";
    print "\t-u DB Username (defaults to ovis)\n";
    print "\t-w DB Password (defaults to none)\n";
    print "\t-c LDMS CompId\n";
    print "\t-m LDMS Metric Name (Beware metric names that are not upper case!)\n";
    print "\t--version Displays version information\n";
    print "\t--help Displays this message\n";
    exit;
}


if ($hostname eq ""){
#    die "no hostname";
    die "-1"
}

if ($dbname eq ""){
#    die "no dbname";
    die "-1"
}

if ($socket eq ""){
#    die "no socket";
    die "-1"
}

if ( $port < 0 ){
#    die "invalid numerical args";
    die "-1"
}

if ( $compid < 0 ){
#    die "invalid comp id";
    die "-1"
}


my $dsn = "DBI:mysql:" . $dbname . ";mysql_socket=" . $socket . ";port=" . $port . ";host=". $hostname;
my $dbh = DBI->connect($dsn, $username, $password, { RaiseError => 1});


my ($dsn) = "DBI:mysql:" . $dbname . ":" . $hostname; # data source name
#print "connecting to : $dsn\n";

# ---------------------------------------------------------------
# connect to database
# ---------------------------------------------------------------
my ($dbh, $sth);             # database and statement handles
my ($query1, $query2 );      # query handlers
my (@ary);                   # array for rows returned by query

$dbh = DBI->connect ($dsn, $username, $password, { RaiseError => 1 });
my ($ctype,  $cid, $metricname) = remoteComponentRules($dbh,$ldmscompid,$ldmsmetricname);
printf "$ctype $cid $metricname\n";


sub remoteComponentRules($dbh, $ldmscompid, $ldmsmetricname){
    my ($dbh, $ldmscompid, $ldmsmetricname) = @_;

    $testcompname = -1;
    $testmetricname = -1;

    if (($ldmsmetricname =~ /^L1_/) || ($ldmsmetricname =~ /(.*)NODE(\d)_PROC(\d)(.*)/) ||
	($ldmsmetricname =~ /(.*)NODE(\d)(.*)/) || ($ldmsmetricname =~ /(.*)GEMINI_MEZZ(.*)_G(\d)(.*)/)){

	$query1 = qq{ SELECT CompName from ComponentTable where CompId = $ldmscompid };
	$sth = $dbh->prepare ( $query1 );
#	print "<$query1>\n";
	$sth->execute ();
	$cname = -1;
	if ( (@ary = $sth->fetchrow_array()) ){
	    $cname = $ary[0];
	}
	if ($cname == -1){
	    die "-1"
	}
	if ($ldmsmetricname =~ /^L1_/) {
	    #special case -- if its the L1 assign it to the L1 (instead of the cabinet)
	    #OVIS naming convention is cabinet+l -- forexample c0-0l. This is the only
	    #compname that will end with an l
	    $testcompname = $cname . "l";
	    $testmetricname = $ldmsmetricname;
	} elsif ($ldmsmetricname =~ /(.*)NODE(\d)_PROC(\d)(.*)/){
	    #special case -- if it has NODEX_PROCY assign it to the proc and put generic X and Y in the name
	    #OVIS naming convention - add node and proc-- for example c0-0c0s0n0p0. 
	    $testcompname = $cname . "n" . $2 . "p" . $3;
	    $testmetricname = $1 . "NODEX_PROCY" . $4;
	} elsif ($ldmsmetricname =~ /(.*)NODE(\d)(.*)/){
	    #special case -- if it has NODEX assign it to the node and put generic X in the name
	    #OVIS naming convention - add node -- for example c0-0c0s0n0.
	    $testcompname = $cname . "n" . $2;
	    $testmetricname = $1 . "NODEX" . $3;
	} elsif ($ldmsmetricname =~ /(.*)GEMINI_MEZZ(.*)_G(\d)(.*)/){
	    #special case -- if it has GEMINI_MEZZ and G with number assign it to the gemini and put generic GX the name
	    #OVIS naming convention - add gemini-- for example c0-0c0s0g0.
	    $testcompname = $cname . "g" . $3;
	    $testmetricname = $1 . "GEMINI_MEZZ" . $2 . "_GX" . $4;
	} else {
	    #shouldnt get here
	    die "-1";
	}
    } else {
	#default to the init component	
	$testcompname = $cname;
	$testmetricname = $ldmsmetricname;
    }

    $query1 = qq{ SELECT CompType, CompId from ComponentTable where CompName='$testcompname' };
    $sth = $dbh->prepare ( $query1 );
#	print "<$query1>\n";
    $sth->execute ();
    $ctype = -1;
    $cid = -1;
    if ( (@ary = $sth->fetchrow_array()) ){
	$ctype = $ary[0];
	$cid = $ary[1];
	$query1 = qq{ SELECT ShortName from ComponentTypes where CompType = $ctype };
	$sth = $dbh->prepare ( $query1 );
#	print "<$query1>\n";
	$sth->execute ();
	$ctypename = -1;
	if ( (@ary = $sth->fetchrow_array()) ){
	    $ctypename = $ary[0];
	    if (($ctype != -1) && ($cid != -1) && ($ctypename != -1)){
		return ($ctypename, $cid, $testmetricname);
	    }
	}
    }
    die "-1";
}

