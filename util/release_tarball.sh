#!/bin/bash -x

if ! test -f m4/Ovis-top.m4; then
	echo This must be run from top of a source tree, which should be clean.
	exit 1
fi
REPO_DIR=`pwd`
OUTPUT_DIR=`pwd`/ldms_release
BRANCH_NAME=ovispublic_RC1.2_a
# Will get this from git in the future
VERSION=2.2.0

# Create output dir
mkdir -p $OUTPUT_DIR

# cd to local git clone
cd $REPO_DIR

# Checkout $BRANCH_NAME
git checkout origin/$BRANCH_NAME -b $BRANCH_NAME

# Find SHA of latest checkin
COMMIT_ID="$(git log -1 --pretty="%H")"

# Get most recent tag id for this branch
TAG_ID="$(git describe --tags --abbrev=0)"

TARGET=ldms-${VERSION}.tar

# Create archive of desired branch
#git archive $BRANCH_NAME --format=tar --output=${OUTPUT_DIR}/ldms-${VERSION}.tar
git archive --prefix=ldms-${VERSION}/ $COMMIT_ID --format=tar --output=${OUTPUT_DIR}/$TARGET
sleep 0.1

# cd to output dir
cd $OUTPUT_DIR

# Untar archive
echo "Untarring archive"
tar xf $TARGET
sleep 0.1

# Add SHA file
pushd ldms-${VERSION}
echo $COMMIT_ID > SHA.txt
echo $TAG_ID > TAG.txt
./autogen.sh
popd

# Tar back up excluding unwanted files and dirs
echo "tarring archive with excludes from "
echo "$REPO_DIR/util/tar-excludes.txt"
TAR_OPTS="-X $REPO_DIR/util/tar-excludes.txt"
tar cf $TARGET $TAR_OPTS ldms-${VERSION}
sleep 0.1

# Remove untarred stuff
echo "Removing cruft"
rm -rf ldms-${VERSION}
sleep 0.1

ls -l
