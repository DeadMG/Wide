#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Util/DebugUtilities.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/IntegralType.h>
#include <sstream>

using namespace Wide;
using namespace Semantic;

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/AST.h>
#pragma warning(pop)

OverloadSet* Type::CreateADLOverloadSet(Lexer::TokenType what, Lexer::Access access) {
    if (IsReference())
        return Decay()->CreateADLOverloadSet(what, access);
    auto context = GetContext();
    if (!context)
        return analyzer.GetOverloadSet();
    return GetContext()->AccessMember(what, access);
}

OverloadSet* Type::CreateOperatorOverloadSet(Lexer::TokenType type, Lexer::Access access) {
    if (IsReference())
        return Decay()->AccessMember(type, access);
    return analyzer.GetOverloadSet();
}

bool Type::IsReference(Type* to) { 
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

bool Type::IsComplexType() {
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
bool Type::IsMoveConstructible(Lexer::Access access) {
    auto set = GetConstructorOverloadSet(access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetRvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsCopyConstructible(Lexer::Access access) {
    // A Clang struct with a deleted copy constructor can be both noncomplex and non-copyable at the same time.
    auto set = GetConstructorOverloadSet(access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetLvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsMoveAssignable(Lexer::Access access) {
    auto set = AccessMember(&Lexer::TokenTypes::Assignment, access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetRvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsCopyAssignable(Lexer::Access access) {
    auto set = AccessMember(&Lexer::TokenTypes::Assignment, access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetLvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsA(Type* self, Type* other, Lexer::Access access) {
    return
        other == self ||
        other == analyzer.GetRvalueType(self) ||
        self == analyzer.GetLvalueType(other) && other->IsCopyConstructible(access) ||
        self == analyzer.GetRvalueType(other) && other->IsMoveConstructible(access);
}

Type* Type::GetConstantContext() {
    return nullptr;
}

bool Type::IsEmpty() {
    return false;
}

Type* Type::GetVirtualPointerType() {
    assert(false && "Called GetVirtualPointerType on a type with no virtual functions.");
    throw "ICE";
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

std::shared_ptr<Expression> Type::GetVirtualPointer(std::shared_ptr<Expression> self) { 
    assert(false); 
    throw std::runtime_error("ICE"); 
}

std::shared_ptr<Expression> Type::AccessStaticMember(std::string name) {
    if (IsReference())
        return Decay()->AccessStaticMember(name);
    return nullptr;
}

std::shared_ptr<Expression> Type::AccessMember(std::shared_ptr<Expression> t, std::string name, Context c) {
    if (IsReference())
        return Decay()->AccessMember(std::move(t), name, c);
    return nullptr;
}

std::shared_ptr<Expression> Type::BuildMetaCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args) {
    if (IsReference())
        return Decay()->BuildMetaCall(std::move(val), std::move(args));
    throw std::runtime_error("fuck");
}

std::shared_ptr<Expression> Type::BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    if (IsReference())
        return Decay()->BuildCall(std::move(val), std::move(args), c);
    auto set = val->GetType()->AccessMember(&Lexer::TokenTypes::OpenBracket, GetAccessSpecifier(c.from, this));
    args.insert(args.begin(), std::move(val));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType());
    auto call = set->Resolve(types, c.from);
    if (!call) set->IssueResolutionError(types, c);
    return call->Call(std::move(args), c);
}

std::shared_ptr<Expression> Type::BuildBooleanConversion(std::shared_ptr<Expression> val, Context c) {
    auto set = val->GetType()->Decay()->AccessMember(&Lexer::TokenTypes::QuestionMark, GetAccessSpecifier(c.from, val->GetType()));
    std::vector<std::shared_ptr<Expression>> args;
    args.push_back(std::move(val));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType());
    auto call = set->Resolve(types, c.from);
    if (!call) set->IssueResolutionError(types, c);
    return call->Call(std::move(args), c);
}

std::function<void(CodegenContext&)> Type::BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize) {
    return [](CodegenContext&) {};
}

OverloadSet* Type::GetConstructorOverloadSet(Lexer::Access access) {
    if (ConstructorOverloadSets.find(access) == ConstructorOverloadSets.end())
        ConstructorOverloadSets[access] = CreateConstructorOverloadSet(access);
    return ConstructorOverloadSets[access];
}

OverloadSet* Type::PerformADL(Lexer::TokenType what, Lexer::Access access) {
    if (IsReference())
        return Decay()->PerformADL(what, access);
    if (ADLResults.find(access) != ADLResults.end())
        if (ADLResults[access].find(what) != ADLResults[access].end())
            return ADLResults[access][what];
    return ADLResults[access][what] = CreateADLOverloadSet(what, access);
}

OverloadSet* Type::AccessMember(Lexer::TokenType type, Lexer::Access access) {
    if (OperatorOverloadSets.find(access) != OperatorOverloadSets.end())
        if (OperatorOverloadSets[access].find(type) != OperatorOverloadSets[access].end())
            return OperatorOverloadSets[access][type];
    return OperatorOverloadSets[access][type] = CreateOperatorOverloadSet(type, access);
}

std::shared_ptr<Expression> Type::BuildInplaceConstruction(std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    auto selfty = self->GetType();
    exprs.insert(exprs.begin(), std::move(self));
    std::vector<Type*> types;
    for (auto&& arg : exprs)
        types.push_back(arg->GetType());
    auto conset = selfty->Decay()->GetConstructorOverloadSet(GetAccessSpecifier(c.from, selfty));
    auto callable = conset->Resolve(types, c.from);
    if (!callable)
        conset->IssueResolutionError(types, c);
    return callable->Call(std::move(exprs), c);
}

struct ValueConstruction : Expression {
    ValueConstruction(Type* self, std::vector<std::shared_ptr<Expression>> args, Context c)
    : self(self) {
        ListenToNode(self);
        no_args = args.size() == 0;
        temporary = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(self, c);
        if (args.size() == 1)
            single_arg = args[0];
        InplaceConstruction = Type::BuildInplaceConstruction(temporary, std::move(args), c);
        destructor = self->BuildDestructorCall(temporary, c, true);
    }
    std::shared_ptr<ImplicitTemporaryExpr> temporary;
    std::function<void(CodegenContext&)> destructor;
    std::shared_ptr<Expression> InplaceConstruction;
    std::shared_ptr<Expression> single_arg;
    bool no_args = false;
    Type* self;
    void OnNodeChanged(Node* n, Change what) {
        if (what == Change::Destroyed)
            assert(false);
    }
    Type* GetType() override final {
        return self;
    }
    llvm::Value* ComputeValue(CodegenContext& con) override final {
        if (!self->Decay()->IsComplexType()) {
            if (self->GetConstantContext() == self && no_args)
                return llvm::UndefValue::get(self->GetLLVMType(con));
            if (auto func = dynamic_cast<Function*>(self))
                return llvm::UndefValue::get(self->GetLLVMType(con));
            if (single_arg) {
                auto otherty = single_arg->GetType();
                if (single_arg->GetType()->Decay() == self->Decay()) {
                    if (single_arg->GetType() == self) {
                        return single_arg->GetValue(con);
                    }
                    if (single_arg->GetType()->IsReference(self)) {
                        //if (auto val = dynamic_cast<ValueConstruction*>(single_arg.get())) {
                        //    if (IsRvalueType(val->GetType())) {
                        //        if (val->single_arg) {
                        //            if (val->single_arg->GetType() == self) {
                        //                state = State::ValueSingleArgument;
                        //                return val->single_arg->GetValue(module, bb, allocas);
                        //            }
                        //        }
                        //    }
                        //}
                        return con->CreateLoad(single_arg->GetValue(con));
                    }
                }
            }
        }
        InplaceConstruction->GetValue(con);
        if (self->IsComplexType()) {
            if (!self->IsTriviallyDestructible())
                con.AddDestructor(destructor);
            return temporary->GetValue(con);
        }
        return con->CreateLoad(temporary->GetValue(con));
    }
};
std::shared_ptr<Expression> Type::BuildValueConstruction(std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    return Wide::Memory::MakeUnique<ValueConstruction>(this, std::move(exprs), c);
}

std::shared_ptr<Expression> Type::BuildRvalueConstruction(std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    struct RValueConstruction : Expression {
        RValueConstruction(Type* self, std::vector<std::shared_ptr<Expression>> args, Context c)
        : self(self) {
            temporary = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(self, c);
            InplaceConstruction = Type::BuildInplaceConstruction(temporary, std::move(args), c);
            destructor = self->BuildDestructorCall(temporary, c, true);
        }
        std::shared_ptr<ImplicitTemporaryExpr> temporary;
        std::shared_ptr<Expression> InplaceConstruction;
        std::function<void(CodegenContext&)> destructor;
        Type* self;
        Type* GetType() override final {
            return self->analyzer.GetRvalueType(self);
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            InplaceConstruction->GetValue(con);
            if (!self->IsTriviallyDestructible())
                con.AddDestructor(destructor);
            return temporary->GetValue(con);
        }
    };
    return Wide::Memory::MakeUnique<RValueConstruction>(this, std::move(exprs), c);
}

std::shared_ptr<Expression> Type::BuildLvalueConstruction(std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    struct LValueConstruction : Expression {
        LValueConstruction(Type* self, std::vector<std::shared_ptr<Expression>> args, Context c)
        : self(self) {
            temporary = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(self, c);
            InplaceConstruction = Type::BuildInplaceConstruction(temporary, std::move(args), c);
            destructor = self->BuildDestructorCall(temporary, c, true);
        }
        std::shared_ptr<ImplicitTemporaryExpr> temporary;
        std::shared_ptr<Expression> InplaceConstruction;
        std::function<void(CodegenContext&)> destructor;
        Type* self;
        Type* GetType() override final {
            return self->analyzer.GetLvalueType(self);
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            InplaceConstruction->GetValue(con);
            if (!self->IsTriviallyDestructible())
                con.AddDestructor(destructor);
            return temporary->GetValue(con);
        }
    };
    return Wide::Memory::MakeUnique<LValueConstruction>(this, std::move(exprs), c);
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
            if (result == InheritanceRelationship::UnambiguouslyDerived)
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

std::shared_ptr<Expression> Type::AccessBase(std::shared_ptr<Expression> self, Type* other) {
    auto selfty = self->GetType()->Decay();
    assert(selfty->IsDerivedFrom(other) == InheritanceRelationship::UnambiguouslyDerived);
    assert(!other->IsReference());
    Type* result = self->GetType()->IsReference()
        ? IsLvalueType(self->GetType()) ? selfty->analyzer.GetLvalueType(other) : selfty->analyzer.GetRvalueType(other)
        : dynamic_cast<PointerType*>(self->GetType())
        ? selfty->analyzer.GetPointerType(other)
        : other;
    struct DerivedToBaseConversion : public Expression {
        DerivedToBaseConversion(std::shared_ptr<Expression> self, Type* result, Type* derived, Type* targetbase)
        : self(std::move(self)), result(result), derived(derived), targetbase(targetbase) {}
        std::shared_ptr<Expression> self;
        Type* derived;
        Type* targetbase;
        Type* result;
        Type* GetType() override final { return result; }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            // Must succeed because we know we're unambiguously derived.
            unsigned offset;
            for (auto base : derived->GetBasesAndOffsets()) {
                if (base.first == targetbase) {
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
        }
    };
    return Wide::Memory::MakeUnique<DerivedToBaseConversion>(std::move(self), result, selfty, other);
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

std::shared_ptr<Expression> Type::BuildUnaryExpression(std::shared_ptr<Expression> self, Lexer::TokenType type, Context c) {
    auto selfty = self->GetType()->Decay();
    auto opset = selfty->AccessMember(type, GetAccessSpecifier(c.from, selfty));
    auto callable = opset->Resolve({ self->GetType() }, c.from);
    if (!callable) {
        if (type == &Lexer::TokenTypes::Negate) {
            if (auto self_as_bool = Type::BuildBooleanConversion(self, c))
                return Type::BuildUnaryExpression(self_as_bool, &Lexer::TokenTypes::Negate, c);
        }
        opset->IssueResolutionError({ self->GetType() }, c);
    }
    return callable->Call({ std::move(self) }, c);
}

std::shared_ptr<Expression> Type::BuildBinaryExpression(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Lexer::TokenType type, Context c) {
    auto&& analyzer = lhs->GetType()->analyzer;
    auto lhsaccess = GetAccessSpecifier(c.from, lhs->GetType());
    auto rhsaccess = GetAccessSpecifier(c.from, rhs->GetType());
    auto lhsadl = lhs->GetType()->PerformADL(type, lhsaccess);
    auto rhsadl = rhs->GetType()->PerformADL(type, rhsaccess);
    auto lhsmember = lhs->GetType()->AccessMember(type, lhsaccess);
    auto finalset = analyzer.GetOverloadSet(lhsadl, analyzer.GetOverloadSet(rhsadl, lhsmember));
    std::vector<Type*> arguments;
    arguments.push_back(lhs->GetType());
    arguments.push_back(rhs->GetType());
    if (auto call = finalset->Resolve(arguments, c.from)) {
        return call->Call({ std::move(lhs), std::move(rhs) }, c);
    }
    
    if (type == &Lexer::TokenTypes::EqCmp) {
        // a == b if (!(a < b) && !(b > a)).
        auto a_lt_b = Type::BuildBinaryExpression(lhs, rhs, &Lexer::TokenTypes::LT, c);
        auto b_lt_a = Type::BuildBinaryExpression(std::move(rhs), std::move(lhs), &Lexer::TokenTypes::LT, c);
        auto not_a_lt_b = Type::BuildUnaryExpression(std::move(a_lt_b), &Lexer::TokenTypes::Negate, c);
        auto not_b_lt_a = Type::BuildUnaryExpression(std::move(b_lt_a), &Lexer::TokenTypes::Negate, c);
        return Type::BuildBinaryExpression(std::move(not_a_lt_b), std::move(not_b_lt_a), &Lexer::TokenTypes::And, c);
    } else if (type == &Lexer::TokenTypes::LTE) {
        auto subexpr = Type::BuildBinaryExpression(std::move(rhs), std::move(lhs), &Lexer::TokenTypes::LT, c);
        return Type::BuildUnaryExpression(std::move(subexpr), &Lexer::TokenTypes::Negate, c);
    } else if (type == &Lexer::TokenTypes::GT) {
        return Type::BuildBinaryExpression(std::move(rhs), std::move(lhs), &Lexer::TokenTypes::LT, c);
    } else if (type == &Lexer::TokenTypes::GTE) {
        auto subexpr = Type::BuildBinaryExpression(std::move(lhs), std::move(rhs), &Lexer::TokenTypes::LT, c);
        return Type::BuildUnaryExpression(std::move(subexpr), &Lexer::TokenTypes::Negate, c);
    } else if (type == &Lexer::TokenTypes::NotEqCmp) {
        auto subexpr = Type::BuildBinaryExpression(std::move(lhs), std::move(rhs), &Lexer::TokenTypes::EqCmp, c);
        return Type::BuildUnaryExpression(std::move(subexpr), &Lexer::TokenTypes::Negate, c);
    }

    finalset->IssueResolutionError(arguments, c);
    return nullptr;
}

OverloadSet* PrimitiveType::CreateConstructorOverloadSet(Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(Lexer::Access::Public);
    auto construct_from_ref = [](std::vector<std::shared_ptr<Expression>> args, Context c) {
        auto&& type = typeid(*args[1]->GetImplementation());
        return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), BuildValue(std::move(args[1])));
    };
    CopyConstructor = MakeResolvable(construct_from_ref, { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) });
    MoveConstructor = MakeResolvable(construct_from_ref, { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) });
    std::unordered_set<OverloadResolvable*> callables;
    callables.insert(CopyConstructor.get());
    callables.insert(MoveConstructor.get());
    return analyzer.GetOverloadSet(callables);
}

OverloadSet* PrimitiveType::CreateOperatorOverloadSet(Lexer::TokenType what, Lexer::Access access) {
    if (what != &Lexer::TokenTypes::Assignment)
        return Type::CreateOperatorOverloadSet(what, access);
    
    AssignmentOperator = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) {
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

Type* MetaType::GetConstantContext() {
    return this;
}

OverloadSet* MetaType::CreateConstructorOverloadSet(Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(Lexer::Access::Public);
    DefaultConstructor = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) {
        return std::move(args[0]);
    }, { analyzer.GetLvalueType(this) });
    return analyzer.GetOverloadSet(PrimitiveType::CreateConstructorOverloadSet(access), analyzer.GetOverloadSet(DefaultConstructor.get()));
}

std::shared_ptr<Expression> Callable::Call(std::vector<std::shared_ptr<Expression>> args, Context c) {
    return CallFunction(AdjustArguments(std::move(args), c), c);
}

std::vector<std::shared_ptr<Expression>> Semantic::AdjustArgumentsForTypes(std::vector<std::shared_ptr<Expression>> args, std::vector<Type*> types, Context c) {
    if (args.size() != types.size())
        Wide::Util::DebugBreak();
    std::vector<std::shared_ptr<Expression>> out;
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (args[i]->GetType() == types[i]) {
            out.push_back(std::move(args[i]));
            continue;
        }
        if (!args[i]->GetType()->IsA(args[i]->GetType(), types[i], GetAccessSpecifier(c.from, args[i]->GetType())))
            Wide::Util::DebugBreak();
        out.push_back(types[i]->BuildValueConstruction({ std::move(args[i]) }, c));
    }
    return out;
}

struct Resolvable : OverloadResolvable, Callable {
    std::vector<Type*> types;
    std::function<std::shared_ptr<Expression>(std::vector<std::shared_ptr<Expression>>, Context)> action;

    Resolvable(std::vector<Type*> tys, std::function<std::shared_ptr<Expression>(std::vector<std::shared_ptr<Expression>>, Context)> func)
        : types(std::move(tys)), action(std::move(func)) {}

    Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
        if (args.size() != types.size()) return Util::none;
        for (unsigned num = 0; num < types.size(); ++num) {
            auto t = args[num];
            if (!t->IsA(t, types[num], GetAccessSpecifier(source, t)))
                return Util::none;
        }
        return types;
    }
    Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final {
        assert(types == tys);
        return this;
    }
    std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        assert(types.size() == args.size());
        for (std::size_t num = 0; num < types.size(); ++num)
            assert(types[num] == args[num]->GetType());
        return action(std::move(args), c);
    }
    std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        return AdjustArgumentsForTypes(std::move(args), types, c);
    }
};

std::unique_ptr<OverloadResolvable> Semantic::MakeResolvable(std::function<std::shared_ptr<Expression>(std::vector<std::shared_ptr<Expression>>, Context)> f, std::vector<Type*> types) {
    return Wide::Memory::MakeUnique<Resolvable>(types, std::move(f));
}

OverloadSet* TupleInitializable::CreateConstructorOverloadSet(Lexer::Access access) {
    if (access != Lexer::Access::Public) return TupleInitializable::CreateConstructorOverloadSet(Lexer::Access::Public);
    struct TupleConstructorType : public OverloadResolvable, Callable {
        TupleConstructorType(TupleInitializable* p) : self(p) {}
        TupleInitializable* self;
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) { return this; }
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
            if (args.size() != 2) return Util::none;
            if (args[0] != a.GetLvalueType(self->GetSelfAsType())) return Util::none;
            auto tup = dynamic_cast<TupleType*>(args[1]->Decay());
            if (!tup) return Util::none;
            if (!tup->IsA(args[1], self->GetSelfAsType(), GetAccessSpecifier(source, tup))) return Util::none;
            return args;
        }
        std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
        std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            // We should already have properly-typed memory at 0.
            // and the tuple at 1.
            // This stuff won't be called unless we are valid
            auto tupty = dynamic_cast<TupleType*>(args[1]->GetType()->Decay());
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
                initializers.push_back(Type::BuildInplaceConstruction(std::move(memory), { std::move(argument) }, c));
            }

            struct TupleInitialization : Expression {
                TupleInitialization(Type* this_t, std::shared_ptr<Expression> s, std::shared_ptr<Expression> t, std::vector<std::shared_ptr<Expression>> inits)
                : self(std::move(s)), tuple(std::move(t)), initializers(std::move(inits)), this_type(this_t) {}
                std::vector<std::shared_ptr<Expression>> initializers;
                std::shared_ptr<Expression> self;
                std::shared_ptr<Expression> tuple;
                Type* this_type;

                Type* GetType() override final {
                    return this_type->analyzer.GetLvalueType(this_type);
                }

                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    // Evaluate left-to-right
                    for (auto&& init : initializers)
                        init->GetValue(con);
                    return self->GetValue(con);
                }
            };

            return Wide::Memory::MakeUnique<TupleInitialization>(self->GetSelfAsType(), std::move(args[0]), std::move(args[1]), std::move(initializers));
        }
    };
    if (!TupleConstructor) TupleConstructor = Wide::Memory::MakeUnique<TupleConstructorType>(this);
    return GetSelfAsType()->analyzer.GetOverloadSet(TupleConstructor.get());
}

std::shared_ptr<Expression> Type::CreateVTable(std::vector<std::pair<Type*, unsigned>> path) {
    std::vector<std::shared_ptr<Expression>> entries;
    unsigned offset_total = 0;
    for (auto base : path)
        offset_total += base.second;
    for (auto func : GetVtableLayout().layout) {
        if (auto mem = boost::get<VTableLayout::SpecialMember>(&func.function)) {
            if (*mem == VTableLayout::SpecialMember::OffsetToTop) {
                auto size = analyzer.GetDataLayout().getPointerSizeInBits();
                entries.push_back(Wide::Memory::MakeUnique<Integer>(llvm::APInt(size, (uint64_t)-offset_total, true), analyzer));
                continue;
            }
            if (*mem == VTableLayout::SpecialMember::RTTIPointer) {
                struct RTTIExpr : Expression {
                    RTTIExpr(Type* ty)
                    : ty(ty) {}
                    Type* ty;
                    llvm::Value* ComputeValue(CodegenContext& con) override final {
                        return con->CreateBitCast(ty->GetRTTI(con), con.GetInt8PtrTy());
                    }
                    Type* GetType() override final {
                        return ty->analyzer.GetPointerType(ty->analyzer.GetIntegralType(8, true));
                    }
                };
                entries.push_back(Wide::Memory::MakeUnique<RTTIExpr>(path.front().first));
                continue;
            }
        }
        bool found = false;
        auto local_copy = offset_total;
        for (auto more_derived : path) {
            auto expr = more_derived.first->VirtualEntryFor(func, local_copy);
            if (expr) {
                entries.push_back(std::move(expr));
                found = true;
                break;
            }
            local_copy -= more_derived.second;
        }
        if (func.abstract && !found) {
            // It's possible we didn't find a more derived impl because we're abstract
            // and we're creating our own constructor vtable here.
            // Just use a null pointer instead of the Itanium ABI function for now.
            entries.push_back(nullptr);
        }
        // Should have found base class impl!
        assert(found);
    }
    struct VTable : Expression {
        VTable(Type* self, std::vector<std::shared_ptr<Expression>> funcs, unsigned offset)
        : self(self), entries(std::move(funcs)), offset(offset) {}
        unsigned offset;
        std::vector<std::shared_ptr<Expression>> entries;
        Type* self;
        Type* GetType() override final {
            return self->analyzer.GetPointerType(self->GetVirtualPointerType());
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto vfuncty = self->GetVirtualPointerType()->GetLLVMType(con);
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
            std::stringstream strstr;
            strstr << this;
            auto global = llvm::dyn_cast<llvm::GlobalVariable>(con.module->getOrInsertGlobal(strstr.str(), vtablety));
            global->setInitializer(vtable);
            global->setLinkage(llvm::GlobalValue::LinkageTypes::InternalLinkage);
            global->setConstant(true);
            return con->CreatePointerCast(con->CreateStructGEP(global, offset), self->analyzer.GetPointerType(self->GetVirtualPointerType())->GetLLVMType(con));
        }
    };
    return Wide::Memory::MakeUnique<VTable>(this, std::move(entries), GetVtableLayout().offset);
}      

std::shared_ptr<Expression> Type::GetVTablePointer(std::vector<std::pair<Type*, unsigned>> path) {
    if (ComputedVTables.find(path) == ComputedVTables.end())
        ComputedVTables[path] = CreateVTable(path);
    return ComputedVTables.at(path);
}

std::shared_ptr<Expression> Type::SetVirtualPointers(std::shared_ptr<Expression> self) {
    return Type::SetVirtualPointers(std::move(self), {});
}

std::shared_ptr<Expression> Type::SetVirtualPointers(std::shared_ptr<Expression> self, std::vector<std::pair<Type*, unsigned>> path) {
    // Set the base vptrs first, because some Clang types share vtables with their base.
    std::vector<std::shared_ptr<Expression>> BasePointerInitializers;
    auto selfty = self->GetType()->Decay();
    for (auto base : selfty->GetBasesAndOffsets()) {
        path.push_back(std::make_pair(selfty, base.second));
        BasePointerInitializers.push_back(Type::SetVirtualPointers(Type::AccessBase(self, base.first), path));
        path.pop_back();
    }
    // If we actually have a vptr, then set it; else just set the bases.
    auto vptr = selfty->GetVirtualPointer(self);
    if (vptr) {
        path.push_back(std::make_pair(selfty, 0));
        BasePointerInitializers.push_back(Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(vptr), selfty->GetVTablePointer(path)));
    }
    
    struct VTableInitializer : Expression {
        VTableInitializer(std::shared_ptr<Expression> obj, std::vector<std::shared_ptr<Expression>> inits)
        : self(std::move(obj)), inits(std::move(inits)) {}
        std::shared_ptr<Expression> self;
        std::vector<std::shared_ptr<Expression>> inits;
        Type* GetType() override final {
            return self->GetType();
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            for (auto&& init : inits)
                init->GetValue(con);
            return self->GetValue(con);
        }
    };

    return Wide::Memory::MakeUnique<VTableInitializer>(std::move(self), std::move(BasePointerInitializers));
}

llvm::Constant* Type::GetRTTI(llvm::Module* module) {
    // If we have a Clang type, then use it for compat.
    auto clangty = GetClangType(*analyzer.GetAggregateTU());
    if (clangty) {
        return analyzer.GetAggregateTU()->GetItaniumRTTI(*clangty, module);
    }
    // Else, nope. Meta types are ... er.
    // Fundamental types.
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
    auto rtti = new llvm::GlobalVariable(*module, ty, true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, llvm::ConstantStruct::get(ty, inits), stream.str());
    return rtti;
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
llvm::Value* Type::GetDestructorFunction(llvm::Module* module) {
    if (IsTriviallyDestructible()) return llvm::Constant::getNullValue(llvm::Type::getInt8PtrTy(module->getContext()));
    if (DestructorFunction) return DestructorFunction;
    auto fty = llvm::FunctionType::get(llvm::Type::getVoidTy(module->getContext()), { llvm::Type::getInt8PtrTy(module->getContext()) }, false);
    std::stringstream str;
    str << "__" << this << "destructor";
    DestructorFunction = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::InternalLinkage, str.str(), module);
    llvm::BasicBlock* allocas = llvm::BasicBlock::Create(module->getContext(), "allocas", DestructorFunction);
    llvm::IRBuilder<> alloca_builder(allocas);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(module->getContext(), "entry", DestructorFunction);
    llvm::IRBuilder<> ir_builder(entry);
    alloca_builder.SetInsertPoint(alloca_builder.CreateBr(entry));
    CodegenContext c;
    c.insert_builder = &ir_builder;
    c.module = module;
    c.alloca_builder = &alloca_builder;
    struct DestructorExpression : Expression {
        DestructorExpression(Type* self, llvm::Value* val)
            : self(self), val(val) {}
        llvm::Value* val;
        Type* self;
        Type* GetType() override final {
            return self->analyzer.GetLvalueType(self);
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            return con->CreateBitCast(val, GetType()->GetLLVMType(con));
        }
    };
    auto obj = std::make_shared<DestructorExpression>(this, DestructorFunction->arg_begin());
    BuildDestructorCall(obj, { this, Lexer::Range(nullptr) }, true)(c);
    c->CreateRetVoid();
    return DestructorFunction;
}

std::shared_ptr<Expression> Type::BuildIndex(std::shared_ptr<Expression> val, std::shared_ptr<Expression> arg, Context c) {
    auto set = val->GetType()->AccessMember(&Lexer::TokenTypes::OpenSquareBracket, GetAccessSpecifier(c.from, val->GetType()));
    std::vector<std::shared_ptr<Expression>> args;
    args.push_back(std::move(val));
    args.push_back(std::move(arg));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType());
    auto call = set->Resolve(types, c.from);
    if (!call) set->IssueResolutionError(types, c);
    return call->Call(std::move(args), c);
}
