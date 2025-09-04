#!/usr/bin/env bash
set -eo pipefail
if [ -f ./edk2_setup.inc ]; then
  cd ../edk2_workspace/edk2
  rm -f Conf/BuildEnv.sh
  source edksetup.inc
fi
if [ -z "${EDK2_BASE:-}" ]; then
  echo "Set EDK2_BASE in edk2_setup.inc first."
  exit 1
fi
export PYTHON_COMMAND=python3
pushd "$EDK2_BASE"
set +u
source .edksetup.sh
build -p ../../SdCardDxe/SdCardDxe.dsc -a X64 -t @@TOOLCHAIN_PREFIX@@
set -u
popd
echo "Build Complete"
exit 0