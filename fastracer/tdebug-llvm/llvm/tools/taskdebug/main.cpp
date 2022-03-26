// PROSPAR: PTracer changes

#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/RegionPass.h"
//#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/IR/DataLayout.h"
//#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
//#include "llvm/IR/LegacyPassNameParser.h"
#include "llvm/IR/Module.h"
//#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/LinkAllIR.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
//#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <algorithm>
#include <memory>

using namespace llvm;

#include "llvm/Transforms/TaskDebug/TaskDebugBranchCheckPass.h"
#include "llvm/Transforms/TaskDebug/TaskDebugPass.h"

#include <algorithm>
#include <memory>

#include <fstream>
#include <iostream>
#include <memory>

using namespace llvm;

static cl::opt<std::string>
    InputFilename(cl::Positional, cl::desc("<input bitcode>"), cl::init("-"));

int main(int argc, char **argv) {
  llvm_shutdown_obj Y;
  LLVMContext &Context = getGlobalContext();

  cl::ParseCommandLineOptions(argc, argv, "Task debugging framework\n");
  sys::PrintStackTraceOnErrorSignal();

  PassRegistry &Registry = *PassRegistry::getPassRegistry();

  initializeCore(Registry);
  initializeScalarOpts(Registry);
  initializeIPO(Registry);
  initializeAnalysis(Registry);
  initializeIPA(Registry);
  initializeTransformUtils(Registry);
  initializeInstCombine(Registry);
  initializeInstrumentation(Registry);
  initializeTarget(Registry);

  llvm::legacy::PassManager Passes;

#if 0
  SMDiagnostic Err;
  std::unique_ptr<Module> M1;

  M1.reset(ParseIRFile(InputFilename, Err, Context));
  if(M1.get() == 0){
    Err.print(argv[0], errs());
    return 1;
  }
#endif

  // const DataLayout * DL = M1.get()->getDataLayout();
  // if (DL)
  // Passes.add(new DataLayoutPass(M1.get()));

  // TargetLibraryInfo *TLI = new
  // TargetLibraryInfo(Triple(M1.get()->getTargetTriple())); Passes.add(TLI);

  Passes.add(new TaskDebug());
  Passes.add(new TaskDebugBranchCheck());

  // Passes.add(createBitcodeWriterPass(Out->os()));
  // Passes.run(*M1.get());

  // Out->keep();

  return 0;
}
