//===-- ThreadSanitizer.cpp - race detector -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer, a race detector.
//
// The tool is under development, for the details about previous versions see
// http://code.google.com/p/data-race-test
//
// The instrumentation phase is quite simple:
//   - Insert calls to run-time library before every memory access.
//      - Optimizations may apply to avoid instrumenting some of the accesses.
//   - Insert calls at function entry/exit.
// The rest is handled by the run-time library.
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/EscapeEnumerator.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <map>
#include <vector>
#include <algorithm>

using namespace llvm;

#define DEBUG_TYPE "tsan"

static cl::opt<bool>  ClInstrumentMemoryAccesses(
    "tsan-instrument-memory-accesses", cl::init(true),
    cl::desc("Instrument memory accesses"), cl::Hidden);
static cl::opt<bool>  ClInstrumentFuncEntryExit(
    "tsan-instrument-func-entry-exit", cl::init(true),
    cl::desc("Instrument function entry and exit"), cl::Hidden);
static cl::opt<bool>  ClHandleCxxExceptions(
    "tsan-handle-cxx-exceptions", cl::init(true),
    cl::desc("Handle C++ exceptions (insert cleanup blocks for unwinding)"),
    cl::Hidden);
static cl::opt<bool>  ClInstrumentAtomics(
    "tsan-instrument-atomics", cl::init(true),
    cl::desc("Instrument atomics"), cl::Hidden);
static cl::opt<bool>  ClInstrumentMemIntrinsics(
    "tsan-instrument-memintrinsics", cl::init(true),
    cl::desc("Instrument memintrinsics (memset/memcpy/memmove)"), cl::Hidden);

STATISTIC(NumInstrumentedReads, "Number of instrumented reads");
STATISTIC(NumInstrumentedWrites, "Number of instrumented writes");
STATISTIC(NumOmittedReadsBeforeWrite,
"Number of reads ignored due to following writes");
STATISTIC(NumAccessesWithBadSize, "Number of accesses with bad size");
STATISTIC(NumInstrumentedVtableWrites, "Number of vtable ptr writes");
STATISTIC(NumInstrumentedVtableReads, "Number of vtable ptr reads");
STATISTIC(NumOmittedReadsFromConstantGlobals,
"Number of reads from constant globals");
STATISTIC(NumOmittedReadsFromVtable, "Number of vtable reads");
STATISTIC(NumOmittedNonCaptured, "Number of accesses ignored due to capturing");

static const char *const kTsanModuleCtorName = "tsan.module_ctor";
static const char *const kTsanInitName = "__tsan_init";

namespace {

struct InstPtrProp {
  Instruction *I;
  Value* src;
  Value* dest;
};

/// ThreadSanitizer: instrument the code in module to find races.
struct ThreadSanitizer : public FunctionPass {
  ThreadSanitizer() : FunctionPass(ID) {}
  StringRef getPassName() const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
  bool doInitialization(Module &M) override;
  static char ID;  // Pass identification, replacement for typeid.

private:
  void initializeCallbacks(Module &M);
  bool instrumentLoadOrStore(Instruction *I, const DataLayout &DL);

//  bool instrumentPtrProp(SmallVectorImpl<Instruction *> &insLsTsan,
//                         SmallVectorImpl<Instruction *> &insLsPtr,
//                         const DataLayout &DL);

  bool instrumentAtomic(Instruction *I, const DataLayout &DL);
  bool instrumentMemIntrinsic(Instruction *I);
  void chooseInstructionsToInstrument(SmallVectorImpl<Instruction *> &Local,
                                      SmallVectorImpl<Instruction *> &All,
                                      const DataLayout &DL);


  void __inst_call2store(CallInst* ins_call, StoreInst* st_ptr);

  void __inst_ptr2ptr(LoadInst *ins_ld, StoreInst *st_ptr);
//  void TracePtrProp(Instruction * I,
//                         SmallVectorImpl<Instruction *> &All,
//                         const DataLayout &DL);

  void __check_ins_store(StoreInst* ins_store);

  bool addrPointsToConstantData(Value *Addr);
  int getMemoryAccessFuncIndex(Value *Addr, const DataLayout &DL);
  void InsertRuntimeIgnores(Function &F);

  Type *IntptrTy;
  IntegerType *OrdTy;
  // Callbacks to run-time library are computed in doInitialization.
  Function *TsanFuncEntry;
  Function *TsanFuncExit;
  Function *TsanIgnoreBegin;
  Function *TsanIgnoreEnd;
  // Accesses sizes are powers of two: 1, 2, 4, 8, 16.
  static const size_t kNumberOfAccessSizes = 5;
  Function *TsanRead[kNumberOfAccessSizes];
  Function *TsanWrite[kNumberOfAccessSizes];
  Function *TsanUnalignedRead[kNumberOfAccessSizes];
  Function *TsanUnalignedWrite[kNumberOfAccessSizes];
  Function *TsanAtomicLoad[kNumberOfAccessSizes];
  Function *TsanAtomicStore[kNumberOfAccessSizes];
  Function *TsanAtomicRMW[AtomicRMWInst::LAST_BINOP + 1][kNumberOfAccessSizes];
  Function *TsanAtomicCAS[kNumberOfAccessSizes];
  Function *TsanAtomicThreadFence;
  Function *TsanAtomicSignalFence;
  Function *TsanVptrUpdate;
  Function *TsanVptrLoad;
  Function *MemmoveFn, *MemcpyFn, *MemsetFn;
  Function *TsanCtorFunction;
  Function *UFOFuncPtrProp;
  Function *UFOFuncPtrDeRef;
};
}  // namespace

char ThreadSanitizer::ID = 0;
INITIALIZE_PASS_BEGIN(
    ThreadSanitizer, "tsan",
"ThreadSanitizer: detects data races.",
false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(
    ThreadSanitizer, "tsan",
"ThreadSanitizer: detects data races.",
false, false)

StringRef ThreadSanitizer::getPassName() const { return "ThreadSanitizer"; }

void ThreadSanitizer::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

FunctionPass *llvm::createThreadSanitizerPass() {
  return new ThreadSanitizer();
}

void ThreadSanitizer::initializeCallbacks(Module &M) {
  IRBuilder<> IRB(M.getContext());
  AttributeSet Attr;
  Attr = Attr.addAttribute(M.getContext(), AttributeSet::FunctionIndex, Attribute::NoUnwind);

  // Initialize the callbacks.

  UFOFuncPtrProp = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      "__ufo_ptr_prop", Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), nullptr));
//  errs() << "\r\n>>>>>>>>>>>> UFOFuncPtrProp " << M.getName() << "  " << UFOFuncPtrProp << "\r\n";

  UFOFuncPtrDeRef = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      "__ufo_ptr_deref", Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), nullptr));


  TsanFuncEntry = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      "__tsan_func_entry", Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), nullptr));

  TsanFuncExit = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("__tsan_func_exit", Attr, IRB.getVoidTy(), nullptr));
  TsanIgnoreBegin = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      "__tsan_ignore_thread_begin", Attr, IRB.getVoidTy(), nullptr));
  TsanIgnoreEnd = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      "__tsan_ignore_thread_end", Attr, IRB.getVoidTy(), nullptr));
  OrdTy = IRB.getInt32Ty();
  for (size_t i = 0; i < kNumberOfAccessSizes; ++i) {
    const unsigned ByteSize = 1U << i;
    const unsigned BitSize = ByteSize * 8;
    std::string ByteSizeStr = utostr(ByteSize);
    std::string BitSizeStr = utostr(BitSize);
    SmallString<32> ReadName("__tsan_read" + ByteSizeStr);
    TsanRead[i] = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
        ReadName, Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), nullptr));

    SmallString<32> WriteName("__tsan_write" + ByteSizeStr);
    TsanWrite[i] = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
        WriteName, Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), nullptr));

    SmallString<64> UnalignedReadName("__tsan_unaligned_read" + ByteSizeStr);
    TsanUnalignedRead[i] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            UnalignedReadName, Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), nullptr));

    SmallString<64> UnalignedWriteName("__tsan_unaligned_write" + ByteSizeStr);
    TsanUnalignedWrite[i] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            UnalignedWriteName, Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), nullptr));

    Type *Ty = Type::getIntNTy(M.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    SmallString<32> AtomicLoadName("__tsan_atomic" + BitSizeStr + "_load");
    TsanAtomicLoad[i] = checkSanitizerInterfaceFunction(
        M.getOrInsertFunction(AtomicLoadName, Attr, Ty, PtrTy, OrdTy, nullptr));

    SmallString<32> AtomicStoreName("__tsan_atomic" + BitSizeStr + "_store");
    TsanAtomicStore[i] = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
        AtomicStoreName, Attr, IRB.getVoidTy(), PtrTy, Ty, OrdTy, nullptr));

    for (int op = AtomicRMWInst::FIRST_BINOP;
         op <= AtomicRMWInst::LAST_BINOP; ++op) {
      TsanAtomicRMW[op][i] = nullptr;
      const char *NamePart = nullptr;
      if (op == AtomicRMWInst::Xchg)
        NamePart = "_exchange";
      else if (op == AtomicRMWInst::Add)
        NamePart = "_fetch_add";
      else if (op == AtomicRMWInst::Sub)
        NamePart = "_fetch_sub";
      else if (op == AtomicRMWInst::And)
        NamePart = "_fetch_and";
      else if (op == AtomicRMWInst::Or)
        NamePart = "_fetch_or";
      else if (op == AtomicRMWInst::Xor)
        NamePart = "_fetch_xor";
      else if (op == AtomicRMWInst::Nand)
        NamePart = "_fetch_nand";
      else
        continue;
      SmallString<32> RMWName("__tsan_atomic" + itostr(BitSize) + NamePart);
      TsanAtomicRMW[op][i] = checkSanitizerInterfaceFunction(
          M.getOrInsertFunction(RMWName, Attr, Ty, PtrTy, Ty, OrdTy, nullptr));
    }

    SmallString<32> AtomicCASName("__tsan_atomic" + BitSizeStr +
                                  "_compare_exchange_val");
    TsanAtomicCAS[i] = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
        AtomicCASName, Attr, Ty, PtrTy, Ty, Ty, OrdTy, OrdTy, nullptr));
  }
  TsanVptrUpdate = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("__tsan_vptr_update", Attr, IRB.getVoidTy(),
                            IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), nullptr));
  TsanVptrLoad = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      "__tsan_vptr_read", Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), nullptr));
  TsanAtomicThreadFence = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      "__tsan_atomic_thread_fence", Attr, IRB.getVoidTy(), OrdTy, nullptr));
  TsanAtomicSignalFence = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      "__tsan_atomic_signal_fence", Attr, IRB.getVoidTy(), OrdTy, nullptr));

  MemmoveFn = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("memmove", Attr, IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                            IRB.getInt8PtrTy(), IntptrTy, nullptr));
  MemcpyFn = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("memcpy", Attr, IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                            IRB.getInt8PtrTy(), IntptrTy, nullptr));
  MemsetFn = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("memset", Attr, IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                            IRB.getInt32Ty(), IntptrTy, nullptr));
}

bool ThreadSanitizer::doInitialization(Module &M) {
  const DataLayout &DL = M.getDataLayout();
  IntptrTy = DL.getIntPtrType(M.getContext());
  std::tie(TsanCtorFunction, std::ignore) = createSanitizerCtorAndInitFunctions(
      M, kTsanModuleCtorName, kTsanInitName, /*InitArgTypes=*/{},
      /*InitArgs=*/{});

  appendToGlobalCtors(M, TsanCtorFunction, 0);

  return true;
}

static bool isVtableAccess(Instruction *I) {
  if (MDNode *Tag = I->getMetadata(LLVMContext::MD_tbaa))
    return Tag->isTBAAVtableAccess();
  return false;
}

// Do not instrument known races/"benign races" that come from compiler
// instrumentatin. The user has no way of suppressing them.
static bool shouldInstrumentReadWriteFromAddress(Value *Addr) {
  // Peel off GEPs and BitCasts.
  Addr = Addr->stripInBoundsOffsets();

  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
    if (GV->hasSection()) {
      StringRef SectionName = GV->getSection();
      // Check if the global is in the PGO counters section.
      if (SectionName.endswith(getInstrProfCountersSectionName(
          /*AddSegment=*/false)))
        return false;
    }

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

bool ThreadSanitizer::addrPointsToConstantData(Value *Addr) {
  // If this is a GEP, just analyze its pointer operand.
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Addr))
    Addr = GEP->getPointerOperand();

  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
    if (GV->isConstant()) {
      // Reads from constant globals can not race with any writes.
      NumOmittedReadsFromConstantGlobals++;
      return true;
    }
  } else if (LoadInst *L = dyn_cast<LoadInst>(Addr)) {
    if (isVtableAccess(L)) {
      // Reads from a vtable pointer can not race with any writes.
      NumOmittedReadsFromVtable++;
      return true;
    }
  }
  return false;
}

// Instrumenting some of the accesses may be proven redundant.
// Currently handled:
//  - read-before-write (within same BB, no calls between)
//  - not captured variables
//
// We do not handle some of the patterns that should not survive
// after the classic compiler optimizations.
// E.g. two reads from the same temp should be eliminated by CSE,
// two writes should be eliminated by DSE, etc.
//
// 'Local' is a vector of insns within the same BB (no calls between).
// 'All' is a vector of insns that will be instrumented.
void ThreadSanitizer::chooseInstructionsToInstrument(
  SmallVectorImpl<Instruction *> &Local, SmallVectorImpl<Instruction *> &All,
  const DataLayout &DL) {
  SmallSet<Value*, 8> WriteTargets;
// Iterate from the end.
  for (Instruction *I : reverse(Local)) {
    if (StoreInst *Store = dyn_cast<StoreInst>(I)) {
      Value *Addr = Store->getPointerOperand();
      if (!shouldInstrumentReadWriteFromAddress(Addr))
        continue;
      WriteTargets.insert(Addr);
    } else {
      LoadInst *Load = cast<LoadInst>(I);
      Value *Addr = Load->getPointerOperand();
      if (!shouldInstrumentReadWriteFromAddress(Addr))
        continue;
      if (WriteTargets.count(Addr)) {
// We will write to this temp, so no reason to analyze the read.
        NumOmittedReadsBeforeWrite++;
        continue;
      }
      if (addrPointsToConstantData(Addr)) {
// Addr points to some constant data -- it can not race with any writes.
        continue;
      }
    }
    Value *Addr = isa<StoreInst>(*I)
    ? cast<StoreInst>(I)->getPointerOperand()
    : cast<LoadInst>(I)->getPointerOperand();
    if (isa<AllocaInst>(GetUnderlyingObject(Addr, DL)) &&
      !PointerMayBeCaptured(Addr, true, true)) {
// The variable is addressable but not captured, so it cannot be
// referenced from a different thread and participate in a data race
// (see llvm/Analysis/CaptureTracking.h for details).
      NumOmittedNonCaptured++;
    continue;
  }
  All.push_back(I);
}
Local.clear();
}

static void __old__check_inst(Instruction& I) {
  errs() << "Instruction: " << I.getOpcodeName() << "  OPD: " << I.getNumOperands();
//  I.dump();
  if (StoreInst *ins_st = dyn_cast<StoreInst>(&I)) {
    // I.dump();

    Value* v_l = ins_st->getPointerOperand();
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
    // I.dump();
    if (ins_ld->hasName()) { // nothing
      errs() << " load name: " << ins_ld->getName() << "\r\n";
    }
    Value* val_ld = ins_ld->getPointerOperand();
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
    // else if (CallInst* ins_call = dyn_cast<CallInst>(&I)) {
    //   errs() << " call " << ins_call->getCalledValue()->getName() << "\r\n";
    // }

  else if (CastInst* ins_cast = dyn_cast<CastInst>(&I)) {
    errs() << " cast ";
    Type* tp_l = ins_cast->getSrcTy();
    Type* tp_r = ins_cast->getDestTy();
    tp_l->dump();
    errs() << " -> ";
    tp_r->dump();
    ins_cast->dump();
    errs() << "\r\n";
  }
}

/*
 load
 store
 getin
 bitcast
 * */
//void ThreadSanitizer::TracePtrProp(
//            Instruction* I,
//            std::vector<InstPtrProp>& seqTransfer,
//            const DataLayout &DL) {
//
//    if (StoreInst * Store = dyn_cast<StoreInst>(I)) {
//      Value *val_left = Store->getPointerOperand();
//      Type* tp_left = val_left->getType();
//      if (tp_left->isPointerTy()) {
//        Type *OrigTy = cast<PointerType>(tp_left)->getElementType();
//        if (OrigTy->isPointerTy()) {
//          All.push_back(I);
//        }
//      }
//    } else if (LoadInst * inst_load = dyn_cast<LoadInst>(I)) {
//
//    } else if (CastInst* ins_cast = dyn_cast<CastInst>(I)) {
//
//    } else if (GetElementPtrInst* inst_arith = dyn_cast<GetElementPtrInst>(I)) {
//
//    }
//}

//bool ThreadSanitizer::InstrumentPtrProp(
//        SmallVectorImpl<Instruction *> &insLsTsan,
//        std::vector<std::pair<Value*, Value*> >& seqTransfer,
//        const DataLayout &DL) {
//
//  bool changed = false;
//  for (auto Inst : insLsPtr) {
////    Res |= instrumentLoadOrStore(Inst, DL);
//    IRBuilder<> irb_l(Inst);
//
//    Value *v_l = cast<StoreInst>(Inst)->getPointerOperand();
//
////    UFOFuncPtrProp = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
////        "__ufo_ptr_prop", Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), nullptr));
//    Value* addr_src = irb_l.CreatePointerCast(v_l, irb_l.getInt8PtrTy());
//    SmallVector<Value *, 8> args;
//    args.push_back(addr_src);
//    args.push_back(v_l);
//    irb_l.CreateCall(UFOFuncPtrProp, args);
//    changed |= true;
//  }
//  return changed;
//}

static bool isAtomic(Instruction *I) {
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return LI->isAtomic() && LI->getSynchScope() == CrossThread;
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->isAtomic() && SI->getSynchScope() == CrossThread;
  if (isa<AtomicRMWInst>(I))
    return true;
  if (isa<AtomicCmpXchgInst>(I))
    return true;
  if (isa<FenceInst>(I))
    return true;
  return false;
}

void ThreadSanitizer::InsertRuntimeIgnores(Function &F) {
  IRBuilder<> IRB(F.getEntryBlock().getFirstNonPHI());
  IRB.CreateCall(TsanIgnoreBegin);
  EscapeEnumerator EE(F, "tsan_ignore_cleanup", ClHandleCxxExceptions);
  while (IRBuilder<> *AtExit = EE.Next()) {
    AtExit->CreateCall(TsanIgnoreEnd);
  }
}

static void _print_use(Value* val) {
  errs() << "\r\nUse: ";
  val->dump();
  for (User* u : val->users()) {
    if (Instruction* ins = dyn_cast<Instruction>(u)) {
      ins->dump();
    }
  }
  errs() << "--- end use ---\r\n";
}
static void _print_def(User* inst) {
  errs() << "\r\nDef (num:" << inst->getNumOperands() << "): ";
  inst->dump();
  for (Use &u : inst->operands()) {
    Value* v = u.get();
    v->dump();
  }
  errs() << "--- end def ---\r\n";
}

static bool __is_func_ret_ptr(CallInst* ins_call) {
//  errs() << "__is_func_ret_ptr ";
//  ins_call->dump();
  Type* t = ins_call->getCalledValue()->getType();
  FunctionType* ft = cast<FunctionType>(cast<PointerType>(t)->getElementType());
  Type* fty = ft->getReturnType();
  bool ret = fty->isPointerTy();// && 14 == fty->getTypeID();
//  errs() << " ret " << ret << "\r\n";
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
static void __trace_src(Value *val) {
  val->dump();
  if (StoreInst* ins_store = dyn_cast<StoreInst>(val)) {
//    errs() << " ins_store <-- \r\n";
    Value* src_val = ins_store->getValueOperand();
    __trace_src(src_val);

  } else if (LoadInst* ins_load = dyn_cast<LoadInst>(val)) {
//    errs() << " ins_load <-- \r\n";
    Value* src_val = ins_load->getPointerOperand();
    __trace_src(src_val);

  } else if (GetElementPtrInst* ins_gep = dyn_cast<GetElementPtrInst>(val)) {
//    errs() << " ins_gep <-- \r\n";
    Value* src_val = ins_gep->getPointerOperand();
    __trace_src(src_val);

  } else if (CastInst* ins_cast = dyn_cast<CastInst>(val)) {
    if (ins_cast->getNumOperands() > 0) {
//      errs() << " ins_cast <-- \r\n";
      Value* src_val = ins_cast->getOperand(0);
      __trace_src(src_val);

    } else errs() << " cast no operands\r\n";

  } else if (AllocaInst* ins_alloc = dyn_cast<AllocaInst>(val)) {
//    errs() << " !!!alloca:";
    ins_alloc->dump();
    errs() << "\r\n";

  } else if (CallInst* ins_call = dyn_cast<CallInst>(val)) {
    errs() << " !!!call " << (__is_func_ret_ptr(ins_call) ? " ret ptr: " : " not ptr: ");
    ins_call->dump();
    errs() << "\r\n";

  } else if (Constant* val_const = dyn_cast<Constant>(val)) {
//    errs() << " !!!constant:";
    val_const->dump();
    errs() << "\r\n";
  }
}

static Value * __get_src(User *inst) {
  inst->dump();
  errs() << " ==> __get_src ";
  if (StoreInst* ins_store = dyn_cast<StoreInst>(inst)) {
//    errs() << " ins_store <-- \r\n";
    return ins_store->getValueOperand();
//    return ins_store;
  } else if (LoadInst* ins_load = dyn_cast<LoadInst>(inst)) {
//    errs() << " ins_load <-- \r\n";
    return ins_load->getPointerOperand();

  } else if (GetElementPtrInst* ins_gep = dyn_cast<GetElementPtrInst>(inst)) {
//    errs() << " ins_gep <-- \r\n";
    return ins_gep->getPointerOperand();

  } else if (CastInst* ins_cast = dyn_cast<CastInst>(inst)) {
    if (ins_cast->getNumOperands() > 0) {
//      errs() << " ins_cast <-- \r\n";
      return ins_cast->getOperand(0);

    } else return NULL;//errs() << " cast no operands\r\n";

  } else if (AllocaInst* ins_alloc = dyn_cast<AllocaInst>(inst)) {
    errs() << " !!!alloca:";
    ins_alloc->dump();
    errs() << "\r\n";
    return inst;

  } else if (CallInst* ins_call = dyn_cast<CallInst>(inst)) {
    errs() << " !!!call " << (__is_func_ret_ptr(ins_call) ? " ret ptr: " : " not ptr: ");
    ins_call->dump();
    errs() << "\r\n";
    return inst;

  } else if (Constant* val_const = dyn_cast<Constant>(inst)) {
    errs() << " !!!constant:";
    val_const->dump();
    errs() << "\r\n";
    return inst;
  }
  return inst;
}

static Value* __get_last_def(User* I, Value* cur_val) {
  I->dump();
  errs() << " ===> __get_last_def (num:" << I->getNumOperands() << "): ";
//  _print_def((Instruction*)I);
  Value* last_val = NULL;
  for (Use &u : I->operands()) {
    if (last_val == NULL) {
      last_val = u.get();
    }
    Value* i_val = u.get();
    if (i_val == cur_val)
      return last_val;
//    Value* v = u.get();
//    if (v != NULL)
//      return v;
  }
  return I;
}
//      if (v2 == NULL) {
//        return v;
//      } else {
//        v2->dump();
//        if (__is_named_ptr(v2)) {
//          return v2;
//        } else if (Instruction * user = dyn_cast<Instruction>(v)) {
//          return __trace_back(user);
//        } else return v2;
//      }
static Value *__trace_back(Value *I) {
  I->dump();
  errs() << "  ==> trace back: ";
  User *user = dyn_cast<User>(I);
  if (NULL != user) {
    Value *val_src = __get_src(user);
    if (val_src == user)
      return val_src;
//      errs() << "\r\n  >>> line 726\r\n";
    User *tmp_user = NULL;
    if (NULL != (tmp_user = dyn_cast<User>(val_src))) {
      val_src = __get_last_def(tmp_user, user);
      if (val_src == user || __is_named_ptr(val_src))
        return val_src;
      else {
        return __trace_back(val_src);
      }
    }
  }
  return NULL;
}

static void _check_tsan_inst(Instruction *I) {
  bool IsWrite = isa<StoreInst>(*I);
  Value *Addr = IsWrite
                ? cast<StoreInst>(I)->getPointerOperand()
                : cast<LoadInst>(I)->getPointerOperand();
  I->dump();
  errs() << ">>> tsan: " << (IsWrite ? "W " : "R " );
  if (Addr->hasName()) {
    errs() << "[" << Addr->getName() << "] ";
  }
  Type* typ = Addr->getType();
  if (typ->isPointerTy()) {
    Type *OrigTy = cast<PointerType>(typ)->getElementType();
    errs() << "  isPtr? " << OrigTy->isPointerTy() << "\r\n";
  }

  Value* v = NULL;
  if (IsWrite) {
    v = __get_last_def(I, I);
  } else {
    // where LD from
    v = __get_last_def((User*)Addr, I);
  }
  if (v != NULL) {
    v->dump();
  }
//errs() << "\r\n  >>> line 719 IsWrite?" << IsWrite << " v:" << v << "\r\n";
  if (v != NULL) {
    Value* src = __trace_back(v);
    errs() << " top: back: ";
    if (src != NULL)
      src->dump();
    else errs() << " null\r\n";
  }
//  Value* src = __trace_back(I);
//  __trace_src(I);
//  _print_def(I);
//  _print_use(I);
  errs() << "\r\n-----------\r\n";
}

static bool __is_stack(Value* val) {
  return false;
//  static void _print_def(User* inst) {
//    errs() << "\r\nDef (num:" << inst->getNumOperands() << "): ";
//    inst->dump();
//    for (Use &u : inst->operands()) {
//      Value* v = u.get();
//      v->dump();
//    }
//    errs() << "--- end def ---\r\n";
//  }
}
void ThreadSanitizer::__check_ins_store(StoreInst* ins_store) {
  Value *val_left = ins_store->getPointerOperand();
  if (!__is_named_ptr(val_left))
    return;
  {
//    User *left_user = dyn_cast<User>(val_left);
//    val_left->dump();
//    if (NULL != left_user) {
//      _print_def(left_user);
//    } else errs() << " not inst left val \r\n";
  }
  IRBuilder<ConstantFolder, IRBuilderAfterInserter<true>> irb_after_st(ins_store);
  LoadInst *ld_ptr = irb_after_st.CreateLoad(val_left);

  IRBuilder<ConstantFolder, IRBuilderAfterInserter<true>> irb_after_ld(ld_ptr);
  Value *addr_src = irb_after_ld.CreatePointerCast(val_left, irb_after_ld.getInt8PtrTy());

  SmallVector < Value * , 8 > args;
  args.push_back(addr_src);
  args.push_back(ld_ptr);

  irb_after_ld.CreateCall(UFOFuncPtrProp, args);
  errs() << " instr:";
  ins_store->dump();

//  bool changed = false;
//  for (auto Inst : insLsPtr) {
////    Res |= instrumentLoadOrStore(Inst, DL);
//    IRBuilder<> irb_l(Inst);
//
//    Value *v_l = cast<StoreInst>(Inst)->getPointerOperand();
//
////    UFOFuncPtrProp = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
////        "__ufo_ptr_prop", Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), nullptr));

//    Value* addr_src = irb_l.CreatePointerCast(v_l, irb_l.getInt8PtrTy());
//    SmallVector<Value *, 8> args;
//    args.push_back(addr_src);
//    args.push_back(v_l);
//    irb_l.CreateCall(UFOFuncPtrProp, args);
//    changed |= true;
//  }
//  return changed;

}

static bool __on_use_chain(Value* val_from, int par_order,
                           Instruction* inst_target, int target_order,
                           int depth,
                           DenseMap<Value*, unsigned>& inst_order,
                           SmallPtrSet<Value*, 32>& set_reversed_vals,
                           SmallPtrSet<Value*, 32>& set_not_matched) {

  int _val_order = 2<<28;
  if (inst_order.count(val_from) != 0) {
    _val_order = inst_order[val_from];
  }
  if (depth <= 0 || target_order < _val_order) {
    set_not_matched.insert(val_from);
    return false;
  }
//  errs() << "\r\n==========\r\n";
//  inst_target->dump();
//  errs() << "  __on_use_chain (" << depth << ")?  :";
//  val_from->dump();
  if (set_reversed_vals.count(val_from) == 0) {
    set_reversed_vals.insert(val_from);
    val_from->reverseUseList();
  }
  for (User* u : val_from->users()) {
    if (set_not_matched.count(val_from) > 0)
      continue;
    if (Instruction* ins = dyn_cast<Instruction>(u)) {
      _val_order = 2<<28;
      if (inst_order.count(ins) != 0) {
        _val_order = inst_order[ins];
      }
      if (target_order < _val_order || _val_order <= par_order)
        continue;
//      errs() << "use: ";
//      ins->dump();
      if (ins == inst_target)
        return true;
    }
    bool fu = __on_use_chain(u, _val_order,
                             inst_target, target_order,
                             depth - 1,
                             inst_order, set_reversed_vals, set_not_matched);
    if (fu) return true;
  }

  if (StoreInst * ins_st = dyn_cast<StoreInst>(val_from)) {
    Value *val_left = ins_st->getPointerOperand();
    if ((!__is_named_ptr(val_left))
        && (set_not_matched.count(val_left) == 0)) {
      if (depth > 8)
        depth = 8;
      bool fu = __on_use_chain(val_left, _val_order,
                               inst_target, target_order,
                               depth - 1,
                               inst_order, set_reversed_vals, set_not_matched);
      if (fu) return true;
    }
  }
  set_not_matched.insert(val_from);
  return false;
}
void ThreadSanitizer::__inst_call2store(CallInst* ins_call, StoreInst* st_ptr) {
  Value *val_dest = st_ptr->getPointerOperand();

  IRBuilder<> irb(st_ptr);
  Value* call_pc = irb.CreatePointerCast(ins_call, irb.getInt8PtrTy());

  SmallVector < Value * , 4 > args;
  args.push_back(call_pc);
  args.push_back(val_dest);
  irb.CreateCall(UFOFuncPtrProp, args);
}

void ThreadSanitizer::__inst_ptr2ptr(LoadInst *ins_ld, StoreInst *st_ptr) {
  // src ==> dest
  Value *val_src = ins_ld->getPointerOperand();
  Value *val_dest = st_ptr->getPointerOperand();

  IRBuilder<> irb(st_ptr);
  SmallVector < Value * , 4> args;
  args.push_back(val_src);
  args.push_back(val_dest);
  irb.CreateCall(UFOFuncPtrProp, args);
}

bool ThreadSanitizer::runOnFunction(Function &F) {
  // This is required to prevent instrumenting call to __tsan_init from within
  // the module constructor.
  if (&F == TsanCtorFunction)
    return false;
//  F.dump();
  initializeCallbacks(*F.getParent());
  SmallVector<Instruction*, 8> AllLoadsAndStores;
  SmallVector<Instruction*, 8> LocalLoadsAndStores;
  SmallVector<Instruction*, 8> AtomicAccesses;
  SmallVector<Instruction*, 8> MemIntrinCalls;

  bool Res = false;
  bool HasCalls = false;
  bool SanitizeFunction = F.hasFnAttribute(Attribute::SanitizeThread);
  const DataLayout &DL = F.getParent()->getDataLayout();
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  // Traverse all instructions, collect loads/stores/returns, check for calls.
  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if (isAtomic(&Inst))
        AtomicAccesses.push_back(&Inst);
      else if (isa<LoadInst>(Inst) || isa<StoreInst>(Inst))
        LocalLoadsAndStores.push_back(&Inst);
      else if (isa<CallInst>(Inst) || isa<InvokeInst>(Inst)) {
        if (CallInst *CI = dyn_cast<CallInst>(&Inst))
          maybeMarkSanitizerLibraryCallNoBuiltin(CI, TLI);
        if (isa<MemIntrinsic>(Inst))
          MemIntrinCalls.push_back(&Inst);
        HasCalls = true;
        chooseInstructionsToInstrument(LocalLoadsAndStores, AllLoadsAndStores,
                                       DL);
      }

    } // for each Inst

    chooseInstructionsToInstrument(LocalLoadsAndStores, AllLoadsAndStores, DL);
  }// for each BB

  SmallVector<CallInst*, 32> vec_call_inst;
  SmallVector<LoadInst*, 32> vec_ptrload_inst;
  SmallVector<StoreInst*, 32> vec_ptrstore_inst;
  DenseMap<Value*, unsigned> inst_order;
  unsigned _inst_count = 0;
  for (auto& B : F)
    for (auto& I : B) {
      inst_order[&I] = _inst_count++;
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
//        _print_use(val_left);
//        val_left->reverseUseList();
//        _print_use(val_left);
        if (__is_named_ptr(val_left))
          vec_ptrstore_inst.push_back(ins_st);
      }
    }
//  errs() << "\r\nvec_call_inst " << vec_call_inst.size()
//  << "\r\nvec_ptrload_inst " << vec_ptrload_inst.size()
//  << "\r\nvec_ptrstore_inst " << vec_ptrstore_inst.size();
//  F.dump();
  int count_inscall = 0;
  int count_insptr = 0;
  SmallPtrSet<Value*, 32> set_reversed_vals;
  SmallPtrSet<Value*, 32> set_not_match;
  SmallVector< Instruction *, 32> vec_found;
  DenseMap<int, Instruction*> match_inst;

  for (auto st_ptr : vec_ptrstore_inst) {
    int target_order = inst_order[st_ptr];
    vec_found.clear();
    set_not_match.clear();
    for (auto *inst_call : vec_call_inst) {
      if (__on_use_chain(inst_call, 0, st_ptr, target_order, 20, inst_order, set_reversed_vals, set_not_match))
        vec_found.push_back(inst_call);
    }
    for (auto *inst_ld : vec_ptrload_inst) {
      if (__on_use_chain(inst_ld, 0, st_ptr, target_order, 20, inst_order, set_reversed_vals, set_not_match))
        vec_found.push_back(inst_ld);
    }
//    for(auto iter = vec_call_inst.begin(); iter != vec_call_inst.end(); ) {
//      auto inst_call = *iter;
//      if (__on_use_chain(inst_call, st_ptr, target_order, 30, inst_order)) {
//        vec_found.push_back(inst_call);
//        iter = vec_call_inst.erase(iter);
//      } else ++iter;
//    }
//
//    for(auto iter = vec_ptrload_inst.begin(); iter != vec_ptrload_inst.end(); ) {
//      auto inst_ld = *iter;
//      if (__on_use_chain(inst_ld, st_ptr, target_order, 30, inst_order)) {
//        vec_found.push_back(inst_ld);
//        vec_ptrload_inst.erase(iter);
//      } else ++iter;
//    }
//    st_ptr->dump();
    if (vec_found.empty()) {
//      errs() << " not found\r\n";
    } if (vec_found.size() == 1) {
//      errs() << "  from \r\n";
      Instruction* inst_from = vec_found[0];
//      inst_from->dump();
      if (CallInst* ins_call = dyn_cast<CallInst>(inst_from)) {
        this->__inst_call2store(ins_call, st_ptr);
        vec_call_inst.erase(std::find(vec_call_inst.begin(), vec_call_inst.end(), ins_call));
        count_inscall++;
      }
      else if (LoadInst* ins_ld = dyn_cast<LoadInst>(inst_from)) {
        this->__inst_ptr2ptr(ins_ld, st_ptr);
        vec_ptrload_inst.erase(std::find(vec_ptrload_inst.begin(), vec_ptrload_inst.end(), ins_ld));
        count_insptr++;
      }
    } else if (vec_found.size() > 1) {
      int target_order = inst_order[st_ptr];
//      errs() << "\r\n!!!!!!!!11 found multi\r\n";
//      st_ptr->dump();
      std::map<int, Instruction*> match_inst;
      match_inst.clear();
      for (size_t i = 0; i < vec_found.size(); ++i) {
//        vec_found[i]->dump();
        int src_order = inst_order[vec_found[i]];
        int dist = target_order - src_order;
        if (dist >= 0)
          match_inst[dist] = vec_found[i];
      }
//      errs() << "  close inst: ";
      Instruction* _inst = match_inst.begin()->second;
      if (auto* inst_call = dyn_cast<CallInst>(_inst)) {
        this->__inst_call2store(inst_call, st_ptr);
        vec_call_inst.erase(std::find(vec_call_inst.begin(), vec_call_inst.end(), inst_call));
        count_inscall++;
      }
      else if (auto* inst_ld = dyn_cast<LoadInst>(_inst)) {
        this->__inst_ptr2ptr(inst_ld, st_ptr);
        vec_ptrload_inst.erase(std::find(vec_ptrload_inst.begin(), vec_ptrload_inst.end(), inst_ld));
        count_insptr++;
      }
    }
  } // for ptr store
  for (auto* val : set_reversed_vals) {
    val->reverseUseList();
  }
  errs() << "\r\nUFO inst call: " << count_inscall
  << "\r\nUFO inst ptr prop: " << count_insptr << "\r\n";
//  F.dump();
  // We have collected all loads and stores.
  // FIXME: many of these accesses do not need to be checked for races
  // (e.g. variables that do not escape, etc).

//  if (ClInstrumentMemoryAccesses && SanitizeFunction)
//    for (auto Inst : AllLoadsAndStores) {
//      _check_tsan_inst(Inst);
//    }

  // Instrument memory accesses only if we want to report bugs in the function.
  if (ClInstrumentMemoryAccesses && SanitizeFunction)
    for (auto Inst : AllLoadsAndStores) {
      Res |= instrumentLoadOrStore(Inst, DL);
    }

//  if (Res) {
//    instrumentPtrProp(AllLoadsAndStores, seqTransfer, DL);
////    errs() << "\r\n>>>> Instrumented " << lsPtrPropInst.size() << "   tsan " << AllLoadsAndStores.size() << "\r\n";
//  }

  // Instrument atomic memory accesses in any case (they can be used to
  // implement synchronization).
  if (ClInstrumentAtomics)
    for (auto Inst : AtomicAccesses) {
      Res |= instrumentAtomic(Inst, DL);
    }

  if (ClInstrumentMemIntrinsics && SanitizeFunction)
    for (auto Inst : MemIntrinCalls) {
      Res |= instrumentMemIntrinsic(Inst);
    }

  if (F.hasFnAttribute("sanitize_thread_no_checking_at_run_time")) {
    assert(!F.hasFnAttribute(Attribute::SanitizeThread));
    if (HasCalls)
      InsertRuntimeIgnores(F);
  }

//   Instrument function entry/exit points if there were instrumented accesses.
  if ((Res || HasCalls) && ClInstrumentFuncEntryExit) {
    IRBuilder<> IRB(F.getEntryBlock().getFirstNonPHI());
    Value *ReturnAddress = IRB.CreateCall(
        Intrinsic::getDeclaration(F.getParent(), Intrinsic::returnaddress),
        IRB.getInt32(0));
    IRB.CreateCall(TsanFuncEntry, ReturnAddress);

    EscapeEnumerator EE(F, "tsan_cleanup", ClHandleCxxExceptions);
    while (IRBuilder<> *AtExit = EE.Next()) {
      AtExit->CreateCall(TsanFuncExit, {});
    }
    Res = true;
  }

//  F.dump();
  return Res;
}


bool ThreadSanitizer::instrumentLoadOrStore(Instruction *I,
                                            const DataLayout &DL) {
  IRBuilder<> IRB(I);
  bool IsWrite = isa<StoreInst>(*I);
  Value *Addr = IsWrite
                ? cast<StoreInst>(I)->getPointerOperand()
                : cast<LoadInst>(I)->getPointerOperand();

  int Idx = getMemoryAccessFuncIndex(Addr, DL);
  if (Idx < 0)
    return false;
  if (IsWrite && isVtableAccess(I)) {
    DEBUG(dbgs() << "  VPTR : " << *I << "\n");
    Value *StoredValue = cast<StoreInst>(I)->getValueOperand();
    // StoredValue may be a vector type if we are storing several vptrs at once.
    // In this case, just take the first element of the vector since this is
    // enough to find vptr races.
    if (isa<VectorType>(StoredValue->getType()))
      StoredValue = IRB.CreateExtractElement(
          StoredValue, ConstantInt::get(IRB.getInt32Ty(), 0));
    if (StoredValue->getType()->isIntegerTy())
      StoredValue = IRB.CreateIntToPtr(StoredValue, IRB.getInt8PtrTy());
    // Call TsanVptrUpdate.
    IRB.CreateCall(TsanVptrUpdate,
                   {IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()),
                    IRB.CreatePointerCast(StoredValue, IRB.getInt8PtrTy())});
    NumInstrumentedVtableWrites++;
    return true;
  }
  if (!IsWrite && isVtableAccess(I)) {
    IRB.CreateCall(TsanVptrLoad,
                   IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()));
    NumInstrumentedVtableReads++;
    return true;
  }
  const unsigned Alignment = IsWrite
                             ? cast<StoreInst>(I)->getAlignment()
                             : cast<LoadInst>(I)->getAlignment();
  Type *OrigTy = cast<PointerType>(Addr->getType())->getElementType();
  const uint32_t TypeSize = DL.getTypeStoreSizeInBits(OrigTy);
  Value *OnAccessFunc = nullptr;
  if (Alignment == 0 || Alignment >= 8 || (Alignment % (TypeSize / 8)) == 0)
    OnAccessFunc = IsWrite ? TsanWrite[Idx] : TsanRead[Idx];
  else
    OnAccessFunc = IsWrite ? TsanUnalignedWrite[Idx] : TsanUnalignedRead[Idx];

  CallInst* tsan_call;
  if (IsWrite) {
      IRBuilder<ConstantFolder, IRBuilderAfterInserter<true>> insertAfter(I);
      tsan_call = insertAfter.CreateCall(OnAccessFunc, IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()));
  } else {
    tsan_call = IRB.CreateCall(OnAccessFunc, IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()));
  }

  if (__is_named_ptr(Addr)) {
    IRBuilder <ConstantFolder, IRBuilderAfterInserter<true>> irb_after_tsan(tsan_call);
    LoadInst *ld_ptr = irb_after_tsan.CreateLoad(Addr);
    IRBuilder <ConstantFolder, IRBuilderAfterInserter<true>> irb_after_ld(ld_ptr);
    irb_after_ld.CreateCall(UFOFuncPtrDeRef, ld_ptr);

//    LoadInst *ld_ptr = irb.CreateLoad(inst_call);
//    IRBuilder <ConstantFolder, IRBuilderAfterInserter<true>> irb_after_ld(ld_ptr);
//    irb_after_ld.CreateCall(UFOFuncPtrDeRef, ld_ptr);

//    IRBuilder <ConstantFolder, IRBuilderAfterInserter<true>> irb(inst_call);
//    irb.CreateCall(UFOFuncPtrDeRef, IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()));

//    errs() << ">>>TSAN inst ptr de ref:";
//    I->dump();
  }

  if (IsWrite) NumInstrumentedWrites++;
  else         NumInstrumentedReads++;
  return true;
}

static ConstantInt *createOrdering(IRBuilder<> *IRB, AtomicOrdering ord) {
  uint32_t v = 0;
  switch (ord) {
    case AtomicOrdering::NotAtomic:
      llvm_unreachable("unexpected atomic ordering!");
    case AtomicOrdering::Unordered:              LLVM_FALLTHROUGH;
    case AtomicOrdering::Monotonic:              v = 0; break;
      // Not specified yet:
      // case AtomicOrdering::Consume:                v = 1; break;
    case AtomicOrdering::Acquire:                v = 2; break;
    case AtomicOrdering::Release:                v = 3; break;
    case AtomicOrdering::AcquireRelease:         v = 4; break;
    case AtomicOrdering::SequentiallyConsistent: v = 5; break;
  }
  return IRB->getInt32(v);
}

// If a memset intrinsic gets inlined by the code gen, we will miss races on it.
// So, we either need to ensure the intrinsic is not inlined, or instrument it.
// We do not instrument memset/memmove/memcpy intrinsics (too complicated),
// instead we simply replace them with regular function calls, which are then
// intercepted by the run-time.
// Since tsan is running after everyone else, the calls should not be
// replaced back with intrinsics. If that becomes wrong at some point,
// we will need to call e.g. __tsan_memset to avoid the intrinsics.
bool ThreadSanitizer::instrumentMemIntrinsic(Instruction *I) {
  IRBuilder<> IRB(I);
  if (MemSetInst *M = dyn_cast<MemSetInst>(I)) {
    IRB.CreateCall(
        MemsetFn,
        {IRB.CreatePointerCast(M->getArgOperand(0), IRB.getInt8PtrTy()),
         IRB.CreateIntCast(M->getArgOperand(1), IRB.getInt32Ty(), false),
         IRB.CreateIntCast(M->getArgOperand(2), IntptrTy, false)});
    I->eraseFromParent();
  } else if (MemTransferInst *M = dyn_cast<MemTransferInst>(I)) {
    IRB.CreateCall(
        isa<MemCpyInst>(M) ? MemcpyFn : MemmoveFn,
        {IRB.CreatePointerCast(M->getArgOperand(0), IRB.getInt8PtrTy()),
         IRB.CreatePointerCast(M->getArgOperand(1), IRB.getInt8PtrTy()),
         IRB.CreateIntCast(M->getArgOperand(2), IntptrTy, false)});
    I->eraseFromParent();
  }
  return false;
}

// Both llvm and ThreadSanitizer atomic operations are based on C++11/C1x
// standards.  For background see C++11 standard.  A slightly older, publicly
// available draft of the standard (not entirely up-to-date, but close enough
// for casual browsing) is available here:
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2011/n3242.pdf
// The following page contains more background information:
// http://www.hpl.hp.com/personal/Hans_Boehm/c++mm/

bool ThreadSanitizer::instrumentAtomic(Instruction *I, const DataLayout &DL) {
  IRBuilder<> IRB(I);
  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    Value *Addr = LI->getPointerOperand();
    int Idx = getMemoryAccessFuncIndex(Addr, DL);
    if (Idx < 0)
      return false;
    const unsigned ByteSize = 1U << Idx;
    const unsigned BitSize = ByteSize * 8;
    Type *Ty = Type::getIntNTy(IRB.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    Value *Args[] = {IRB.CreatePointerCast(Addr, PtrTy),
                     createOrdering(&IRB, LI->getOrdering())};
    Type *OrigTy = cast<PointerType>(Addr->getType())->getElementType();
    Value *C = IRB.CreateCall(TsanAtomicLoad[Idx], Args);
    Value *Cast = IRB.CreateBitOrPointerCast(C, OrigTy);
    I->replaceAllUsesWith(Cast);
  } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    Value *Addr = SI->getPointerOperand();
    int Idx = getMemoryAccessFuncIndex(Addr, DL);
    if (Idx < 0)
      return false;
    const unsigned ByteSize = 1U << Idx;
    const unsigned BitSize = ByteSize * 8;
    Type *Ty = Type::getIntNTy(IRB.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    Value *Args[] = {IRB.CreatePointerCast(Addr, PtrTy),
                     IRB.CreateBitOrPointerCast(SI->getValueOperand(), Ty),
                     createOrdering(&IRB, SI->getOrdering())};
    CallInst *C = CallInst::Create(TsanAtomicStore[Idx], Args);
    ReplaceInstWithInst(I, C);
  } else if (AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(I)) {
    Value *Addr = RMWI->getPointerOperand();
    int Idx = getMemoryAccessFuncIndex(Addr, DL);
    if (Idx < 0)
      return false;
    Function *F = TsanAtomicRMW[RMWI->getOperation()][Idx];
    if (!F)
      return false;
    const unsigned ByteSize = 1U << Idx;
    const unsigned BitSize = ByteSize * 8;
    Type *Ty = Type::getIntNTy(IRB.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    Value *Args[] = {IRB.CreatePointerCast(Addr, PtrTy),
                     IRB.CreateIntCast(RMWI->getValOperand(), Ty, false),
                     createOrdering(&IRB, RMWI->getOrdering())};
    CallInst *C = CallInst::Create(F, Args);
    ReplaceInstWithInst(I, C);
  } else if (AtomicCmpXchgInst *CASI = dyn_cast<AtomicCmpXchgInst>(I)) {
    Value *Addr = CASI->getPointerOperand();
    int Idx = getMemoryAccessFuncIndex(Addr, DL);
    if (Idx < 0)
      return false;
    const unsigned ByteSize = 1U << Idx;
    const unsigned BitSize = ByteSize * 8;
    Type *Ty = Type::getIntNTy(IRB.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    Value *CmpOperand =
        IRB.CreateBitOrPointerCast(CASI->getCompareOperand(), Ty);
    Value *NewOperand =
        IRB.CreateBitOrPointerCast(CASI->getNewValOperand(), Ty);
    Value *Args[] = {IRB.CreatePointerCast(Addr, PtrTy),
                     CmpOperand,
                     NewOperand,
                     createOrdering(&IRB, CASI->getSuccessOrdering()),
                     createOrdering(&IRB, CASI->getFailureOrdering())};
    CallInst *C = IRB.CreateCall(TsanAtomicCAS[Idx], Args);
    Value *Success = IRB.CreateICmpEQ(C, CmpOperand);
    Value *OldVal = C;
    Type *OrigOldValTy = CASI->getNewValOperand()->getType();
    if (Ty != OrigOldValTy) {
      // The value is a pointer, so we need to cast the return value.
      OldVal = IRB.CreateIntToPtr(C, OrigOldValTy);
    }

    Value *Res =
        IRB.CreateInsertValue(UndefValue::get(CASI->getType()), OldVal, 0);
    Res = IRB.CreateInsertValue(Res, Success, 1);

    I->replaceAllUsesWith(Res);
    I->eraseFromParent();
  } else if (FenceInst *FI = dyn_cast<FenceInst>(I)) {
    Value *Args[] = {createOrdering(&IRB, FI->getOrdering())};
    Function *F = FI->getSynchScope() == SingleThread ?
                  TsanAtomicSignalFence : TsanAtomicThreadFence;
    CallInst *C = CallInst::Create(F, Args);
    ReplaceInstWithInst(I, C);
  }
  return true;
}

int ThreadSanitizer::getMemoryAccessFuncIndex(Value *Addr,
                                              const DataLayout &DL) {
  Type *OrigPtrTy = Addr->getType();
  Type *OrigTy = cast<PointerType>(OrigPtrTy)->getElementType();
  assert(OrigTy->isSized());
  uint32_t TypeSize = DL.getTypeStoreSizeInBits(OrigTy);
  if (TypeSize != 8  && TypeSize != 16 &&
      TypeSize != 32 && TypeSize != 64 && TypeSize != 128) {
    NumAccessesWithBadSize++;
    // Ignore all unusual sizes.
    return -1;
  }
  size_t Idx = countTrailingZeros(TypeSize / 8);
  assert(Idx < kNumberOfAccessSizes);
  return Idx;
}
