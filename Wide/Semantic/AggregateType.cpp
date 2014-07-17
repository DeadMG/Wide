#include <Wide/Semantic/AggregateType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/PointerType.h>
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

bool AggregateType::IsMoveAssignable(Lexer::Access access) {
    return GetLayout().moveassignable;
}
bool AggregateType::IsMoveConstructible(Lexer::Access access) {
    return GetLayout().moveconstructible;
}
bool AggregateType::IsCopyAssignable(Lexer::Access access) {
    return GetLayout().copyassignable;
}
bool AggregateType::IsCopyConstructible(Lexer::Access access) {
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
        llvm::Value* ComputeValue(CodegenContext& con) override final { return con->CreateStructGEP(self->GetValue(con), 0); }
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
            auto obj = src->getType()->isPointerTy() ? con->CreateStructGEP(src, fieldindex) : con->CreateExtractValue(src, { fieldindex });
            if ((source->GetType()->IsReference() && offsets[num].ty->IsReference()) || (source->GetType()->IsComplexType() && !offsets[num].ty->IsComplexType()))
                return con->CreateLoad(obj);
            return obj;
        }
    };
    assert(self);
    return Wide::Memory::MakeUnique<FieldAccess>(std::move(self), this, num);
}
AggregateType::AggregateType(Analyzer& a) : Type(a) {}

llvm::Function* AggregateType::CreateDestructorFunction(llvm::Module* module) {    
    CodegenContext newcon;
    newcon.module = module;
    auto functy = llvm::FunctionType::get(llvm::Type::getVoidTy(newcon), { analyzer.GetLvalueType(this)->GetLLVMType(newcon) }, false);
    auto DestructorFunction = llvm::Function::Create(functy, llvm::GlobalValue::LinkageTypes::InternalLinkage, "", newcon);
    this->DestructorFunction = DestructorFunction;
    llvm::BasicBlock* allocas = llvm::BasicBlock::Create(newcon, "allocas", DestructorFunction);
    llvm::BasicBlock* entries = llvm::BasicBlock::Create(newcon, "entry", DestructorFunction);
    llvm::IRBuilder<> allocabuilder(allocas);
    allocabuilder.SetInsertPoint(allocabuilder.CreateBr(entries));
    llvm::IRBuilder<> insertbuilder(entries);
    newcon.alloca_builder = &allocabuilder;
    newcon.insert_builder = &insertbuilder;
    for (auto des : destructors)
        des(newcon);
    newcon->CreateRetVoid();
    if (llvm::verifyFunction(*DestructorFunction, llvm::VerifierFailureAction::PrintMessageAction))
        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
    return DestructorFunction;
}
std::function<void(CodegenContext&)> AggregateType::BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize) {
    return [this, self](CodegenContext& con) {
        con->CreateCall(GetDestructorFunction(con), self->GetValue(con));
    };
}

OverloadSet* AggregateType::CreateOperatorOverloadSet(Lexer::TokenType type, Lexer::Access access) {
    if (type != &Lexer::TokenTypes::Assignment)
        return analyzer.GetOverloadSet();
    if (access != Lexer::Access::Public)
        return AccessMember(type, Lexer::Access::Public);

    // Similar principle to constructor
    std::function<Type*(Type*)> modify;
    auto createoperator = [this, &modify] {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(this));
        types.push_back(modify(this));
        return MakeResolvable([this, modify](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {
            if (GetLayout().Offsets.size() == 0)
                return BuildChain(std::move(args[1]), std::move(args[0]));
            
            std::vector<std::shared_ptr<Expression>> exprs;
            // For every type, call the operator
            for (std::size_t i = 0; i < GetLayout().Offsets.size(); ++i) {
                auto type = GetLayout().Offsets[i].ty;
                auto lhs = PrimitiveAccessMember(args[0], i);
                std::vector<Type*> types;
                types.push_back(analyzer.GetLvalueType(type));
                types.push_back(modify(type));
                auto overset = type->AccessMember(&Lexer::TokenTypes::Assignment, GetAccessSpecifier(this, type));
                auto callable = overset->Resolve(types, this);
                if (!callable)
                    assert(false); // dafuq, the appropriate assignable was set but we're not assignable?
                exprs.push_back(callable->Call({ std::move(lhs), PrimitiveAccessMember(args[1], i) }, c));
            }

            struct AggregateOperator : Expression {
                AggregateType* this_type;
                std::shared_ptr<Expression> self;
                std::shared_ptr<Expression> arg;
                std::vector<std::shared_ptr<Expression>> exprs;
                Type* GetType() override final { return self->GetType(); }
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    if (!this_type->IsComplexType() && !this_type->GetLayout().hasvptr) {
                        // Screw calling all the operators, just store.
                        // This makes debugging the IR output a lot easier.
                        auto val = arg->GetType()->IsReference() ? con->CreateLoad(arg->GetValue(con)) : arg->GetValue(con);
                        con->CreateStore(val, self->GetValue(con));
                        return self->GetValue(con);
                    }
                    for (auto&& arg : exprs)
                        arg->GetValue(con);
                    return self->GetValue(con);
                }
                AggregateOperator(AggregateType* s, std::shared_ptr<Expression> expr, std::shared_ptr<Expression> arg, std::vector<std::shared_ptr<Expression>> exprs)
                    : self(std::move(expr)), arg(std::move(arg)), exprs(std::move(exprs)), this_type(s) {}
            };
            return Wide::Memory::MakeUnique<AggregateOperator>(this, std::move(args[0]), std::move(args[1]), std::move(exprs));
        }, types);
    };

    std::unordered_set<OverloadResolvable*> set;
    if (GetLayout().moveassignable) {
        modify = [this](Type* t) { return analyzer.GetRvalueType(t); };
        MoveAssignmentOperator = createoperator();
        set.insert(MoveAssignmentOperator.get());
    }
    if (GetLayout().copyassignable) {
        modify = [this](Type* t) { return analyzer.GetLvalueType(t); };
        CopyAssignmentOperator = createoperator();
        set.insert(CopyAssignmentOperator.get());
    }
    return analyzer.GetOverloadSet(set);
}

OverloadSet* AggregateType::CreateNondefaultConstructorOverloadSet() {
    // First, move/copy
    std::function<Type*(Type*)> modify;
    auto createconstructor = [this, &modify]() -> std::unique_ptr<OverloadResolvable> {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(this));
        types.push_back(modify(this));
        return MakeResolvable([this, modify](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression>  {
            if (GetLayout().Offsets.size() == 0)
                return BuildChain(std::move(args[1]), std::move(args[0]));

            // For every type, call the appropriate constructor.
            std::vector<std::shared_ptr<Expression>> exprs;
            for (std::size_t i = 0; i < GetLayout().Offsets.size(); ++i) {
                auto type = GetLayout().Offsets[i].ty;
                // If it's a reference we need to not collapse it- otherwise use PrimAccessMember to take care of ECBO and such matters.
                auto lhs = type->IsReference()
                    ? CreatePrimUnOp(args[0], analyzer.GetLvalueType(type), [this, i](llvm::Value* val, CodegenContext& con) {
                          return con->CreateStructGEP(val, *GetLayout().Offsets[i].FieldIndex);
                      })
                    : PrimitiveAccessMember(args[0], i);
                auto rhs = PrimitiveAccessMember(args[1], i);
                auto set = type->GetConstructorOverloadSet(GetAccessSpecifier(this, type));
                std::vector<Type*> types;
                types.push_back(analyzer.GetLvalueType(type));
                types.push_back(modify(type));
                auto callable = set->Resolve(types, this);
                assert(callable);// Should not be generated if this fails!
                exprs.push_back(callable->Call({ std::move(lhs), std::move(rhs) }, c));
            }
            exprs.push_back(Type::SetVirtualPointers(args[0]));
            struct AggregateConstructor : Expression {
                AggregateType* this_type;
                std::shared_ptr<Expression> self;
                std::shared_ptr<Expression> arg;
                std::vector<std::shared_ptr<Expression>> exprs;
                Type* GetType() override final { return self->GetType(); }
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    if (!this_type->IsComplexType() && !this_type->GetLayout().hasvptr) {
                        // Screw calling all the operators, just store.
                        // This makes debugging the IR output a lot easier.
                        auto val = arg->GetType()->IsReference() ? con->CreateLoad(arg->GetValue(con)) : arg->GetValue(con);
                        con->CreateStore(val, self->GetValue(con));
                        return self->GetValue(con);
                    }
                    for (auto&& arg : exprs)
                        arg->GetValue(con);
                    return self->GetValue(con);
                }
                AggregateConstructor(AggregateType* s, std::shared_ptr<Expression> expr, std::shared_ptr<Expression> arg, std::vector<std::shared_ptr<Expression>> exprs)
                    : this_type(s), self(std::move(expr)), arg(std::move(arg)), exprs(std::move(exprs)) {}
            };
            return Wide::Memory::MakeUnique<AggregateConstructor>(this, std::move(args[0]), std::move(args[1]), std::move(exprs));
        }, types);
    };

    std::unordered_set<OverloadResolvable*> set;
    if (GetLayout().moveconstructible) {
        modify = [this](Type* t) -> Type* { if (t->IsReference()) return t; return analyzer.GetRvalueType(t); };
        if (!MoveConstructor) MoveConstructor = createconstructor();
        set.insert(MoveConstructor.get());
    }
    if (GetLayout().copyconstructible) {
        modify = [this](Type* t) -> Type* { if (t->IsReference()) return t; return analyzer.GetLvalueType(t); };
        if (!CopyConstructor) CopyConstructor = createconstructor();
        set.insert(CopyConstructor.get());
    }
    return analyzer.GetOverloadSet(set);
}
OverloadSet* AggregateType::CreateConstructorOverloadSet(Lexer::Access access) {
    // Use create instead of get because get will return the more derived class's constructors and that is not what we want.
    if (access != Lexer::Access::Public) return AggregateType::CreateConstructorOverloadSet(Lexer::Access::Public);
    std::unordered_set<OverloadResolvable*> set;
    // Then default.
    auto is_default_constructible = [this] {
        for (auto mem : GetLayout().Offsets) {
            std::vector<Type*> types;
            types.push_back(analyzer.GetLvalueType(mem.ty));
            if (!mem.ty->GetConstructorOverloadSet(GetAccessSpecifier(this, mem.ty))->Resolve(types, this))
                return false;
        }
        return true;
    };
    if (is_default_constructible()) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(this));
        if (!DefaultConstructor)
            DefaultConstructor = MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {
                if (GetLayout().Offsets.size() == 0)
                    return std::move(args[0]);
                
                // For every type, call the appropriate constructor.
                // No references possible so just use PrimAccessMember
                std::vector<std::shared_ptr<Expression>> exprs;
                for (std::size_t i = 0; i < GetLayout().Offsets.size(); ++i) {
                    auto type = GetLayout().Offsets[i].ty;
                    auto lhs = PrimitiveAccessMember(args[0], i);
                    auto set = type->GetConstructorOverloadSet(GetAccessSpecifier(this, type));
                    std::vector<Type*> types;
                    types.push_back(analyzer.GetLvalueType(type));
                    auto callable = set->Resolve(types, this);
                    assert(callable);// Should not be generated if this fails!
                    exprs.push_back(callable->Call({ std::move(lhs) }, c));
                }
                exprs.push_back(Type::SetVirtualPointers(args[0]));
                struct AggregateConstructor : Expression {
                    std::shared_ptr<Expression> self;
                    std::vector<std::shared_ptr<Expression>> exprs;
                    Type* GetType() override final { return self->GetType(); }
                    llvm::Value* ComputeValue(CodegenContext& con) override final {
                        for (auto&& arg : exprs)
                            arg->GetValue(con);
                        return self->GetValue(con);
                    }
                    AggregateConstructor(std::shared_ptr<Expression> expr, std::vector<std::shared_ptr<Expression>> exprs)
                        : self(std::move(expr)), exprs(std::move(exprs)) {}
                };
                return Wide::Memory::MakeUnique<AggregateConstructor>(std::move(args[0]), std::move(exprs));
            }, types);
        set.insert(DefaultConstructor.get());
    }
    return analyzer.GetOverloadSet(analyzer.GetOverloadSet(set), AggregateType::CreateNondefaultConstructorOverloadSet());
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
llvm::Constant* AggregateType::GetRTTI(llvm::Module* module) {
    // NOPE
    // If we have a Clang type, then use it for compat.
    auto clangty = GetClangType(*analyzer.GetAggregateTU());
    if (clangty) {
        return analyzer.GetAggregateTU()->GetItaniumRTTI(*clangty, module);
    }
    // Else, nope. Aggregate types are user-defined types with no bases by default.
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