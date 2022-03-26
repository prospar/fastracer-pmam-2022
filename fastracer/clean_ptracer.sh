#!/bin/bash

root_cwd="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" #
echo "$root_cwd"

echo "***** Cleaning LLVM *****"
cd tdebug-llvm || exit
make clean
source llvmvars.sh

cd "$root_cwd" || exit

echo "***** Cleaning PTRacer *****"
export LD_LIBRARY_PATH=""
cd tdebug-lib || exit
make clean
source tdvars.sh

cd "$root_cwd" || exit

echo "***** Cleaning TBB *****"
cd tbb-lib || exit
make clean

cd "$root_cwd" || exit
