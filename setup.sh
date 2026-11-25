#!/usr/bin/env bash
set -euo pipefail

# nesrecomp/ and recomp-ui/ are git submodules; fetch them at the commits this
# repo pins (the gitlinks recorded in the index; see .gitmodules). The sync
# moves a clone made before nesrecomp moved to cdmckay's fork (#143) onto it.
git submodule sync nesrecomp recomp-ui
git submodule update --init --recursive nesrecomp recomp-ui

echo "Ready - nesrecomp + recomp-ui checked out at their pinned commits."
