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
#include <clang/AST/ASTContext.h>
#include <clang/AST/AST.h>
#include <clang/AST/RecordLayout.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

AggregateType::Properties& AggregateType::GetProperties() {
    if (!props) props = Properties(this);
    return *props;
}
AggregateType::Properties::Properties(AggregateType* self)
    : copyassignable(true)
    , copyconstructible(true)
    , moveassignable(true)
    , moveconstructible(true)
    , constant(true)
{
    auto UpdatePropertiesForMember = [this, self](Type* D) {
        constant &= D->GetConstantContext() == D;
        moveconstructible &= D->IsMoveConstructible(GetAccessSpecifier(self, D));
        copyconstructible &= D->IsCopyConstructible(GetAccessSpecifier(self, D));
        moveassignable &= D->IsMoveAssignable(GetAccessSpecifier(self, D));
        copyassignable &= D->IsCopyAssignable(GetAccessSpecifier(self, D));
        triviallycopyconstructible &= D->IsTriviallyCopyConstructible();
        triviallydestructible &= D->IsTriviallyDestructible();
    };
    for (auto base : self->GetBases())
        UpdatePropertiesForMember(base);
    for (auto mem : self->GetMembers())
        UpdatePropertiesForMember(mem);
}
AggregateType::Layout::Layout(Layout&& other)
    : align(other.align)
    , allocsize(other.allocsize)
    , EmptyTypes(std::move(other.EmptyTypes))
    , Offsets(std::move(other.Offsets))
    , codegen(std::move(other.codegen))
    , AlignOverrideError(std::move(other.AlignOverrideError))
    , SizeOverrideError(std::move(other.SizeOverrideError))
    , hasvptr(other.hasvptr)
{}

AggregateType::Layout& AggregateType::Layout::operator=(Layout&& other) {
    allocsize = other.allocsize;
    align = other.align;
    EmptyTypes = std::move(other.EmptyTypes);
    Offsets = std::move(other.Offsets);
    codegen = std::move(other.codegen);
    AlignOverrideError = std::move(other.AlignOverrideError);
    SizeOverrideError = std::move(other.SizeOverrideError);
    hasvptr = other.hasvptr;
    return *this;
}

AggregateType::Layout::Layout(AggregateType* agg, Wide::Semantic::Analyzer& a)
: allocsize(0)
, align(1)
{
    // Originally meeting Itanium but no longer required.
    auto bases_size = agg->GetBases().size();
    auto members_size = agg->GetMembers().size();
    Offsets.resize(bases_size + members_size);

    // http://refspecs.linuxbase.org/cxxabi-1.83.html#class-types
    // 2.4.1
    // Initialize variables, including primary base.
    auto& sizec = allocsize;
    auto& alignc = align;
    auto dsizec = 0;
    unsigned PrimaryBaseNum;
    
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

    // 2.4.2
    // Clause 1 does not apply because we don't support virtual bases.
    // Clause 2 (second argument not required w/o virtual base support
    auto layout_nonemptybase = [&](Type* D, bool base, std::size_t offset, std::size_t num) {
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
        }
        // Update the empty map for our new layout.
        auto DEmpties = D->GetEmptyLayout();
        for (auto newempty : DEmpties) {
            for (auto empty : newempty.second) 
                EmptyTypes[newempty.first + offset].insert(empty);
        }
        // If we inserted any padding, increase the field number to account for the padding array
        Offsets[num] = { D, offset };
        // update sizeof(C) to max (sizeof(C), offset(D)+sizeof(D))
        sizec = std::max(sizec, offset + D->size());
        // update dsize(C) to offset(D)+sizeof(D),
        dsizec = offset + D->size();
        // align(C) to max(align(C), align(D)).
        alignc = std::max(alignc, D->alignment());
    };

    // Clause 3
    auto layout_emptybase = [&](Type* D, std::size_t num) {
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
        if (size_override->first < sizec)
            SizeOverrideError = Wide::Memory::MakeUnique<Semantic::SpecificError<SizeOverrideTooSmall>>(a, size_override->second, "The size override was smaller than the required size");
        else
            sizec = size_override->first;
    }
    auto align_override = agg->AlignOverride();
    if (align_override) {
        if (align_override->first < alignc)
            AlignOverrideError = Wide::Memory::MakeUnique<Semantic::SpecificError<AlignOverrideTooSmall>>(a, align_override->second, "The alignment override was smaller than the required alignment.");
        else
            alignc = align_override->first;
    }

    // Round sizeof(C) up to a non-zero multiple of align(C). If C is a POD, but not a POD for the purpose of layout, set nvsize(C) = sizeof(C). 
    if (sizec % alignc != 0 || sizec == 0)
        sizec += alignc - (sizec % alignc);
}
std::size_t AggregateType::size() {
    return GetLayout().allocsize;
}
std::size_t AggregateType::alignment() {
    return GetLayout().align;
}

bool AggregateType::IsMoveAssignable(Parse::Access access) {
    return GetProperties().moveassignable;
}
bool AggregateType::IsMoveConstructible(Parse::Access access) {
    return GetProperties().moveconstructible;
}
bool AggregateType::IsCopyAssignable(Parse::Access access) {
    return GetProperties().copyassignable;
}
bool AggregateType::IsCopyConstructible(Parse::Access access) {
    return GetProperties().copyconstructible;
}
std::shared_ptr<Expression> AggregateType::AccessVirtualPointer(Expression::InstanceKey key, std::shared_ptr<Expression> self) {
    if (GetPrimaryBase()) return Type::GetVirtualPointer(key, Type::AccessBase(key, std::move(self), GetPrimaryBase()));
    if (!HasDeclaredDynamicFunctions()) return nullptr;
    auto vptrty = analyzer.GetPointerType(GetVirtualPointerType());
    assert(self->GetType(key)->IsReference(this) || dynamic_cast<PointerType*>(self->GetType(key))->GetPointee() == this);
    return CreatePrimGlobal(Range::Elements(self), analyzer.GetLvalueType(vptrty), [=](CodegenContext& con) {
       return con.CreateStructGEP(self->GetValue(con), 0);
    });
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
    if (llvmtype) return llvmtype;
    auto llvmname = "struct." + self->GetLLVMTypeName();
    if (auto structty = module->getTypeByName(llvmname)) {
        // Hook up the field indices before we go.
        for (auto&& offset : self->GetLayout().Offsets) {
            if (!offset.ty->IsEmpty()) {
                offset.FieldIndex = self->analyzer.GetDataLayout().getStructLayout(structty)->getElementContainingOffset(offset.ByteOffset);
            }
        }
        return llvmtype = structty;
    }
    auto ty = llvm::StructType::create(module->getContext(), llvmname);
    llvmtype = ty;
    std::vector<llvm::Type*> llvmtypes;

    //if (self->IsEmpty() && !self->GetLayout().hasvptr && !self->SizeOverride() && !self->AlignOverride()) {
    //    llvmtypes.push_back(self->analyzer.GetIntegralType(8, true)->GetLLVMType(module));
    //    ty->setBody(llvmtypes, false);
    //    return ty;
    //}

    unsigned curroffset = 0;
    if (self->GetLayout().hasvptr) {
        llvmtypes.push_back(self->analyzer.GetPointerType(self->GetVirtualPointerType())->GetLLVMType(module));
        curroffset = self->analyzer.GetDataLayout().getPointerSize();
    }

    for (auto&& llvmmember : self->GetLayout().Offsets) {
        if (!llvmmember.ty->IsEmpty()) {
            if (curroffset != llvmmember.ByteOffset) {
                llvmtypes.push_back(llvm::ArrayType::get(llvm::Type::getInt8Ty(module->getContext()), llvmmember.ByteOffset - curroffset));
            }
            llvmmember.FieldIndex = llvmtypes.size();
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

std::shared_ptr<Expression> AggregateType::PrimitiveAccessMember(std::shared_ptr<Expression> source, unsigned num) {
    return CreateResultExpression(Range::Elements(source), [=](Expression::InstanceKey f) {
        auto source_ty = source->GetType(f);
        auto root_ty = num < GetBases().size()
            ? GetBases()[num]
            : GetMembers()[num - GetBases().size()];

        auto result_ty = Semantic::CollapseType(source->GetType(f), root_ty);
        return CreatePrimGlobal(Range::Elements(source), result_ty, [=](CodegenContext& con) -> llvm::Value* {
            auto src = source->GetValue(con);
            // Is there even a field index? This may not be true for EBCO
            auto&& offsets = GetLayout().Offsets;
            if (!offsets[num].FieldIndex) {
                if (src->getType()->isPointerTy()) {
                    // Move it by that offset to get a unique pointer
                    auto self_as_int8ptr = con->CreatePointerCast(src, con.GetInt8PtrTy());
                    auto offset_self = con->CreateConstGEP1_32(self_as_int8ptr, offsets[num].ByteOffset);
                    return con->CreatePointerCast(offset_self, result_ty->GetLLVMType(con));
                }
                // Downcasting to an EBCO'd value -> just produce a value.
                return llvm::UndefValue::get(GetLayout().Offsets[num].ty->GetLLVMType(con));
            }
            auto fieldindex = *offsets[num].FieldIndex;
            auto obj = src->getType()->isPointerTy() ? con.CreateStructGEP(src, fieldindex) : con->CreateExtractValue(src, { fieldindex });
            return Semantic::CollapseMember(source->GetType(f), { obj, offsets[num].ty }, con);
        });
    });
}
AggregateType::AggregateType(Analyzer& a) : Type(a) {
    CopyAssignmentName = analyzer.GetUniqueFunctionName();
    CopyConstructorName = analyzer.GetUniqueFunctionName();
    MoveAssignmentName = analyzer.GetUniqueFunctionName();
    MoveConstructorName = analyzer.GetUniqueFunctionName();
    DefaultConstructorName = analyzer.GetUniqueFunctionName();
    DestructorName = analyzer.GetUniqueFunctionName();
}

OverloadSet* AggregateType::CreateOperatorOverloadSet(Parse::OperatorName type, Parse::Access access, OperatorAccess kind) {
    if (type != Parse::OperatorName{ &Lexer::TokenTypes::Assignment }) return Type::CreateOperatorOverloadSet(type, access, kind);
    if (access != Parse::Access::Public) return AccessMember(type, Parse::Access::Public, kind);
    return CreateAssignmentOperatorOverloadSet({ true, true });
}

std::function<llvm::Function*(llvm::Module*)> AggregateType::CreateDestructorFunction() {
    // May only run once according to the laws of Type::GetDestructorFunction
    // but let's be careful anyway.
    if (destructors.empty()) {
        std::vector<std::function<void(CodegenContext&)>> destructors;
        auto destructor_self = CreatePrimGlobal(Range::Empty(), analyzer.GetLvalueType(this), [this](CodegenContext& con) {
            return con->GetInsertBlock()->getParent()->arg_begin();
        });
        int i = 0;
        for (auto member : GetBases()) {
            destructors.push_back(member->BuildDestructorCall(Expression::NoInstance(), PrimitiveAccessMember(destructor_self, i), { this, Lexer::Range(std::make_shared<std::string>("AggregateDestructor")) }, true));
            ++i;
        }
        for (auto member : GetMembers()) {
            destructors.push_back(member->BuildDestructorCall(Expression::NoInstance(), PrimitiveAccessMember(destructor_self, i), { this, Lexer::Range(std::make_shared<std::string>("AggregateDestructor")) }, true));
            ++i;
        }
        this->destructors = destructors;
    }
    return[this](llvm::Module* module) {
        auto functy = llvm::FunctionType::get(llvm::Type::getVoidTy(module->getContext()), { analyzer.GetLvalueType(this)->GetLLVMType(module) }, false);
        auto desfunc = llvm::Function::Create(functy, llvm::GlobalValue::LinkageTypes::ExternalLinkage, DestructorName, module);
        CodegenContext::EmitFunctionBody(desfunc, { analyzer.GetLvalueType(this) }, [this](CodegenContext& newcon) {
            for (auto des : destructors)
                des(newcon);
            newcon->CreateRetVoid();
        });
        return desfunc;
    };
}
std::function<void(CodegenContext&)> AggregateType::BuildDestruction(Expression::InstanceKey, std::shared_ptr<Expression> self, Context c, bool devirtualize) {
    auto destructor = GetDestructorFunction();
    return [this, destructor, self](CodegenContext& con) {
        con->CreateCall(destructor(con), self->GetValue(con));
    };
}
OverloadSet* AggregateType::GetCopyAssignmentOperator() {
    return MaybeCreateSet(CopyAssignmentOperator,
        [this](Type* ty, unsigned i) { return ty->IsCopyAssignable(GetAccessSpecifier(this, ty)); },
        [this] {
            return MakeResolvable([this](Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr < Expression > {
                if (GetLayout().Offsets.size() == 0)
                    return BuildChain(std::move(args[0]), std::move(args[1]));
                CreateCopyAssignmentExpressions();
        
                auto functy = analyzer.GetFunctionType(analyzer.GetLvalueType(this), { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) }, false);
                return Type::BuildCall(key, CreatePrimGlobal(Range::Container(args), functy, [this](CodegenContext& con) { EmitCopyAssignmentOperator(con); return CopyAssignmentFunction; }), { args[0], args[1] }, c);
            }, { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) });
        }
    );
}
OverloadSet* AggregateType::GetMoveAssignmentOperator() {
    return MaybeCreateSet(MoveAssignmentOperator, 
        [this](Type* ty, unsigned i) { return ty->IsMoveAssignable(GetAccessSpecifier(this, ty)); },
        [this] {
            return MakeResolvable([this](Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr < Expression > {
                if (GetLayout().Offsets.size() == 0)
                    return BuildChain(std::move(args[0]), std::move(args[1]));
                CreateMoveAssignmentExpressions();
            
                auto functy = analyzer.GetFunctionType(analyzer.GetLvalueType(this), { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) }, false);
                return Type::BuildCall(key, CreatePrimGlobal(Range::Container(args), functy, [this](CodegenContext& con) { EmitMoveAssignmentOperator(con); return MoveAssignmentFunction; }), { args[0], args[1] }, c);
            }, { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) });            
        }
    );
}
OverloadSet* AggregateType::GetMoveConstructor() {
    return MaybeCreateSet(MoveConstructor, 
        [this](Type* ty, unsigned i) { return ty->IsMoveConstructible(GetAccessSpecifier(this, ty)); },
        [this] {
            return MakeResolvable([this](Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr < Expression > {  
                CreateMoveConstructorInitializers();
            
                auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) }, false);
                return Type::BuildCall(key, CreatePrimGlobal(Range::Container(args), functy, [this](CodegenContext& con) { EmitMoveConstructor(con); return MoveConstructorFunction; }), { args[0], args[1] }, c);
            }, { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) });
        }
    );
}
OverloadSet* AggregateType::GetCopyConstructor() {
    return MaybeCreateSet(CopyConstructor,
        [this](Type* ty, unsigned i) { return ty->IsCopyConstructible(GetAccessSpecifier(this, ty)); },
        [this] {
            return MakeResolvable([this](Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {     
                CreateCopyConstructorInitializers();
        
                auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) }, false);
                return Type::BuildCall(key, CreatePrimGlobal(Range::Container(args), functy, [this](CodegenContext& con) { EmitCopyConstructor(con); return CopyConstructorFunction; }), { args[0], args[1] }, c);
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
        for (auto mem : GetBases())
            if (!member_should(mem, i++))
                return false;
        for (auto mem : GetMembers())
            if (!member_should(mem, i++))
                return false;
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
    return CreatePrimGlobal(Range::Empty(), analyzer.GetLvalueType(this), [&func](CodegenContext& con) {
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
    // assert(inits.size() == GetMembers().size() + GetBases().size() + 1);
    for (auto init : inits)
        init->GetValue(con);
    //for (unsigned i = 0; i < GetMembers().size() + GetBases().size(); ++i)
    //    inits[i]->GetValue(con);
    inits.back()->GetValue(con);
    con.DestroyAll(false);
    con->CreateRetVoid();
}
std::shared_ptr<Expression> AggregateType::GetMemberFromThis(std::shared_ptr<Expression> self, std::function<unsigned()> offset, Type* member) {
    auto result = member;
    return CreatePrimUnOp(self, result, [offset, result](llvm::Value* val, CodegenContext& con) {
        auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
        self = con->CreateConstGEP1_32(self, offset());
        return con->CreatePointerCast(self, result->GetLLVMType(con));
    });
}
std::vector<std::shared_ptr<Expression>> AggregateType::GetAssignmentOpExpressions(std::shared_ptr<Expression> self, Context c, std::function<std::shared_ptr<Expression>(unsigned)> initializers) {
    std::vector<std::shared_ptr<Expression>> exprs;
    for (std::size_t i = 0; i < GetBases().size(); ++i) {
        auto lhs = GetMemberFromThis(self, [this, i] { return GetLayout().Offsets[i].ByteOffset; }, analyzer.GetLvalueType(GetBases()[i]));
        exprs.push_back(Type::BuildBinaryExpression(Expression::NoInstance(), lhs, initializers(i), &Lexer::TokenTypes::Assignment, c));
    }
    for (std::size_t i = 0; i < GetMembers().size(); ++i) {
        auto lhs = GetMemberFromThis(self, [this, i] { return GetLayout().Offsets[i + GetBases().size()].ByteOffset; }, analyzer.GetLvalueType(GetMembers()[i]));
        exprs.push_back(Type::BuildBinaryExpression(Expression::NoInstance(), lhs, initializers(i), &Lexer::TokenTypes::Assignment, c));
    }
    return exprs;
}
std::vector<std::shared_ptr<Expression>> AggregateType::GetConstructorInitializers(std::shared_ptr<Expression> self, Context c, std::function<std::vector<std::shared_ptr<Expression>>(unsigned)> initializers) {
    std::vector<std::shared_ptr<Expression>> exprs;
    auto MemberConstructionAccess = [](Type* member, Lexer::Range where, std::shared_ptr<Expression> Construction, std::shared_ptr<Expression> memexpr) {
        std::function<void(CodegenContext&)> destructor; 
        if (!member->IsTriviallyDestructible())
            destructor = member->BuildDestructorCall(Expression::NoInstance(), memexpr, { member, where }, true);
        return CreatePrimGlobal(Range::Elements(Construction), Construction->GetType(Expression::NoInstance()), [=](CodegenContext& con) {
            auto val = Construction->GetValue(con);
            if (destructor)
                con.AddExceptionOnlyDestructor(destructor);
            return val;
        });
    };
    for (std::size_t i = 0; i < GetBases().size(); ++i) {
        auto lhs = GetMemberFromThis(self, [this, i] { return GetLayout().Offsets[i].ByteOffset; }, analyzer.GetLvalueType(GetBases()[i]));
        auto init = Type::BuildInplaceConstruction(Expression::NoInstance(), lhs, initializers(i), c);
        exprs.push_back(MemberConstructionAccess(GetBases()[i], c.where, init, lhs));
    }
    for (std::size_t i = 0; i < GetMembers().size(); ++i) {
        auto lhs = GetMemberFromThis(self, [this, i] { return GetLayout().Offsets[i + GetBases().size()].ByteOffset; }, analyzer.GetLvalueType(GetMembers()[i]));
        auto init = Type::BuildInplaceConstruction(Expression::NoInstance(), lhs, initializers(i), c);
        exprs.push_back(MemberConstructionAccess(GetMembers()[i], c.where, init, lhs));
    }
    exprs.push_back(Type::SetVirtualPointers(Expression::NoInstance(), self));
    return exprs;
}
OverloadSet* AggregateType::GetDefaultConstructor() {
    // Not created yet.
    auto is_default_constructible = [this](Type* ty, unsigned i) {
        if (i < GetBases().size())
            return ty->GetConstructorOverloadSet(GetAccessSpecifier(this, ty))->Resolve({ analyzer.GetLvalueType(ty) }, this);
        auto inits = GetDefaultInitializerForMember(i - GetBases().size());
        std::vector<Type*> types = { analyzer.GetLvalueType(ty) };
        for (auto init : inits) types.push_back(init->GetType(nullptr));
        return ty->GetConstructorOverloadSet(GetAccessSpecifier(this, ty))->Resolve(types, this);
    };
    // If we shouldn't generate, give back an empty set and set the cache for future use.
    auto create = [this] {
        return MakeResolvable([this](Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr < Expression > {
            CreateDefaultConstructorInitializers();

            auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this) }, false);
            return Type::BuildCall(key, CreatePrimGlobal(Range::Container(args), functy, [this](CodegenContext& con) { EmitDefaultConstructor(con); return DefaultConstructorFunction; }), { args[0] }, c);
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
        stream << "struct.__" << this << "_rtti";
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
    // Not used until code-gen time.
    if (GetLayout().Offsets[i].FieldIndex)
        return LLVMFieldIndex{ *GetLayout().Offsets[i].FieldIndex };
    return EmptyBaseOffset{ GetLayout().Offsets[i].ByteOffset };
}
bool AggregateType::IsTriviallyDestructible() {
    return GetProperties().triviallydestructible;
}
bool AggregateType::IsTriviallyCopyConstructible() {
    return GetProperties().triviallycopyconstructible;
}
std::string AggregateType::GetSpecialFunctionName(SpecialFunction f) {
    switch (f) {
    case SpecialFunction::CopyAssignmentOperator: return CopyAssignmentName;
    case SpecialFunction::MoveAssignmentOperator: return MoveAssignmentName;
    case SpecialFunction::CopyConstructor: return CopyConstructorName;
    case SpecialFunction::MoveConstructor: return MoveConstructorName;
    case SpecialFunction::DefaultConstructor: return DefaultConstructorName;
    case SpecialFunction::Destructor: return DestructorName;
    }
}

void AggregateType::CreateMoveConstructorInitializers() {
    if (!MoveConstructorInitializers) {
        auto rhs = CreatePrimGlobal(Range::Empty(), analyzer.GetLvalueType(this), [this](CodegenContext& con) {
            return ++MoveConstructorFunction->arg_begin();
        });
        auto self = GetConstructorSelf(MoveConstructorFunction);
        auto initializers = GetConstructorInitializers(self, { this, Wide::Lexer::Range(std::make_shared<std::string>("Analyzer Internal Function")) }, [this, rhs](unsigned i) -> std::vector<std::shared_ptr<Expression>> {
            return{ PrimitiveAccessMember(rhs, i) };
        });
        MoveConstructorInitializers = std::vector<std::shared_ptr<Expression>> {
            Type::SetVirtualPointers(Expression::NoInstance(), self),
            CreatePrimGlobal(Wide::Range::Container(initializers), analyzer, [=](CodegenContext& con) {
                if (AlwaysKeepInMemory(con)) {
                    for (auto&& init : initializers)
                        init->GetValue(con);
                } else
                    con->CreateStore(con->CreateLoad(rhs->GetValue(con)), self->GetValue(con));
            })
        };
    }

}
void AggregateType::CreateCopyConstructorInitializers() {
    if (!CopyConstructorInitializers) {
        auto rhs = CreatePrimGlobal(Range::Empty(), analyzer.GetLvalueType(this), [this](CodegenContext& con) {
            return ++CopyConstructorFunction->arg_begin();
        });
        if (IsTriviallyCopyConstructible()) {
            CopyConstructorInitializers = std::vector<std::shared_ptr<Expression>> {
                Wide::Memory::MakeUnique<ImplicitStoreExpr>(GetConstructorSelf(CopyConstructorFunction), Wide::Memory::MakeUnique<ImplicitLoadExpr>(rhs))
            };
        } else
            CopyConstructorInitializers = GetConstructorInitializers(GetConstructorSelf(CopyConstructorFunction), { this, Wide::Lexer::Range(std::make_shared<std::string>("Analyzer Internal Function")) }, [this, rhs](unsigned i) -> std::vector<std::shared_ptr<Expression>> {
                return{ PrimitiveAccessMember(rhs, i) };
            });
    }
}
void AggregateType::CreateMoveAssignmentExpressions() {
    if (!MoveAssignmentExpressions) {
        auto rhs = CreatePrimGlobal(Range::Empty(), analyzer.GetLvalueType(this), [this](CodegenContext& con) {
            return ++MoveAssignmentFunction->arg_begin();
        });
        MoveAssignmentExpressions = GetAssignmentOpExpressions(GetConstructorSelf(MoveAssignmentFunction), { this, Wide::Lexer::Range(std::make_shared<std::string>("Analyzer Internal Function")) }, [this, rhs](unsigned i) -> std::shared_ptr<Expression> {
            return PrimitiveAccessMember(rhs, i);
        });
    }
}
void AggregateType::CreateCopyAssignmentExpressions() {
    if (!CopyAssignmentExpressions) {
        auto rhs = CreatePrimGlobal(Range::Empty(), analyzer.GetLvalueType(this), [this](CodegenContext& con) {
            return ++CopyAssignmentFunction->arg_begin();
        });
        CopyAssignmentExpressions = GetAssignmentOpExpressions(GetConstructorSelf(CopyAssignmentFunction), { this, Wide::Lexer::Range(std::make_shared<std::string>("Analyzer Internal Function")) }, [this, rhs](unsigned i) -> std::shared_ptr<Expression> {
            return PrimitiveAccessMember(rhs, i);
        });
    }
}
void AggregateType::CreateDefaultConstructorInitializers() {
    if (!DefaultConstructorInitializers)
        DefaultConstructorInitializers = GetConstructorInitializers(GetConstructorSelf(DefaultConstructorFunction), { this, Wide::Lexer::Range(std::make_shared<std::string>("Analyzer Internal Function")) }, [this](unsigned i) -> std::vector < std::shared_ptr<Expression> > {
        if (i < GetBases().size())
            return{};
        return GetDefaultInitializerForMember(i - GetBases().size());
    });
}
void AggregateType::EmitMoveConstructor(llvm::Module* mod) {
    if (!MoveConstructorFunction && MoveConstructorInitializers) {
        auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) }, false);
        MoveConstructorFunction = llvm::Function::Create(llvm::cast<llvm::FunctionType>(functy->GetLLVMType(mod)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, MoveConstructorName, mod);
        CodegenContext::EmitFunctionBody(MoveConstructorFunction, functy->GetArguments(), [this](CodegenContext& con) {
            EmitConstructor(con, *MoveConstructorInitializers);
        });
    }
}
void AggregateType::EmitCopyConstructor(llvm::Module* mod) {
    if (!CopyConstructorFunction && CopyConstructorInitializers) {
        auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) }, false);
        CopyConstructorFunction = llvm::Function::Create(llvm::cast<llvm::FunctionType>(functy->GetLLVMType(mod)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, CopyConstructorName, mod);
        CodegenContext::EmitFunctionBody(CopyConstructorFunction, functy->GetArguments(), [this](CodegenContext& con) {
            EmitConstructor(con, *CopyConstructorInitializers);
        });
    }
}
void AggregateType::EmitDefaultConstructor(llvm::Module* mod) {
    if (!DefaultConstructorFunction && DefaultConstructorInitializers) {
        auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this) }, false);
        DefaultConstructorFunction = llvm::Function::Create(llvm::cast<llvm::FunctionType>(functy->GetLLVMType(mod)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, DefaultConstructorName, mod);
        CodegenContext::EmitFunctionBody(DefaultConstructorFunction, functy->GetArguments(), [this](CodegenContext& con) {
            EmitConstructor(con, *DefaultConstructorInitializers);
        });
    }
}
void AggregateType::EmitMoveAssignmentOperator(llvm::Module* mod) {
    if (!MoveAssignmentFunction && MoveAssignmentExpressions) {
        auto functy = analyzer.GetFunctionType(analyzer.GetLvalueType(this), { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) }, false);
        MoveAssignmentFunction = llvm::Function::Create(llvm::cast<llvm::FunctionType>(functy->GetLLVMType(mod)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, MoveAssignmentName, mod);
        CodegenContext::EmitFunctionBody(MoveAssignmentFunction, functy->GetArguments(), [this](CodegenContext& con) {
            EmitAssignmentOperator(con, *MoveAssignmentExpressions);
        });
    }
}
void AggregateType::EmitCopyAssignmentOperator(llvm::Module* mod) {
    if (!CopyAssignmentFunction && CopyAssignmentExpressions) {
        auto functy = analyzer.GetFunctionType(analyzer.GetLvalueType(this), { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) }, false);
        CopyAssignmentFunction = llvm::Function::Create(llvm::cast<llvm::FunctionType>(functy->GetLLVMType(mod)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, CopyAssignmentName, mod);
        CodegenContext::EmitFunctionBody(CopyAssignmentFunction, functy->GetArguments(), [this](CodegenContext& con) {
            EmitAssignmentOperator(con, *CopyAssignmentExpressions);
        });
    }
}

void AggregateType::PrepareExportedFunctions(AggregateAssignmentOperators ops, AggregateConstructors cons, bool des) {
    if (ops.copy_operator) CreateCopyConstructorInitializers();
    if (ops.move_operator) CreateMoveConstructorInitializers();
    if (cons.default_constructor) CreateDefaultConstructorInitializers();
    if (cons.move_constructor) CreateMoveConstructorInitializers();
    if (cons.copy_constructor) CreateCopyConstructorInitializers();
}
void AggregateType::Export(llvm::Module* mod) {
    EmitMoveConstructor(mod);
    EmitCopyConstructor(mod);
    EmitMoveAssignmentOperator(mod);
    EmitCopyAssignmentOperator(mod);
    EmitDefaultConstructor(mod);
    if (!destructors.empty()) GetDestructorFunction()(mod);
}
std::string AggregateType::GetLLVMTypeName() {
    std::stringstream strstr;
    strstr << this;
    return "__" + strstr.str();
}