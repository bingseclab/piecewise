//===- DepGraph.cpp - Build a Module's dependency graph ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "DGI"

#include "llvm/Analysis/DepGraphInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <queue>
using namespace llvm;

//===----------------------------------------------------------------------===//
// Implementations of the DepGraphInfo class methods.
//

INITIALIZE_PASS(DepGraphInfo, "func-dep-graph-info",
                "Function Dependency Graph Info Construction", true, true)
char DepGraphInfo::ID = 0;

ImmutablePass *llvm::createDepGraphInfoPass() {
  return new DepGraphInfo();
}

DepGraphInfo::DepGraphInfo() : ImmutablePass(ID){
  initializeDepGraphInfoPass(*PassRegistry::getPassRegistry());
}

DepGraphInfo::~DepGraphInfo() {}

void DepGraphInfo::addToDepGraph(Function *F) {
  /*DepGraphNode *Node = */getOrInsertFunction(F);
}

DepGraphNode* DepGraphInfo::getOrInsertFunction(const Function *F) {
  DepGraphNode *DGN = FunctionMap[F];
  if (DGN)
    return DGN;

  DGN = new DepGraphNode(const_cast<Function *>(F));
  return DGN;
}

DepGraphNode* DepGraphInfo::getOrInsertFunction(const CallGraphNode *CGN) {
  Function *F = CGN->getFunction();
  DepGraphNode *DGN = FunctionMap[F];
  if (DGN)
    return DGN;

  DGN = new DepGraphNode(const_cast<Function *>(F));
  FunctionMap[F] = DGN;

  //Add dependent function to dependency graph node
  for (CallGraphNode::iterator I = const_cast<CallGraphNode*>(CGN)->begin(), E = const_cast<CallGraphNode*>(CGN)->end(); 
    I != E; ++I) {
    Function *FI = I->second->getFunction();
    if (FI){
      DGN->DepFunctions[FI] = 0;
    }
  }

  for(auto F : CGN->OtherDirectTargets) {
    if(F) DGN->DepFunctions[F] = 0;
  }

  return DGN;
}

void DepGraphInfo::print(raw_ostream &OS) const {
  OS << "Contents of Dependency Graph " << this << "\n";
  for(auto I=FunctionMap.begin(); I != FunctionMap.end(); I++) {
    DepGraphNode * Node = I->second;
    if (Node) {
      OS << Node->getFunction()->getName() << " ";
      Node->print(OS); 
    }
  }
}

void DepGraphInfo::AddCodePointer(Function *F) {
  CodePointers[F] = 0;
}

void DepGraphInfo::removeRecursion() {
  for(auto I=this->FunctionMap.begin(); I != this->FunctionMap.end(); I++) {
    DepGraphNode* Node = I->second;
    Node->DepFunctions.erase(const_cast<Function *>(I->first));
  }
}

void DepGraphInfo::addFuncDep(){
  removeRecursion();

  Module *M = nullptr;

  DepGraphInfo * NewGraph = new DepGraphInfo();
  FunctionMapTy::iterator it;
  for(it = this->FunctionMap.begin(); it != this->FunctionMap.end(); it++) {
    DepGraphNode * DGN = it->second;
    Function *F = DGN->getFunction();
    if(!M) {
      M = F->getParent();
    }
    NewGraph->FunctionMap[F] = new DepGraphNode(F);

    std::map<Function*, bool> IncludedFunc;
    std::queue<Function*> Q;
    Q.push(DGN->getFunction());
    while(!Q.empty()) {
      Function * Current = Q.front();
      Q.pop();
      auto it = IncludedFunc.find(Current);
      if(it == IncludedFunc.end()) {

        //Node not discovered
        IncludedFunc[Current] = true;
        if(Current->getName() != F->getName()) {
          NewGraph->FunctionMap[F]->DepFunctions[Current] = 0;
        }
        
        if(this->FunctionMap[Current]) {
          for (auto KV: this->FunctionMap[Current]->DepFunctions) {
            Q.push(KV.first);
          }
        }
      }
    }
  }

  this->FunctionMap.clear();
  this->FunctionMap = NewGraph->FunctionMap;

  delete NewGraph;
}

bool DepGraphInfo::hasCGEdge(Function *Caller, Function *Callee) {
  auto it = this->FunctionMap[Caller]->DepFunctions.find(Callee);
  return it!=this->FunctionMap[Caller]->end();
}

//Add a function to a dependency list
void DepGraphInfo::addDependency(Function *Caller, Function *Callee) {
  if(!Caller) return;
  assert(this->FunctionMap[Caller]);
  if(Caller == Callee) return;
  this->FunctionMap[Caller]->DepFunctions[Callee] = 0;
}

//===----------------------------------------------------------------------===//
// Implementations of the DepGraphNode class methods.
//

void DepGraphNode::print(raw_ostream &OS) const {
  OS << ": ";
  for(auto KV : DepFunctions) {
    OS << KV.first->getName() << " ";
  }
  OS << "\n";
}
