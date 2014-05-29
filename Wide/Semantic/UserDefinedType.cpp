#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Reference.h>
#include <sstream>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#include <clang/AST/Type.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ASTContext.h>
#include <clang/Sema/Sema.h>
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

// I only store MY OWN dynamic functions in my vtable.
// My base's dynamic functions can go in my base's vtable.

bool UserDefinedType::IsDynamic() {
    return GetVtableLayout().size() != 0;
}
void AddAllBases(std::unordered_set<BaseType*>& all_bases, BaseType* root) {
    all_bases.insert(root);
    for (auto base : root->GetBases()) {
        all_bases.insert(base);
        AddAllBases(all_bases, base);
    }
}
UserDefinedType::MemberData::MemberData(UserDefinedType* self) {
    for (auto expr : self->type->bases) {
        auto base = AnalyzeExpression(self->context, expr, self->analyzer);
        auto con = dynamic_cast<ConstructorType*>(base->GetType()->Decay());
        if (!con) throw NotAType(base->GetType(), expr->location);
        auto udt = dynamic_cast<BaseType*>(con->GetConstructedType());
        if (!udt) throw InvalidBase(con->GetConstructedType(), expr->location);
        bases.push_back(udt);
        if (udt == self) throw InvalidBase(con->GetConstructedType(), expr->location);
        contents.push_back(con->GetConstructedType());
    }
    for (auto&& var : self->type->variables) {
        members[var.first->name.front().name] = contents.size();
        auto expr = AnalyzeExpression(self->context, var.first->initializer, self->analyzer);
        if (auto con = dynamic_cast<ConstructorType*>(expr->GetType()->Decay())) {
            contents.push_back(con->GetConstructedType());
            NSDMIs.push_back(nullptr);
        }
        else {
            contents.push_back(expr->GetType()->Decay());
            HasNSDMI = true;
            NSDMIs.push_back(var.first->initializer);
        }
        if (auto agg = dynamic_cast<AggregateType*>(contents.back())) {
            if (agg == self || agg->HasMemberOfType(self))
                throw RecursiveMember(self, var.first->initializer->location);
        }
    }
    std::unordered_set<BaseType*> all_bases;
    for (auto&& base : bases)
        AddAllBases(all_bases, base);

    unsigned index = 0;
    for (auto overset : self->type->Functions) {
        if (overset.first == "type") continue; // Do not check constructors.
        for (auto func : overset.second->functions) {
            // The function has to be not explicitly marked dynamic *and* not dynamic by any base class.
            if (IsMultiTyped(func)) continue;

            VirtualFunction f;
            f.name = overset.first;
            if (func->args.size() == 0 || func->args.front().name != "this")
                f.args.push_back(self->analyzer.GetLvalueType(self));
            for (auto arg : func->args) {
                auto ty = AnalyzeExpression(self->context, arg.type, self->analyzer)->GetType()->Decay();
                auto con_type = dynamic_cast<ConstructorType*>(ty);
                if (!con_type)
                    throw Wide::Semantic::NotAType(ty, arg.location);
                f.args.push_back(con_type->GetConstructedType());
            }
            auto is_dynamic = [&, this] {
                if (func->dynamic) return true;
                for (auto base : all_bases) {
                    for (auto func : base->GetVtableLayout()) {
                        if (func.name != overset.first) continue;
                        // The this arguments will be different.
                        if (f.args.size() != func.args.size()) continue;
                        for (auto i = 1; i < f.args.size(); ++i) {
                            if (func.args[i] != f.args[i])
                                continue;
                        }
                        return true;
                    }
                }
                return false;
            };
            if (!is_dynamic()) continue;

            f.abstract = false;
            funcs.push_back(f);
            VTableIndices[func] = index++;
        }
    }
    if (!funcs.empty()) {
        HasNSDMI = true;
        contents.push_back(self->analyzer.GetPointerType(self->GetVirtualPointerType()));
    }
}
UserDefinedType::MemberData::MemberData(MemberData&& other)
: members(std::move(other.members))
, NSDMIs(std::move(other.NSDMIs))
, HasNSDMI(other.HasNSDMI)
, funcs(std::move(other.funcs))
, VTableIndices(std::move(other.VTableIndices))
, contents(std::move(other.contents))
, bases(std::move(other.bases)) {}

UserDefinedType::MemberData& UserDefinedType::MemberData::operator=(MemberData&& other) {
    members = std::move(other.members);
    NSDMIs = std::move(other.NSDMIs);
    HasNSDMI = other.HasNSDMI;
    funcs = std::move(other.funcs);
    VTableIndices = std::move(other.VTableIndices);
    contents = std::move(other.contents);
    bases = std::move(other.bases);
    return *this;
}

UserDefinedType::UserDefinedType(const AST::Type* t, Analyzer& a, Type* higher, std::string name)
: AggregateType(a)
, context(higher)
, type(t)
, source_name(name) 
{
}

std::vector<UserDefinedType::member> UserDefinedType::GetMembers() {
    std::vector<UserDefinedType::member> out;
    for (unsigned i = 0; i < type->bases.size(); ++i) {
        member m(type->bases[i]->location);
        m.t = GetContents()[i];
        m.name = dynamic_cast<AST::Identifier*>(type->bases[i])->val;
        m.num = GetFieldIndex(i);
        m.InClassInitializer = nullptr;
        m.vptr = false;
        out.push_back(std::move(m));
    }
    for (unsigned i = 0; i < type->variables.size(); ++i) {
        member m(type->variables[i].first->location);
        m.t = GetContents()[i + type->bases.size()];
        m.name = type->variables[i].first->name.front().name;
        m.num = GetFieldIndex(i) + type->bases.size();
        if (GetMemberData().NSDMIs[i])
            m.InClassInitializer = [this, i](std::unique_ptr<Expression>) { return AnalyzeExpression(context, GetMemberData().NSDMIs[i], analyzer); };
        m.vptr = false;
        out.push_back(std::move(m));
    }
    if (IsDynamic()) {
        member m(Lexer::Range(nullptr));
        m.num = GetFieldIndex(type->bases.size() + type->variables.size());
        m.t = GetContents()[type->bases.size() + type->variables.size()];
        m.name = "__vfptr";
        m.InClassInitializer = [this](std::unique_ptr<Expression> self) { return SetVirtualPointers(std::move(self)); };
        m.vptr = true;
        out.push_back(std::move(m));
    }
    return out;
}

std::unique_ptr<Expression> UserDefinedType::AccessMember(std::unique_ptr<Expression> self, std::string name, Context c) {
    //if (!self->GetType()->IsReference())
    //    self = BuildRvalueConstruction(Expressions(std::move(self)), { this, c.where });
    auto spec = GetAccessSpecifier(c.from, this);
    if (GetMemberData().members.find(name) != GetMemberData().members.end()) {
        auto member = type->variables[GetMemberData().members[name] - type->bases.size()];
        if (spec >= member.second)
            return PrimitiveAccessMember(std::move(self), GetMemberData().members[name]);
    }
    if (type->Functions.find(name) != type->Functions.end()) {
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto f : type->Functions.at(name)->functions) {
            if (spec >= f->access)
                resolvables.insert(analyzer.GetCallableForFunction(f, self->GetType(), name));
        }
        if (!resolvables.empty())
            return analyzer.GetOverloadSet(resolvables, analyzer.GetRvalueType(self->GetType()))->BuildValueConstruction(Expressions(std::move(self)), c);
    }
    // Any of our bases have this member?
    Type* BaseType = nullptr;
    OverloadSet* BaseOverloadSet = nullptr;
    for (auto base : GetMemberData().bases) {
        auto baseobj = AccessBase(Wide::Memory::MakeUnique<ExpressionReference>(self.get()), base->GetSelfAsType());
        if (auto member = base->GetSelfAsType()->AccessMember(std::move(baseobj), name, c)) {
            // If there's nothing there, we win.
            // If we're an OS and the existing is an OS, we win by unifying.
            // Else we lose.
            auto otheros = dynamic_cast<OverloadSet*>(member->GetType()->Decay());
            if (!BaseType) {
                if (otheros) {
                    if (BaseOverloadSet)
                        BaseOverloadSet = analyzer.GetOverloadSet(BaseOverloadSet, otheros, self->GetType());
                    else
                        BaseOverloadSet = otheros;
                    continue;
                }
                BaseType = base->GetSelfAsType();
                continue;
            }
            throw AmbiguousLookup(name, base->GetSelfAsType(), BaseType, c.where);
        }
    }
    if (BaseOverloadSet)
        return BaseOverloadSet->BuildValueConstruction(Expressions(std::move(self)), c);
    if (!BaseType)
        return nullptr;
    return BaseType->AccessMember(AccessBase(std::move(self), BaseType), name, c);
}

Wide::Util::optional<clang::QualType> UserDefinedType::GetClangType(ClangTU& TU) {
    if (clangtypes.find(&TU) != clangtypes.end())
        return clangtypes[&TU];
    if (IsDynamic()) return Wide::Util::none;
    
    std::stringstream stream;
    // The name of the LLVM type is generated by AggregateType based on "this".
    // so downcast before using this to get the same name.
    // Should refactor someday
    stream << "__" << (AggregateType*)this;

    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(stream.str()));
    recdecl->startDefinition();
    clangtypes[&TU] = TU.GetASTContext().getTypeDeclType(recdecl);
    if (type->bases.size() != 0) return Util::none;

    for (std::size_t i = 0; i < type->variables.size(); ++i) {
        auto memberty = GetContents()[i]->GetClangType(TU);
        if (!memberty) return Wide::Util::none;
        auto var = clang::FieldDecl::Create(
            TU.GetASTContext(),
            recdecl,
            clang::SourceLocation(),
            clang::SourceLocation(),
            TU.GetIdentifierInfo(type->variables[i].first->name.front().name),
            *memberty,
            nullptr,
            nullptr,
            false,
            clang::InClassInitStyle::ICIS_NoInit
            );
        var->setAccess(clang::AccessSpecifier::AS_public);
        recdecl->addDecl(var);
    }
    /*for (auto overset : type->Functions) {
        for (auto func : overset.second->functions) {
            if (IsMultiTyped(func)) continue;
            std::vector<Type*> types;
            for (auto arg : func->args) {
                if (!arg.type) return;
                auto expr = a.AnalyzeExpression(GetContext(a), arg.type, [](ConcreteExpression expr) {});
                if (auto ty = dynamic_cast<ConstructorType*>(expr.t->Decay()))
                    types.push_back(ty->GetConstructedType());
                else
                    return;
            }
            auto mfunc = a.GetWideFunction(func, GetContext(a), types, overset.first);
            mfunc->ComputeBody(a);

        }
    }*/
    // Todo: Expose member functions
    // Only those which are not generic right now
    recdecl->completeDefinition();
    auto size = TU.GetASTContext().getTypeSizeInChars(TU.GetASTContext().getTypeDeclType(recdecl).getTypePtr());
    TU.GetDeclContext()->addDecl(recdecl);
    analyzer.AddClangType(TU.GetASTContext().getTypeDeclType(recdecl), this);
    return clangtypes[&TU];
}

bool UserDefinedType::HasMember(std::string name) {
    return type->Functions.find(name) != type->Functions.end() || GetMemberData().members.find(name) != GetMemberData().members.end();
}

bool UserDefinedType::BinaryComplex(llvm::Module* module) {
    if (BCCache)
        return *BCCache;
    bool IsBinaryComplex = AggregateType::IsComplexType(module);
    if (type->Functions.find("type") != type->Functions.end()) {
        std::vector<Type*> copytypes;
        copytypes.push_back(analyzer.GetLvalueType(this));
        copytypes.push_back(copytypes.front());
        IsBinaryComplex = IsBinaryComplex || analyzer.GetOverloadSet(type->Functions.at("type"), analyzer.GetLvalueType(this), "type")->Resolve(copytypes, this);
    }
    if (type->Functions.find("~") != type->Functions.end()) 
        IsBinaryComplex = true;
    BCCache = IsBinaryComplex;
    return *BCCache;
}

#pragma warning(disable : 4800)

bool UserDefinedType::UserDefinedComplex() {
    if (UDCCache)
        return *UDCCache;
    // We are user-defined complex if the user specified any of the following:
    // Copy/move constructor, copy/move assignment operator, destructor.
    bool IsUserDefinedComplex = false;
    if (type->Functions.find("type") != type->Functions.end()) {
        std::vector<Type*> copytypes;
        copytypes.push_back(analyzer.GetLvalueType(this));
        copytypes.push_back(copytypes.front());
        IsUserDefinedComplex = IsUserDefinedComplex || analyzer.GetOverloadSet(type->Functions.at("type"), analyzer.GetLvalueType(this), "type")->Resolve(copytypes, this);

        std::vector<Type*> movetypes;
        movetypes.push_back(analyzer.GetLvalueType(this));
        movetypes.push_back(analyzer.GetRvalueType(this));
        IsUserDefinedComplex = IsUserDefinedComplex || analyzer.GetOverloadSet(type->Functions.at("type"), analyzer.GetLvalueType(this), "type")->Resolve(movetypes, this);
    }
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end()) {
        std::vector<Type*> copytypes;
        copytypes.push_back(analyzer.GetLvalueType(this));
        copytypes.push_back(copytypes.front());
        IsUserDefinedComplex = IsUserDefinedComplex || analyzer.GetOverloadSet(type->opcondecls.at(Lexer::TokenType::Assignment), analyzer.GetLvalueType(this), GetNameForOperator(Lexer::TokenType::Assignment))->Resolve(copytypes, this);

        std::vector<Type*> movetypes;
        movetypes.push_back(analyzer.GetLvalueType(this));
        movetypes.push_back(analyzer.GetRvalueType(this));
        IsUserDefinedComplex = IsUserDefinedComplex || analyzer.GetOverloadSet(type->opcondecls.at(Lexer::TokenType::Assignment), analyzer.GetLvalueType(this), GetNameForOperator(Lexer::TokenType::Assignment))->Resolve(movetypes, this);
    }
    if (type->Functions.find("~") != type->Functions.end())
        IsUserDefinedComplex = true;
    UDCCache = IsUserDefinedComplex;
    return *UDCCache;
}

OverloadSet* UserDefinedType::CreateConstructorOverloadSet(Lexer::Access access) {
    auto user_defined = [&, this] {
        if (type->Functions.find("type") == type->Functions.end())
            return analyzer.GetOverloadSet();
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto f : type->Functions.at("type")->functions) {
            if (f->access <= access)
                resolvables.insert(analyzer.GetCallableForFunction(f, analyzer.GetLvalueType(this), "type"));
        }
        return analyzer.GetOverloadSet(resolvables, analyzer.GetLvalueType(this));
    };
    auto user_defined_constructors = user_defined();

    if (UserDefinedComplex())
        return user_defined_constructors;
    
    user_defined_constructors = analyzer.GetOverloadSet(user_defined_constructors, TupleInitializable::CreateConstructorOverloadSet(access));

    std::vector<Type*> types;
    types.push_back(analyzer.GetLvalueType(this));
    if (auto default_constructor = user_defined_constructors->Resolve(types, this))
        return analyzer.GetOverloadSet(user_defined_constructors, CreateNondefaultConstructorOverloadSet());
    if (GetMemberData().HasNSDMI) {
        bool shouldgenerate = true;
        for (auto&& mem : GetMembers()) {
            // If we are not default constructible *and* we don't have an NSDMI to construct us, then we cannot generate a default constructor.
            if (mem.vptr) {
                continue;
            }
            if (mem.InClassInitializer) {
                auto totally_not_this = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(this, Context( this, mem.location ));
                auto init = mem.InClassInitializer(std::move(totally_not_this));
                assert(mem.t->GetConstructorOverloadSet(GetAccessSpecifier(this, mem.t))->Resolve({ analyzer.GetLvalueType(mem.t), init->GetType() }, this));
                continue;
            }
            if (!mem.t->GetConstructorOverloadSet(GetAccessSpecifier(this, mem.t))->Resolve({ analyzer.GetLvalueType(mem.t) }, this))
                shouldgenerate = false;
        }
        if (!shouldgenerate)
            return analyzer.GetOverloadSet(user_defined_constructors, CreateNondefaultConstructorOverloadSet());
        // Our automatically generated default constructor needs to initialize each member appropriately.
        struct DefaultConstructorType : OverloadResolvable, Callable {
            DefaultConstructorType(UserDefinedType* ty) : self(ty) {}
            UserDefinedType* self;

            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
                if (args.size() != 1) return Util::none;
                if (args[0] != a.GetLvalueType(self)) return Util::none;
                return args;
            }
            Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final { return this; }
            std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c)  override final { return args; }
            std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
                struct NSDMIConstructor : Expression {
                    UserDefinedType* self;
                    std::unique_ptr<Expression> arg;
                    std::vector<std::unique_ptr<Expression>> initializers;
                    NSDMIConstructor(UserDefinedType* s, std::unique_ptr<Expression> obj, Lexer::Range where)
                        : self(s), arg(std::move(obj)) 
                    {
                        for (auto&& mem : self->GetMembers()) {
                            if (mem.vptr) {
                                initializers.push_back(mem.InClassInitializer(Wide::Memory::MakeUnique<ExpressionReference>(arg.get())));
                                continue;
                            }
                            auto num = mem.num;
                            auto member = CreatePrimUnOp(Wide::Memory::MakeUnique<ExpressionReference>(arg.get()), self->analyzer.GetLvalueType(mem.t), [num](llvm::Value* val, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                                return bb.CreateStructGEP(val, num);
                            });
                            if (mem.InClassInitializer)
                                initializers.push_back(mem.t->BuildInplaceConstruction(std::move(member), Expressions(mem.InClassInitializer(Wide::Memory::MakeUnique<ExpressionReference>(arg.get()))), Context(self, where)));
                            else
                                initializers.push_back(mem.t->BuildInplaceConstruction(std::move(member), Expressions(), { self, where }));
                        }
                    }
                    Type* GetType() override final {
                        return self->analyzer.GetLvalueType(self);
                    }
                    void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                        for (auto rit = initializers.rbegin(); rit != initializers.rend(); ++rit)
                            (*rit)->DestroyLocals(module, bb, allocas);
                        arg->DestroyLocals(module, bb, allocas);
                    }
                    llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                        for (auto&& init : initializers)
                            init->GetValue(module, bb, allocas);
                        return arg->GetValue(module, bb, allocas);
                    }
                };
                return Wide::Memory::MakeUnique<NSDMIConstructor>(self, std::move(args[0]), c.where);
            }
        };
        DefaultConstructor = Wide::Memory::MakeUnique<DefaultConstructorType>(this);
        return analyzer.GetOverloadSet(analyzer.GetOverloadSet(DefaultConstructor.get()), CreateNondefaultConstructorOverloadSet());
    }
    return analyzer.GetOverloadSet(user_defined_constructors, AggregateType::CreateConstructorOverloadSet(access));
}

OverloadSet* UserDefinedType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType name, Lexer::Access access) {
    assert(self->Decay() == this);
    auto user_defined = [&, this] {
        if (type->opcondecls.find(name) != type->opcondecls.end()) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto&& f : type->opcondecls.at(name)->functions) {
                if (f->access <= access)
                    resolvable.insert(analyzer.GetCallableForFunction(f, self, GetNameForOperator(name)));
            }
            return analyzer.GetOverloadSet(resolvable, self);
        }
        return analyzer.GetOverloadSet();
    };

    if (name == Lexer::TokenType::Assignment) {
        if (UserDefinedComplex()) {
            return user_defined();
        }
    }
    if (type->opcondecls.find(name) != type->opcondecls.end())
        return analyzer.GetOverloadSet(user_defined(), AggregateType::CreateOperatorOverloadSet(self, name, access));
    return AggregateType::CreateOperatorOverloadSet(self, name, access);
}

std::unique_ptr<Expression> UserDefinedType::BuildDestructorCall(std::unique_ptr<Expression> self, Context c) {
    auto selfref = Wide::Memory::MakeUnique<ExpressionReference>(self.get());
    auto aggcall = AggregateType::BuildDestructorCall(std::move(self), c);
    if (type->Functions.find("~") != type->Functions.end()) {
        auto desset = analyzer.GetOverloadSet(type->Functions.at("~"), selfref->GetType(), "~");
        auto call = desset->BuildCall(desset->BuildValueConstruction(Expressions(std::move(selfref) ), { this, type->Functions.at("~")->where.front() }), Expressions(), { this, type->Functions.at("~")->where.front() });
        return BuildChain(std::move(call), std::move(aggcall));
    }
    return aggcall;
}

// Gotta override these to respect our user-defined functions
// else aggregatetype will just assume them.
// Could just return Type:: all the time but AggregateType will be faster.
bool UserDefinedType::IsCopyConstructible(Lexer::Access access) {
    if (type->Functions.find("type") != type->Functions.end())
        return Type::IsCopyConstructible(access);
    return AggregateType::IsCopyConstructible(access);
}
bool UserDefinedType::IsMoveConstructible(Lexer::Access access) {
    if (type->Functions.find("type") != type->Functions.end())
        return Type::IsMoveConstructible(access);
    return AggregateType::IsMoveConstructible(access);
}
bool UserDefinedType::IsCopyAssignable(Lexer::Access access) {
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end())
        return Type::IsCopyAssignable(access);
    return AggregateType::IsCopyAssignable(access);
}
bool UserDefinedType::IsMoveAssignable(Lexer::Access access) {
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end())
        return Type::IsMoveAssignable(access);
    return AggregateType::IsMoveAssignable(access);
}

bool UserDefinedType::IsComplexType(llvm::Module* module) {
    return BinaryComplex(module);
}

Wide::Util::optional<std::vector<Type*>> UserDefinedType::GetTypesForTuple() {
    if (UserDefinedComplex())
        return Wide::Util::none;
    return GetContents();
}

InheritanceRelationship UserDefinedType::IsDerivedFrom(Type* other) {
    auto base = dynamic_cast<BaseType*>(other);
    if (!base) return InheritanceRelationship::NotDerived;
    InheritanceRelationship result = InheritanceRelationship::NotDerived;
    for (std::size_t i = 0; i < type->bases.size(); ++i) {
        auto ourbase = dynamic_cast<BaseType*>(GetContents()[i]);
        assert(ourbase);
        if (ourbase == base) {
            if (result == InheritanceRelationship::NotDerived)
                result = InheritanceRelationship::UnambiguouslyDerived;
            else if (result == InheritanceRelationship::UnambiguouslyDerived)
                result = InheritanceRelationship::AmbiguouslyDerived;
            continue;
        }
        auto subresult = ourbase->IsDerivedFrom(other);
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
std::unique_ptr<Expression> UserDefinedType::AccessBase(std::unique_ptr<Expression> self, Type* other) {
    other = other->Decay();
    assert(IsDerivedFrom(other) == InheritanceRelationship::UnambiguouslyDerived);
    auto otherbase = dynamic_cast<BaseType*>(other);
    // It is unambiguous so just hit on the first base we come across
    for (std::size_t i = 0; i < type->bases.size(); ++i) {
        auto base = GetMemberData().bases[i];
        if (base == otherbase)
            return PrimitiveAccessMember(std::move(self), i);
        if (base->IsDerivedFrom(other) == InheritanceRelationship::UnambiguouslyDerived)
            return base->AccessBase(PrimitiveAccessMember(std::move(self), i), other);
    }
    assert(false);
    return nullptr;
    // shush warning
}

// Implements TupleInitializable::PrimitiveAccessMember.
std::unique_ptr<Expression> UserDefinedType::PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num) {
    return AggregateType::PrimitiveAccessMember(std::move(self), num);
}

std::string UserDefinedType::explain() {
    if (context == analyzer.GetGlobalModule())
        return source_name;
    return GetContext()->explain() + "." + source_name;
} 
std::vector<BaseType::VirtualFunction> UserDefinedType::ComputeVTableLayout() {
    for (auto vfunc : GetMemberData().VTableIndices) {
        GetMemberData().funcs[vfunc.second].ret = analyzer.GetWideFunction(vfunc.first, this, GetMemberData().funcs[vfunc.second].args, GetMemberData().funcs[vfunc.second].name)->GetSignature()->GetReturnType();
    }
    return GetMemberData().funcs;
}

std::unique_ptr<Expression> UserDefinedType::FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset) {
    if (type->Functions.find(name) == type->Functions.end())
        return nullptr;
    // args includes this, which will have a different type here.
    // Pretend that it really has our type.
    if (IsLvalueType(args[0]))
        args[0] = analyzer.GetLvalueType(this);
    else
        args[0] = analyzer.GetRvalueType(this);
    for (auto func : type->Functions.at(name)->functions) {
        std::vector<Type*> f_args;
        if (func->args.size() == 0 || func->args.front().name != "this")
            f_args.push_back(analyzer.GetLvalueType(this));
        for (auto arg : func->args) {
            auto ty = AnalyzeExpression(GetContext(), arg.type, analyzer)->GetType()->Decay();
            auto con_type = dynamic_cast<ConstructorType*>(ty);
            if (!con_type)
                throw Wide::Semantic::NotAType(ty, arg.location);
            f_args.push_back(con_type->GetConstructedType());
        }
        if (args != f_args)
            continue;
        auto widefunc = analyzer.GetWideFunction(func, this, f_args, name);
        widefunc->ComputeBody();
        if (widefunc->GetSignature()->GetReturnType() != ret)
            throw std::runtime_error("fuck");

        struct VTableThunk : Expression {
            VTableThunk(Function* f, unsigned off)
                : widefunc(f), offset(off) {}
            Function* widefunc;
            unsigned offset;
            Type* GetType() override final {
                return widefunc->GetSignature();
            }
            llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>&, llvm::IRBuilder<>&) override final {
                widefunc->EmitCode(module);
                if (offset == 0)
                    return module->getFunction(widefunc->GetName());
                auto this_index = (std::size_t)widefunc->GetSignature()->GetReturnType()->IsComplexType(module);
                std::stringstream strstr;
                strstr << "__" << this << offset;
                auto thunk = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(widefunc->GetSignature()->GetLLVMType(module)->getElementType()), llvm::GlobalValue::LinkageTypes::InternalLinkage, strstr.str(), module);
                llvm::BasicBlock* bb = llvm::BasicBlock::Create(module->getContext(), "entry", thunk);
                llvm::IRBuilder<> irbuilder(bb);
                auto self = std::next(thunk->arg_begin(), widefunc->GetSignature()->GetReturnType()->IsComplexType(module));
                auto offset_self = irbuilder.CreateConstGEP1_32(irbuilder.CreatePointerCast(self, llvm::IntegerType::getInt8PtrTy(module->getContext())), -offset);
                auto cast_self = irbuilder.CreatePointerCast(offset_self, std::next(module->getFunction(widefunc->GetName())->arg_begin(), this_index)->getType());
                std::vector<llvm::Value*> args;
                for (std::size_t i = 0; i < thunk->arg_size(); ++i) {
                    if (i == this_index)
                        args.push_back(cast_self);
                    else
                        args.push_back(std::next(thunk->arg_begin(), i));
                }
                auto call = irbuilder.CreateCall(module->getFunction(widefunc->GetName()), args);
                if (call->getType() == llvm::Type::getVoidTy(module->getContext()))
                    irbuilder.CreateRetVoid();
                else
                    irbuilder.CreateRet(call);
                return thunk;
            }
            void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& b, llvm::IRBuilder<>&) override final {}
        };
        return Wide::Memory::MakeUnique<VTableThunk>(widefunc, offset);
    }
    return nullptr;
}

std::unique_ptr<Expression> UserDefinedType::GetVirtualPointer(std::unique_ptr<Expression> self) {
    assert(self->GetType()->IsReference());
    if (!IsDynamic()) return nullptr;
    return CreatePrimUnOp(std::move(self), analyzer.GetLvalueType(analyzer.GetPointerType(GetVirtualPointerType())), [this](llvm::Value* self, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
        return bb.CreateStructGEP(self, GetFieldIndex(type->variables.size() + type->bases.size()));
    });
}

Type* UserDefinedType::GetVirtualPointerType() {
    return analyzer.GetFunctionType(analyzer.GetIntegralType(32, false), {}, false);
}

std::vector<std::pair<BaseType*, unsigned>> UserDefinedType::GetBasesAndOffsets() {
    std::vector<std::pair<BaseType*, unsigned>> out;
    for (unsigned i = 0; i < type->bases.size(); ++i) {
        out.push_back(std::make_pair(GetMemberData().bases[i], GetOffset(i)));
    }
    return out;
}
std::vector<BaseType*> UserDefinedType::GetBases() {
    return GetMemberData().bases;
}
Wide::Util::optional<unsigned> UserDefinedType::GetVirtualFunctionIndex(const AST::Function* func) {
    if (GetMemberData().VTableIndices.size() == 0)
        ComputeVTableLayout();
    if (GetMemberData().VTableIndices.find(func) == GetMemberData().VTableIndices.end())
        return Wide::Util::none;
    return GetMemberData().VTableIndices.at(func);
}
bool UserDefinedType::IsA(Type* self, Type* other, Lexer::Access access) {
    if (Type::IsA(self, other, access)) return true;
    if (self == this)
        return analyzer.GetRvalueType(this)->IsA(analyzer.GetRvalueType(self), other, access);
    return false;
}