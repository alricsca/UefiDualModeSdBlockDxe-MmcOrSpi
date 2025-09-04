#!/bin/bash
set -e
cd ../edk2_workspace/edk2
source edksetup.sh
export PACKAGES_PATH=/workspaces/projects/SdCardDxe:/workspaces/projects/edk2_workspace/edk2
build -p /workspaces/projects/SdCardDxe/SdCardDxe.dsc -a X64 -t GCC
