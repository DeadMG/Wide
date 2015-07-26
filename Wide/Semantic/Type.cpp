#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/IntegralType.h>
#include <sstream>

using namespace Wide;
using namespace Semantic;

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/AST.h>
#pragma warning(pop)

OverloadSet* Type::CreateADLOverloadSet(Parse::OperatorName what, Parse::Access access) {
    if (IsReference())
        return Decay()->CreateADLOverloadSet(what, access);
    auto context = GetContext();
    if (!context)
        return analyzer.GetOverloadSet();
    return GetContext()->AccessMember(what, access, OperatorAccess::Explicit);
}

OverloadSet* Type::CreateOperatorOverloadSet(Parse::OperatorName type, Parse::Access access, OperatorAccess implicit) {
    if (IsReference())
        return Decay()->AccessMember(type, access, implicit);
    return analyzer.GetOverloadSet();
}

bool Type::HasVirtualDestructor() {
    for (auto base : GetBases())
        if (base->HasVirtualDestructor())
            return true;
    return false;
}

bool Type::IsAbstract() {
    return false;
}

bool Type::IsFinal() { 
    return true; 
}

bool Type::IsReference(Type* to) { 
    return false; 
}
bool Type::IsNonstaticMemberContext() {
    return false;
}
bool Type::IsLookupContext() {
    return false;
}

bool Type::IsReference() { 
    return false; 
}

Type* Type::Decay() { 
    return this; 
}

Type* Type::GetContext() {
    return analyzer.GetGlobalModule();
}

bool Type::AlwaysKeepInMemory(llvm::Module* mod) {
    return !IsTriviallyDestructible() || !IsTriviallyCopyConstructible();
}

bool Type::IsTriviallyDestructible() {
    return true;
}

bool Type::IsTriviallyCopyConstructible() {
    return true;
}

Wide::Util::optional<clang::QualType> Type::GetClangType(ClangTU& TU) {
    return Wide::Util::none;
}

// I doubt that converting a T* to bool is super-duper slow.
#pragma warning(disable : 4800)
bool Type::IsMoveConstructible(Parse::Access access) {
    auto set = GetConstructorOverloadSet(access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetRvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsCopyConstructible(Parse::Access access) {
    // A Clang struct with a deleted copy constructor can be both noncomplex and non-copyable at the same time.
    auto set = GetConstructorOverloadSet(access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetLvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsMoveAssignable(Parse::Access access) {
    auto set = AccessMember({ &Lexer::TokenTypes::Assignment }, access, OperatorAccess::Implicit);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetRvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsCopyAssignable(Parse::Access access) {
    auto set = AccessMember({ &Lexer::TokenTypes::Assignment }, access, OperatorAccess::Implicit);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetLvalueType(this));
    return set->Resolve(std::move(arguments), this);
}
bool Type::IsFirstASecond(Type* source, Type* target, Type* context) {
    if (source == target) return true;
    if (target == target->analyzer.GetRvalueType(source)) return true;
    if (source == target->analyzer.GetLvalueType(target) && target->IsCopyConstructible(GetAccessSpecifier(context, target))) return true;
    if (source == target->analyzer.GetRvalueType(target) && target->IsMoveConstructible(GetAccessSpecifier(context, target))) return true;
    if (source->IsSourceATarget(source, target, context)) return true;
    if (target->IsSourceATarget(source, target, context)) return true;
    return false;
}

bool Type::IsConstant() {
    return false;
}
unsigned Type::GetOffsetToBase(Type* base) {
    assert(IsDerivedFrom(base) == InheritanceRelationship::UnambiguouslyDerived);
    for (auto pair : GetBasesAndOffsets()) {
        if (pair.first == base)
            return pair.second;
        if (pair.first->IsDerivedFrom(base) == InheritanceRelationship::UnambiguouslyDerived)
            return pair.second + pair.first->GetOffsetToBase(base);
    }
    assert(false && "WTF.");
    return 0;
}

bool Type::IsEmpty() {
    return false;
}

Type* Type::GetVirtualPointerType() {
    return nullptr;
}

std::vector<Type*> Type::GetBases() { 
    std::vector<Type*> bases;
    for (auto base : GetBasesAndOffsets())
        bases.push_back(base.first);
    return bases;
}

Type* Type::GetPrimaryBase() {
    return nullptr;
}

std::unordered_map<unsigned, std::unordered_set<Type*>> Type::GetEmptyLayout() {
    std::unordered_map<unsigned, std::unordered_set<Type*>> out;
    // Create this mapping by merging our bases'
    // AggregateType has to get this during layout and overrides this function.
    for (auto pair : GetBasesAndOffsets()) {
        auto base_mapping = pair.first->GetEmptyLayout();
        for (auto offset : base_mapping) {
            for (auto empty : offset.second) {
                assert(out[offset.first + pair.second].find(empty) == out[offset.first + pair.second].end());
                out[offset.first + pair.second].insert(empty);
            }
        }
    }
    return out;
}

Type::VTableLayout Type::ComputePrimaryVTableLayout() { 
    return VTableLayout(); 
}

std::vector<std::pair<Type*, unsigned>> Type::GetBasesAndOffsets() { 
    return {}; 
}

std::shared_ptr<Expression> Type::GetVirtualPointer(Expression::InstanceKey key, std::shared_ptr<Expression> self) {
    auto selfty = self->GetType(key)->Decay();
    if (!selfty->GetVirtualPointerType())
        return nullptr;
    return self->GetType(key)->Decay()->AccessVirtualPointer(key, self);
}

std::shared_ptr<Expression> Type::AccessStaticMember(std::string name, Context c) {
    return nullptr;
}

std::shared_ptr<Expression> Type::AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> t, std::string name, Context c) {
    if (IsReference())
        return Decay()->AccessNamedMember(key, std::move(t), name, c);
    return nullptr;
}
std::shared_ptr<Expression> Type::AccessMember(Expression::InstanceKey key, std::shared_ptr<Expression> t, Parse::Name name, Context c) {
    auto ty = t->GetType(key);
    if (auto string = boost::get<std::string>(&name))
        return ty->Decay()->AccessNamedMember(key, t, *string, c);
    auto set = ty->Decay()->AccessMember(boost::get<Parse::OperatorName>(name), GetAccessSpecifier(c.from, ty), OperatorAccess::Explicit);
    if (ty->Decay()->IsLookupContext())
        return ty->analyzer.GetOverloadSet(set, ty->analyzer.GetOverloadSet())->BuildValueConstruction(key, {}, c);
    return ty->analyzer.GetOverloadSet(set, ty->analyzer.GetOverloadSet(), ty)->BuildValueConstruction(key, { t }, c);
}
std::shared_ptr<Expression> Type::AccessStaticMember(Parse::Name name, Context c) {
    if (auto string = boost::get<std::string>(&name))
        return AccessStaticMember(*string, c);
    return analyzer.GetOverloadSet(AccessMember(boost::get<Parse::OperatorName>(name), GetAccessSpecifier(c.from, this), OperatorAccess::Explicit), analyzer.GetOverloadSet())->BuildValueConstruction(Expression::NoInstance(), {}, c);
}

std::shared_ptr<Expression> Type::BuildCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
   auto ty = val->GetType(key);
   if (ty->IsReference())
       ty = ty->Decay();
   return ty->ConstructCall(key, val, std::move(args), c);
}
std::shared_ptr<Expression> Type::ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    args.insert(args.begin(), std::move(val));
    auto set = args[0]->GetType(key)->AccessMember({ &Lexer::TokenTypes::OpenBracket, &Lexer::TokenTypes::CloseBracket }, GetAccessSpecifier(c.from, this), OperatorAccess::Implicit);
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType(key));
    auto call = set->Resolve(types, c.from);
    if (!call) return set->IssueResolutionError(types, c);
    return call->Call(key, std::move(args), c);
}
std::function<void(CodegenContext&)> Type::BuildDestruction(Expression::InstanceKey, std::shared_ptr<Expression> self, Context c, bool devirtualize) {
    return [](CodegenContext&) {};
}
std::shared_ptr<Expression> Type::AccessVirtualPointer(Expression::InstanceKey key, std::shared_ptr<Expression> self) {
    return nullptr;
}

std::shared_ptr<Expression> Type::BuildBooleanConversion(Expression::InstanceKey key, std::shared_ptr<Expression> val, Context c) {
    auto set = val->GetType(key)->Decay()->AccessMember({ &Lexer::TokenTypes::QuestionMark }, GetAccessSpecifier(c.from, val->GetType(key)), OperatorAccess::Implicit);
    std::vector<std::shared_ptr<Expression>> args;
    args.push_back(std::move(val));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType(key));
    auto call = set->Resolve(types, c.from);
    if (!call) return set->IssueResolutionError(types, c);
    return call->Call(key, std::move(args), c);
}

std::function<void(CodegenContext&)> Type::BuildDestructorCall(Expression::InstanceKey key, std::shared_ptr<Expression> self, Context c, bool devirtualize) {
    return BuildDestruction(key, self, c, devirtualize);
}

OverloadSet* Type::GetConstructorOverloadSet(Parse::Access access) {
    if (ConstructorOverloadSets.find(access) == ConstructorOverloadSets.end())
        ConstructorOverloadSets[access] = CreateConstructorOverloadSet(access);
    return ConstructorOverloadSets[access];
}

OverloadSet* Type::PerformADL(Parse::OperatorName what, Parse::Access access) {
    if (IsReference())
        return Decay()->PerformADL(what, access);
    if (ADLResults.find(access) != ADLResults.end())
        if (ADLResults[access].find(what) != ADLResults[access].end())
            return ADLResults[access][what];
    return ADLResults[access][what] = CreateADLOverloadSet(what, access);
}

OverloadSet* Type::AccessMember(Parse::OperatorName type, Parse::Access access, OperatorAccess kind) {
    if (OperatorOverloadSets.find(access) == OperatorOverloadSets.end()
     || OperatorOverloadSets[access].find(type) == OperatorOverloadSets[access].end()
     || OperatorOverloadSets[access][type].find(kind) == OperatorOverloadSets[access][type].end())
        OperatorOverloadSets[access][type][kind] = CreateOperatorOverloadSet(type, access, kind);
    return OperatorOverloadSets[access][type][kind];
}

std::shared_ptr<Expression> Type::BuildInplaceConstruction(Expression::InstanceKey key, std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    exprs.insert(exprs.begin(), std::move(self));
    auto selfty = exprs[0]->GetType(key);
    std::vector<Type*> types;
    for (auto&& arg : exprs)
        types.push_back(arg->GetType(key));
    auto conset = selfty->Decay()->GetConstructorOverloadSet(GetAccessSpecifier(c.from, selfty));
    auto callable = conset->Resolve(types, c.from);
    if (!callable)
        return conset->IssueResolutionError(types, c);
    return callable->Call(key, std::move(exprs), c);
}

struct ValueConstruction : Expression {
    ValueConstruction(Expression::InstanceKey key, Type* self, std::vector<std::shared_ptr<Expression>> args, Context c)
    : self(self) {
        no_args = args.size() == 0;
        temporary = CreateTemporary(self, c);
        if (args.size() == 1)
            single_arg = args[0];
        InplaceConstruction = Type::BuildInplaceConstruction(key, temporary, std::move(args), c);
        destructor = self->BuildDestructorCall(key, temporary, c, true);
        //if (auto implicitstore = dynamic_cast<ImplicitStoreExpr*>(InplaceConstruction.get())) {
        //    if (implicitstore->mem == temporary)
        //        assert(implicitstore->val->GetType() == self);
        //}
    }
    std::shared_ptr<Expression> temporary;
    std::function<void(CodegenContext&)> destructor;
    std::shared_ptr<Expression> InplaceConstruction;
    std::shared_ptr<Expression> single_arg;
    bool no_args = false;
    Type* self;
    bool IsConstantExpression(Expression::InstanceKey key) override final {
        if (self->IsConstant() && no_args) return true;
        if (single_arg) {
            auto otherty = single_arg->GetType(key);
            if (single_arg->GetType(key)->Decay() == self->Decay()) {
                if (single_arg->GetType(key) == self && single_arg->IsConstant(key))
                    return true;
                if (single_arg->GetType(key)->IsReference(self) && single_arg->IsConstant(key))
                    return true;
            }
        }
        return InplaceConstruction->IsConstant(key);
    }
    Type* GetType(Expression::InstanceKey key) override final {
        return self;
    }
    llvm::Value* ComputeValue(CodegenContext& con) override final {
        if (!self->AlwaysKeepInMemory(con)) {
            if (auto implicitstore = dynamic_cast<ImplicitStoreExpr*>(InplaceConstruction.get())) {
                if (implicitstore->mem == temporary) {
                    assert(implicitstore->val->GetType(con.func) == self);
                    return implicitstore->val->GetValue(con);
                }
            }
            if (self->IsConstant() && no_args && self->GetVirtualPointerType() == nullptr)
                return llvm::UndefValue::get(self->GetLLVMType(con));
            if (single_arg) {
                auto otherty = single_arg->GetType(con.func);
                if (single_arg->GetType(con.func)->Decay() == self->Decay()) {
                    if (single_arg->GetType(con.func) == self) {
                        return single_arg->GetValue(con);
                    }
                    if (single_arg->GetType(con.func)->IsReference(self)) {
                        return con->CreateLoad(single_arg->GetValue(con));
                    }
                }
            }
        }
        InplaceConstruction->GetValue(con);
        if (self->AlwaysKeepInMemory(con)) {
            if (!self->IsTriviallyDestructible())
                con.AddDestructor(destructor);
            return temporary->GetValue(con);
        }
        return con->CreateLoad(temporary->GetValue(con));
    }
};
std::shared_ptr<Expression> Type::BuildValueConstruction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    return Wide::Memory::MakeUnique<ValueConstruction>(key, this, std::move(exprs), c);
}

std::shared_ptr<Expression> Type::BuildRvalueConstruction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) {
    auto temporary = CreateTemporary(this, c);
    auto InplaceConstruction = Type::BuildInplaceConstruction(key, temporary, std::move(args), c);
    auto destructor = BuildDestructorCall(key, temporary, c, true);
    return CreatePrimGlobal(Range::Elements(InplaceConstruction), analyzer.GetRvalueType(this), [=](CodegenContext& con) {
        InplaceConstruction->GetValue(con);
        if (!IsTriviallyDestructible())
            con.AddDestructor(destructor);
        return temporary->GetValue(con);
    });
}

Type::InheritanceRelationship Type::IsDerivedFrom(Type* base) {
    InheritanceRelationship result = InheritanceRelationship::NotDerived;
    for (auto ourbase : GetBases()) {
        if (ourbase == base) {
            if (result == InheritanceRelationship::NotDerived)
                result = InheritanceRelationship::UnambiguouslyDerived;
            else if (result == InheritanceRelationship::UnambiguouslyDerived)
                result = InheritanceRelationship::AmbiguouslyDerived;
            continue;
        }
        auto subresult = ourbase->IsDerivedFrom(base);
        if (subresult == InheritanceRelationship::AmbiguouslyDerived)
            result = InheritanceRelationship::AmbiguouslyDerived;
        if (subresult == InheritanceRelationship::UnambiguouslyDerived) {
            if (result == InheritanceRelationship::NotDerived)
                result = subresult;
            else if (result == InheritanceRelationship::UnambiguouslyDerived)
                result = InheritanceRelationship::AmbiguouslyDerived;
        }
    }
    return result;
}

Type::VTableLayout Type::GetVtableLayout() {
    if (!VtableLayout)
        VtableLayout = ComputeVTableLayout();
    return *VtableLayout;
}

Type::VTableLayout Type::GetPrimaryVTable() {
    if (!PrimaryVtableLayout)
        PrimaryVtableLayout = ComputePrimaryVTableLayout();
    return *PrimaryVtableLayout;
}

std::shared_ptr<Expression> Type::AccessBase(Expression::InstanceKey key, std::shared_ptr<Expression> self, Type* other) {
    auto selfty = self->GetType(key)->Decay();
    assert(selfty->IsDerivedFrom(other) == InheritanceRelationship::UnambiguouslyDerived);
    assert(!other->IsReference());
    Type* result = self->GetType(key)->IsReference()
        ? IsLvalueType(self->GetType(key)) ? selfty->analyzer.GetLvalueType(other) : selfty->analyzer.GetRvalueType(other)
        : dynamic_cast<PointerType*>(self->GetType(key))
        ? selfty->analyzer.GetPointerType(other)
        : other;
    return CreatePrimGlobal(Range::Elements(self), result, [=](CodegenContext& con) -> llvm::Value* {
        // Must succeed because we know we're unambiguously derived.
        unsigned offset;
        for (auto base : selfty->GetBasesAndOffsets()) {
            if (base.first == other) {
                offset = base.second;
            }
        }
        auto value = self->GetValue(con);
        if (!result->IsReference() && !dynamic_cast<PointerType*>(result)) {
            // It's a value, and we're a value, so just create struct gep.
            if (result->IsEmpty())
                return llvm::Constant::getNullValue(result->GetLLVMType(con));
            // If it's not empty it must have a field index.
            auto layout = result->analyzer.GetDataLayout().getStructLayout(llvm::dyn_cast<llvm::StructType>(result->GetLLVMType(con)));
            return con->CreateExtractValue(value, { layout->getElementContainingOffset(offset) });
        }
        if (offset == 0)
            return con->CreatePointerCast(value, result->GetLLVMType(con));
        if (result->IsReference()) {
            // Just do the offset because null references are illegal.
            value = con->CreatePointerCast(value, con.GetInt8PtrTy());
            value = con->CreateConstGEP1_32(value, offset);
            return con->CreatePointerCast(value, result->GetLLVMType(con));
        }
        // It could be null. Branch here
        auto source_block = con->GetInsertBlock();
        llvm::BasicBlock* not_null_bb = llvm::BasicBlock::Create(con, "not_null_bb", con->GetInsertBlock()->getParent());
        llvm::BasicBlock* continue_bb = llvm::BasicBlock::Create(con, "continue_bb", con->GetInsertBlock()->getParent());
        con->CreateCondBr(con->CreateIsNull(value), continue_bb, not_null_bb);
        con->SetInsertPoint(not_null_bb);
        value = con->CreatePointerCast(value, con.GetInt8PtrTy());
        value = con->CreateConstGEP1_32(value, offset);
        auto offset_ptr = con->CreatePointerCast(value, result->GetLLVMType(con));
        con->CreateBr(continue_bb);
        con->SetInsertPoint(continue_bb);
        auto phi = con->CreatePHI(result->GetLLVMType(con), 2);
        phi->addIncoming(llvm::Constant::getNullValue(result->GetLLVMType(con)), source_block);
        phi->addIncoming(offset_ptr, not_null_bb);
        return phi;
    });
}

bool Type::InheritsFromAtOffsetZero(Type* other) {
    auto bases = GetBasesAndOffsets();
    if (bases.empty()) return false;
    for (auto base : bases) {
        if (base.second == 0) {
            if (base.first == other)
                return true;
            return base.first->InheritsFromAtOffsetZero(other);
        }
    }
    return false;
}

std::shared_ptr<Expression> Type::BuildUnaryExpression(Expression::InstanceKey key, std::shared_ptr<Expression> self, Lexer::TokenType type, Context c) {
    auto selfty = self->GetType(key)->Decay();
    auto opset = selfty->AccessMember({ type }, GetAccessSpecifier(c.from, selfty), OperatorAccess::Implicit);
    auto callable = opset->Resolve({ self->GetType(key) }, c.from);
    if (!callable) {
        if (type == &Lexer::TokenTypes::Negate) {
            if (auto self_as_bool = Type::BuildBooleanConversion(key, self, c))
                return Type::BuildUnaryExpression(key, self_as_bool, &Lexer::TokenTypes::Negate, c);
        }
        return opset->IssueResolutionError({ self->GetType(key) }, c);
    }
    return callable->Call(key, { std::move(self) }, c);
}

std::shared_ptr<Expression> Type::BuildBinaryExpression(Expression::InstanceKey key, std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Lexer::TokenType type, Context c) {
    auto&& analyzer = lhs->GetType(key)->analyzer;
    auto lhsaccess = GetAccessSpecifier(c.from, lhs->GetType(key));
    auto rhsaccess = GetAccessSpecifier(c.from, rhs->GetType(key));
    auto lhsadl = lhs->GetType(key)->PerformADL({ type }, lhsaccess);
    auto rhsadl = rhs->GetType(key)->PerformADL({ type }, rhsaccess);
    auto lhsmember = lhs->GetType(key)->AccessMember({ type }, lhsaccess, OperatorAccess::Implicit);
    auto finalset = analyzer.GetOverloadSet(lhsadl, analyzer.GetOverloadSet(rhsadl, lhsmember));
    std::vector<Type*> arguments;
    arguments.push_back(lhs->GetType(key));
    arguments.push_back(rhs->GetType(key));
    if (auto call = finalset->Resolve(arguments, c.from)) {
        return call->Call(key, { std::move(lhs), std::move(rhs) }, c);
    }
    
    if (type == &Lexer::TokenTypes::EqCmp) {
        // a == b if (!(a < b) && !(b > a)).
        auto a_lt_b = Type::BuildBinaryExpression(key, lhs, rhs, &Lexer::TokenTypes::LT, c);
        auto b_lt_a = Type::BuildBinaryExpression(key, std::move(rhs), std::move(lhs), &Lexer::TokenTypes::LT, c);
        auto not_a_lt_b = Type::BuildUnaryExpression(key, std::move(a_lt_b), &Lexer::TokenTypes::Negate, c);
        auto not_b_lt_a = Type::BuildUnaryExpression(key, std::move(b_lt_a), &Lexer::TokenTypes::Negate, c);
        return Type::BuildBinaryExpression(key, std::move(not_a_lt_b), std::move(not_b_lt_a), &Lexer::TokenTypes::And, c);
    } else if (type == &Lexer::TokenTypes::LTE) {
        auto subexpr = Type::BuildBinaryExpression(key, std::move(rhs), std::move(lhs), &Lexer::TokenTypes::LT, c);
        return Type::BuildUnaryExpression(key, std::move(subexpr), &Lexer::TokenTypes::Negate, c);
    } else if (type == &Lexer::TokenTypes::GT) {
        return Type::BuildBinaryExpression(key, std::move(rhs), std::move(lhs), &Lexer::TokenTypes::LT, c);
    } else if (type == &Lexer::TokenTypes::GTE) {
        auto subexpr = Type::BuildBinaryExpression(key, std::move(lhs), std::move(rhs), &Lexer::TokenTypes::LT, c);
        return Type::BuildUnaryExpression(key, std::move(subexpr), &Lexer::TokenTypes::Negate, c);
    } else if (type == &Lexer::TokenTypes::NotEqCmp) {
        auto subexpr = Type::BuildBinaryExpression(key, std::move(lhs), std::move(rhs), &Lexer::TokenTypes::EqCmp, c);
        return Type::BuildUnaryExpression(key, std::move(subexpr), &Lexer::TokenTypes::Negate, c);
    }
    
    return finalset->IssueResolutionError(arguments, c);
}

OverloadSet* PrimitiveType::CreateConstructorOverloadSet(Parse::Access access) {
    if (access != Parse::Access::Public) return GetConstructorOverloadSet(Parse::Access::Public);
    auto construct_from_ref = [](Expression::InstanceKey key,  std::vector<std::shared_ptr<Expression>> args, Context c) {
        return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), BuildValue(std::move(args[1])));
    };
    CopyConstructor = MakeResolvable(construct_from_ref, { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) });
    MoveConstructor = MakeResolvable(construct_from_ref, { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) });
    ValueConstructor = MakeResolvable([](Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) {
        return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), std::move(args[1]));
    }, { analyzer.GetLvalueType(this), this });
    std::unordered_set<OverloadResolvable*> callables;
    callables.insert(CopyConstructor.get());
    callables.insert(MoveConstructor.get());
    callables.insert(ValueConstructor.get());
    return analyzer.GetOverloadSet(callables);
}

OverloadSet* PrimitiveType::CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access, OperatorAccess kind) {
    if (what != Parse::OperatorName({ &Lexer::TokenTypes::Assignment }))
        return Type::CreateOperatorOverloadSet(what, access, kind);
    
    AssignmentOperator = MakeResolvable([](Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) {
        return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), std::move(args[1]));
    }, { analyzer.GetLvalueType(this), this });
    return analyzer.GetOverloadSet(AssignmentOperator.get());
}

std::size_t MetaType::size() { return 1; }
std::size_t MetaType::alignment() { return 1; }

llvm::Type* MetaType::GetLLVMType(llvm::Module* module) {
    std::stringstream typenam;
    typenam << this;
    auto nam = typenam.str();
    if (module->getTypeByName(nam))
        return module->getTypeByName(nam);
    return llvm::StructType::create(nam, llvm::IntegerType::getInt8Ty(module->getContext()), nullptr);
}
#pragma warning(disable : 4800)

bool MetaType::IsConstant() {
    return true;
}

OverloadSet* MetaType::CreateConstructorOverloadSet(Parse::Access access) {
    if (access != Parse::Access::Public) return GetConstructorOverloadSet(Parse::Access::Public);
    DefaultConstructor = MakeResolvable([](Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) {
        return std::move(args[0]);
    }, { analyzer.GetLvalueType(this) });
    return analyzer.GetOverloadSet(PrimitiveType::CreateConstructorOverloadSet(access), analyzer.GetOverloadSet(DefaultConstructor.get()));
}

std::shared_ptr<Expression> Callable::Call(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) {
    return CallFunction(key, AdjustArguments(key, std::move(args), c), c);
}

std::vector<std::shared_ptr<Expression>> Semantic::AdjustArgumentsForTypes(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, std::vector<Type*> types, Context c) {
    assert(args.size() == types.size());
    std::vector<std::shared_ptr<Expression>> out;
    for (std::size_t i = 0; i < types.size(); ++i) {
        auto argty = args[i]->GetType(key);
        if (argty == types[i]) {
            out.push_back(std::move(args[i]));
            continue;
        }
        assert(Type::IsFirstASecond(argty, types[i], c.from));
        out.push_back(types[i]->BuildValueConstruction(key, { std::move(args[i]) }, c));
    }
    return out;
}

struct Resolvable : OverloadResolvable, Callable {
    std::vector<Type*> types;
    std::function<std::shared_ptr<Expression>(Expression::InstanceKey, std::vector<std::shared_ptr<Expression>>, Context)> action;

    Resolvable(std::vector<Type*> tys, std::function<std::shared_ptr<Expression>(Expression::InstanceKey, std::vector<std::shared_ptr<Expression>>, Context)> func)
        : types(std::move(tys)), action(std::move(func)) {}

    Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
        if (args.size() != types.size()) return Util::none;
        for (unsigned num = 0; num < types.size(); ++num) {
            auto t = args[num];
            if (!Type::IsFirstASecond(t, types[num], source))
                return Util::none;
        }
        return types;
    }
    Callable* GetCallableForResolution(std::vector<Type*> tys, Type*, Analyzer& a) override final {
        assert(types == tys);
        return this;
    }
    std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        assert(types.size() == args.size());
        for (std::size_t num = 0; num < types.size(); ++num)
            assert(types[num] == args[num]->GetType(key));
        return action(key, std::move(args), c);
    }
    std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        return AdjustArgumentsForTypes(key, std::move(args), types, c);
    }
};

std::unique_ptr<OverloadResolvable> Semantic::MakeResolvable(std::function<std::shared_ptr<Expression>(Expression::InstanceKey, std::vector<std::shared_ptr<Expression>>, Context)> f, std::vector<Type*> types) {
    return Wide::Memory::MakeUnique<Resolvable>(types, std::move(f));
}

OverloadSet* TupleInitializable::CreateConstructorOverloadSet(Parse::Access access) {
    if (access != Parse::Access::Public) return TupleInitializable::CreateConstructorOverloadSet(Parse::Access::Public);
    struct TupleConstructorType : public OverloadResolvable, Callable {
        TupleConstructorType(TupleInitializable* p) : self(p) {}
        TupleInitializable* self;
        Callable* GetCallableForResolution(std::vector<Type*>, Type* source, Analyzer& a) override final { return this; }
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
            if (args.size() != 2) return Util::none;
            if (args[0] != a.GetLvalueType(self->GetSelfAsType())) return Util::none;
            auto tup = dynamic_cast<TupleType*>(args[1]->Decay());
            if (!tup) return Util::none;
            // We accept it if it's constructible.
            auto types = self->GetTypesForTuple();
            if (!types) return Util::none;
            if (tup->GetMembers().size() != types->size()) return Util::none;
            for (unsigned i = 0; i < types->size(); ++i) {
                auto member = (*types)[i];
                auto sourcety = Semantic::CollapseType(args[1], tup->GetMembers()[i]);
                if (!member->GetConstructorOverloadSet(GetAccessSpecifier(self->GetSelfAsType(), member))->Resolve({ member->analyzer.GetLvalueType(member), sourcety }, source))
                    return Util::none;
            }
            return args;
        }
        std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
        std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            // We should already have properly-typed memory at 0.
            // and the tuple at 1.
            // This stuff won't be called unless we are valid
            auto tupty = dynamic_cast<TupleType*>(args[1]->GetType(key)->Decay());
            assert(tupty);
            auto members_opt = self->GetTypesForTuple();
            assert(members_opt);
            auto members = *members_opt;

            if (members.size() == 0)
                return std::move(args[0]);

            std::vector<std::shared_ptr<Expression>> initializers;
            for (std::size_t i = 0; i < members.size(); ++i) {
                auto memory = self->PrimitiveAccessMember(args[0], i);
                auto argument = tupty->PrimitiveAccessMember(args[1], i);
                initializers.push_back(Type::BuildInplaceConstruction(key, std::move(memory), { std::move(argument) }, c));
            }

            return CreatePrimGlobal(Range::Container(initializers), self->GetSelfAsType()->analyzer.GetLvalueType(self->GetSelfAsType()), [=](CodegenContext& con) {
                for (auto&& init : initializers)
                    init->GetValue(con);
                return args[0]->GetValue(con);
            });
        }
    };
    if (!TupleConstructor) TupleConstructor = Wide::Memory::MakeUnique<TupleConstructorType>(this);
    return GetSelfAsType()->analyzer.GetOverloadSet(TupleConstructor.get());
}

std::shared_ptr<Expression> Type::CreateVTable(std::vector<std::pair<Type*, unsigned>> path) {
    if (path.size() >= 2) {
        if (path[path.size() - 2].first->GetPrimaryBase() == path.back().first)
            return path.front().first->GetVTablePointer(std::vector<std::pair<Type*, unsigned>>(path.begin(), path.end() - 1));
    }
    std::vector<std::shared_ptr<Expression>> entries;
    unsigned offset_total = 0;
    for (auto base : path)
        offset_total += base.second;
    for (auto func : GetVtableLayout().layout) {
        if (auto mem = boost::get<VTableLayout::SpecialMember>(&func.func)) {
            if (*mem == VTableLayout::SpecialMember::OffsetToTop) {
                auto size = analyzer.GetDataLayout().getPointerSizeInBits();
                // LLVM interprets this as signed regardless of the fact that it's technically unsigned.
                if (path.front().first != this)
                    entries.push_back(Wide::Memory::MakeUnique<Integer>(llvm::APInt(size, (uint64_t)-offset_total, true), analyzer));
                else
                    entries.push_back(Wide::Memory::MakeUnique<Integer>(llvm::APInt(size, (uint64_t)0, true), analyzer));
                continue;
            }
            if (*mem == VTableLayout::SpecialMember::RTTIPointer) {
                auto type = path.front().first;
                auto rtti = type->GetRTTI();
                entries.push_back(CreatePrimGlobal(Range::Empty(), type->analyzer.GetPointerType(type->analyzer.GetIntegralType(8, true)), [=](CodegenContext& con) {
                    return con->CreateBitCast(rtti(con), con.GetInt8PtrTy());
                }));
                continue;
            }
        }
        bool found = false;
        for (auto more_derived : path) {
            auto expr = more_derived.first->VirtualEntryFor(func);
            auto thunk = CreatePrimGlobal(Range::Empty(), expr.first, [=](CodegenContext& con) {
                return expr.second(con);
            });
            if (expr.first) {
                if (func.type != expr.first)
                    entries.push_back(func.type->CreateThunkFrom(Expression::NoInstance(), thunk, this));
                else
                    entries.push_back(thunk);
                found = true;
                break;
            }
        }
        if (boost::get<VTableLayout::VirtualFunction>(&func.func) && boost::get<VTableLayout::VirtualFunction>(func.func).abstract) {
            // It's possible we didn't find a more derived impl because we're abstract
            // and we're creating our own constructor vtable here.
            // Just use a null pointer instead of the Itanium ABI function for now.
            if (!found)
                entries.push_back(nullptr);
        } else
            assert(found);
    }
    std::string vtableid;
    for (auto type : path) {
        std::stringstream strstream;
        strstream << type.first;
        vtableid += strstream.str() + "__";
    }
    return CreatePrimGlobal(Range::Container(entries), analyzer.GetPointerType(GetVirtualPointerType()), [=](CodegenContext& con) {
        auto vfuncty = GetVirtualPointerType()->GetLLVMType(con);
        std::vector<llvm::Constant*> constants;
        for (auto&& init : entries) {
            if (!init)
                constants.push_back(llvm::Constant::getNullValue(vfuncty));
            else {
                auto val = init->GetValue(con);
                auto con = llvm::dyn_cast<llvm::Constant>(val);
                constants.push_back(con);
            }
        }
        auto vtablety = llvm::ConstantStruct::getTypeForElements(constants);
        auto vtable = llvm::ConstantStruct::get(vtablety, constants);
        auto global = llvm::dyn_cast<llvm::GlobalVariable>(con.module->getOrInsertGlobal(vtableid, vtablety));
        global->setInitializer(vtable);
        global->setLinkage(llvm::GlobalValue::LinkageTypes::InternalLinkage);
        global->setConstant(true);
        return con->CreatePointerCast(con.CreateStructGEP(global, GetVtableLayout().offset), analyzer.GetPointerType(GetVirtualPointerType())->GetLLVMType(con));
    });
}      

std::shared_ptr<Expression> Type::GetVTablePointer(std::vector<std::pair<Type*, unsigned>> path) {
    if (ComputedVTables.find(path) == ComputedVTables.end())
        ComputedVTables[path] = CreateVTable(path);
    return ComputedVTables.at(path);
}

std::shared_ptr<Expression> Type::SetVirtualPointers(Expression::InstanceKey key, std::shared_ptr<Expression> self) {
    return Type::SetVirtualPointers(key, std::move(self), {});
}

std::shared_ptr<Expression> Type::SetVirtualPointers(Expression::InstanceKey key, std::shared_ptr<Expression> self, std::vector<std::pair<Type*, unsigned>> path) {
    // Set the base vptrs first, because some Clang types share vtables with their base.
    auto selfty = self->GetType(key)->Decay();
    std::vector<std::shared_ptr<Expression>> BasePointerInitializers;
    for (auto base : selfty->GetBasesAndOffsets()) {
        path.push_back(std::make_pair(selfty, base.second));
        BasePointerInitializers.push_back(Type::SetVirtualPointers(key, Type::AccessBase(key, self, base.first), path));
        path.pop_back();
    }
    // If we actually have a vptr, and we don't share it, then set it; else just set the bases.   
    auto vptr = selfty->AccessVirtualPointer(key, self);
    if (vptr && !selfty->GetPrimaryBase()) {
        path.push_back(std::make_pair(selfty, 0));
        BasePointerInitializers.push_back(Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(vptr), selfty->GetVTablePointer(path)));
    }

    return CreatePrimGlobal(Range::Container(BasePointerInitializers), self->GetType(key), [=](CodegenContext& con) {
        for (auto&& init : BasePointerInitializers)
            init->GetValue(con);
        return self->GetValue(con);
    });
}

std::function<llvm::Constant*(llvm::Module*)> Type::GetRTTI() {
    // If we have a Clang type, then use it for compat.
    auto clangty = GetClangType(*analyzer.GetAggregateTU());
    if (clangty) {
        return [clangty, this](llvm::Module* module) { return analyzer.GetAggregateTU()->GetItaniumRTTI(*clangty, module); };
    }
    // Else, nope. Everything else we consider to be fundamental.
    return [this](llvm::Module* module) {
        std::stringstream stream;
        stream << "struct.__" << this;
        if (auto existing = module->getGlobalVariable(stream.str())) {
            return existing;
        }
        llvm::Constant *name = GetGlobalString(stream.str(), module);
        auto vtable_name_of_rtti = "_ZTVN10__cxxabiv123__fundamental_type_infoE";
        auto vtable = module->getOrInsertGlobal(vtable_name_of_rtti, llvm::Type::getInt8PtrTy(module->getContext()));
        vtable = llvm::ConstantExpr::getInBoundsGetElementPtr(vtable, llvm::ConstantInt::get(llvm::Type::getInt8Ty(module->getContext()), 2));
        vtable = llvm::ConstantExpr::getBitCast(vtable, llvm::Type::getInt8PtrTy(module->getContext()));
        std::vector<llvm::Constant*> inits = { vtable, name };
        auto ty = llvm::ConstantStruct::getTypeForElements(inits);
        auto rtti = new llvm::GlobalVariable(*module, ty, true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, llvm::ConstantStruct::get(ty, inits), stream.str() + "_rtti");
        return rtti;
    };
}
llvm::Constant* Semantic::GetGlobalString(std::string string, llvm::Module* m) {
    auto strcon = llvm::ConstantDataArray::getString(m->getContext(), string);
    auto global = new llvm::GlobalVariable(*m, strcon->getType(),
                                           true, llvm::GlobalValue::PrivateLinkage,
                                            strcon);
    global->setName("");
    global->setUnnamedAddr(true);
    auto zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m->getContext()), 0);
    llvm::Constant *Args[] = { zero, zero };
    return llvm::ConstantExpr::getInBoundsGetElementPtr(global, Args);
}
Type::VTableLayout Type::ComputeVTableLayout() {
    auto playout = GetPrimaryVTable();
    // Append ITANIUM ABI SPECIFIC secondary tables.
    for (auto base : GetBases()) {
        if (base == GetPrimaryBase())
            continue;
        auto secondarytable = base->GetVtableLayout();
        playout.layout.insert(playout.layout.end(), secondarytable.layout.begin(), secondarytable.layout.end());
    }
    return playout;
}
std::function<llvm::Function*(llvm::Module*)> Type::GetDestructorFunction() {
    if (!GetDestructorFunctionCache) GetDestructorFunctionCache = CreateDestructorFunction();
    return[=](llvm::Module* mod) {
        if (!DestructorFunction) DestructorFunction = (*GetDestructorFunctionCache)(mod);
        return DestructorFunction;
    };
}
std::function<llvm::Function*(llvm::Module*)> Wide::Semantic::Type::GetDestructorFunctionForEH()
{
    auto func = CreateDestructorFunction();
    return [this, func](llvm::Module* module) {
        auto f = func(module);
        auto name = std::string(f->getName()) + "_for_eh";
        if (module->getFunction(name))
            return module->getFunction(name);
        auto newfunc = llvm::Function::Create(llvm::cast<llvm::FunctionType>(f->getType()->getElementType()), f->getLinkage(), name, module);
        newfunc->setCallingConv(llvm::CallingConv::X86_ThisCall);
        CodegenContext::EmitFunctionBody(newfunc, {}, [f, newfunc](CodegenContext& c) {
            c->CreateCall(f, newfunc->args().begin());
            c->CreateRetVoid();
        });
        return newfunc;
    };
}
std::function<llvm::Function*(llvm::Module*)> Type::CreateDestructorFunction() {
    return [this](llvm::Module* module) {
        auto fty = llvm::FunctionType::get(llvm::Type::getVoidTy(module->getContext()), { llvm::Type::getInt8PtrTy(module->getContext()) }, false);
        auto desfunc = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, analyzer.GetUniqueFunctionName(), module);
        CodegenContext::EmitFunctionBody(desfunc, {}, [this, desfunc](CodegenContext& c) {
            auto obj = CreatePrimGlobal(Range::Empty(), analyzer.GetLvalueType(this), [=](CodegenContext& con) {
                return con->CreateBitCast(desfunc->arg_begin(), analyzer.GetLvalueType(this)->GetLLVMType(con));
            });
            BuildDestructorCall(Expression::NoInstance(), obj, { this, Lexer::Range(nullptr) }, true)(c);
            c->CreateRetVoid();
        });
        return desfunc;
    };
}

std::shared_ptr<Expression> Type::BuildIndex(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::shared_ptr<Expression> arg, Context c) {
    auto set = val->GetType(key)->AccessMember({ &Lexer::TokenTypes::OpenSquareBracket, &Lexer::TokenTypes::CloseSquareBracket }, GetAccessSpecifier(c.from, val->GetType(key)), OperatorAccess::Implicit);
    std::vector<std::shared_ptr<Expression>> args;
    args.push_back(std::move(val));
    args.push_back(std::move(arg));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType(key));
    auto call = set->Resolve(types, c.from);
    if (!call) return set->IssueResolutionError(types, c);
    return call->Call(key, std::move(args), c);
}
std::string Type::Export() {
    return explain();
}
void Type::Export(llvm::Module* mod) {}
std::string Type::GetExportBody() {
    return "";
}