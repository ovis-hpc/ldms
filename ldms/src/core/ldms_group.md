Set Group
=========

A group of sets, or a "group" in short, is just another LDMS set that contain
information of other sets being the member of the group. A group of sets is not
defined in LDMS layer. The semantics of the group is defined in LDMSD context.
Hence, there is no need to modify LDMS library for this.

**NOTE**: We went to the route where the group is defined in LDMS layer, where a
group lookup resulted in `N` member lookup callbacks plus the `grp` itself at
the end, and a group update resulted in `N` member update callbacks plus the
`grp` itself at the end. This means that LMDS library needs to maintain RBDs of
the members. We ran into a problem when the sampler updated the group (e.g. add
a new member). There seems to be no good way to handle this nicely in LDMS layer
in the aggregator.


Actors
------
* L0 being an LDMSD sampler.
  * L0 has `N` normal sets and 1 group `grp` containing the `N` sets.
* L1 being a level-1 LDMSD aggregator.
  * L1 connects to L0.
* L2 being a level-2 LDMSD aggregator.
  * L2 connects to L1.


Lookup
------
* `ldmsd` looks up all listed directory. So, `grp` will be looked up like other
  sets.


Update and Group Modification
-----------------------------
* L1 iteratively issues updates to `N` members of `grp`.
  * Using `prdcr_set`. If the set has not been created yet, just skip it and
    retry next time.
  * After `N` members update issued, L1 issue an update to the `grp` itself.
* `grp` update completion will tell whether the group content has been modified
  or not.
  * If the group content has been modified, the application (L1) has to
    update the `ldms_set_info` to see the modified content of the group.
    * Re-lookup the group will update the info.
    * The new sets will be updated in the next update iteration.


L2, L1 Race
-----------
* Don't worry about race. If the `grp` on L2 was completed before its members,
  L2 will just skip the member sets that weren't ready and try again in the next
  iteration.
