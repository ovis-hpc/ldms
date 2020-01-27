LDMS - Light-weight Distributed Metric Service
==============================================

LDMS means Lightweight Distributed Metric Service. It is an Application
Programming Interface for publishing and gathering system metrics in a clustered
environment. A metric set provider publishes metric sets to remote peers. The
remote peers update their local copies of these metrics whenever they choose,
i.e. the metric set published does not push the data, it is pulled by the
client. Only the metric set publisher can change the contents of the metric set.
Although the client can change its local copy, the data will be overwritten the
next time the set is updated. For more information, please see [LDMS-API].

LDMSD (LDMS Daemon) is a program using LDMS to collect metrics. LDMSD can have
two roles: sampler, and aggregator. A sampler gaters data (usually locally, but
not necessary) and publish the data as LDMS sets using **LDMSD Sampler Plugin**.
An aggregater then gathers the sets from multiple samplers over LDMS network. An
aggregator can collect LDMS sets from another aggregator. This creates tree-like
data gathering for scalability. An aggregator can also store the collected data
using **LDMSD Store Plugin**.

For more LDMSD usage information, please see [ldmsd-sampler], and
[ldmsd-aggregator].

For plugin developers, please see
- [ldmsd-sampler-dev] for LDMSD Sampler Plugin
  development, and
- [ldmsd-store-dev] for LDMSD Store Plugin
  development.

See [mdtest](doc/mdtest.md) for a concise instruction on how to write
a man-sturctured markdown document and add it in `ldms` project.

[LDMS-API]: LDMS.html
[ldmsd-sampler]: src/ldmsd/ldmsd-sampler.md
[ldmsd-aggregator]: src/ldmsd/ldmsd-aggregator.md
[ldmsd-sampler-dev]: src/ldmsd/ldmsd-sampler-dev.md
[ldmsd-store-dev]: src/ldmsd/ldmsd-store-dev.md
