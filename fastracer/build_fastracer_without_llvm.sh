#!/bin/bash

root_cwd="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" #
echo "$root_cwd"

echo "***** Building LLVM *****"
cd tdebug-llvm || exit
source llvmvars.sh

echo "***** Building Fasttrack *****"
export LD_LIBRARY_PATH=""
cd "$root_cwd" || exit
cd fastracer || exit
make clean
if ! make; then
    echo "Make failed!"
    exit
fi
source tdvars.sh

echo "***** Building TBB *****"
cd "$root_cwd" || exit
cd tbb-lib || exit
make -s
source obj/tbbvars.sh

cd "$root_cwd" || exit
