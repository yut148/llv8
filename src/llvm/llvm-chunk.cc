// Copyright 2015 ISP RAS. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include "llvm-chunk.h"

namespace v8 {
namespace internal {

LLVMChunk::~LLVMChunk() {}

Handle<Code> LLVMChunk::Codegen() {
  uint64_t address = LLVMGranularity::getInstance().GetFunctionAddress(
      llvm_function_id_);

#ifdef DEBUG
  std::cerr << "\taddress == " <<  address << std::endl;
  std::cerr << "\tlast code allocated == "
      << reinterpret_cast<uint64_t>(
          LLVMGranularity::getInstance()
            .memory_manager_ref()
            ->LastAllocatedCode()
            .buffer)
      << std::endl;
  LLVMGranularity::getInstance().Err();
#endif

  Isolate* isolate = info()->isolate();
  // Allocate and install the code.
  Handle<Code> code = isolate->factory()->NewLLVMCode(
      LLVMGranularity::getInstance().memory_manager_ref()->LastAllocatedCode(),
      info()->flags());
  isolate->counters()->total_compiled_code_size()->Increment(
      code->instruction_size());
  return code;
}

LLVMChunk* LLVMChunk::NewChunk(HGraph *graph) {
  DisallowHandleAllocation no_handles;
  DisallowHeapAllocation no_gc;
  graph->DisallowAddingNewValues();
//  int values = graph->GetMaximumValueID();
  CompilationInfo* info = graph->info();

//  if (values > LUnallocated::kMaxVirtualRegisters) {
//    info->AbortOptimization(kNotEnoughVirtualRegistersForValues);
//    return NULL;
//  }
//  LAllocator allocator(values, graph);
  LLVMChunkBuilder builder(info, graph);
  LLVMChunk* chunk = builder.Build();
  if (chunk == NULL) return NULL;

//  if (!allocator.Allocate(chunk)) {
//    info->AbortOptimization(kNotEnoughVirtualRegistersRegalloc);
//    return NULL;
//  }

//  chunk->set_allocated_double_registers(
//      allocator.assigned_double_registers());

  return chunk;
}

LLVMChunk* LLVMChunkBuilder::Build() {
  chunk_ = new(zone()) LLVMChunk(info(), graph());
  module_ = LLVMGranularity::getInstance().CreateModule();
  status_ = BUILDING;

//  // If compiling for OSR, reserve space for the unoptimized frame,
//  // which will be subsumed into this frame.
//  if (graph()->has_osr()) {
//    for (int i = graph()->osr()->UnoptimizedFrameSlots(); i > 0; i--) {
//      chunk()->GetNextSpillIndex(GENERAL_REGISTERS);
//    }
//  }

  LLVMContext& context = LLVMGranularity::getInstance().context();

  // First param is context (v8, js context) which goes to esi,
  // second param is the callee's JSFunction object (edi),
  // third param is Parameter 0 which is I am not sure what
  int num_parameters = info()->num_parameters() + 3;

  std::vector<llvm::Type*> params(num_parameters, nullptr);
  for (auto i = 0; i < num_parameters; i++) {
    // For now everything is Int64. Probably it is even right for x64.
    // So in that case we are going to do come casts AFAIK
    params[i] = llvm::Type::getInt64Ty(context);
  }
  llvm::ArrayRef<llvm::Type*> paramsRef(params);
  llvm::FunctionType *function_type = llvm::FunctionType::get(
      llvm::Type::getInt64Ty(context), params, false);

  // TODO(llvm): return type for void JS functions?
  // I think it's all right, because undefined is a tagged value
  function_ = llvm::cast<llvm::Function>(
      module_->getOrInsertFunction(module_->getModuleIdentifier(),
                                   function_type));

  function_->setCallingConv(llvm::CallingConv::X86_64_V8);

  const ZoneList<HBasicBlock*>* blocks = graph()->blocks();
  for (int i = 0; i < blocks->length(); i++) {
    HBasicBlock* next = NULL;
    if (i < blocks->length() - 1) next = blocks->at(i + 1);
    DoBasicBlock(blocks->at(i), next);
    if (is_aborted()) return NULL;
  }
  DCHECK(module_);
  chunk()->set_llvm_function_id(std::stoi(module_->getModuleIdentifier()));
  LLVMGranularity::getInstance().AddModule(std::move(module_));
  status_ = DONE;
  return chunk();
}

void LLVMChunkBuilder::VisitInstruction(HInstruction* current) {
  HInstruction* old_current = current_instruction_;
  current_instruction_ = current;

//  LInstruction* instr = NULL;
  if (current->CanReplaceWithDummyUses()) {
    if (current->OperandCount() == 0) {
//      instr = DefineAsRegister(new(zone()) LDummy());
      UNIMPLEMENTED();
    } else {
      DCHECK(!current->OperandAt(0)->IsControlInstruction());
      UNIMPLEMENTED();
//      instr = DefineAsRegister(new(zone())
//          LDummyUse(UseAny(current->OperandAt(0))));
    }
    for (int i = 1; i < current->OperandCount(); ++i) {
      if (current->OperandAt(i)->IsControlInstruction()) continue;
        UNIMPLEMENTED();
//      LInstruction* dummy =
//          new(zone()) LDummyUse(UseAny(current->OperandAt(i)));
//      dummy->set_hydrogen_value(current);
//      chunk()->AddInstruction(dummy, current_block_);
    }
  } else {
    HBasicBlock* successor;
    if (current->IsControlInstruction() &&
        HControlInstruction::cast(current)->KnownSuccessorBlock(&successor) &&
        successor != NULL) {
      // Goto(successor)
      llvm_ir_builder_->CreateBr(Use(successor));
    } else {
      current->CompileToLLVM(this); // the meat
    }
  }

//  argument_count_ += current->argument_delta();
//  DCHECK(argument_count_ >= 0);
//
//  if (instr != NULL) {
//    AddInstruction(instr, current);
//  }
//
  current_instruction_ = old_current;
}

llvm::BasicBlock* LLVMChunkBuilder::Use(HBasicBlock* block) {
  if (!block->llvm_basic_block()) {
    llvm::BasicBlock *llvm_block = llvm::BasicBlock::Create(
        LLVMGranularity::getInstance().context(),
        "BlockEntry", function_);
    block->set_llvm_basic_block(llvm_block);
  }
  return block->llvm_basic_block();
}

llvm::Value* LLVMChunkBuilder::Use(HValue* value) {
  if (value->EmitAtUses() && !value->llvm_value()) {
    HInstruction* instr = HInstruction::cast(value);
    VisitInstruction(instr);
  }
  DCHECK(value->llvm_value());
  return value->llvm_value();
}

llvm::Value* LLVMChunkBuilder::SmiToInteger32(HValue* value) {
  llvm::Value* res = nullptr;
  if (SmiValuesAre32Bits()) {
    res = llvm_ir_builder_->CreateLShr(Use(value), kSmiShift);
  } else {
    DCHECK(SmiValuesAre31Bits());
    UNIMPLEMENTED();
    // TODO(llvm): just implement sarl(dst, Immediate(kSmiShift));
  }
  return res;
}

llvm::Value* LLVMChunkBuilder::Integer32ToSmi(HValue* value) {
  return llvm_ir_builder_->CreateShl(Use(value), kSmiShift);
}

llvm::CmpInst::Predicate LLVMChunkBuilder::TokenToPredicate(Token::Value op,
                                                            bool is_unsigned) {
  llvm::CmpInst::Predicate pred = llvm::CmpInst::BAD_FCMP_PREDICATE;
  switch (op) {
    case Token::EQ:
    case Token::EQ_STRICT:
      pred = llvm::CmpInst::ICMP_EQ;
      break;
    case Token::NE:
    case Token::NE_STRICT:
      pred = llvm::CmpInst::ICMP_NE;
      break;
    case Token::LT:
      pred = is_unsigned ? llvm::CmpInst::ICMP_ULT : llvm::CmpInst::ICMP_SLT;
      break;
    case Token::GT:
      pred = is_unsigned ? llvm::CmpInst::ICMP_UGT : llvm::CmpInst::ICMP_SGT;
      break;
    case Token::LTE:
      pred = is_unsigned ? llvm::CmpInst::ICMP_ULE : llvm::CmpInst::ICMP_SLE;
      break;
    case Token::GTE:
      pred = is_unsigned ? llvm::CmpInst::ICMP_UGE : llvm::CmpInst::ICMP_SGE;
      break;
    case Token::IN:
    case Token::INSTANCEOF:
    default:
      UNREACHABLE();
  }
  return pred;
}

void LLVMChunkBuilder::DoBasicBlock(HBasicBlock* block,
                                    HBasicBlock* next_block) {
#ifdef DEBUG
  std::cerr << __FUNCTION__ << std::endl;
#endif
  DCHECK(is_building());
  Use(block);
  llvm_ir_builder_ = llvm::make_unique<llvm::IRBuilder<>>(
      block->llvm_basic_block());
  current_block_ = block;
  next_block_ = next_block;
  if (block->IsStartBlock()) {
    block->UpdateEnvironment(graph_->start_environment());
    argument_count_ = 0;
  } else if (block->predecessors()->length() == 1) {
    // We have a single predecessor => copy environment and outgoing
    // argument count from the predecessor.
    DCHECK(block->phis()->length() == 0);
    HBasicBlock* pred = block->predecessors()->at(0);
    HEnvironment* last_environment = pred->last_environment();
    DCHECK(last_environment != NULL);
    // Only copy the environment, if it is later used again.
    if (pred->end()->SecondSuccessor() == NULL) {
      DCHECK(pred->end()->FirstSuccessor() == block);
    } else {
      if (pred->end()->FirstSuccessor()->block_id() > block->block_id() ||
          pred->end()->SecondSuccessor()->block_id() > block->block_id()) {
        last_environment = last_environment->Copy();
      }
    }
    block->UpdateEnvironment(last_environment);
    DCHECK(pred->argument_count() >= 0);
    argument_count_ = pred->argument_count();
  } else {
    // We are at a state join => process phis.
    HBasicBlock* pred = block->predecessors()->at(0);
    // No need to copy the environment, it cannot be used later.
    HEnvironment* last_environment = pred->last_environment();
    for (int i = 0; i < block->phis()->length(); ++i) {
      HPhi* phi = block->phis()->at(i);
      if (phi->HasMergedIndex()) {
        last_environment->SetValueAt(phi->merged_index(), phi);
      }
    }
    for (int i = 0; i < block->deleted_phis()->length(); ++i) {
      if (block->deleted_phis()->at(i) < last_environment->length()) {
        last_environment->SetValueAt(block->deleted_phis()->at(i),
                                     graph_->GetConstantUndefined());
      }
    }
    block->UpdateEnvironment(last_environment);
    // Pick up the outgoing argument count of one of the predecessors.
    argument_count_ = pred->argument_count();
  }
  HInstruction* current = block->first();
//  int start = chunk()->instructions()->length();
  while (current != NULL && !is_aborted()) {
    // Code for constants in registers is generated lazily.
    if (!current->EmitAtUses()) {
      VisitInstruction(current);
    }
    current = current->next();
  }
//  int end = chunk()->instructions()->length() - 1;
//  if (end >= start) {
//    block->set_first_instruction_index(start);
//    block->set_last_instruction_index(end);
//  }
  block->set_argument_count(argument_count_);
  next_block_ = NULL;
  current_block_ = NULL;
}

void LLVMChunkBuilder::DoBlockEntry(HBlockEntry* instr) {
  Use(instr->block());
  // TODO(llvm): LGap & parallel moves (OSR support)
  // return new(zone()) LLabel(instr->block());
}

void LLVMChunkBuilder::DoContext(HContext* instr) {
  if (instr->HasNoUses()) return;
//  std::cerr << "#Uses == " << instr->UseCount() << std::endl;
  if (info()->IsStub()) {
    UNIMPLEMENTED();
  }
//  FIXME(llvm): we need it for bailouts and such
//  UNIMPLEMENTED();
//  return DefineAsRegister(new(zone()) LContext);
}

void LLVMChunkBuilder::DoParameter(HParameter* instr) {
  int index = instr->index();
#ifdef DEBUG
  std::cerr << "Parameter #" << index << std::endl;
#endif

  int num_parameters = info()->num_parameters() + 3;
  llvm::Function::arg_iterator it = function_->arg_begin();
  // First off, skip first 2 parameters: context (esi)
  // and callee's JSFunction object (edi).
  // Now, I couldn't find a way to tweak the calling convention through LLVM
  // in a way that parameters are passed left-to-right on the stack.
  // So for now they are passed right-to-left, as in cdecl.
  // And therefore we do the magic here.
  index = -index;
  while (--index + num_parameters > 0) ++it;
  instr->set_llvm_value(it);
}

void LLVMChunkBuilder::DoArgumentsObject(HArgumentsObject* instr) {
  // There are no real uses of the arguments object.
  // arguments.length and element access are supported directly on
  // stack arguments, and any real arguments object use causes a bailout.
  // So this value is never used.
  return;
}

void LLVMChunkBuilder::DoGoto(HGoto* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoSimulate(HSimulate* instr) {
//  The “Simulate” instructions are for keeping track of what the stack
//  machine state would be, in case we need to bail out and start using
//  unoptimized code. They don’t generate any actual machine instructions.

  // TODO(llvm): we need to implement this for deoptimization support.
  // seems to be the right implementation (same as for Lithium)
  instr->ReplayEnvironment(current_block_->last_environment());
}

void LLVMChunkBuilder::DoStackCheck(HStackCheck* instr) {
//  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoConstant(HConstant* instr) {
  // Note: constants might have EmitAtUses() == true
  Representation r = instr->representation();
  if (r.IsSmi()) {
    // TODO(llvm): use/write a function for that
    // FIXME(llvm): this block was not tested
    int64_t int32_value = instr->Integer32Value();
    llvm::Value* value = llvm_ir_builder_->getInt64(int32_value << (kSmiShift));
    instr->set_llvm_value(value);
  } else if (r.IsInteger32()) {
    int64_t int32_value = instr->Integer32Value();
    llvm::Value* value = llvm_ir_builder_->getInt64(int32_value);
    instr->set_llvm_value(value);
  } else if (r.IsDouble()) {
    UNIMPLEMENTED();
  } else if (r.IsExternal()) {
    UNIMPLEMENTED();
  } else if (r.IsTagged()) {
    Handle<Object> object = instr->handle(isolate());
    if (object->IsSmi()) {
      // TODO(llvm): use/write a function for that
      Smi* smi = Smi::cast(*object);
      intptr_t intptr_value = reinterpret_cast<intptr_t>(smi);
      llvm::Value* value = llvm_ir_builder_->getInt64(intptr_value);
      instr->set_llvm_value(value);
    } else {
      UNIMPLEMENTED();
    }
  } else {
    UNREACHABLE();
  }
}

void LLVMChunkBuilder::DoReturn(HReturn* instr) {
  if (info()->IsStub()) {
    UNIMPLEMENTED();
  }
  if (info()->saves_caller_doubles()) {
    UNIMPLEMENTED();
  }
  // see NeedsEagerFrame() in lithium-codegen. For now here it's always true.
  DCHECK(!info()->IsStub());
  // I don't know what the absence (= 0) of this field means
  DCHECK(instr->parameter_count());
  if (instr->parameter_count()->IsConstant()) {
    llvm::Value* ret_val = Use(instr->value());
    llvm_ir_builder_->CreateRet(ret_val);
  } else {
    UNIMPLEMENTED();
  }
}

void LLVMChunkBuilder::DoAbnormalExit(HAbnormalExit* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoAccessArgumentsAt(HAccessArgumentsAt* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoAdd(HAdd* instr) {
  if(instr->representation().IsInteger32() || instr->representation().IsSmi()) {
    DCHECK(instr->left()->representation().Equals(instr->representation()));
    DCHECK(instr->right()->representation().Equals(instr->representation()));
    HValue* left = instr->left();
    HValue* right = instr->right();
    llvm::Value* Add = llvm_ir_builder_->CreateAdd(Use(left), Use(right),"");
    instr->set_llvm_value(Add);
    llvm::outs() << "Adding module " << *(module_.get());
  } 
  else {    
    UNIMPLEMENTED();
  }
}

void LLVMChunkBuilder::DoAllocateBlockContext(HAllocateBlockContext* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoAllocate(HAllocate* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoApplyArguments(HApplyArguments* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoArgumentsElements(HArgumentsElements* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoArgumentsLength(HArgumentsLength* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoBitwise(HBitwise* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoBoundsCheck(HBoundsCheck* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoBoundsCheckBaseIndexInformation(HBoundsCheckBaseIndexInformation* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoBranch(HBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCallWithDescriptor(HCallWithDescriptor* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCallJSFunction(HCallJSFunction* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCallFunction(HCallFunction* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCallNew(HCallNew* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCallNewArray(HCallNewArray* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCallRuntime(HCallRuntime* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCallStub(HCallStub* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCapturedObject(HCapturedObject* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoChange(HChange* instr) {
  Representation from = instr->from();
  Representation to = instr->to();
  HValue* val = instr->value();
  if (from.IsSmi()) {
    if (to.IsTagged()) {
      instr->set_llvm_value(Use(val));
      return;
    }
    from = Representation::Tagged();
  }
  if (from.IsTagged()) {
    if (to.IsDouble()) {
      UNIMPLEMENTED();
    } else if (to.IsSmi()) {
      if (!val->type().IsSmi()) {
        // TODO(llvm): environment
        // TODO(llvm): checkSmi, bailout
      }
      instr->set_llvm_value(Use(val));
    } else {
      DCHECK(to.IsInteger32());
      if (val->type().IsSmi() || val->representation().IsSmi()) {
        // convert smi to int32, no need to perform smi check
        // lithium codegen does __ AssertSmi(input)
        instr->set_llvm_value(SmiToInteger32(val));
      } else {
        // TODO(llvm): perform smi check, bailout if not a smi
        // see LCodeGen::DoTaggedToI
        instr->set_llvm_value(SmiToInteger32(val));
      }
    }
  } else if (from.IsDouble()) {
    UNIMPLEMENTED();
  } else if (from.IsInteger32()) {
    if (to.IsTagged()) {
      if (!instr->CheckFlag(HValue::kCanOverflow)) {
        instr->set_llvm_value(Integer32ToSmi(val));
      } else {
        UNIMPLEMENTED();
      }
    } else if (to.IsSmi()) {
      UNIMPLEMENTED();
    } else {
      DCHECK(to.IsDouble());
      UNIMPLEMENTED();
    }
  }
}

void LLVMChunkBuilder::DoCheckHeapObject(HCheckHeapObject* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCheckInstanceType(HCheckInstanceType* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCheckMaps(HCheckMaps* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCheckMapValue(HCheckMapValue* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCheckSmi(HCheckSmi* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCheckValue(HCheckValue* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoClampToUint8(HClampToUint8* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoClassOfTestAndBranch(HClassOfTestAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCompareNumericAndBranch(HCompareNumericAndBranch* instr) {
  Representation r = instr->representation();
  HValue* left = instr->left();
  HValue* right = instr->right();
  DCHECK(left->representation().Equals(r));
  DCHECK(right->representation().Equals(r));
  bool is_unsigned = r.IsDouble()
      || left->CheckFlag(HInstruction::kUint32)
      || right->CheckFlag(HInstruction::kUint32);
  llvm::CmpInst::Predicate pred = TokenToPredicate(instr->token(), is_unsigned);

  if (r.IsSmi()) {
    UNIMPLEMENTED();
  } else if (r.IsInteger32()) {
    llvm::Value* compare = llvm_ir_builder_->CreateICmp(pred, Use(left),
                                                        Use(right));
    llvm::Value* branch = llvm_ir_builder_->CreateCondBr(compare,
        Use(instr->SuccessorAt(0)), Use(instr->SuccessorAt(1)));
    instr->set_llvm_value(branch);
  } else {
    DCHECK(r.IsDouble());
    UNIMPLEMENTED();
  }
}

void LLVMChunkBuilder::DoCompareHoleAndBranch(HCompareHoleAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCompareGeneric(HCompareGeneric* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCompareMinusZeroAndBranch(HCompareMinusZeroAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCompareObjectEqAndBranch(HCompareObjectEqAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoCompareMap(HCompareMap* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoConstructDouble(HConstructDouble* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoDateField(HDateField* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoDebugBreak(HDebugBreak* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoDeclareGlobals(HDeclareGlobals* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoDeoptimize(HDeoptimize* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoDiv(HDiv* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoDoubleBits(HDoubleBits* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoDummyUse(HDummyUse* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoEnterInlined(HEnterInlined* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoEnvironmentMarker(HEnvironmentMarker* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoForceRepresentation(HForceRepresentation* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoForInCacheArray(HForInCacheArray* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoForInPrepareMap(HForInPrepareMap* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoFunctionLiteral(HFunctionLiteral* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoGetCachedArrayIndex(HGetCachedArrayIndex* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoHasCachedArrayIndexAndBranch(HHasCachedArrayIndexAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoHasInstanceTypeAndBranch(HHasInstanceTypeAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoInnerAllocatedObject(HInnerAllocatedObject* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoInstanceOf(HInstanceOf* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoInstanceOfKnownGlobal(HInstanceOfKnownGlobal* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoInvokeFunction(HInvokeFunction* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoIsConstructCallAndBranch(HIsConstructCallAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoIsObjectAndBranch(HIsObjectAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoIsStringAndBranch(HIsStringAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoIsSmiAndBranch(HIsSmiAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoIsUndetectableAndBranch(HIsUndetectableAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLeaveInlined(HLeaveInlined* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadContextSlot(HLoadContextSlot* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadFieldByIndex(HLoadFieldByIndex* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadFunctionPrototype(HLoadFunctionPrototype* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadGlobalCell(HLoadGlobalCell* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadGlobalGeneric(HLoadGlobalGeneric* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadKeyed(HLoadKeyed* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadKeyedGeneric(HLoadKeyedGeneric* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadNamedField(HLoadNamedField* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadNamedGeneric(HLoadNamedGeneric* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoLoadRoot(HLoadRoot* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoMapEnumLength(HMapEnumLength* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoMathFloorOfDiv(HMathFloorOfDiv* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoMathMinMax(HMathMinMax* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoMod(HMod* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoMul(HMul* instr) {
  if(instr->representation().IsInteger32() || instr->representation().IsSmi()) {
    DCHECK(instr->left()->representation().Equals(instr->representation()));
    DCHECK(instr->right()->representation().Equals(instr->representation()));
    HValue* left = instr->left();
    HValue* right = instr->right();
    CHECK(left->llvm_value());
    CHECK(right->llvm_value());
    llvm::Value* Mul = llvm_ir_builder_->CreateMul(left->llvm_value(), right->llvm_value(),"");
    instr->set_llvm_value(Mul);
    llvm::outs() << "Adding module " << *(module_.get()); 
  }
  else {
    UNIMPLEMENTED();
  }
}

void LLVMChunkBuilder::DoOsrEntry(HOsrEntry* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoPower(HPower* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoPushArguments(HPushArguments* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoRegExpLiteral(HRegExpLiteral* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoRor(HRor* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoSar(HSar* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoSeqStringGetChar(HSeqStringGetChar* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoSeqStringSetChar(HSeqStringSetChar* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoShl(HShl* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoShr(HShr* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStoreCodeEntry(HStoreCodeEntry* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStoreContextSlot(HStoreContextSlot* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStoreFrameContext(HStoreFrameContext* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStoreGlobalCell(HStoreGlobalCell* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStoreKeyed(HStoreKeyed* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStoreKeyedGeneric(HStoreKeyedGeneric* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStoreNamedField(HStoreNamedField* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStoreNamedGeneric(HStoreNamedGeneric* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStringAdd(HStringAdd* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStringCharCodeAt(HStringCharCodeAt* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStringCharFromCode(HStringCharFromCode* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoStringCompareAndBranch(HStringCompareAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoSub(HSub* instr) {
  if(instr->representation().IsInteger32() || instr->representation().IsSmi()) {
    DCHECK(instr->left()->representation().Equals(instr->representation()));
    DCHECK(instr->right()->representation().Equals(instr->representation()));
    HValue* left = instr->left();
    HValue* right = instr->right();
    CHECK(left->llvm_value());
    CHECK(right->llvm_value());
    llvm::Value* Sub = llvm_ir_builder_->CreateSub(left->llvm_value(), right->llvm_value(),"");
    instr->set_llvm_value(Sub);
    llvm::outs() << "Adding module " << *(module_.get());
  }
  else {
    UNIMPLEMENTED();
  }
}

void LLVMChunkBuilder::DoTailCallThroughMegamorphicCache(HTailCallThroughMegamorphicCache* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoThisFunction(HThisFunction* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoToFastProperties(HToFastProperties* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoTransitionElementsKind(HTransitionElementsKind* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoTrapAllocationMemento(HTrapAllocationMemento* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoTypeof(HTypeof* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoTypeofIsAndBranch(HTypeofIsAndBranch* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoUnaryMathOperation(HUnaryMathOperation* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoUnknownOSRValue(HUnknownOSRValue* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoUseConst(HUseConst* instr) {
  UNIMPLEMENTED();
}

void LLVMChunkBuilder::DoWrapReceiver(HWrapReceiver* instr) {
  UNIMPLEMENTED();
}

}  // namespace internal
}  // namespace v8
