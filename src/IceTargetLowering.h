//===- subzero/src/IceTargetLowering.h - Lowering interface -----*- C++ -*-===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the TargetLowering and LoweringContext
// classes.  TargetLowering is an abstract class used to drive the
// translation/lowering process.  LoweringContext maintains a
// context for lowering each instruction, offering conveniences such
// as iterating over non-deleted instructions.
//
//===----------------------------------------------------------------------===//

#ifndef SUBZERO_SRC_ICETARGETLOWERING_H
#define SUBZERO_SRC_ICETARGETLOWERING_H

#include "IceDefs.h"
#include "IceInst.h" // for the names of the Inst subtypes
#include "IceTypes.h"

namespace Ice {

typedef uint8_t AsmCodeByte;

class Assembler;

// LoweringContext makes it easy to iterate through non-deleted
// instructions in a node, and insert new (lowered) instructions at
// the current point.  Along with the instruction list container and
// associated iterators, it holds the current node, which is needed
// when inserting new instructions in order to track whether variables
// are used as single-block or multi-block.
class LoweringContext {
public:
  LoweringContext() : Node(NULL) {}
  ~LoweringContext() {}
  void init(CfgNode *Node);
  Inst *getNextInst() const {
    if (Next == End)
      return NULL;
    return *Next;
  }
  Inst *getNextInst(InstList::iterator &Iter) const {
    advanceForward(Iter);
    if (Iter == End)
      return NULL;
    return *Iter;
  }
  CfgNode *getNode() const { return Node; }
  bool atEnd() const { return Cur == End; }
  InstList::iterator getCur() const { return Cur; }
  InstList::iterator getEnd() const { return End; }
  // Adaptor to enable range-based for loops.
  InstList::iterator begin() const { return getCur(); }
  InstList::iterator end() const { return getEnd(); }
  void insert(Inst *Inst);
  Inst *getLastInserted() const;
  void advanceCur() { Cur = Next; }
  void advanceNext() { advanceForward(Next); }
  void setInsertPoint(const InstList::iterator &Position) { Next = Position; }

private:
  // Node is the argument to Inst::updateVars().
  CfgNode *Node;
  // Cur points to the current instruction being considered.  It is
  // guaranteed to point to a non-deleted instruction, or to be End.
  InstList::iterator Cur;
  // Next doubles as a pointer to the next valid instruction (if any),
  // and the new-instruction insertion point.  It is also updated for
  // the caller in case the lowering consumes more than one high-level
  // instruction.  It is guaranteed to point to a non-deleted
  // instruction after Cur, or to be End.  TODO: Consider separating
  // the notion of "next valid instruction" and "new instruction
  // insertion point", to avoid confusion when previously-deleted
  // instructions come between the two points.
  InstList::iterator Next;
  // Begin is a copy of Insts.begin(), used if iterators are moved backward.
  InstList::iterator Begin;
  // End is a copy of Insts.end(), used if Next needs to be advanced.
  InstList::iterator End;

  void skipDeleted(InstList::iterator &I) const;
  void advanceForward(InstList::iterator &I) const;
  void advanceBackward(InstList::iterator &I) const;
  LoweringContext(const LoweringContext &) LLVM_DELETED_FUNCTION;
  LoweringContext &operator=(const LoweringContext &) LLVM_DELETED_FUNCTION;
};

class TargetLowering {
public:
  static TargetLowering *createLowering(TargetArch Target, Cfg *Func);
  static Assembler *createAssembler(TargetArch Target, Cfg *Func);
  void translate() {
    switch (Ctx->getOptLevel()) {
    case Opt_m1:
      translateOm1();
      break;
    case Opt_0:
      translateO0();
      break;
    case Opt_1:
      translateO1();
      break;
    case Opt_2:
      translateO2();
      break;
    default:
      Func->setError("Target doesn't specify lowering steps.");
      break;
    }
  }
  virtual void translateOm1() {
    Func->setError("Target doesn't specify Om1 lowering steps.");
  }
  virtual void translateO0() {
    Func->setError("Target doesn't specify O0 lowering steps.");
  }
  virtual void translateO1() {
    Func->setError("Target doesn't specify O1 lowering steps.");
  }
  virtual void translateO2() {
    Func->setError("Target doesn't specify O2 lowering steps.");
  }

  // Tries to do address mode optimization on a single instruction.
  void doAddressOpt();
  // Randomly insert NOPs.
  void doNopInsertion();
  // Lowers a single instruction.
  void lower();
  // Tries to do branch optimization on a single instruction.  Returns
  // true if some optimization was done.
  virtual bool doBranchOpt(Inst * /*I*/, const CfgNode * /*NextNode*/) {
    return false;
  }

  // Returns a variable pre-colored to the specified physical
  // register.  This is generally used to get very direct access to
  // the register such as in the prolog or epilog or for marking
  // scratch registers as killed by a call.
  virtual Variable *getPhysicalRegister(SizeT RegNum) = 0;
  // Returns a printable name for the register.
  virtual IceString getRegName(SizeT RegNum, Type Ty) const = 0;

  virtual bool hasFramePointer() const { return false; }
  virtual SizeT getFrameOrStackReg() const = 0;
  virtual size_t typeWidthInBytesOnStack(Type Ty) const = 0;
  virtual SizeT getBundleAlignLog2Bytes() const = 0;
  virtual llvm::ArrayRef<AsmCodeByte> getNonExecBundlePadding() const = 0;
  bool hasComputedFrame() const { return HasComputedFrame; }
  bool shouldDoNopInsertion() const;
  // Returns true if this function calls a function that has the
  // "returns twice" attribute.
  bool callsReturnsTwice() const { return CallsReturnsTwice; }
  void setCallsReturnsTwice(bool RetTwice) {
    CallsReturnsTwice = RetTwice;
  }
  int32_t getStackAdjustment() const { return StackAdjustment; }
  void updateStackAdjustment(int32_t Offset) { StackAdjustment += Offset; }
  void resetStackAdjustment() { StackAdjustment = 0; }
  LoweringContext &getContext() { return Context; }

  enum RegSet {
    RegSet_None = 0,
    RegSet_CallerSave = 1 << 0,
    RegSet_CalleeSave = 1 << 1,
    RegSet_StackPointer = 1 << 2,
    RegSet_FramePointer = 1 << 3,
    RegSet_All = ~RegSet_None
  };
  typedef uint32_t RegSetMask;

  virtual llvm::SmallBitVector getRegisterSet(RegSetMask Include,
                                              RegSetMask Exclude) const = 0;
  virtual const llvm::SmallBitVector &getRegisterSetForType(Type Ty) const = 0;
  void regAlloc();

  virtual void emitVariable(const Variable *Var) const = 0;

  // Performs target-specific argument lowering.
  virtual void lowerArguments() = 0;

  virtual void addProlog(CfgNode *Node) = 0;
  virtual void addEpilog(CfgNode *Node) = 0;

  virtual void emitConstants() const = 0;

  virtual ~TargetLowering() {}

protected:
  TargetLowering(Cfg *Func)
      : Func(Func), Ctx(Func->getContext()), HasComputedFrame(false),
        CallsReturnsTwice(false), StackAdjustment(0) {}
  virtual void lowerAlloca(const InstAlloca *Inst) = 0;
  virtual void lowerArithmetic(const InstArithmetic *Inst) = 0;
  virtual void lowerAssign(const InstAssign *Inst) = 0;
  virtual void lowerBr(const InstBr *Inst) = 0;
  virtual void lowerCall(const InstCall *Inst) = 0;
  virtual void lowerCast(const InstCast *Inst) = 0;
  virtual void lowerFcmp(const InstFcmp *Inst) = 0;
  virtual void lowerExtractElement(const InstExtractElement *Inst) = 0;
  virtual void lowerIcmp(const InstIcmp *Inst) = 0;
  virtual void lowerInsertElement(const InstInsertElement *Inst) = 0;
  virtual void lowerIntrinsicCall(const InstIntrinsicCall *Inst) = 0;
  virtual void lowerLoad(const InstLoad *Inst) = 0;
  virtual void lowerPhi(const InstPhi *Inst) = 0;
  virtual void lowerRet(const InstRet *Inst) = 0;
  virtual void lowerSelect(const InstSelect *Inst) = 0;
  virtual void lowerStore(const InstStore *Inst) = 0;
  virtual void lowerSwitch(const InstSwitch *Inst) = 0;
  virtual void lowerUnreachable(const InstUnreachable *Inst) = 0;

  virtual void doAddressOptLoad() {}
  virtual void doAddressOptStore() {}
  virtual void randomlyInsertNop(float Probability) = 0;
  // This gives the target an opportunity to post-process the lowered
  // expansion before returning.  The primary intention is to do some
  // Register Manager activity as necessary, specifically to eagerly
  // allocate registers based on affinity and other factors.  The
  // simplest lowering does nothing here and leaves it all to a
  // subsequent global register allocation pass.
  virtual void postLower() {}

  Cfg *Func;
  GlobalContext *Ctx;
  bool HasComputedFrame;
  bool CallsReturnsTwice;
  // StackAdjustment keeps track of the current stack offset from its
  // natural location, as arguments are pushed for a function call.
  int32_t StackAdjustment;
  LoweringContext Context;

private:
  TargetLowering(const TargetLowering &) LLVM_DELETED_FUNCTION;
  TargetLowering &operator=(const TargetLowering &) LLVM_DELETED_FUNCTION;
};

// TargetGlobalInitLowering is used for "lowering" global
// initializers.  It is separated out from TargetLowering because it
// does not require a Cfg.
class TargetGlobalInitLowering {
public:
  static TargetGlobalInitLowering *createLowering(TargetArch Target,
                                                  GlobalContext *Ctx);
  virtual ~TargetGlobalInitLowering();

  // TODO: Allow relocations to be represented as part of the Data.
  virtual void lower(const IceString &Name, SizeT Align, bool IsInternal,
                     bool IsConst, bool IsZeroInitializer, SizeT Size,
                     const char *Data, bool DisableTranslation) = 0;

protected:
  TargetGlobalInitLowering(GlobalContext *Ctx) : Ctx(Ctx) {}
  GlobalContext *Ctx;

private:
  TargetGlobalInitLowering(const TargetGlobalInitLowering &)
  LLVM_DELETED_FUNCTION;
  TargetGlobalInitLowering &
  operator=(const TargetGlobalInitLowering &) LLVM_DELETED_FUNCTION;
};

} // end of namespace Ice

#endif // SUBZERO_SRC_ICETARGETLOWERING_H
