add_llvm_library(LLVMPTaskDebug
  PTaskDebug.cpp
  PTaskDebugBranchCheck.cpp
  PAnnotationInfo.cpp
  )

add_dependencies(LLVMPTaskDebug intrinsics_gen)
