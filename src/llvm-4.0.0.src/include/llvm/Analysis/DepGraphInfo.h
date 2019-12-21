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

#ifndef LLVM_ANALYSIS_DEPGRAPHINFO_H
#define LLVM_ANALYSIS_DEPGRAPHINFO_H

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/CallGraph.h"

#include "llvm/ADT/DenseMap.h"
#include <map>
#include <vector>
#include <set>

namespace llvm {
class Function;
class Module;
class DepGraphNode;

class DepGraphInfo : public ImmutablePass{
public:
  static char ID;
  explicit DepGraphInfo();
  ~DepGraphInfo();
  StringRef getPassName() const override{
    return "Function Dependency Graph Info";
  }

  typedef std::map<const Function *, DepGraphNode *> FunctionMapTy;
  typedef std::map<Function *, std::vector <Function *>> PointerMapTy;
  typedef std::map<std::string, std::set<std::string>> AsmCGTy;
  typedef std::set<std::string> InitFuncPtrTy;
  typedef FunctionMapTy::iterator iterator;
  typedef FunctionMapTy::const_iterator const_iterator;

  DepGraphNode * getOrInsertFunction(const Function *F);
  DepGraphNode * getOrInsertFunction(const CallGraphNode *CGN);

  void addFuncDep();
  void addDependency(Function *Caller, Function *Callee);
  void print(raw_ostream &OS) const;
  bool isEmpty() { return FunctionMap.empty(); };

  void addToDepGraph(Function *F);
  bool hasCGEdge(Function *Caller, Function *Callee);

  //Functions to access code pointers in current module
  void AddCodePointer(Function *F);
  inline std::map<Function *, int> *GetCodePointers() { return &CodePointers; }

  inline iterator begin() { return FunctionMap.begin(); }
  inline iterator end() { return FunctionMap.end(); }
  inline const_iterator begin() const { return FunctionMap.begin(); }
  inline const_iterator end() const { return FunctionMap.end(); }

  inline size_t size() { return FunctionMap.size(); }

  inline bool contains(Function *F) {
    auto I = FunctionMap.find(F);
    return I != FunctionMap.end();
  }

  inline const DepGraphNode *operator[](const Function *F) const {
    auto I = FunctionMap.find(F);
    assert(I != FunctionMap.end() && "Function not in depgraph!");
    return I->second;
  }

  inline const DepGraphNode *operator[](Function *F) const {
    auto I = FunctionMap.find(F);
    if(I==FunctionMap.end()) return nullptr;
    return I->second;
  }

  inline void addDynLoadFunc() {}

  PointerMapTy IndirectTargets;
  AsmCGTy AsmCG;
  InitFuncPtrTy InitFuncs;

private:
  //Module &M;
  FunctionMapTy FunctionMap;

  std::vector<StringRef> DynLoadFunc;

  //List of function pointers, including initialization and termination functions
  std::map<Function *, int> CodePointers;

  void removeRecursion();
};

class DepGraphNode {
public:
  typedef DenseMap<Function*, int> DepMapTy;
  typedef DenseMap<Function*, int>::iterator iterator;
  typedef DenseMap<Function*, int>::const_iterator const_iterator;

  inline DepGraphNode(Function *F) : F(F) {}

  Function *getFunction() const { return F; }

  void print(raw_ostream &OS) const;


  inline bool isEmpty() { return DepFunctions.empty(); }

  inline int size() const { return DepFunctions.size(); }

  inline iterator begin() { return DepFunctions.begin(); }
  inline iterator end() { return DepFunctions.end(); }
  inline const_iterator begin() const { return DepFunctions.begin(); }
  inline const_iterator end() const { return DepFunctions.end(); }

private:
  friend class DepGraphInfo;

  DenseMap<Function*, int> DepFunctions;

  Function *F;

  //Comparators
  inline bool operator<(const DepGraphNode &second) {
    return (this->getFunction()->getName().str().compare(second.getFunction()->getName().str()) < 0);
  }

  inline bool operator==(const DepGraphNode &second) {
    return (this->getFunction()->getName().str().compare(second.getFunction()->getName().str()) == 0);
  }
};

} // End of namespaces

#endif