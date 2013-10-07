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
import java.net.*;
import java.util.Collection;

import org.apache.commons.configuration.SubsetConfiguration;
import org.apache.hadoop.metrics2.MetricsException;
import org.apache.hadoop.metrics2.Metric;
import org.apache.hadoop.metrics2.MetricsRecord;
import org.apache.hadoop.metrics2.MetricsSink;
import org.apache.hadoop.metrics2.lib.DefaultMetricsSystem;

public class LdmsSink implements MetricsSink{

	protected static final int DEFAULT_PORT = 50000;
	protected static final String DEFAULT_HOST = "localhost";

	private final LdmsVisitor visitor = new LdmsVisitor();

	private DatagramSocket socket;
	private InetAddress ldmsSamplerAddr;
	private int ldmsSamplerPort;
	private String hostname;
	private String daemonName;

	private PrintWriter writer;

	@Override
	public void init(SubsetConfiguration conf) {

		DefaultMetricsSystem.INSTANCE.register("ldms",
				"Send metrics to Hadoop sampler in LDMS",
				new LdmsSink());

		ldmsSamplerPort = conf.getInt("port", DEFAULT_PORT);
		hostname = conf.getString("host", DEFAULT_HOST);
		daemonName = conf.getString("daemon", "unknown");
		try {
			ldmsSamplerAddr = InetAddress.getByName(hostname);
			socket = new DatagramSocket();
		} catch (Exception e) {
			throw new MetricsException("ldmsSink: Failed to new " +
					"DatagramSocket", e);
		}

		String filename = "/home/hduser/tmp.out";
		try {
			writer = filename == null
					? new PrintWriter(new BufferedOutputStream
						(System.out))
					: new PrintWriter(new FileWriter(
						new File(filename), true));
		} catch (Exception e) {
			throw new MetricsException("Error creating " + filename, e);
		}
		writer.println("starting the LdmsSink");
		writer.println("host: " + hostname);
		writer.println("port: " + ldmsSamplerPort);
		writer.flush();
	}

	private void sendToLdms(StringBuilder sb) {
		sb.append("\0");
		String s = sb.toString();
		byte[] outMetrics = s.getBytes();

		try {
			DatagramPacket outPacket = new DatagramPacket(outMetrics,
								outMetrics.length,
								ldmsSamplerAddr,
								ldmsSamplerPort);
			writer.println(s);
			socket.send(outPacket);
		} catch (Exception e) {
			writer.println("failed to send");
			throw new MetricsException("Error sending to LDMS.", e);
		}
	}

	@Override
	public void putMetrics(MetricsRecord record) {
		try {
			String recordName = record.name();
			String contextName = record.context();

			StringBuilder sb = new StringBuilder();
			sb.append(contextName);
			sb.append(".");
			sb.append(recordName);
			sb.append(":");

			Collection<Metric> ms = (Collection<Metric>)
							record.metrics();
			if (ms.size() > 0) {
				for (Metric m : ms) {
					m.visit(visitor);
					sb.append(m.name());
					sb.append("=");
					sb.append(m.value());
					sb.append(",");
				}
				/* Delete the last comma */
				sb.deleteCharAt(sb.length() - 1);
				sendToLdms(sb);
			}
		} catch (Exception e) {
			throw new MetricsException("LdmsSink: Error in " +
							"putMetrics.", e);
		}
	}

	@Override
	public void flush() {
		// do nothing
	}
}
