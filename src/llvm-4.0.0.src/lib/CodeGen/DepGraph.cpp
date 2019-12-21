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
#define DEBUG_TYPE "dep-graph"

#include "llvm/CodeGen/DepGraph.h"
#include "llvm/Analysis/DepGraphInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/Casting.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Attributes.h"
#include <cctype>

#include <map>
#include <ctime>

/*Pwise indirect callsite resolve mode:
0: Scan and include all function pointers. All functions with address taken will
    be loaded by default.
1: Scan and and add function pointers as dependency. In this mode,
    we only include a function pointer if the function that references it 
    is loaded in memory.
2: Use SVF's implementation of Andersen algorithm to resolve each indirect calls.
*/

/*Pwise Cpp handling mode:
0: Include all virtual functions
1: Object-sensitive analysis
2: Pointer Analysis
*/
using namespace llvm;

char DepGraph::ID = 0;

INITIALIZE_PASS_BEGIN(DepGraph, "depgraph",
                      "DepGraph Construction", true, true)
INITIALIZE_PASS_DEPENDENCY(DepGraphInfo);
INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass);
INITIALIZE_PASS_END(DepGraph, "depgraph",
                    "DepGraph Construction", true, true)

//const char DepGraph::dep_section_prefix[] = ".func.dep.graph.text.";

std::map<User *, Value *> UserAdded;

ModulePass *llvm::createDepGraphPass() {
  return new DepGraph();
}

ModulePass *llvm::createDepGraphPass(unsigned PwiseModeIn, unsigned CppModeIn) {
  return new DepGraph(PwiseModeIn, CppModeIn);
}

DepGraph::DepGraph() : ModulePass(ID) {
  PwiseMode = 1;
  CppMode = 1;
  initializeDepGraphPass(*PassRegistry::getPassRegistry());
}

DepGraph::DepGraph(unsigned PwiseModeIn, unsigned CppModeIn) : ModulePass(ID) {
  PwiseMode = PwiseModeIn;
  CppMode = CppModeIn;
  initializeDepGraphPass(*PassRegistry::getPassRegistry());
}

DepGraph::~DepGraph() {}


/// Given a llvm.global_ctors list that we can understand,
/// return a list of the functions and null terminator as a vector.
std::vector<Function *> parseGlobalCtors(GlobalVariable *GV) {
  if (GV->getInitializer()->isNullValue())
    return std::vector<Function *>();
  ConstantArray *CA = cast<ConstantArray>(GV->getInitializer());
  std::vector<Function *> Result;
  Result.reserve(CA->getNumOperands());
  for (User::op_iterator i = CA->op_begin(), e = CA->op_end(); i != e; ++i) {
    ConstantStruct *CS = cast<ConstantStruct>(*i);
    Result.push_back(dyn_cast<Function>(CS->getOperand(1)));
  }
  return Result;
}

void DepGraph::getPointerFromInst(Instruction *Inst) {
  for(size_t i=0; i<Inst->getNumOperands(); i++) {
    Value *OP = Inst->getOperand(i);

    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(OP)) {
      if(Function *F = dyn_cast<Function>(CE->getOperand(0))) {
        if(!F->isIntrinsic())
          G->AddCodePointer(F);
      }
    }

    if(Constant *CO = dyn_cast<Constant>(OP)) {
      if(Function *F = dyn_cast<Function>(CO)) {
        if(!F->isIntrinsic())
          G->AddCodePointer(F);
      }
    }
  }
}

void DepGraph::getPointerFromCallInst(CallInst *CI) {
  for (CallInst::op_iterator I = CI->arg_begin(), E = CI->arg_end();
        I != E; ++I) {
    if(Function *F = dyn_cast<Function>(*I)) {
      if(!F->isIntrinsic())
        G->AddCodePointer(F);
    }

    Value *OP = I->getUser();

    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(OP)) {
      if(Function *F = dyn_cast<Function>(CE->getOperand(0))) {
        if(!F->isIntrinsic())
          G->AddCodePointer(F);
      }
    }

    if(Constant *CO = dyn_cast<Constant>(OP)) {
      if(Function *F = dyn_cast<Function>(CO)) {
        if(!F->isIntrinsic())
          G->AddCodePointer(F);
      }
    }

    //Check every operands of this instruction
    if (Instruction *Inst = dyn_cast<Instruction>(*I)) {
      getPointerFromInst(Inst);
    }
  }
}

void DepGraph::getPointerFromArray(ConstantArray *CA) {
  for (User::op_iterator i = CA->op_begin(), e = CA->op_end(); i != e; ++i) {
    if(Function *F = dyn_cast<Function>(*i)) {
      if(!F->isIntrinsic())
        G->AddCodePointer(F);
    }
    if(ConstantStruct *CS = dyn_cast<ConstantStruct>(*i)) {
      getPointerFromStruct(CS);
    }
  }
}

void DepGraph::getPointerFromStruct(ConstantStruct *CS) {
  for (User::op_iterator i = CS->op_begin(), e = CS->op_end(); i != e; ++i) {
    if(Constant *CO = dyn_cast<Constant>(*i)) {
      if(Function *F = dyn_cast<Function>(CO)) {
        if(!F->isIntrinsic())
          G->AddCodePointer(F);
      }
    }
    if(isa<ConstantStruct>(i->get())) {
      getPointerFromStruct(cast<ConstantStruct>(i->get()));
    }
    if(isa<ConstantArray>(i->get())) {
      getPointerFromArray(cast<ConstantArray>(i->get()));
    }
  }
}

void DepGraph::processInstruction(User *FU, Function *Callee) {
  if(isa<BlockAddress>(FU) || isa<CallInst>(FU) || isa<InvokeInst>(FU)) {
    Instruction *I = dyn_cast<Instruction>(FU);
    if(I && !G->hasCGEdge(I->getFunction(), Callee)) {
      UserAdded[FU] = Callee;
      G->addDependency(I->getFunction(), Callee);
    }
  }
  if(isa<Instruction>(FU)){
    Instruction *I = cast<Instruction>(FU);
    UserAdded[FU] = Callee;
    G->addDependency(I->getFunction(), Callee);
  }
}

void DepGraph::processConstant(User *FU, Function *Callee) {
  UserAdded[FU] = Callee;
  Constant *C = dyn_cast<Constant>(FU);
  for(auto ThisUser : C->users()){
    auto I = UserAdded.find(ThisUser);
    if(I != UserAdded.end() && I->second == Callee) continue;
    if(isa<Instruction>(ThisUser)){
      processInstruction(ThisUser, Callee);
    }
    if(isa<Operator>(ThisUser))
      processOperator(ThisUser, Callee);
    if(isa<Constant>(ThisUser))
      processConstant(ThisUser, Callee);
  }
}

void DepGraph::processOperator(User *FU, Function *Callee) {
  UserAdded[FU] = Callee;
  Operator *Op = dyn_cast<Operator>(FU);
  for(auto ThisUser : Op->users()){
    auto I = UserAdded.find(ThisUser);
    if(I != UserAdded.end() && I->second == Callee) continue;
    if(isa<Instruction>(ThisUser))
      processInstruction(ThisUser, Callee);
    if(isa<Operator>(ThisUser))
      processOperator(ThisUser, Callee);
    if(isa<Constant>(ThisUser))
      processConstant(ThisUser, Callee);
  }
}

// Check is a Contant contains a valid string for a function name.
// alnum
bool IsValidFuncName(StringRef Name) {
  for(auto C : Name.str()) {
    if(C == '\00') continue;
    if(!isalnum(C) && (C != '_')) return false;
  }
  return true;
}

bool DepGraph::runOnModule(Module &M) {
  G = &getAnalysis<DepGraphInfo>();

  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();

  //CG.print(errs());

  for (const auto &I : CG) {
    CallGraphNode *CGN = I.second.get();
    //CGN->dump();
    Function *F = CGN->getFunction();

    if (!F || F->isIntrinsic())
      continue;

    G->getOrInsertFunction(CGN);
  }

  //Identify initialization and termination functions
  GlobalVariable *GV = M.getGlobalVariable("llvm.global_ctors");
  if(GV != nullptr) {
    std::vector<Function *> Ctors = parseGlobalCtors(GV);
    if(!Ctors.empty()) {
      for (auto F : Ctors) {
        if(F && F->isIntrinsic()) continue;
        if(!F) continue;
        G->AddCodePointer(F);
      }
    }
  }

  GV = M.getGlobalVariable("llvm.global_dtors");
  if(GV != nullptr) {
    std::vector<Function *> Ctors = parseGlobalCtors(GV);
    if(!Ctors.empty()) {
      for (auto F : Ctors) {
        if(F->isIntrinsic()) continue;
        G->AddCodePointer(F);
      }
    }
  }

  //Precise global function pointer scanning
  if(PwiseMode == 0) {
    errs() << "Code Pointer Mode: full-module scan\n";
    UserAdded.clear();
    for(Function &F : M.getFunctionList()) {
      if(F.hasAddressTaken()) {
        G->AddCodePointer(&F);
      }
    }
  }

  if(CppMode == 1) {
    errs() << "Object-sensitive Analysis\n";
    NamedMDNode *VFMD =M.getNamedMetadata("llvm.vfuncs");
    if(VFMD && VFMD->getNumOperands()) {
      MDNode *Node = VFMD->getOperand(0);
      for(unsigned i=1; i<Node->getNumOperands(); i++) {
        MDString *vfuncs = dyn_cast_or_null<MDString>(Node->getOperand(i));
        if(vfuncs) {
          const char *CurrentAddr = (const char *) vfuncs->bytes_begin();
          assert(CurrentAddr && "Invalid vfuncs address"); 
          const char *SectionEnd = (const char *) vfuncs->bytes_end();
          Function *CxxConstr = nullptr;
          while(CurrentAddr < SectionEnd) {
            //Read caller
            std::string FnStr(CurrentAddr);
            CurrentAddr += FnStr.size()+1;
            Function *Fn = M.getFunction(FnStr);
            if(Fn) {
              //Constructor
              if(Fn->hasFnAttribute(Attribute::CXXConstructor)) {
                CxxConstr = Fn;
              }else {
                G->addDependency(CxxConstr, Fn);
              }
            }
          }
        }
      }
    }
  }

  //Scan for C++ virtual functions as global dep
  if(CppMode == 0) {
    errs() << "Include all virtual functions\n";
    for(Function &F : M.getFunctionList()) {
      if(F.hasFnAttribute(Attribute::GlobalDepVFunc)) {
        G->AddCodePointer(&F);
      }
    }
  }

  //Identify if each function is referenced anywhere in the module. If so,
  //it is likely to be invoked via indirect calls and therefore should be
  //treated as a potential function pointer.
  if(PwiseMode == 1 ) {
    errs() << "Code Pointer Mode: local scan\n";
    UserAdded.clear();
    for(Function &F : M.getFunctionList()) {
      if(F.hasAddressTaken()) {
        for(Use &U : F.uses()) {
          //Check if we've looked into this user
          User *FU = U.getUser();
          auto I = UserAdded.find(FU);
          if(I != UserAdded.end() && I->second == &F) continue;
          if(!FU) continue;

          if(isa<BlockAddress>(FU) || isa<CallInst>(FU) || isa<InvokeInst>(FU)) {
            Instruction *I = dyn_cast<Instruction>(FU);
            if(I && !G->hasCGEdge(I->getFunction(), &F)) {
              UserAdded[FU] = &F;
              G->addDependency(I->getFunction(), &F);
              continue;
            }
          }
          if(isa<Instruction>(FU)){
            Instruction *I = dyn_cast<Instruction>(FU);
            UserAdded[FU] = &F;
            G->addDependency(I->getFunction(), &F);
            continue;
          }
          if(isa<Operator>(FU)){
            processOperator(FU, &F);
          }
          if(isa<Constant>(FU)){
            Constant *Op = dyn_cast<Constant>(FU);
            processConstant(FU, &F);
            continue;
          }
        }
      }
    }
  }

  //Read assembly functions callgraphs
  NamedMDNode *CGMD =M.getNamedMetadata("llvm.asm.cg");
  if(CGMD && CGMD->getNumOperands()) {
    MDNode *Node = CGMD->getOperand(0);
    //Read callgraphs
    MDString *CG = dyn_cast_or_null<MDString>(Node->getOperand(0));
    if(CG) {
      const char *CurrentAddr = (const char *) CG->bytes_begin();
      assert(CurrentAddr && "Invalid Asm CG address"); 
      const char *SectionEnd = (const char *) CG->bytes_end();
      //Read caller's name
      while(CurrentAddr < SectionEnd) {
        //Read caller
        std::string Caller(CurrentAddr);
        CurrentAddr += Caller.size()+1;
        size_t NCallee = *(size_t *)(CurrentAddr);
        CurrentAddr += sizeof(size_t);
        while(NCallee > 0) {
          //Read callee's name
          std::string Callee(CurrentAddr);
          CurrentAddr += Callee.size() + 1;
          NCallee--;
          if(Callee.front() == '.') continue; //ignore temporary labels
          G->AsmCG[Caller].insert(Callee);
          Function *CalleeF = M.getFunction(Callee);
          if(CalleeF) { //not asm function, add its dependency graph
            const DepGraphNode *DG = (*G)[CalleeF];
            if(DG)
              for(auto I : *DG) {
                G->AsmCG[Caller].insert(I.first->getName());
              }
          }
        }
      }
    }

    //Read func ptrs
    for(unsigned i=1; i<Node->getNumOperands(); i++) {
      MDString *FuncPtrStr = dyn_cast_or_null<MDString>(Node->getOperand(i));
      if(FuncPtrStr) {
        const char *CurrentAddr = (const char *) FuncPtrStr->bytes_begin();
        assert(CurrentAddr && "Invalid Asm CG address"); 
        const char *SectionEnd = (const char *) FuncPtrStr->bytes_end();
        while(CurrentAddr < SectionEnd) {
          //Read caller
          std::string F(CurrentAddr);
          CurrentAddr += F.size()+1;
          G->InitFuncs.insert(F);
        }
      }
    }
    
  }

  //Perform context-sensitive pointer analysis
  if(PwiseMode == 2 || CppMode == 2) {
    errs() << "Pointer analysis is disabled.\n";
  }

    //Add dependencies for each functions in module
  G->addFuncDep();
  return false;
}

void DepGraph::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<CallGraphWrapperPass>();
  AU.addRequired<DepGraphInfo>();
  AU.setPreservesAll();
}
