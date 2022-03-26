add_llvm_library(LLVMProspar
  InstrMemAccesses.cpp
  )

add_dependencies(LLVMProspar intrinsics_gen)

add_llvm_loadable_module(LLVMProsparso
InstrMemAccesses.cpp

DEPENDS
intrinsics_gen
)
