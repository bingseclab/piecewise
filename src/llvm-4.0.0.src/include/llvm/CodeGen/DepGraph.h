//===- DependencyGraph.h - Build a Module's dependency graph ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides interfaces used to build and manipulate a dependency graph
/// which is necessary for piece wise compilation.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DEPGRAPH_H
#define LLVM_CODEGEN_DEPGRAPH_H

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/SVF/MemoryModel/PointerAnalysis.h"
#include "llvm/Analysis/SVF/WPA/Andersen.h"
#include "llvm/Analysis/SVF/WPA/FlowSensitive.h"
#include <llvm/Analysis/AliasAnalysis.h>
#include "llvm/Analysis/DepGraphInfo.h"
#include "llvm/Analysis/CallPrinter.h"
#include <map>
#include <vector>

namespace llvm {
class Function;
class Module;

class DepGraph : public ModulePass {
  DepGraphInfo* G;
  AndersenWaveDiff* Pta;
  unsigned PwiseMode;
  unsigned CppMode;
  void getPointerFromStruct(ConstantStruct *CS);
  void getPointerFromArray(ConstantArray *CA);
  void getPointerFromCallInst(CallInst *CI);
  void getPointerFromInst(Instruction *Inst);

  void processInstruction(User *FU, Function *Callee);
  void processConstant(User *FU, Function *Callee);
  void processOperator(User *FU, Function *Callee);
  void getStringUser(ConstantDataArray *Val, Module &M);

public:
  static char ID; // Class identification, replacement for typeinfo

  DepGraph();
  DepGraph(unsigned PwiseModeIn, unsigned CppModeIn);
  ~DepGraph() override;

  const DepGraphInfo &getDepGraph() const { return *G; }
  DepGraphInfo &getDepGraph() { return *G; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;
  StringRef getPassName() const override{return "Function Dependency Graph";}

};

ModulePass *createDepGraphPass(unsigned PwiseMode, unsigned CppMode);

} // End of namespaces

#endif