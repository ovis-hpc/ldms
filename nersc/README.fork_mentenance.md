OVIS Fork Maintenance
---

We have a fork of the ovis repo.

Our fork mainly adds .gitlab-ci.yml, nersc directory, samplers, and storage plugins.

From time to time we merge the next upstream branch into our current branch, making a new branch 


Steps to upgrade from upstream 
---

Merging upstream OVIS-4.4.2 branch into nersc/OVIS-4.3.11 branch, forming the nersc/OVIS-4.4 branch.

```shell
git checkout https://gitlab.nersc.gov/nersc/csg/ovis.git

# Get into position
cd ovis

# Add upstream 
git remote add https://github.com/ovis-hpc/ovis.git upstream

# Get upstram references
git fetch  --all --tags --prune upstream

# Delete any local branch
git branch -D nersc/OVIS-4.4.2

# Delete any origin branch
git push -d origin nersc/OVIS-4.4.2

# Make new branch
git checkout -b nersc/OVIS-4.4.2

  Switched to a new branch 'nersc/OVIS-4.4.2'

# Merge from upstream branch
git merge upstream/OVIS-4.4.2

  Auto-merging configure.ac
  CONFLICT (content): Merge conflict in configure.ac
  Auto-merging ldms/src/sampler/Makefile.am
  Automatic merge failed; fix conflicts and then commit the result.

# They want to remove a line from configure.ac.  

  <<<<<<< HEAD
  AS_IF([test "$enable_ibnet" = xyes],[
          AC_MSG_NOTICE([Disable ibnet module NOT requested])
  =======
  AS_IF([test "x$enable_ibnet" = xyes],[
  >>>>>>> upstream/OVIS-4.4.2

# Accept and resume
vi configure.ac
git commit -am 'accept upstram change'
git merge --continue

  fatal: There is no merge in progress (MERGE_HEAD missing).

# Check the merge
git merge upstream/OVIS-4.4.2

   Already up to date.

# Push to gitlab
git push -u origin nersc/OVIS-4.4.2
```
