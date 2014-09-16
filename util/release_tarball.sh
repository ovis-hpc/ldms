#!/bin/bash

REPO_DIR=/home/brandt/Source/ovispublic_hekili
OUTPUT_DIR=/home/brandt/ldms_release
TMP_DIR=/home/brandt/ldms_release_tmp
BRANCH_NAME=ovispublic_RC1.2_a
# Will get this from git in the future
VERSION=2.2.0


# Create output dir
mkdir -p $OUTPUT_DIR

# Create tmp dir
mkdir -p $TMP_DIR

# cd to local git clone
cd $REPO_DIR

# Checkout $BRANCH_NAME
git checkout origin/$BRANCH_NAME -b $BRANCH_NAME

# Find SHA of latest checkin
COMMIT_ID="$(git log -1 --pretty="%H")"

# Get most recent tag id for this branch
TAG_ID="$(git describe --tags --abbrev=0)"

# Create archive of desired branch
#git archive $BRANCH_NAME --format=tar --output=${OUTPUT_DIR}/ldms-${VERSION}.tar
git archive $COMMIT_ID --format=tar --output=${OUTPUT_DIR}/ldms-${VERSION}.tar
sleep 0.1

# cd to output dir
cd $OUTPUT_DIR

# Untar archive
echo "Untarring archive"
tar xf ldms-${VERSION}.tar
sleep 0.1

# Add SHA file
echo $COMMIT_ID > SHA.txt
echo $TAG_ID > TAG.txt

# Tar back up excluding unwanted files and dirs
echo "tarring archive with excludes"
tar cf ldms-${VERSION}.tar \
	--exclude='ldms/src/sampler/sedc.c' \
	--exclude='ldms/src/store/store_sos.c' \
	--exclude='ldms/src/store/store_mysql.c' \
	--exclude='ldms/src/store/store_mysqlbulk.c' \
	--exclude='sos' \
	--exclude='lib/src/zap' \
	--exclude='util/release_tarball.sh' \
	*
sleep 0.1
# Move archive to tmp dir
echo "Moving tarball to tmp"
mv ldms-${VERSION}.tar $TMP_DIR
sleep 0.1

# Remove untarred stuff
echo "Removing cruft"
rm -rf *
sleep 0.1

# Move archive from tmp to release dir
echo "Moving archive back to release dir"
mv $TMP_DIR/ldms-${VERSION}.tar .
sleep 0.1

# Remove tmp dir
echo "Removing tmp dir"
rmdir $TMP_DIR

ls -l
