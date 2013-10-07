/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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

package org.apache.hadoop.metrics2.sink.ldms;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;

import org.apache.commons.configuration.SubsetConfiguration;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.metrics2.Metric;
import org.apache.hadoop.metrics2.MetricsException;
import org.apache.hadoop.metrics2.MetricsRecord;
import org.apache.hadoop.metrics2.MetricsSink;

public class LdmsMetricNameSink implements MetricsSink {

	public final Log log = LogFactory.getLog(this.getClass());
	private Boolean isFlushed = false;

	class LdmsMetric {
		String name;
		String type;

		@Override
		public int hashCode() {
			return name.hashCode();
		}

		@Override
		public boolean equals(Object obj) {
			LdmsMetric m = (LdmsMetric) obj;
			if (this.name.equals(m.name)) {
				return true;
			} else {
				return false;
			}
		}

		public LdmsMetric(String name, String type) {
			this.name = name;
			this.type = type;
		}
	}

	class LdmsRecord {
		String recordContext;
		String recordName;
		HashSet<LdmsMetric> metricHash;

		@Override
		public boolean equals(Object obj) {
			LdmsRecord record = (LdmsRecord) obj;
			if (this.recordContext.equals(record.recordContext) &&
					this.recordName.equals(record.recordName)) {
				return true;
			} else {
				return false;
			}
		}

		public LdmsRecord(String context, String name) {
			this.recordContext = context;
			this.recordName = name;
			metricHash = new HashSet<LdmsMetricNameSink.LdmsMetric>();
		}
	}

	private PrintWriter writer;
	private String filename;
	private LdmsVisitor visitor = new LdmsVisitor();
	private ArrayList<LdmsRecord> records = new ArrayList<LdmsRecord>();
	int count = 0;
	int numCountEqual = 0;

	@Override
	public void init(SubsetConfiguration conf) {

		filename = conf.getString("filename");
		try {
			if (filename == null) {
				writer = new PrintWriter(new BufferedOutputStream(
								System.out));
			} else {
				writer = new PrintWriter(new FileWriter(
							new File(filename)));
			}
		} catch (Exception e) {
			throw new MetricsException("LdmsMetricNameSink: " +
				"Error in creating file " + filename, e);
		}
	}

	@Override
	public void putMetrics(MetricsRecord record) {
		if (isFlushed)
			return;

		int recordIndex;
		LdmsRecord oldRecord;
		int oldcount = count;
		LdmsMetric metric;

		try {
			Collection<Metric> ms = (Collection<Metric>)
							record.metrics();
			if (ms.size() <= 0)
				return;

			LdmsRecord r = new LdmsRecord(record.context(),
							record.name());
			recordIndex = records.indexOf(r);
			if (recordIndex == -1) {
				/* The record is not in the list. */
				for (Metric m : ms) {
					m.visit(visitor);
					r.metricHash.add(new LdmsMetric(m.name(),
							visitor.getType()));
					count++;
				}
				records.add(r);
			} else {
				/* The record is in the list. */
				oldRecord = records.get(recordIndex);
				for (Metric m : ms) {
					m.visit(visitor);
					metric = new LdmsMetric(m.name(),
							visitor.getType());
					if (oldRecord.metricHash.contains(metric)
								== false) {
						oldRecord.metricHash.add(metric);
						count++;
					}
				}
			}

			if (oldcount == count)
				numCountEqual++;
			else
				numCountEqual = 0;

		} catch (Exception e) {
			throw new MetricsException("LdmsMetricNameSink: error", e);
		}
	}

	@Override
	public void flush() {
		if (isFlushed)
			return;

		if (numCountEqual < 100)
			return;

		isFlushed = true;
		for (LdmsRecord r : records) {
			for (LdmsMetric m : r.metricHash) {
				writer.println(r.recordContext + "." +
					r.recordName + ":" + m.name +
					"\t" + m.type);
			}
		}
		writer.flush();
		writer.close();
		numCountEqual = 0;
	}
}
