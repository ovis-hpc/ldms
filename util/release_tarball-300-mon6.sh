#!/bin/bash -x
# This script handles pulling the unified ldms etc from the various repos
# bundling them into a single tree
# and tarring them up after pruning any dev-only stuff.

if ! test -f m4/Ovis-top.m4; then
	echo This must be run from top of a source tree, which should be clean.
	exit 1
fi
REPO_DIR=`pwd`
OUTPUT_DIR=`pwd`/Release

BRANCH_NAME=trinity_mon_rhel6

# Will get this from git in the future
VERSION=3.0.0

if ! test -f libevent-2.0.21-stable.tar.gz; then
	echo "do not need a copy of libevent-2.0.21-stable.tar.gz in $REPO_DIR"
	echo "for release $VERSION before packing."
	#	exit 1
fi
# Create output dir
mkdir -p $OUTPUT_DIR

# cd to local git clone
cd $REPO_DIR

# Checkout $BRANCH_NAME
git checkout origin/$BRANCH_NAME -b $BRANCH_NAME

git submodule init sos
git submodule update

# Find SHA of latest checkin
COMMIT_ID="$(git log -1 --pretty="%H")"
cd sos
SOS_COMMIT_ID="$(git log -1 --pretty="%H")"
cd ..

# Get most recent tag id for this branch
TAG_ID="$(git describe --tags --abbrev=0)"

TARGET=ldms-${VERSION}.tar
SOSTARGET=sos.tar

# Create archive of desired branch
#git archive $BRANCH_NAME --format=tar --output=${OUTPUT_DIR}/ldms-${VERSION}.tar
git archive --prefix=ldms-${VERSION}/ $COMMIT_ID --format=tar --output=${OUTPUT_DIR}/$TARGET
sleep 0.1
cd sos
git archive --prefix=sos/ $SOS_COMMIT_ID --format=tar --output=${OUTPUT_DIR}/$SOSTARGET
cd ..

# cd to output dir
cd $OUTPUT_DIR

# Untar archive
echo "Untarring archive"
tar xf $TARGET
cd ldms-${VERSION}
tar xf ../$SOSTARGET
cd ..
sleep 0.1


# Add SHA file
pushd ldms-${VERSION}
echo $COMMIT_ID > SHA.txt
echo $SOS_COMMIT_ID > SHASOS.txt
echo $TAG_ID > TAG.txt
./autogen.sh
#cp $REPO_DIR/libevent-2.0.21-stable.tar.gz .
popd

# Tar back up excluding unwanted files and dirs
echo "tarring archive with excludes from "
echo "$REPO_DIR/util/tar-excludes.txt"
TAR_OPTS="-X $REPO_DIR/util/tar-excludes.txt"
tar czf $TARGET.gz $TAR_OPTS ldms-${VERSION}
sleep 0.1

# Remove untarred stuff
echo "Relocating cruft"
rm -rf old
mkdir old
mv ldms-${VERSION} $TARGET $SOSTARGET old
sleep 0.1

ls -l
