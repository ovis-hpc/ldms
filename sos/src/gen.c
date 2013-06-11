/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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

/*
 * Author: Tom Tucker tom at ogc dot us
 */

//#include "ovis-storage.h" // Implement me

#include <iostream>

#include <set>
#include <string>
#include <vector>

#include <stdlib.h>
#include <string.h>
#include <math.h>

// Implement me:
void ov_store( std::string& ctyp, std::string& mnam, long long c, int v, struct timeval& t )
{
  std::cout << ctyp.c_str() << "," << mnam.c_str() << "," << c << "," << v << "," << t.tv_sec << "," << t.tv_usec << "\n";
}

struct options
{
  unsigned seed;
  unsigned num_vals;
  long long num_comps;
  long long num_metrics;
  long long num_tuples;
  long long num_comp_types;
};

bool parse( int& argc, char**& argv, options& opts )
{
  opts.seed = 0xdeadbeef;
  opts.num_vals = 1024;
  opts.num_metrics = 1024LL;
  opts.num_comp_types = log2( opts.num_metrics );
  opts.num_comps = 10000000LL;
  opts.num_tuples = 500000000LL;
  bool stat = true;

  for ( int i = 1; i < argc; ++ i )
    {
    if ( i + 1 < argc )
      {
      if ( ! strcmp( argv[i], "-s" ) )
        {
        opts.seed = atoll( argv[++ i] );
        }
      else if ( ! strcmp( argv[i], "-nc" ) )
        {
        opts.num_comps = atoll( argv[++ i] );
        }
      else if ( ! strcmp( argv[i], "-nm" ) )
        {
        opts.num_metrics = atoll( argv[++ i] );
        }
      else if ( ! strcmp( argv[i], "-nv" ) )
        {
        opts.num_vals = atol( argv[++ i] );
        }
      else if ( ! strcmp( argv[i], "-nt" ) )
        {
        opts.num_tuples = atoll( argv[++ i] );
        }
      else
        {
        stat = false;
        }
      }
    else
      {
      stat = false;
      }
    }
  return stat;
}

struct generator
{
  std::vector<std::string> comp_types;
  std::vector<std::pair<std::string,std::string> > metrics;
};

void advance_time( struct timeval& t )
{
  ++ t.tv_sec;
  t.tv_usec = random() % 1000000;
}

int main( int argc, char* argv[] )
{
  options opts;
  parse( argc, argv, opts );

  long long i;
  char rndstate[256];
  if ( opts.seed == 0xdeadbeef )
    {
	    //srandomdev();
    }
  else
    {
    initstate( opts.seed, rndstate, sizeof(rndstate) );
    setstate( rndstate );
    }

  generator gen;

  // Generate component types
  std::set<std::string> unique;
  char name[3] = { 0, 0, 0 };
  for ( i = 0; i < opts.num_comp_types; ++ i )
    {
    do
      {
      name[0] = random() % 26 + 97;
      name[1] = random() % 26 + 97;
      }
    while ( ! unique.insert( name ).second );
    gen.comp_types.push_back( name );
    }
  unique.clear();

  // Generator metric names
  for ( i = 0; i < opts.num_metrics; ++ i )
    {
    long long ct = random() % opts.num_comp_types;
    char name[7] = { 0, 0, 0, 0, 0, 0, 0 };
    do
      {
      name[0] = random() % 26 + 97;
      name[1] = random() % 26 + 97;
      name[2] = random() % 26 + 97;
      name[3] = random() % 26 + 97;
      name[4] = random() % 26 + 97;
      name[5] = random() % 26 + 97;
      }
    while ( ! unique.insert( name ).second );
    gen.metrics.push_back( std::pair<std::string,std::string>( gen.comp_types[ct], name ) );
    }
  unique.clear();

  // Generate random values
  struct timeval t;
  t.tv_sec = 0;
  t.tv_usec = 0;
  long long vals_per_step = opts.num_comps * opts.num_metrics;
  long long thresh = opts.num_comps * opts.num_metrics + 2;
  for ( i = 0; i < opts.num_tuples; ++ i )
    {
    // Perhaps advance the time
    if ( ( random() % thresh ) > vals_per_step )
      advance_time( t );

    // Choose a random metric
    long long m = random() % opts.num_metrics;

    // Choose a random component
    long long c = (random() % opts.num_comps) + 1;

    // Choose a random value
    int v = random() % opts.num_vals;

    // Store it.
    ov_store( gen.metrics[m].first, gen.metrics[m].second, c, v, t );
    }

  return 0;
}
