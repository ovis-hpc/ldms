#!/bin/bash -x
# This script handles pulling the unified ldms etc from the various repos
# bundling them into a single tree
# and tarring them up after pruning any dev-only stuff.
##
# on lanl, can't reach remote repos, so tar is our friend for sos.

if ! test -f m4/Ovis-top.m4; then
	echo This must be run from top of a source tree, which should be clean.
	exit 1
fi
REPO_DIR=`pwd`
OUTPUT_DIR=`pwd`/Release

BRANCH_NAME=master

# Will get this from git in the future
VERSION=3.3.2

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
if ! test "$BRANCH_NAME" = "master"; then
	git checkout origin/$BRANCH_NAME -b $BRANCH_NAME
fi
git submodule init sos
git submodule init gpcd-support
git submodule update sos
git submodule update gpcd-support

# Find SHA of latest checkin
COMMIT_ID="$(git log -1 --pretty="%H")"
SOS_COMMIT_ID=cfe185184d8d4d9451ad120a6a1cf06806c6af4d

# Get most recent tag id for this branch
TAG_ID="$(git describe --tags --abbrev=0)"

TARGET=ovis-${VERSION}.tar
SOSTARGET=sos.tar
GPCDTARGET=gpcd.tar


#cp util/cray-sos.patch $OUTPUT_DIR
# Create archive of desired branch
#git archive $BRANCH_NAME --format=tar --output=${OUTPUT_DIR}/ldms-${VERSION}.tar
git archive --prefix=ovis-${VERSION}/ $COMMIT_ID --format=tar --output=${OUTPUT_DIR}/$TARGET
sleep 0.1
cd sos
git archive --prefix=sos/ $SOS_COMMIT_ID --format=tar --output=${OUTPUT_DIR}/$SOSTARGET
# cp $SOSTARGET ${OUTPUT_DIR}/
cd ..

cd gpcd-support
git archive --prefix=gpcd-support/ master --format=tar --output=${OUTPUT_DIR}/$GPCDTARGET
# cp $GPCDTARGET ${OUTPUT_DIR}/
cd ..

# cd to output dir
cd $OUTPUT_DIR

# Untar archive
echo "Untarring archive"
tar xf $TARGET
cd ovis-${VERSION}
tar xf ../$SOSTARGET
tar xf ../$GPCDTARGET
if ! test -f sos/configure.ac; then
	echo SOS not untarred ok
	exit 1
else
	: # cd sos; patch < ../../cray-sos.patch)
fi
if ! test -f gpcd-support/configure.ac; then
	echo GPCD-SUPPORT not untarred ok
	exit 1
fi
cd ..
sleep 0.1


# Add SHA file
pushd ovis-${VERSION}
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
tar czf $TARGET.gz $TAR_OPTS ovis-${VERSION}
sleep 0.1

# Remove untarred stuff
echo "Relocating cruft"
rm -rf old
mkdir old
mv -f ovis-${VERSION} $TARGET $SOSTARGET old
sleep 0.1

ls -l
