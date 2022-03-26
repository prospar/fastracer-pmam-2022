// PROSPAR: PTracer changes

#include "llvm/Transforms/TaskDebug/AnnotationInfo.h"
#include <llvm/IR/Constants.h>

using namespace llvm;

AnnotationInfo::AnnotationInfo() : ModulePass(ID) {}

bool AnnotationInfo::runOnModule(Module &M) { return false; }

bool AnnotationInfo::hasAnnotation(Value *V, StringRef Ann, uint8_t level) {
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

char AnnotationInfo::ID = 0;
static RegisterPass<AnnotationInfo> X("annotation-info",
                                      "gather type annotations", false, true);
