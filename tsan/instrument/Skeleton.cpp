
  #include "llvm/Pass.h"
  #include "llvm/IR/Function.h"
  #include "llvm/Support/raw_ostream.h"
  #include "llvm/IR/LegacyPassManager.h"
  #include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Metadata.h"


#include "llvm/Transforms/Instrumentation.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

#include "llvm/ProfileData/InstrProf.h"

#include <typeinfo>
#include <vector>
#include <string>

using namespace llvm;

/*
clang -o0 -Xclang -load -Xclang skeleton/libSkeletonPass.* test2.c 

 UFO_ON=1 UFO_PTR_PROP=1 ./tsan.exe 

*/
namespace {

  static bool shouldInstrumentReadWriteFromAddress(Value *Addr) {
  // Peel off GEPs and BitCasts.
    Addr = Addr->stripInBoundsOffsets();

    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {


    // Check if the global is private gcov data.
      if (GV->getName().startswith("__llvm_gcov") ||
        GV->getName().startswith("__llvm_gcda"))
        return false;
    }

  // Do not instrument acesses from different address spaces; we cannot deal
  // with them.
    if (Addr) {
      Type *PtrTy = cast<PointerType>(Addr->getType()->getScalarType());
      if (PtrTy->getPointerAddressSpace() != 0)
        return false;
    }

    return true;
  }


  struct SkeletonPass : public FunctionPass {
    static char ID;
    SkeletonPass() : FunctionPass(ID) {}

    virtual bool _runOnFunction(Function &F) {

      errs() << "==================== FUN " 
      << F.getName() << "\r\n";

      errs() << "Function body:\n";
      F.dump();

      std::vector<Instruction*> worklist;
      for (auto& B : F) {
        for (auto& I : B) {
          worklist.push_back(&I);
        }
      }

// def-use chain for Instruction
      for(std::vector<Instruction*>::iterator iter = worklist.begin(); iter != worklist.end(); ++iter) {
        Instruction* instr = *iter;
        errs() << "def: " << *instr << "\n"; 
        for(Value::use_iterator i = instr->use_begin(), ie = instr->use_end(); i!=ie; ++i) {
          Value *v = *i;
          Instruction *vi = dyn_cast<Instruction>(v);
          vi->dump();
        }
        errs() << "\r\n";
   // use-def chain for Instruction for(std::vector<Instruction*>::iterator iter = worklist.begin(); iter != worklist.end(); ++iter){

        errs() << "use: " <<*instr << "\n"; 
        for (User::op_iterator i = instr -> op_begin(), e = instr -> op_end(); i != e; ++i) {
          Value *v = *i;
          Instruction *vi = dyn_cast<Instruction>(v);
          v->dump();
        }
        errs() << "\r\n====================\r\n"; 
      }
      return false; 
    }
/*
Function *F = ...;

for (User *U : F->users()) {
  if (Instruction *Inst = dyn_cast<Instruction>(U)) {
    errs() << "F is used in instruction:\n";
    errs() << *Inst << "\n";
  }
*/
    void print_use(Value* val) {
      errs() << " Use:\r\n";
      val->dump();
      for (User* u : val->users()) {
        if (Instruction* ins = dyn_cast<Instruction>(u)) {
          ins->dump();
        }
      }
      errs() << "--- end Use ---\r\n";
    }
/*
Instruction *pi = ...;

for (Use &U : pi->operands()) {
  Value *v = U.get();
  // ...
}
*/
    void print_def(User* inst) {
      errs() << " Def:\r\n";
      inst->dump();
      for (Use &u : inst->operands()) {
        Value* v = u.get();
        v->dump();
      }
      errs() << "--- end Def ---\r\n";
    }

    bool is_ptr(Value* v_l) {
      Type* tp_l = v_l->getType();
      if (tp_l->isPointerTy()) {
        Type *OrigTy = cast<PointerType>(v_l->getType())->getElementType();
        return OrigTy->isPointerTy();
      }
      return false;
    }

    bool ret_ptr(CallInst* ins_call) {
      Type* t = ins_call->getCalledValue()->getType();
      FunctionType* ft = cast<FunctionType>(cast<PointerType>(t)->getElementType());
      Type* fty = ft->getReturnType();
      return fty->isPointerTy() && 14 == fty->getTypeID();
    }

static bool __is_func_ret_ptr(CallInst* ins_call) {
  // errs() << "__is_func_ret_ptr ";
  ins_call->dump();
  Type* t = ins_call->getCalledValue()->getType();
  FunctionType* ft = cast<FunctionType>(cast<PointerType>(t)->getElementType());
  Type* fty = ft->getReturnType();
  bool ret = fty->isPointerTy() && 14 == fty->getTypeID();
  // errs() << " ret " << ret << "\r\n";
  return ret;
}
static bool __is_named_ptr(Value *val) {
  if (val == NULL)
    return false;
  Type* typ = val->getType();
  if (typ->isPointerTy()) {
    Type *OrigTy = cast<PointerType>(typ)->getElementType();
    if (OrigTy->isPointerTy()) {
      return val->hasName();
    }
  }
  return false;
}


bool search_use(Value* val_from, Instruction* inst_target, int depth) {
  if (depth <= 0)
    return false;
  errs() << "\r\n__on_use_chain " << depth;
  inst_target->dump();
  errs() << " check use:\r\n";
  val_from->dump();
  for (User* u : val_from->users()) {
    errs() << "use: ";
    u->dump();
    if (Instruction* ins = dyn_cast<Instruction>(u)) {
      if (ins == inst_target)
        return true;
    }
    bool fu = search_use(u, inst_target, depth - 1);
    if (fu) return true;
  }
  return false;
}

virtual bool runOnFunction(Function& F) {

F.dump();

std::vector<CallInst*> vec_call_inst;
std::vector<LoadInst*> vec_ptrload_inst;
std::vector<StoreInst*> vec_ptrstore_inst;

  for (auto& B : F) 
    for (auto& I : B) {
     if (CallInst* ins_call = dyn_cast<CallInst>(&I)) {
      if(__is_func_ret_ptr(ins_call))
        vec_call_inst.push_back(ins_call);
    }
    else if (LoadInst* ins_ld = dyn_cast<LoadInst>(&I)) {
      Value *val_from = ins_ld->getPointerOperand();
      if (__is_named_ptr(val_from))
        vec_ptrload_inst.push_back(ins_ld);
    }
    if (StoreInst *ins_st = dyn_cast<StoreInst>(&I)) {
      Value *val_left = ins_st->getPointerOperand();
      if (__is_named_ptr(val_left)) 
        vec_ptrstore_inst.push_back(ins_st);
    }
  }

  errs() << "\r\nvec_call_inst " << vec_call_inst.size()
      << "\r\vec_ptrload_inst " << vec_ptrload_inst.size()
      << "\r\vec_ptrstore_inst " << vec_ptrstore_inst.size() << "\r\n";

  for(auto st_ptr : vec_ptrstore_inst) {
    std::vector<Instruction*> vec_found;
    for (auto inst_call : vec_call_inst) {
      if (search_use(inst_call, st_ptr, 8)) {
        vec_found.push_back(inst_call);
      }
    }
    for (auto inst_ld : vec_ptrload_inst) {
      if (search_use(inst_ld, st_ptr, 8)) {
        vec_found.push_back(inst_ld);
      }
    }
    st_ptr->dump();
    if (vec_found.empty())
      errs() << " not found\r\n";
    if (vec_found.size() == 1) {
        errs() << "  from \r\n";
        vec_found[0]->dump();
    } else if (vec_found.size() > 1) errs() << "\r\n!!!!!!!!11 found multi\r\n";
    errs() << "\r\n";
  }
}

    virtual bool __asdas__runOnFunction(Function &F) {

      int status = -1; 
       // const char* name = F.getName().data();
    //    if (name != NULL)
      errs() << "==================== FUN " 
      << F.getName() << "\r\n";
      F.dump();

      for (auto& B : F) 

        for (auto& I : B) {
         if (CallInst* ins_call = dyn_cast<CallInst>(&I)) {
          errs() << " call " << ins_call->getCalledValue()->getName() << "\r\n";
          print_use(ins_call);
          errs() << " ret? " << ret_ptr(ins_call) << "\r\n\r\n";
        }       
        else if (GetElementPtrInst* ins_get = dyn_cast<GetElementPtrInst>(&I)) {
            I.dump();
            Value* v_l = ins_get->getPointerOperand();

            errs() << " GEP inst use:\r\n";
            print_use(&I);
            errs() << " left use:\r\n";
            print_use(v_l);

            errs() << " GEP inst def:\r\n";
            print_def(&I);

            User* _u = dyn_cast<User>(v_l);
            if (NULL != _u) {
              errs() << " left def:\r\n";
              print_def(_u);
            } else {
              errs() << " left not user \r\n";
            }

          errs() << "\r\n\r\n";

        }
        else if (CastInst* ins_cast = dyn_cast<CastInst>(&I)) {
               I.dump();
          errs() << " cast inst use:\r\n";
          print_use(&I);
            errs() << " cast inst def:\r\n";
            print_def(&I);

          errs() << "\r\n";
        }
      }
    }


    virtual bool ______runOnFunction(Function &F) {

      Module &M = *F.getParent();

      IRBuilder<> IRB(M.getContext());

      AttributeSet Attr;
      Attr = Attr.addAttribute(M.getContext(), AttributeSet::FunctionIndex, Attribute::NoUnwind);

      Constant* _const = M.getOrInsertFunction(
        "__print_ptr", Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), nullptr);

      Function * _ins_func = cast<Function>(_const);
      errs() << ">>> _ins_func  " << _ins_func << "\r\n";



      int status = -1; 
       // const char* name = F.getName().data();
    //    if (name != NULL)
      errs() << "==================== FUN " 
      << F.getName() << "\r\n";
         // << abi::__cxa_demangle(name, NULL, NULL, &status) << "!\n";

      errs() << "Function body:\n";
      F.dump();

  // for (auto& B : F) {
  //   for (auto& I : B) {
  //     errs() << I.getOpcodeName() << "  ";
  //     I.dump();
  //   }
  // }
      for (auto& B : F) {

        for (auto& I : B) {
      // errs() << "Instruction: " << I.getOpcodeName() << "  OPD: " << I.getNumOperands();
      // I.dump();

          if (StoreInst *ins_st = dyn_cast<StoreInst>(&I)) {
            I.dump();
            Value* v_l = ins_st->getPointerOperand();

            errs() << " store inst use:\r\n";
            print_use(&I);
            errs() << " left use:\r\n";
            print_use(v_l);

            errs() << " store inst def:\r\n";
            print_def(&I);

            User* _u = dyn_cast<User>(v_l);
            if (NULL != _u) {
              errs() << " left def:\r\n";
              print_def(_u);
            } else {
              errs() << " left not user \r\n";
            }


              // v_l = v_l->stripInBoundsOffsets();
            Type* tp_l = v_l->getType();
            errs() << "L: [";
            if (v_l->hasName()) {
              errs() << v_l->getName();
            } else if (AllocaInst* _ins_alloca = dyn_cast<AllocaInst>(v_l)) {
              errs() << "alloca:" << _ins_alloca->getName() << " arr sz:";
              _ins_alloca->getArraySize()->dump();
            }
            errs() << "] typeid:" <<  tp_l->getTypeID() ;

    //            << "  inst?" << shouldInstrumentReadWriteFromAddress(v_l);
            if (Instruction* _ins = dyn_cast<Instruction>(v_l)) {
              errs() << " ins opname " << _ins->getOpcodeName();
            }
            if (tp_l->isPointerTy()) {
              Type *OrigTy = cast<PointerType>(v_l->getType())->getElementType();
              errs() << "  isPtr? " << OrigTy->isPointerTy() << "\r\n";
               // IRBuilder<> irb_l(&I);
    // irb_l.CreateCall(_ins_func, irb_l.CreatePointerCast(v_l, irb_l.getInt8PtrTy()));
            }

            Value* v_r = ins_st->getValueOperand();
              // v_r = v_r->stripInBoundsOffsets();
            Type* tp_r = v_r->getType();
            errs() << "R: [";
            if (v_r->hasName()) {
              errs() << v_r->getName();
            } else if (AllocaInst* _ins_alloca = dyn_cast<AllocaInst>(v_r)) {
              errs() << "alloca:" << _ins_alloca->getName() << " arr sz:";
              _ins_alloca->getArraySize()->dump();
            }

            errs() << "] typeid:" <<  tp_r->getTypeID();
            if (Instruction* _ins = dyn_cast<Instruction>(v_r)) {
              errs() << " ins opname " << _ins->getOpcodeName();
            }
            if (tp_r->isPointerTy()) {
              Type *OrigTy = cast<PointerType>(v_r->getType())->getElementType();
              errs() << "  isPtr? " << OrigTy->isPointerTy() << "\r\n";
            }

            errs() << "\r\n\r\n";
      } // store ins


      else if (LoadInst* ins_ld = dyn_cast<LoadInst>(&I)) {
        ins_ld->dump();
        if (ins_ld->hasName()) { // nothing
          errs() << " load name: " << ins_ld->getName() << "\r\n";
        }

        Value* val_ld = ins_ld->getPointerOperand();

        errs() << " load inst use:\r\n";
        print_use(ins_ld);
        errs() << " left use:\r\n";
        print_use(val_ld);
        errs() << " load inst def:\r\n";
        print_def(ins_ld);

        User* _u = dyn_cast<User>(val_ld);
        if (NULL != _u) {
          errs() << " left def:\r\n";
          print_def(_u);
        } else {
          errs() << " left not user \r\n";
        }

                      // v_l = v_l->stripInBoundsOffsets();
        Type* tp_l = val_ld->getType();
        errs() << "from: [";
        if (val_ld->hasName()) {
          errs() << val_ld->getName();
        }
        errs() << "] typeid:" <<  tp_l->getTypeID() ;
    //            << "  inst?" << shouldInstrumentReadWriteFromAddress(v_l);
        if (Instruction* _ins = dyn_cast<Instruction>(val_ld)) {
          errs() << " ins opname " << _ins->getOpcodeName();
        }
        if (tp_l->isPointerTy()) {
          Type *OrigTy = cast<PointerType>(val_ld->getType())->getElementType();
          errs() << "  isPtr? " << OrigTy->isPointerTy() << "\r\n";
        }
        errs() << "\r\n\r\n";
      }

      else if (CallInst* ins_call = dyn_cast<CallInst>(&I)) {
        errs() << " call " << ins_call->getCalledValue()->getName() << "\r\n";
        print_use(ins_call);
        errs() << " ret? " << ret_ptr(ins_call) << "\r\n\r\n";
      }

      else if (GetElementPtrInst* ins_get = dyn_cast<GetElementPtrInst>(&I)) {

        Value* val = ins_get->getPointerOperand();
        Type* tp_l = val->getType();
        errs() << "from: [";
        if (val->hasName()) {
          errs() << val->getName();
        }
        errs() << "] typeid:" <<  tp_l->getTypeID() ;

        print_def(ins_get);

        errs() << " getele ins ";
        print_use(&I);
        errs() << " left ";
        print_use(val);
        errs() << "\r\n\r\n";

      }
      else if (CastInst* ins_cast = dyn_cast<CastInst>(&I)) {
        errs() << " cast ";
        Type* tp_l = ins_cast->getSrcTy();
        Type* tp_r = ins_cast->getDestTy();
        tp_l->dump();
        errs() << " -> ";
        tp_r->dump();

        errs() << "  operands: " << ins_cast->getNumOperands() << "   ";
        for (int i = 0; i < ins_cast->getNumOperands(); ++i) {
          Value * opr = ins_cast->getOperand(i);
          errs() << i << ":";
          opr->dump();
          errs() << "\r\n";
        }
        errs() << "\r\n";
      }

    }
  }

  return false;
}
};
}

char SkeletonPass::ID = 0;

  // Automatically enable the pass.
  // http://adriansampson.net/blog/clangpass.html
static void registerSkeletonPass(const PassManagerBuilder &,
 legacy::PassManagerBase &PM) {
  PM.add(new SkeletonPass());
}
static RegisterStandardPasses
RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
 registerSkeletonPass);
