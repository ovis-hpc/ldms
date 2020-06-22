#!/usr/bin/env python3

# Test/verify ldms_grp index locally (no remote update) for member
# adding/removing capabilities.

from ovis_ldms import ldms

ldms.init(128*1024*1024)

class Debug(object): pass

D = Debug()

def grp_add(grp, m):
    print("adding:", m)
    D.s0 = set(grp)
    grp.transaction_begin()
    grp.add(m)
    grp.transaction_end()
    D.s1 = set(grp)
    print(grp)
    assert(D.s0 < D.s1 and D.s1 - D.s0 == set([m]))
    grp.verify()

def grp_rm(grp, m):
    print("removing:", m)
    D.s0 = set(grp)
    grp.transaction_begin()
    grp.remove(m)
    grp.transaction_end()
    D.s1 = set(grp)
    print(grp)
    assert(D.s1 < D.s0 and D.s0 - D.s1 == set([m]))
    grp.verify()

grp = ldms.Grp(name="grp")
grp.publish()
members = [ "set{:02d}".format(i) for i in range(1, 16) ]
members_rev = list(members)
members_rev.reverse()

members_mid = [ "set{:02d}".format(i) for i in
        [ 1, 15, 8, 4, 11, 3, 6, 5, 14, 13, 12, 9, 10, 7, 2 ] ]

members_mid_rm = [ "set{:02d}".format(i) for i in
        [ 8, 9, 11, 10, 7, 13, 12, 5, 4, 15, 3, 14, 2, 6, 1 ] ]

def inc_ins_inc_rm():
    print("adding (incremental order)")
    for m in members:
        grp_add(grp, m)
    print("removing (incremental order)")
    for m in members:
        grp_rm(grp, m)
inc_ins_inc_rm()

def dec_ins_dec_rm():
    print("adding (decremental order)")
    for m in members_rev:
        grp_add(grp, m)

    print("removing (decremental order)")
    for m in members_rev:
        grp_rm(grp, m)
dec_ins_dec_rm()

def inc_ins_dec_rm():
    print("adding (incremental order)")
    for m in members:
        grp_add(grp, m)

    print("removing (decremental order)")
    for m in members_rev:
        grp_rm(grp, m)
inc_ins_dec_rm()

def dec_ins_inc_rm():
    print("adding (decremental order)")
    for m in members_rev:
        grp_add(grp, m)

    print("removing (incremental order)")
    for m in members:
        grp_rm(grp, m)
dec_ins_inc_rm()

def mid_ins_rm():
    print("adding (split-middle)")
    for m in members_mid:
        grp_add(grp, m)
    print("removing (same order)")
    for m in members_mid:
        grp_rm(grp, m)
mid_ins_rm()

def mid_ins_mid_rm():
    print("adding (split-middle)")
    for m in members_mid:
        grp_add(grp, m)
    print("removing (aiming for the middle)")
    for m in members_mid_rm:
        grp_rm(grp, m)
mid_ins_mid_rm()

print("\033[01;32m-- OK --\033[0m")
