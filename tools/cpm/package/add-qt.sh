#!/bin/sh -e

# SPDX-FileCopyrightText: Copyright 2026 crueter
# SPDX-License-Identifier: LGPL-3.0-or-later

RETURN=0

usage() {
	cat <<EOF
Usage: cpmutil.sh package add-qt [repo] [tag]

Add a repository and version of compatible Qt builds to the cpmfile.

See https://github.com/crueter-ci/Qt.

EOF

	exit "$RETURN"
}

die() {
	echo "-- $*" >&2
	RETURN=1 usage
}

REPO="$1"
VERSION="$2"

[ -n "$REPO" ] || die "You must specify a Qt build repository."
[ -n "$VERSION" ] || die "You must specify a version tag."

# invoke add.cmake
cmake \
	-DKEY=qt \
	-DREPO="$REPO" \
	-DVERSION="$VERSION" \
	-DCI=true \
	-DPACKAGE=Qt6 \
	-DARTIFACT=qt \
	-P "$CMAKE"/add.cmake

echo "Added package qt to cpmfile.json. Include it in your project with AddJsonPackage(NAME qt MODULE_PATH)"
