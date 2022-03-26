#!/bin/bash

root_cwd="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" #
echo "$root_cwd"

echo "***** Building LLVM *****"
cd tdebug-llvm || exit
make -s
source llvmvars.sh

echo "***** Building SPD3 *****"
export LD_LIBRARY_PATH=""
cd "$root_cwd" || exit
cd spd3-lib || exit
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

cd "$root_cwd" || exit
