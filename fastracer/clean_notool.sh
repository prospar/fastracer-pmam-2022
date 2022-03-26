#!/bin/bash

root_cwd="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" #
echo "$root_cwd"

echo "***** Cleaning LLVM *****"
cd tdebug-llvm || exit
make clean
source llvmvars.sh

cd "$root_cwd" || exit

echo "***** Cleaning TBB *****"
cd tbb-lib || exit
make clean

cd "$root_cwd" || exit
