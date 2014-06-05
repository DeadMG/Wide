#include <Wide/Semantic/AggregateType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/OverloadSet.h>
#include <sstream>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

AggregateType::Layout::Layout(const std::vector<Type*>& types, Wide::Semantic::Analyzer& a)
: allocsize(0)
, align(1)
, copyassignable(true)
, copyconstructible(true)
, moveassignable(true)
, moveconstructible(true)
, constant(true)
{
    // Treat empties differently to match Clang's expectations
    if (types.empty()) {
        allocsize = 1;
        return;
    }

    auto adjust_alignment = [this](std::size_t alignment) {
        if (allocsize % alignment != 0) {
            auto adjustment = alignment - (allocsize % alignment);
            allocsize += adjustment;
            llvmtypes.push_back([adjustment](llvm::Module* module) {
                return llvm::ArrayType::get(llvm::IntegerType::getInt8Ty(module->getContext()), adjustment);
            });
        }
        align = std::max(alignment, align);
        assert(allocsize % alignment == 0);
    };

    for (auto ty : types) {
        // Check that we are suitably aligned for the next member and if not, align it with some padding.
        auto talign = ty->alignment();
        adjust_alignment(talign);

        // Add the type itself to the list- zero-based index.
        Offsets.push_back(allocsize);
        allocsize += ty->size();
        FieldIndices.push_back(llvmtypes.size());
        llvmtypes.push_back([ty](llvm::Module* module) { return ty->GetLLVMType(module); });

        copyconstructible = copyconstructible && ty->IsCopyConstructible(Lexer::Access::Public);
        moveconstructible = moveconstructible && ty->IsMoveConstructible(Lexer::Access::Public);
        copyassignable = copyassignable && ty->IsCopyAssignable(Lexer::Access::Public);
        moveassignable = moveassignable && ty->IsMoveAssignable(Lexer::Access::Public);
        constant = constant && ty->GetConstantContext();
    }

    // Fix the alignment of the whole structure
    adjust_alignment(align);    
}
std::size_t AggregateType::size() {
    return GetLayout().allocsize;
}
std::size_t AggregateType::alignment() {
    return GetLayout().align;
}
bool AggregateType::IsComplexType(llvm::Module* module) {
    return GetLayout().GetCodegen(this, module).IsComplex;
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

AggregateType::Layout::CodeGen::CodeGen(AggregateType* self, Layout& lay, llvm::Module* module)
: IsComplex(false) 
{
    llvmtype = nullptr;

    if (self->GetContents().empty()) {
        IsComplex = false;
    } else {
        for (auto ty : self->GetContents())
            IsComplex = IsComplex || ty->IsComplexType(module);
    }

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
    if (self->GetContents().empty()) {
        llvmtypes.push_back(self->analyzer.GetIntegralType(8, true)->GetLLVMType(module));
    } else {
        for (auto ty : self->GetLayout().llvmtypes)
            llvmtypes.push_back(ty(module));
    }
    ty->setBody(llvmtypes, false);
    return ty;
}
llvm::Type* AggregateType::GetLLVMType(llvm::Module* module) {
    return GetLayout().GetCodegen(this, module).GetLLVMType(this, module);
}
Type* AggregateType::GetConstantContext() {
    for (auto ty : GetContents())
    if (!ty->GetConstantContext())
        return nullptr;
    return this;
}

std::unique_ptr<Expression> AggregateType::PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num) {
    struct FieldAccess : Expression {
        FieldAccess(std::unique_ptr<Expression> src, AggregateType* agg, unsigned n)
        : source(std::move(src)), self(agg), num(n) {}
        std::unique_ptr<Expression> source;
        AggregateType* self;
        unsigned num;
        void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            source->DestroyLocals(module, bb, allocas);
        }
        Type* GetType() override final {
            auto source_ty = source->GetType();
            auto root_ty = self->GetContents()[num];

            // If it's not a reference, just return the root type.
            if (!source_ty->IsReference())
                return root_ty;

            // If the source is an lvalue, the result is an lvalue.
            if (IsLvalueType(source_ty))
                return self->analyzer.GetLvalueType(root_ty->Decay());

            // It's not a value or an lvalue so must be rvalue.
            return self->analyzer.GetRvalueType(root_ty);
        }
        llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            auto src = source->GetValue(module, bb, allocas);
            auto obj = src->getType()->isPointerTy() ? bb.CreateStructGEP(src, self->GetLayout().FieldIndices[num]) : bb.CreateExtractValue(src, { self->GetLayout().FieldIndices[num] });
            if ((source->GetType()->IsReference() && self->GetContents()[num]->IsReference()) || (source->GetType()->IsComplexType(module) && !self->GetContents()[num]->IsComplexType(module)))
                return bb.CreateLoad(obj);
            return obj;
        }
    };
    assert(self);
    return Wide::Memory::MakeUnique<FieldAccess>(std::move(self), this, num);
}
std::unique_ptr<Expression> AggregateType::BuildDestructorCall(std::unique_ptr<Expression> self, Context c) {
    std::vector<std::unique_ptr<Expression>> destructors;
    unsigned i = 0;
    for (auto member : GetContents())
        destructors.push_back(member->BuildDestructorCall(PrimitiveAccessMember(Wide::Memory::MakeUnique<ExpressionReference>(self.get()), i++), c));

    struct AggregateDestructor : Expression {
        AggregateDestructor(AggregateType* agg, std::unique_ptr<Expression> s, std::vector<std::unique_ptr<Expression>> des)
        : self(std::move(s)), destructors(std::move(des)), agg(agg) {}
        AggregateType* agg;
        std::unique_ptr<Expression> self;
        std::vector<std::unique_ptr<Expression>> destructors;
        Type* GetType() override final {
            return self->GetType();
        }
        llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            if (!agg->GetLayout().GetCodegen(agg, module).IsComplex)
                return self->GetValue(module, bb, allocas);
            for (auto&& des : destructors)
                des->GetValue(module, bb, allocas);
            return self->GetValue(module, bb, allocas);
        }
        void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            self->DestroyLocals(module, bb, allocas);
        }
    };
    return Wide::Memory::MakeUnique<AggregateDestructor>(this, std::move(self), std::move(destructors));
}

OverloadSet* AggregateType::CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Lexer::Access access) {
    if (type != Lexer::TokenType::Assignment)
        return analyzer.GetOverloadSet();
    if (access != Lexer::Access::Public)
        return AccessMember(t, type, Lexer::Access::Public);

    // Similar principle to constructor
    std::function<Type*(Type*)> modify;
    auto createoperator = [this, &modify] {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(this));
        types.push_back(modify(this));
        return MakeResolvable([this, modify](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
            if (GetContents().size() == 0)
                return BuildChain(std::move(args[1]), std::move(args[0]));
            
            std::vector<std::unique_ptr<Expression>> exprs;
            // For every type, call the operator
            for (std::size_t i = 0; i < GetContents().size(); ++i) {
                auto type = GetContents()[i];
                auto lhs = PrimitiveAccessMember(Wide::Memory::MakeUnique<ExpressionReference>(args[0].get()), i);
                std::vector<Type*> types;
                types.push_back(analyzer.GetLvalueType(type));
                types.push_back(modify(type));
                auto overset = type->AccessMember(analyzer.GetLvalueType(type), Lexer::TokenType::Assignment, GetAccessSpecifier(this, type));
                auto callable = overset->Resolve(types, this);
                if (!callable)
                    assert(false); // dafuq, the appropriate assignable was set but we're not assignable?
                exprs.push_back(callable->Call(Expressions( std::move(lhs), PrimitiveAccessMember(Wide::Memory::MakeUnique<ExpressionReference>(args[1].get()), i) ), c));
            }

            struct AggregateOperator : Expression {
                AggregateType* this_type;
                std::unique_ptr<Expression> self;
                std::unique_ptr<Expression> arg;
                std::vector<std::unique_ptr<Expression>> exprs;
                Type* GetType() override final { return self->GetType(); }
                llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                    if (!this_type->IsComplexType(module)) {
                        // Screw calling all the operators, just store.
                        // This makes debugging the IR output a lot easier.
                        auto val = arg->GetType()->IsReference() ? bb.CreateLoad(arg->GetValue(module, bb, allocas)) : arg->GetValue(module, bb, allocas);
                        bb.CreateStore(val, self->GetValue(module, bb, allocas));
                        return self->GetValue(module, bb, allocas);
                    }
                    for (auto&& arg : exprs)
                        arg->GetValue(module, bb, allocas);
                    return self->GetValue(module, bb, allocas);
                }
                void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                    if (this_type->IsComplexType(module)) {
                        for (auto rit = exprs.rbegin(); rit != exprs.rend(); ++rit) {
                            (*rit)->DestroyLocals(module, bb, allocas);
                        }
                    }
                    arg->DestroyLocals(module, bb, allocas);
                    self->DestroyLocals(module, bb, allocas);
                }
                AggregateOperator(AggregateType* s, std::unique_ptr<Expression> expr, std::unique_ptr<Expression> arg, std::vector<std::unique_ptr<Expression>> exprs)
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
        return MakeResolvable([this, modify](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression>  {
            if (GetContents().size() == 0)
                return BuildChain(std::move(args[1]), std::move(args[0]));
            // For every type, call the appropriate constructor.
            std::vector<std::unique_ptr<Expression>> exprs;
            for (std::size_t i = 0; i < GetContents().size(); ++i) {
                auto type = GetContents()[i];
                auto lhs = CreatePrimUnOp(Wide::Memory::MakeUnique<ExpressionReference>(args[0].get()), analyzer.GetLvalueType(type), [this, i](llvm::Value* val, llvm::Module* mod, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                    return bb.CreateStructGEP(val, GetFieldIndex(i));
                });
                auto rhs = PrimitiveAccessMember(Wide::Memory::MakeUnique<ExpressionReference>(args[1].get()), i);
                auto set = type->GetConstructorOverloadSet(GetAccessSpecifier(this, type));
                std::vector<Type*> types;
                types.push_back(analyzer.GetLvalueType(type));
                types.push_back(modify(type));
                auto callable = set->Resolve(types, this);
                assert(callable);// Should not be generated if this fails!
                exprs.push_back(callable->Call(Expressions( std::move(lhs), std::move(rhs) ), c));                
            }
            struct AggregateConstructor : Expression {
                AggregateType* this_type;
                std::unique_ptr<Expression> self;
                std::unique_ptr<Expression> arg;
                std::vector<std::unique_ptr<Expression>> exprs;
                Type* GetType() override final { return self->GetType(); }
                llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                    if (!this_type->IsComplexType(module)) {
                        // Screw calling all the operators, just store.
                        // This makes debugging the IR output a lot easier.
                        auto val = arg->GetType()->IsReference() ? bb.CreateLoad(arg->GetValue(module, bb, allocas)) : arg->GetValue(module, bb, allocas);
                        bb.CreateStore(val, self->GetValue(module, bb, allocas));
                        return self->GetValue(module, bb, allocas);
                    }
                    for (auto&& arg : exprs)
                        arg->GetValue(module, bb, allocas);
                    return self->GetValue(module, bb, allocas);
                }
                void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                    if (this_type->IsComplexType(module)) {
                        for (auto rit = exprs.rbegin(); rit != exprs.rend(); ++rit) {
                            (*rit)->DestroyLocals(module, bb, allocas);
                        }
                    }
                    arg->DestroyLocals(module, bb, allocas);
                    self->DestroyLocals(module, bb, allocas);
                }
                AggregateConstructor(AggregateType* s, std::unique_ptr<Expression> expr, std::unique_ptr<Expression> arg, std::vector<std::unique_ptr<Expression>> exprs)
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
        for (auto ty : GetContents()) {
            std::vector<Type*> types;
            types.push_back(analyzer.GetLvalueType(ty));
            if (!ty->GetConstructorOverloadSet(GetAccessSpecifier(this, ty))->Resolve(types, this))
                return false;
        }
        return true;
    };
    if (is_default_constructible()) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(this));
        if (!DefaultConstructor)
            DefaultConstructor = MakeResolvable([this](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
                if (GetContents().size() == 0)
                    return std::move(args[0]);
                
                // For every type, call the appropriate constructor.
                std::vector<std::unique_ptr<Expression>> exprs;
                for (std::size_t i = 0; i < GetContents().size(); ++i) {
                    auto type = GetContents()[i];
                    auto lhs = PrimitiveAccessMember(Wide::Memory::MakeUnique<ExpressionReference>(args[0].get()), i);
                    auto set = type->GetConstructorOverloadSet(GetAccessSpecifier(this, type));
                    std::vector<Type*> types;
                    types.push_back(analyzer.GetLvalueType(type));
                    auto callable = set->Resolve(types, this);
                    assert(callable);// Should not be generated if this fails!
                    exprs.push_back(callable->Call(Expressions( std::move(lhs) ), c));
                }
                struct AggregateConstructor : Expression {
                    std::unique_ptr<Expression> self;
                    std::vector<std::unique_ptr<Expression>> exprs;
                    Type* GetType() override final { return self->GetType(); }
                    llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                        for (auto&& arg : exprs)
                            arg->GetValue(module, bb, allocas);
                        return self->GetValue(module, bb, allocas);
                    }
                    void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                        for (auto rit = exprs.rbegin(); rit != exprs.rend(); ++rit) {
                            (*rit)->DestroyLocals(module, bb, allocas);
                        }
                        self->DestroyLocals(module, bb, allocas);
                    }
                    AggregateConstructor(std::unique_ptr<Expression> expr, std::vector<std::unique_ptr<Expression>> exprs)
                        : self(std::move(expr)), exprs(std::move(exprs)) {}
                };
                return Wide::Memory::MakeUnique<AggregateConstructor>(std::move(args[0]), std::move(exprs));
            }, types);
        set.insert(DefaultConstructor.get());
    }
    return analyzer.GetOverloadSet(analyzer.GetOverloadSet(set), AggregateType::CreateNondefaultConstructorOverloadSet());
}
bool AggregateType::IsEliminateType() {
    return GetContents().size() == 0;
}
bool AggregateType::HasMemberOfType(Type* t) {
    for (auto ty : GetContents()) {
        if (ty == t)
            return true;
        if (auto agg = dynamic_cast<AggregateType*>(ty)) 
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