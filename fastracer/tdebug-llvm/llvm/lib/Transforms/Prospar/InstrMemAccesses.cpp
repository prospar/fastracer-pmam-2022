// A pass to instrument all memory accesses (loads/stores). This implementation
// uses a predefined function.

// clang++ -O0 -emit-llvm mem-accesses.cpp -c -o mem-accesses.bc
// opt -load ./modulepass/libModulePass.so -mem-access ../progs/mem-accesses.bc
// > tmp.bc
// clang++ tmp.bc -o mem-accesses; ./mem-accesses

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Pass.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Transforms/Prospar/InstrMemAccesses.h"

using namespace llvm;

STATISTIC(NumInstrumentedReads, "Number of instrumented reads");
STATISTIC(NumInstrumentedWrites, "Number of instrumented writes");

static StringRef RecordMemAccessDefined = "__record_mem_access_addr_type";
static StringRef MainSig = "main";

ProsparMemAccesses::ProsparMemAccesses() : ModulePass(ID) {}
Value *ProsparMemAccesses::isInterestingMemoryAccess(Module &M, Instruction *I,
                                                     Value **op,
                                                     AccessType &type) {
  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    // errs() << "Load Instruction:" << I << "\n";
    *op = LI->getPointerOperand();
    // errs() << "Load instruction with getPointerOperand: " << op << " " <<
    // *op << "\n";
    type = AccessType::READ;
    return LI;
  } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    // errs() << "Store Instruction:" << I << "\n";
    *op = SI->getPointerOperand();
    // errs() << "Store instruction with getPointerOperand: " << op << " " <<
    // *op << "\n";
    type = AccessType::WRITE;
    return SI;
  } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I)) {
    return nullptr; // Free of data races
  }
  return nullptr;
}

bool ProsparMemAccesses::runOnModule(Module &M) {
  Function *RecordMemFn = M.getFunction(RecordMemAccessDefined);
  if (!RecordMemFn) {
    errs() << "Unknown function referenced\n";
  }

  for (auto &F : M) {
    errs() << "Function name: " << F.getName() // << F
           << "\n";
  }

  for (auto &F : M) {
    // errs() << "Function name: " << F.getName() << F << "\n";

    if (F.getName() != MainSig) { // We need a better mechanism than this
      continue;
    }

    for (auto &BB : F) {
      for (auto &I : BB) {
        AccessType type;
        Value *op;
        if (isInterestingMemoryAccess(M, &I, &op, type)) {
          instrumentMemAccess(M, &I, op, type, RecordMemFn);
        }
      }
    }
  }
  return true; // Instructions have been changed
}

// A good example to study would be AddressSanitizer from LLVM
void ProsparMemAccesses::instrumentMemAccess(Module &M, Instruction *I,
                                             Value *Addr, AccessType type,
                                             Function *RecordMemFn) {
  if (type == AccessType::READ) {
    NumInstrumentedReads++;
  } else {
    NumInstrumentedWrites++;
  }

  IRBuilder<> IRB(I);

  BitCastInst *bitcast = new BitCastInst(
      Addr, PointerType::getUnqual(Type::getInt8Ty(M.getContext())), "", I);
  Value *RdWr = (type == AccessType::READ) ? IRB.getInt32(1) : IRB.getInt32(0);

  Value *args[] = {bitcast, RdWr};
  CallInst *call = IRB.CreateCall(RecordMemFn, args);
}

char ProsparMemAccesses::ID = 0;

// Register our pass with the pass manager in opt. For more information, see:
// http://llvm.org/docs/WritingAnLLVMPass.html
static RegisterPass<ProsparMemAccesses>
    X("prospar-mem-accesses", "Instrument memory accesses in a module",
      false /* true if only looks at CFG */, false /* true if analysis pass */);
