#include <Wide/Semantic/AggregateType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/FunctionType.h>
#include <sstream>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Analysis/Verifier.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

// A quick helper to get an expression that emits a function.
struct AggregateType::AggregateFunction : Expression {
    AggregateFunction(llvm::Function*& func, FunctionType* functy, std::function<void(CodegenContext&)> emit)
    : func(func), functy(functy), emit(emit) {}
    llvm::Function*& func;
    std::function<void(CodegenContext&)> emit;
    FunctionType* functy;
    Type* GetType() override final {
        return functy;
    }
    llvm::Value* ComputeValue(CodegenContext& con) override final {
        if (!func) {
            func = llvm::Function::Create(llvm::cast<llvm::FunctionType>(functy->GetLLVMType(con)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, "", con);
            CodegenContext::EmitFunctionBody(func, emit);
        }
        return func;
    }
};
AggregateType::Layout::Layout(AggregateType* agg, Wide::Semantic::Analyzer& a)
: allocsize(0)
, align(1)
, copyassignable(true)
, copyconstructible(true)
, moveassignable(true)
, moveconstructible(true)
, constant(true)
{
    // http://refspecs.linuxbase.org/cxxabi-1.83.html#class-types
    // 2.4.1
    // Initialize variables, including primary base.
    auto& sizec = allocsize;
    auto& alignc = align;
    auto dsizec = 0;
    
    for (unsigned i = 0; i < agg->GetBases().size(); ++i) {
        auto base = agg->GetBases()[i];
        if (!base->GetVtableLayout().layout.empty()) {
            PrimaryBase = base;
            PrimaryBaseNum = i;
            break;
        }
    }
    
    //  If C has no primary base class, allocate the virtual table pointer for C at offset zero
    //  and set sizeof(C), align(C), and dsize(C) to the appropriate values for a pointer
    if (agg->HasDeclaredDynamicFunctions() && !PrimaryBase) {
        sizec = agg->analyzer.GetDataLayout().getPointerSize();
        alignc = agg->analyzer.GetDataLayout().getPointerPrefAlignment();
        dsizec = sizec;
        hasvptr = true;
    }

    auto UpdatePropertiesForMember = [this, agg](Type* D) {
        constant &= D->GetConstantContext() == D;
        moveconstructible &= D->IsMoveConstructible(GetAccessSpecifier(agg, D));
        copyconstructible &= D->IsCopyConstructible(GetAccessSpecifier(agg, D));
        moveassignable &= D->IsMoveAssignable(GetAccessSpecifier(agg, D));
        copyassignable &= D->IsCopyAssignable(GetAccessSpecifier(agg, D));
        triviallycopyconstructible &= D->IsTriviallyCopyConstructible();
        triviallydestructible &= D->IsTriviallyDestructible();
    };

    auto bases_size = agg->GetBases().size();
    auto members_size = agg->GetMembers().size();
    Offsets.resize(bases_size + members_size);

    // If we have a vptr then all fields offset by 1.
    unsigned fieldnum = hasvptr ? 1 : 0;

    // 2.4.2
    // Clause 1 does not apply because we don't support virtual bases.
    // Clause 2 (second argument not required w/o virtual base support
    auto layout_nonemptybase = [&](Type* D, bool base, std::size_t offset, std::size_t num) {
        UpdatePropertiesForMember(D);

        // Start at offset dsize(C), incremented if necessary for alignment to align(D).
        bool AddedPadding = false;
        if (offset % D->alignment() != 0) {
            offset += D->alignment() - (offset % D->alignment());
            AddedPadding = true;
        }

        auto AttemptLayout = [&, this] {
            auto DEmpties = D->GetEmptyLayout();
            for (auto newempty : DEmpties) {
                // If there are any empties which clash, move on.
                for (auto empty : newempty.second) {
                    if (EmptyTypes[newempty.first + offset].find(empty) != EmptyTypes[newempty.first + offset].end())
                        return false;
                }
            }
            return true;
        };

        while (!AttemptLayout()) {
            offset += D->alignment();
            AddedPadding = true;
        }
        // Update the empty map for our new layout.
        auto DEmpties = D->GetEmptyLayout();
        for (auto newempty : DEmpties) {
            for (auto empty : newempty.second) 
                EmptyTypes[newempty.first + offset].insert(empty);
        }
        // If we inserted any padding, increase the field number to account for the padding array
        if (AddedPadding)
            fieldnum++;
        Offsets[num] = { D, offset, fieldnum++ };
        // update sizeof(C) to max (sizeof(C), offset(D)+sizeof(D))
        sizec = std::max(sizec, offset + D->size());
        // update dsize(C) to offset(D)+sizeof(D),
        dsizec = offset + D->size();
        // align(C) to max(align(C), align(D)).
        alignc = std::max(alignc, D->alignment());
    };

    // Clause 3
    auto layout_emptybase = [&](Type* D, std::size_t num) {
        UpdatePropertiesForMember(D);
        // Its allocation is similar to case (2) above, except that additional candidate offsets are considered before starting at dsize(C). First, attempt to place D at offset zero.
        unsigned offset = 0;
        if (EmptyTypes[offset].find(D) != EmptyTypes[offset].end()) {
            // If unsuccessful (due to a component type conflict), proceed with attempts at dsize(C) as for non-empty bases. 
            offset = dsizec;
            while (EmptyTypes[offset].find(D) != EmptyTypes[offset].end()) {
                // As for that case, if there is a type conflict at dsize(C) (with alignment updated as necessary), increment the candidate offset by nvalign(D)
                ++offset;
            }
        }
        // Success
        Offsets[num] = { D, offset };
        // Insert the empty type
        EmptyTypes[offset].insert(D);
        // Once offset(D) has been chosen, update sizeof(C) to max (sizeof(C), offset(D)+sizeof(D))
        sizec = std::max(sizec, offset + D->size());
    };

    // For each data component D (first the primary base of C, if any, then the non-primary, non-virtual direct base classes in declaration order, then the non-static data members
    if (PrimaryBase) layout_nonemptybase(PrimaryBase, true, dsizec, PrimaryBaseNum);

    for (unsigned i = 0; i < agg->GetBases().size(); ++i) {
        auto base = agg->GetBases()[i];
        if (base == PrimaryBase)
            continue;
        if (base->IsEmpty())
            layout_emptybase(base, i);
        else
            layout_nonemptybase(base, true, dsizec, i);
    }
    for (unsigned i = 0; i < agg->GetMembers().size(); ++i) {
        auto member = agg->GetMembers()[i];
        layout_nonemptybase(member, false, dsizec, i + agg->GetBases().size());
    }

    // Check our size and alignment overrides.
    auto size_override = agg->SizeOverride();
    if (size_override) {
        if (*size_override < sizec)
            throw std::runtime_error("Size override was too small.");
        sizec = *size_override;
    }
    auto align_override = agg->AlignOverride();
    if (align_override) {
        if (*align_override < alignc)
            throw std::runtime_error("Alignment override was too small.");
        alignc = *align_override;
    }

    // Round sizeof(C) up to a non-zero multiple of align(C). If C is a POD, but not a POD for the purpose of layout, set nvsize(C) = sizeof(C). 
    if (sizec % alignc != 0)
        sizec += alignc - (sizec % alignc);

    struct Self : public Expression {
        Self(AggregateType* me) : self(me) {}
        AggregateType* self;
        Type* GetType() override final { return self->analyzer.GetLvalueType(self); }
        llvm::Value* ComputeValue(CodegenContext& c) override final { return self->DestructorFunction->arg_begin(); }
    };
    auto destructor_self = std::make_shared<Self>(agg);
    for (int i = Offsets.size() - 1; i >= 0; --i) {
        auto member = Offsets[i].ty;
        agg->destructors.push_back(member->BuildDestructorCall(agg->PrimitiveAccessMember(destructor_self, i), { agg, Lexer::Range(std::make_shared<std::string>("AggregateDestructor")) }, true));
    }
}
std::size_t AggregateType::size() {
    return GetLayout().allocsize;
}
std::size_t AggregateType::alignment() {
    return GetLayout().align;
}

bool AggregateType::IsMoveAssignable(Parse::Access access) {
    return GetLayout().moveassignable;
}
bool AggregateType::IsMoveConstructible(Parse::Access access) {
    return GetLayout().moveconstructible;
}
bool AggregateType::IsCopyAssignable(Parse::Access access) {
    return GetLayout().copyassignable;
}
bool AggregateType::IsCopyConstructible(Parse::Access access) {
    return GetLayout().copyconstructible;
}
std::shared_ptr<Expression> AggregateType::GetVirtualPointer(std::shared_ptr<Expression> self) {
    if (GetPrimaryBase()) return GetPrimaryBase()->GetVirtualPointer(Type::AccessBase(std::move(self), GetPrimaryBase()));
    if (!HasDeclaredDynamicFunctions()) return nullptr;
    auto vptrty = analyzer.GetPointerType(GetVirtualPointerType());
    assert(self->GetType()->IsReference(this) || dynamic_cast<PointerType*>(self->GetType())->GetPointee() == this);
    struct VPtrAccess : Expression {
        VPtrAccess(Type* t, std::shared_ptr<Expression> self)
        : ty(t), self(std::move(self)) {}
        Type* ty;
        std::shared_ptr<Expression> self;
        llvm::Value* ComputeValue(CodegenContext& con) override final { return con.CreateStructGEP(self->GetValue(con), 0); }
        Type* GetType() override final { return ty; }
    };
    return Wide::Memory::MakeUnique<VPtrAccess>(analyzer.GetLvalueType(vptrty), std::move(self));
}

bool AggregateType::IsEmpty() {
    // A class with no non - static data members other than zero - width bitfields, no virtual functions, no virtual base classes, and no non - empty non - virtual proper base classes.
    if (!GetMembers().empty()) return false;
    if (GetLayout().hasvptr) return false;
    for (auto base : GetBases())
        if (!base->IsEmpty())
            return false;
    return true;
}

AggregateType::Layout::CodeGen::CodeGen(AggregateType* self, Layout& lay, llvm::Module* module)
{
    llvmtype = nullptr;
}
llvm::Type* AggregateType::Layout::CodeGen::GetLLVMType(AggregateType* self, llvm::Module* module) {
    std::stringstream stream;
    stream << "struct.__" << self;
    auto llvmname = stream.str();
    if (llvmtype) return llvmtype;
    if (llvmtype = module->getTypeByName(llvmname))
        return llvmtype;
    auto ty = llvm::StructType::create(module->getContext(), llvmname);
    llvmtype = ty;
    std::vector<llvm::Type*> llvmtypes;

    if (self->IsEmpty() && !self->GetLayout().hasvptr) {
        llvmtypes.push_back(self->analyzer.GetIntegralType(8, true)->GetLLVMType(module));
        ty->setBody(llvmtypes, false);
        return ty;
    }

    unsigned curroffset = 0;
    if (self->GetLayout().hasvptr) {
        llvmtypes.push_back(self->analyzer.GetPointerType(self->GetVirtualPointerType())->GetLLVMType(module));
        curroffset = self->analyzer.GetDataLayout().getPointerSize();
    }

    for (auto llvmmember : self->GetLayout().Offsets) {
        if (llvmmember.FieldIndex) {
            if (curroffset != llvmmember.ByteOffset) {
                llvmtypes.push_back(llvm::ArrayType::get(llvm::Type::getInt8Ty(module->getContext()), llvmmember.ByteOffset - curroffset));
            }
            assert(*llvmmember.FieldIndex == llvmtypes.size());
            llvmtypes.push_back(llvmmember.ty->GetLLVMType(module));
            curroffset = llvmmember.ByteOffset + llvmmember.ty->size();
        }
    }
    if (self->size() != curroffset)
        llvmtypes.push_back(llvm::ArrayType::get(llvm::Type::getInt8Ty(module->getContext()), self->size() - curroffset));
    ty->setBody(llvmtypes, false);
    return ty;
}
llvm::Type* AggregateType::GetLLVMType(llvm::Module* module) {
    return GetLayout().GetCodegen(this, module).GetLLVMType(this, module);
}
Type* AggregateType::GetConstantContext() {
    for (auto ty : GetBases())
        if (!ty->GetConstantContext())
            return nullptr;
    for (auto ty : GetMembers())
        if (!ty->GetConstantContext())
            return nullptr;
    if (GetLayout().hasvptr)
        return nullptr;
    return this;
}

std::shared_ptr<Expression> AggregateType::PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) {
    struct FieldAccess : Expression {
        FieldAccess(std::shared_ptr<Expression> src, AggregateType* agg, unsigned n)
        : source(std::move(src)), self(agg), num(n) {}
        std::shared_ptr<Expression> source;
        AggregateType* self;
        unsigned num;
        Type* GetType() override final {
            auto source_ty = source->GetType();
            auto root_ty = num < self->GetBases().size()
                ? self->GetBases()[num]
                : self->GetMembers()[num - self->GetBases().size()];

            // If it's not a reference, just return the root type.
            if (!source_ty->IsReference())
                return root_ty;

            // If the source is an lvalue, the result is an lvalue.
            if (IsLvalueType(source_ty))
                return self->analyzer.GetLvalueType(root_ty->Decay());

            // It's not a value or an lvalue so must be rvalue.
            return self->analyzer.GetRvalueType(root_ty);
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto src = source->GetValue(con);
            // Is there even a field index? This may not be true for EBCO
            auto&& offsets = self->GetLayout().Offsets;
            if (!offsets[num].FieldIndex) {
                if (src->getType()->isPointerTy()) {
                    // Move it by that offset to get a unique pointer
                    auto self_as_int8ptr = con->CreatePointerCast(src, con.GetInt8PtrTy());
                    auto offset_self = con->CreateConstGEP1_32(self_as_int8ptr, offsets[num].ByteOffset);
                    return con->CreatePointerCast(offset_self, GetType()->GetLLVMType(con));
                }
                // Downcasting to an EBCO'd value -> just produce a value.
                return llvm::Constant::getNullValue(self->GetLayout().Offsets[num].ty->GetLLVMType(con));
            }
            auto fieldindex = *offsets[num].FieldIndex;
            auto obj = src->getType()->isPointerTy() ? con.CreateStructGEP(src, fieldindex) : con->CreateExtractValue(src, { fieldindex });
            if ((source->GetType()->IsReference() && offsets[num].ty->IsReference()) || (source->GetType()->AlwaysKeepInMemory() && !offsets[num].ty->AlwaysKeepInMemory()))
                return con->CreateLoad(obj);
            return obj;
        }
    };
    assert(self);
    return Wide::Memory::MakeUnique<FieldAccess>(std::move(self), this, num);
}
AggregateType::AggregateType(Analyzer& a) : Type(a) {}

OverloadSet* AggregateType::CreateOperatorOverloadSet(Parse::OperatorName type, Parse::Access access) {
    if (type != Parse::OperatorName{ &Lexer::TokenTypes::Assignment })
        return analyzer.GetOverloadSet();
    if (access != Parse::Access::Public)
        return AccessMember(type, Parse::Access::Public);
    return CreateAssignmentOperatorOverloadSet({ true, true });
}

llvm::Function* AggregateType::CreateDestructorFunction(llvm::Module* module) {    
    auto functy = llvm::FunctionType::get(llvm::Type::getVoidTy(module->getContext()), { analyzer.GetLvalueType(this)->GetLLVMType(module) }, false);
    auto DestructorFunction = llvm::Function::Create(functy, llvm::GlobalValue::LinkageTypes::InternalLinkage, "", module);
    this->DestructorFunction = DestructorFunction;
    CodegenContext::EmitFunctionBody(DestructorFunction, [this](CodegenContext& newcon) {
        for (auto des : destructors)
            des(newcon);
        newcon->CreateRetVoid();
    });
    if (llvm::verifyFunction(*DestructorFunction, llvm::VerifierFailureAction::PrintMessageAction))
        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
    return DestructorFunction;
}
std::function<void(CodegenContext&)> AggregateType::BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize) {
    return [this, self](CodegenContext& con) {
        con->CreateCall(GetDestructorFunction(con), self->GetValue(con));
    };
}
OverloadSet* AggregateType::GetCopyAssignmentOperator() {
    return MaybeCreateSet(CopyAssignmentOperator,
        [this](Type* ty, unsigned i) { return ty->IsCopyAssignable(GetAccessSpecifier(this, ty)); },
        [this] {
            return MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr < Expression > {
                if (GetLayout().Offsets.size() == 0)
                    return BuildChain(std::move(args[0]), std::move(args[1]));
        
                if (!CopyAssignmentExpressions) {
                    auto rhs = CreatePrimGlobal(analyzer.GetLvalueType(this), [this](CodegenContext& con) {
                        return ++CopyAssignmentFunction->arg_begin();
                    });
                    CopyAssignmentExpressions = GetAssignmentOpExpressions(GetConstructorSelf(CopyAssignmentFunction), c, [this, rhs](unsigned i) -> std::shared_ptr<Expression> {
                        return PrimitiveAccessMember(rhs, i);
                    });
                }
        
                auto functy = analyzer.GetFunctionType(analyzer.GetLvalueType(this), { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) }, false);
                return functy->BuildCall(std::make_shared<AggregateFunction>(CopyAssignmentFunction, functy, [this](CodegenContext& con) { EmitAssignmentOperator(con, *CopyAssignmentExpressions); }), { args[0], args[1] }, c);
            }, { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) });
        }
    );
}
OverloadSet* AggregateType::GetMoveAssignmentOperator() {
    return MaybeCreateSet(MoveAssignmentOperator, 
        [this](Type* ty, unsigned i) { return ty->IsMoveAssignable(GetAccessSpecifier(this, ty)); },
        [this] {
            return MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr < Expression > {
                if (GetLayout().Offsets.size() == 0)
                    return BuildChain(std::move(args[0]), std::move(args[1]));
            
                if (!MoveAssignmentExpressions) {
                    auto rhs = CreatePrimGlobal(analyzer.GetLvalueType(this), [this](CodegenContext& con) {
                        return ++MoveAssignmentFunction->arg_begin();
                    });
                    MoveAssignmentExpressions = GetAssignmentOpExpressions(GetConstructorSelf(MoveAssignmentFunction), c, [this, rhs](unsigned i) -> std::shared_ptr<Expression> {
                        return PrimitiveAccessMember(rhs, i);
                    });
                }
            
                auto functy = analyzer.GetFunctionType(analyzer.GetLvalueType(this), { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) }, false);
                return functy->BuildCall(std::make_shared<AggregateFunction>(MoveAssignmentFunction, functy, [this](CodegenContext& con) { EmitAssignmentOperator(con, *MoveAssignmentExpressions); }), { args[0], args[1] }, c);
            }, { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) });            
        }
    );
}
OverloadSet* AggregateType::GetMoveConstructor() {
    return MaybeCreateSet(MoveConstructor, 
        [this](Type* ty, unsigned i) { return ty->IsMoveConstructible(GetAccessSpecifier(this, ty)); },
        [this] {
            return MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr < Expression > {
                if (GetLayout().Offsets.size() == 0)
                    return BuildChain(std::move(args[0]), std::move(args[1]));
            
                if (!MoveConstructorInitializers) {
                    auto rhs = CreatePrimGlobal(analyzer.GetLvalueType(this), [this](CodegenContext& con) {
                        return ++MoveConstructorFunction->arg_begin();
                    });
                    MoveConstructorInitializers = GetConstructorInitializers(GetConstructorSelf(MoveConstructorFunction), c, [this, rhs](unsigned i) -> std::vector<std::shared_ptr<Expression>> {
                        return { PrimitiveAccessMember(rhs, i) };
                    });
                }
            
                auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) }, false);
                return functy->BuildCall(std::make_shared<AggregateFunction>(MoveConstructorFunction, functy, [this](CodegenContext& con) { EmitConstructor(con, *MoveConstructorInitializers); }), { args[0], args[1] }, c);
            }, { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) });
        }
    );
}
OverloadSet* AggregateType::GetCopyConstructor() {
    return MaybeCreateSet(CopyConstructor,
        [this](Type* ty, unsigned i) { return ty->IsCopyConstructible(GetAccessSpecifier(this, ty)); },
        [this] {
            return MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr < Expression > {
                if (GetLayout().Offsets.size() == 0)
                    return BuildChain(std::move(args[0]), std::move(args[1]));
        
                if (!CopyConstructorInitializers) {
                    auto rhs = CreatePrimGlobal(analyzer.GetLvalueType(this), [this](CodegenContext& con) {
                        return ++CopyConstructorFunction->arg_begin();
                    });
                    CopyConstructorInitializers = GetConstructorInitializers(GetConstructorSelf(CopyConstructorFunction), c, [this, rhs](unsigned i) -> std::vector<std::shared_ptr<Expression>> {
                        return { PrimitiveAccessMember(rhs, i) };
                    });
                }
        
                auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) }, false);
                return functy->BuildCall(std::make_shared<AggregateFunction>(CopyConstructorFunction, functy, [this](CodegenContext& con) { EmitConstructor(con, *CopyConstructorInitializers); }), { args[0], args[1] }, c);
            }, { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) });
        }
    );
}
OverloadSet* AggregateType::MaybeCreateSet(Wide::Util::optional<std::unique_ptr<OverloadResolvable>>& func, std::function<bool(Type*, unsigned)> member_should, std::function<std::unique_ptr<OverloadResolvable>()> create) {
    if (func) {
        if (!*func) return analyzer.GetOverloadSet();
        return analyzer.GetOverloadSet(func->get());
    }
    // Not created yet.
    auto should = [=] {
        unsigned i = 0;
        for (auto mem : GetLayout().Offsets) {
            if (!member_should(mem.ty, i++))
                return false;
        }
        return true;
    };
    if (!should()) {
        func = nullptr;
        return analyzer.GetOverloadSet();
    }
    func = create();
    return analyzer.GetOverloadSet(func->get());
}
std::shared_ptr<Expression> AggregateType::GetConstructorSelf(llvm::Function*& func) {
    return CreatePrimGlobal(analyzer.GetLvalueType(this), [&func](CodegenContext& con) {
        return func->arg_begin();
    });
}
void AggregateType::EmitAssignmentOperator(CodegenContext& con, std::vector<std::shared_ptr<Expression>> inits) {
    // Should be members + bases + 1.
    assert(inits.size() == GetMembers().size() + GetBases().size());
    for (unsigned i = 0; i < GetMembers().size() + GetBases().size(); ++i)
        inits[i]->GetValue(con);
    con.DestroyAll(false);
    con->CreateRet(con->GetInsertBlock()->getParent()->arg_begin());
}
void AggregateType::EmitConstructor(CodegenContext& con, std::vector<std::shared_ptr<Expression>> inits) {
    // Should be members + bases + 1.
    assert(inits.size() == GetMembers().size() + GetBases().size() + 1);
    for (unsigned i = 0; i < GetMembers().size() + GetBases().size(); ++i)
        inits[i]->GetValue(con);
    inits.back()->GetValue(con);
    con.DestroyAll(false);
    con->CreateRetVoid();
}
std::shared_ptr<Expression> AggregateType::GetMemberFromThis(std::shared_ptr<Expression> self, unsigned offset, Type* member) {
    auto result = member;
    return CreatePrimUnOp(self, result, [offset, result](llvm::Value* val, CodegenContext& con) {
        auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
        self = con->CreateConstGEP1_32(self, offset);
        return con->CreatePointerCast(self, result->GetLLVMType(con));
    });
}
std::vector<std::shared_ptr<Expression>> AggregateType::GetAssignmentOpExpressions(std::shared_ptr<Expression> self, Context c, std::function<std::shared_ptr<Expression>(unsigned)> initializers) {
    std::vector<std::shared_ptr<Expression>> exprs;
    for (std::size_t i = 0; i < GetLayout().Offsets.size(); ++i) {
        auto type = GetLayout().Offsets[i].ty;
        auto offset = GetLayout().Offsets[i].ByteOffset;
        auto lhs = GetMemberFromThis(self, offset, analyzer.GetLvalueType(type));
        exprs.push_back(Type::BuildBinaryExpression(lhs, initializers(i), &Lexer::TokenTypes::Assignment, c));
    }
    return exprs;
}
std::vector<std::shared_ptr<Expression>> AggregateType::GetConstructorInitializers(std::shared_ptr<Expression> self, Context c, std::function<std::vector<std::shared_ptr<Expression>>(unsigned)> initializers) {
    std::vector<std::shared_ptr<Expression>> exprs;
    for (std::size_t i = 0; i < GetLayout().Offsets.size(); ++i) {
        auto type = GetLayout().Offsets[i].ty;
        auto offset = GetLayout().Offsets[i].ByteOffset;
        auto lhs = GetMemberFromThis(self, offset, analyzer.GetLvalueType(type));
        auto init = Type::BuildInplaceConstruction(lhs, initializers(i), c);
        struct MemberConstructionAccess : Expression {
            Type* member;
            Lexer::Range where;
            std::shared_ptr<Expression> Construction;
            std::shared_ptr<Expression> memexpr;
            std::function<void(CodegenContext&)> destructor;
            llvm::Value* ComputeValue(CodegenContext& con) override final {
                auto val = Construction->GetValue(con);
                if (destructor)
                    con.AddExceptionOnlyDestructor(destructor);
                return val;
            }
            Type* GetType() override final {
                return Construction->GetType();
            }
            MemberConstructionAccess(Type* mem, Lexer::Range where, std::shared_ptr<Expression> expr, std::shared_ptr<Expression> memexpr)
                : member(mem), where(where), Construction(std::move(expr)), memexpr(memexpr)
            {
                if (!member->IsTriviallyDestructible())
                    destructor = member->BuildDestructorCall(memexpr, { member, where }, true);
            }
        };
        exprs.push_back(std::make_shared<MemberConstructionAccess>(type, c.where, init, lhs));
    }
    exprs.push_back(Type::SetVirtualPointers(self));
    return exprs;
}
OverloadSet* AggregateType::GetDefaultConstructor() {
    // Not created yet.
    auto is_default_constructible = [this](Type* ty, unsigned i) {
        if (i < GetBases().size())
            return ty->GetConstructorOverloadSet(GetAccessSpecifier(this, ty))->Resolve({ analyzer.GetLvalueType(ty) }, this);
        auto inits = GetDefaultInitializerForMember(i - GetBases().size());
        std::vector<Type*> types = { analyzer.GetLvalueType(ty) };
        for (auto init : inits) types.push_back(init->GetType());
        return ty->GetConstructorOverloadSet(GetAccessSpecifier(this, ty))->Resolve(types, this);
    };
    // If we shouldn't generate, give back an empty set and set the cache for future use.
    auto create = [this] {
        return MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr < Expression > {
            if (GetLayout().Offsets.size() == 0)
                return std::move(args[0]);

            if (!DefaultConstructorInitializers)
                DefaultConstructorInitializers = GetConstructorInitializers(GetConstructorSelf(DefaultConstructorFunction), c, [this](unsigned i) -> std::vector < std::shared_ptr<Expression> > {
                if (i < GetBases().size())
                    return{};
                return GetDefaultInitializerForMember(i - GetBases().size());
            });

            auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this) }, false);
            return functy->BuildCall(std::make_shared<AggregateFunction>(DefaultConstructorFunction, functy, [this](CodegenContext& con) { EmitConstructor(con, *DefaultConstructorInitializers); }), { args[0] }, c);
        }, { analyzer.GetLvalueType(this) });
    };
    return MaybeCreateSet(DefaultConstructor, is_default_constructible, create);
}
OverloadSet* AggregateType::CreateAssignmentOperatorOverloadSet(AggregateAssignmentOperators which) {
    auto set = analyzer.GetOverloadSet();
    if (which.copy_operator) set = analyzer.GetOverloadSet(set, GetCopyAssignmentOperator());
    if (which.move_operator) set = analyzer.GetOverloadSet(set, GetMoveAssignmentOperator());
    return set;    
}
OverloadSet* AggregateType::CreateConstructorOverloadSet(AggregateConstructors cons) {
    auto set = analyzer.GetOverloadSet();
    if (cons.copy_constructor) set = analyzer.GetOverloadSet(set, GetCopyConstructor());
    if (cons.default_constructor) set = analyzer.GetOverloadSet(set, GetDefaultConstructor());
    if (cons.move_constructor) set = analyzer.GetOverloadSet(set, GetMoveConstructor());
    return set;
}
OverloadSet* AggregateType::CreateConstructorOverloadSet(Parse::Access access) {
    return CreateConstructorOverloadSet({ true, true, true });
}
bool AggregateType::HasMemberOfType(Type* t) {
    for (auto ty : GetLayout().Offsets) {
        if (ty.ty == t)
            return true;
        if (auto agg = dynamic_cast<AggregateType*>(ty.ty)) 
            if (agg->HasMemberOfType(t))
                return true;
    }
    return false;
}
std::function<llvm::Constant*(llvm::Module*)> AggregateType::GetRTTI() {
    // NOPE
    // If we have a Clang type, then use it for compat.
    auto clangty = GetClangType(*analyzer.GetAggregateTU());
    if (GetVtableLayout().layout.empty()) {
        if (auto clangty = GetClangType(*analyzer.GetAggregateTU())) {
            return [clangty, this](llvm::Module* module) {
                return analyzer.GetAggregateTU()->GetItaniumRTTI(*clangty, module);
            };
        }
    }
    // Else, nope. Aggregate types are user-defined types with no bases by default.
    return [this](llvm::Module* module) {
        std::stringstream stream;
        stream << "struct.__" << this;
        if (auto existing = module->getGlobalVariable(stream.str())) {
            return existing;
        }
        auto mangledname = GetGlobalString(stream.str(), module);
        auto vtable_name_of_rtti = "_ZTVN10__cxxabiv117__class_type_infoE";
        auto vtable = module->getOrInsertGlobal(vtable_name_of_rtti, llvm::Type::getInt8PtrTy(module->getContext()));
        vtable = llvm::ConstantExpr::getInBoundsGetElementPtr(vtable, llvm::ConstantInt::get(llvm::Type::getInt8Ty(module->getContext()), 2));
        vtable = llvm::ConstantExpr::getBitCast(vtable, llvm::Type::getInt8PtrTy(module->getContext()));
        std::vector<llvm::Constant*> inits = { vtable, mangledname };
        auto ty = llvm::ConstantStruct::getTypeForElements(inits);
        auto rtti = new llvm::GlobalVariable(*module, ty, true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, llvm::ConstantStruct::get(ty, inits), stream.str());
        return rtti;
    };
}

MemberLocation AggregateType::GetLocation(unsigned i) {
    if (GetLayout().Offsets[i].FieldIndex)
        return LLVMFieldIndex{ *GetLayout().Offsets[i].FieldIndex };
    return EmptyBaseOffset{ GetLayout().Offsets[i].ByteOffset };
}
bool AggregateType::IsTriviallyDestructible() {
    return GetLayout().triviallydestructible;
}
bool AggregateType::IsTriviallyCopyConstructible() {
    return GetLayout().triviallycopyconstructible;
}