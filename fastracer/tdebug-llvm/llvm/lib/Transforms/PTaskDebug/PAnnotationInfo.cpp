#include "llvm/Transforms/PTaskDebug/PAnnotationInfo.h"
#include <llvm/IR/Constants.h>

using namespace llvm;

PAnnotationInfo::PAnnotationInfo() : ModulePass(ID) {}

bool PAnnotationInfo::runOnModule(Module &M) {
  return false;
}

bool PAnnotationInfo::hasAnnotation(Value *V, StringRef Ann, uint8_t level) {
  // Check instruction metadata.
  if (Instruction *I = dyn_cast<Instruction>(V)) {
    MDNode *MD = I->getMetadata("tyann");
    if (MD) {
      MDString *MDS = cast<MDString>(MD->getOperand(0));
      if (MDS->getString().equals(Ann)) {
        ConstantAsMetadata *CAM = cast<ConstantAsMetadata>(MD->getOperand(1));
        ConstantInt *CI = cast<ConstantInt>(CAM->getValue());
        if (CI->getValue() == level) {
          return true;
        } else {
          return false;
        }
      }
    }
  }

  // TODO: Check for annotations on globals, parameters.

  return false;
}

char PAnnotationInfo::ID = 0;
static RegisterPass<PAnnotationInfo> X("pannotation-info",
                                      "gather type ptarcer annotations",
                                      false,
                                      true);
