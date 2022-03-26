#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Pass.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "prospar-mem-accesses"

class ProsparMemAccesses : public llvm::ModulePass {
private:
  enum AccessType { WRITE = 0, READ = 1 };

public:
  static char ID;
  ProsparMemAccesses();
  virtual bool runOnModule(llvm::Module &M) override;
  void instrumentMemAccess(llvm::Module &M, llvm::Instruction *I,
                           llvm::Value *Addr, AccessType type,
                           llvm::Function *RecordMemFn);
  llvm::Value *isInterestingMemoryAccess(llvm::Module &M, llvm::Instruction *I,
                                         llvm::Value **op, AccessType &type);
};
