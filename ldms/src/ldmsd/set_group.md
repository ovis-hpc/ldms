Group of sets
=============

Purpose of a group of sets is to help users and plugins manage sets easier. For
example, a sampler plugin may have multiple devices to monitor, each of the
devices can be an `ldms_set`. A group of sets can tie these sets together for an
easy lookup / update.

A group of sets (maybe referred to as "group") uses `ldms_set` with
`ldms_set_info` to implement. In LDMS layer, a group will appear as a normal
set.


Use cases
=========
...
