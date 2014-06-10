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
#include <Wide/Semantic/ClangType.h>
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

// Reference http://refspecs.linuxbase.org/cxxabi-1.83.html
namespace {
    static const std::string DestructorName = "~";
}

void AddAllBases(std::unordered_set<Type*>& all_bases, Type* root) {
    all_bases.insert(root);
    for (auto base : root->GetBases()) {
        all_bases.insert(base);
        AddAllBases(all_bases, base);
    }
}
UserDefinedType::BaseData::BaseData(UserDefinedType* self) {
    for (auto expr : self->type->bases) {
        auto base = self->analyzer.AnalyzeExpression(self->context, expr);
        auto con = dynamic_cast<ConstructorType*>(base->GetType()->Decay());
        if (!con) throw NotAType(base->GetType(), expr->location);
        // Should be UDT or ClangType right now.
        auto udt = (Type*)dynamic_cast<UserDefinedType*>(con->GetConstructedType());
        if (udt == self) throw InvalidBase(con->GetConstructedType(), expr->location);
        if (!udt) udt = dynamic_cast<ClangType*>(con->GetConstructedType());
        if (!udt) throw InvalidBase(con->GetConstructedType(), expr->location);
        bases.push_back(udt);
        // 2.4.1 (b)
        if (!PrimaryBase && !udt->GetVtableLayout().layout.empty())
            PrimaryBase = udt;
    }
}

UserDefinedType::VTableData::VTableData(UserDefinedType* self) {
    // If we have a primary base, our primary vtable starts with theirs.
    if (self->GetPrimaryBase()) funcs = self->GetPrimaryBase()->GetPrimaryVTable();
    std::unordered_set<Type*> all_bases;
    for (auto&& base : self->GetBases())
        AddAllBases(all_bases, base);

    std::unordered_map<const AST::Function*, unsigned> primary_dynamic_functions;
    std::unordered_map<const AST::Function*, VTableLayout::VirtualFunctionEntry> dynamic_functions;
    unsigned index = 0;
    for (auto overset : self->type->Functions) {
        if (overset.first == "type") continue; // Do not check constructors.
        for (auto func : overset.second->functions) {
            // The function has to be not explicitly marked dynamic *and* not dynamic by any base class.
            if (IsMultiTyped(func)) continue;

            if (overset.first == DestructorName) {
                for (auto base : all_bases) {
                    for (unsigned i = 0; i < base->GetPrimaryVTable().layout.size(); ++i) {
                        if (auto mem = boost::get<VTableLayout::SpecialMember*>(base->GetPrimaryVTable().layout[i].function)) {
                            if (*mem == VTableLayout::SpecialMember::Destructor) {
                                if (base == self->GetPrimaryBase())
                                    primary_dynamic_functions[func] = i;
                                else
                                    dynamic_functions[func] = { false, VTableLayout::SpecialMember::Destructor };
                            }
                        }
                    }
                }
                if (func->dynamic)
                    dynamic_functions[func] = { false, VTableLayout::SpecialMember::Destructor };
            }
            else {
                std::vector<Type*> arguments;
                if (func->args.size() == 0 || func->args.front().name != "this")
                    arguments.push_back(self->analyzer.GetLvalueType(self));
                for (auto arg : func->args) {
                    auto ty = self->analyzer.AnalyzeCachedExpression(self->context, arg.type)->GetType()->Decay();
                    auto con_type = dynamic_cast<ConstructorType*>(ty);
                    if (!con_type)
                        throw Wide::Semantic::NotAType(ty, arg.location);
                    arguments.push_back(con_type->GetConstructedType());
                }

                for (auto base : all_bases) {
                    auto base_layout = base->GetPrimaryVTable();
                    for (unsigned i = 0; i < base_layout.layout.size(); ++i) {
                        if (auto vfunc = boost::get<VTableLayout::VirtualFunction>(&base_layout.layout[i].function)) {
                            if (vfunc->name != overset.first) continue;
                            // The this arguments will be different, but they should have the same category of lvalue/rvalue.
                            if (arguments.size() != vfunc->args.size()) continue;
                            for (std::size_t argi = 1; argi < arguments.size(); ++argi) {
                                if (vfunc->args[argi] != arguments[argi])
                                    continue;
                            }
                            // Score- it's inherited.
                            if (base == self->GetPrimaryBase())
                                primary_dynamic_functions[func] = i;
                            else {
                                auto vfunc = VTableLayout::VirtualFunction{ overset.first, arguments, self->analyzer.GetWideFunction(func, self, arguments, overset.first)->GetSignature()->GetReturnType() };
                                dynamic_functions[func] = { false, vfunc };
                            }
                        }
                    }
                }
                if (func->dynamic){
                    auto vfunc = VTableLayout::VirtualFunction{ overset.first, arguments, self->analyzer.GetWideFunction(func, self, arguments, overset.first)->GetSignature()->GetReturnType() };
                    dynamic_functions[func] = { false, vfunc };
                }
            }
        }
    }

    // If we don't have a primary base but we do have dynamic functions, first add offset and RTTI
    if (!self->GetPrimaryBase() && !dynamic_functions.empty()) {
        funcs.offset = 2;
        VTableLayout::VirtualFunctionEntry offset;
        VTableLayout::VirtualFunctionEntry rtti;
        offset.abstract = false;
        rtti.abstract = false;
        offset.function = VTableLayout::SpecialMember::OffsetToTop;
        rtti.function = VTableLayout::SpecialMember::RTTIPointer;
        funcs.layout.insert(funcs.layout.begin(), rtti);
        funcs.layout.insert(funcs.layout.begin(), offset);
    }

    // For every dynamic function that is not inherited from the primary base, add a slot.
    // Else, set the slot to the primary base's slot.
    for (auto func : dynamic_functions) {
        if (primary_dynamic_functions.find(func.first) == primary_dynamic_functions.end()) {
            VTableIndices[func.first] = funcs.layout.size() - funcs.offset;
            funcs.layout.push_back(func.second);
            continue;
        }
        VTableIndices[func.first] = primary_dynamic_functions[func.first] - funcs.offset;
    }
}
UserDefinedType::MemberData::MemberData(UserDefinedType* self) {
    for (auto&& var : self->type->variables) {
        member_indices[var.first->name.front().name] = members.size();
        auto expr = self->analyzer.AnalyzeExpression(self->context, var.first->initializer);
        if (auto con = dynamic_cast<ConstructorType*>(expr->GetType()->Decay())) {
            members.push_back(con->GetConstructedType());
            NSDMIs.push_back(nullptr);
        } else {
            members.push_back(expr->GetType()->Decay());
            HasNSDMI = true;
            NSDMIs.push_back(var.first->initializer);
        }
        if (auto agg = dynamic_cast<AggregateType*>(members.back())) {
            if (agg == self || agg->HasMemberOfType(self))
                throw RecursiveMember(self, var.first->initializer->location);
        }
    }
}
UserDefinedType::MemberData::MemberData(MemberData&& other)
: members(std::move(other.members))
, NSDMIs(std::move(other.NSDMIs))
, HasNSDMI(other.HasNSDMI)
, member_indices(std::move(other.member_indices)) {}

UserDefinedType::MemberData& UserDefinedType::MemberData::operator=(MemberData&& other) {
    members = std::move(other.members);
    NSDMIs = std::move(other.NSDMIs);
    HasNSDMI = other.HasNSDMI;
    member_indices = std::move(other.member_indices);
    return *this;
}

UserDefinedType::UserDefinedType(const AST::Type* t, Analyzer& a, Type* higher, std::string name)
: AggregateType(a)
, context(higher)
, type(t)
, source_name(name) 
{
}

std::vector<UserDefinedType::member> UserDefinedType::GetConstructionMembers() {
    std::vector<UserDefinedType::member> out;
    for (unsigned i = 0; i < type->bases.size(); ++i) {
        member m(type->bases[i]->location);
        m.t = GetBases()[i];
        m.name = dynamic_cast<AST::Identifier*>(type->bases[i])->val;
        m.num = { GetOffset(i) };
        out.push_back(std::move(m));
    }
    for (unsigned i = 0; i < type->variables.size(); ++i) {
        member m(type->variables[i].first->location);
        m.t = GetMembers()[i];
        m.name = type->variables[i].first->name.front().name;
        m.num = { GetOffset(i + type->bases.size()) };
        if (GetMemberData().NSDMIs[i])
            m.InClassInitializer = [this, i](std::unique_ptr<Expression>) { return analyzer.AnalyzeExpression(context, GetMemberData().NSDMIs[i]); };
        out.push_back(std::move(m));
    }
    return out;
}

std::unique_ptr<Expression> UserDefinedType::AccessMember(std::unique_ptr<Expression> self, std::string name, Context c) {
    //if (!self->GetType()->IsReference())
    //    self = BuildRvalueConstruction(Expressions(std::move(self)), { this, c.where });
    auto spec = GetAccessSpecifier(c.from, this);
    if (GetMemberData().member_indices.find(name) != GetMemberData().member_indices.end()) {
        auto member = type->variables[GetMemberData().member_indices[name]];
        if (spec >= member.second)
            return PrimitiveAccessMember(std::move(self), GetMemberData().member_indices[name] + type->bases.size());
    }
    if (type->Functions.find(name) != type->Functions.end()) {
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto f : type->Functions.at(name)->functions) {
            if (spec >= f->access)
                resolvables.insert(analyzer.GetCallableForFunction(f, self->GetType(), name));
        }
        auto selfty = self->GetType();
        if (!resolvables.empty())
            return analyzer.GetOverloadSet(resolvables, analyzer.GetRvalueType(selfty))->BuildValueConstruction(Expressions(std::move(self)), c);
    }
    // Any of our bases have this member?
    Type* BaseType = nullptr;
    OverloadSet* BaseOverloadSet = nullptr;
    for (auto base : GetBaseData().bases) {
        auto baseobj = AccessBase(Wide::Memory::MakeUnique<ExpressionReference>(self.get()), base);
        if (auto member = base->AccessMember(std::move(baseobj), name, c)) {
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
                BaseType = base;
                continue;
            }
            throw AmbiguousLookup(name, base, BaseType, c.where);
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
        auto memberty = GetMembers()[i]->GetClangType(TU);
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
    return type->Functions.find(name) != type->Functions.end() || GetMemberData().member_indices.find(name) != GetMemberData().member_indices.end();
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
        for (auto&& mem : GetConstructionMembers()) {
            // If we are not default constructible *and* we don't have an NSDMI to construct us, then we cannot generate a default constructor.
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
                        for (auto&& mem : self->GetConstructionMembers()) {
                            auto num = mem.num;
                            auto result = self->analyzer.GetLvalueType(mem.t);
                            auto member = CreatePrimUnOp(Wide::Memory::MakeUnique<ExpressionReference>(arg.get()), self->analyzer.GetLvalueType(mem.t), [num, result](llvm::Value* val, CodegenContext& con) {
                                auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
                                self = con->CreateConstGEP1_32(self, num.offset);
                                return con->CreatePointerCast(self, result->GetLLVMType(con));                            
                            });
                            if (mem.InClassInitializer)
                                initializers.push_back(mem.t->BuildInplaceConstruction(std::move(member), Expressions(mem.InClassInitializer(Wide::Memory::MakeUnique<ExpressionReference>(arg.get()))), Context(self, where)));
                            else
                                initializers.push_back(mem.t->BuildInplaceConstruction(std::move(member), Expressions(), { self, where }));
                        }
                        // We can be asked to use this constructor to initialize a base vptr override.
                        initializers.push_back(self->SetVirtualPointers(Wide::Memory::MakeUnique<ExpressionReference>(arg.get())));
                    }
                    Type* GetType() override final {
                        return self->analyzer.GetLvalueType(self);
                    }
                    llvm::Value* ComputeValue(CodegenContext& con) {
                        for (auto&& init : initializers)
                            init->GetValue(con);
                        return arg->GetValue(con);
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
    auto bases = GetBases();
    auto members = GetMembers();
    bases.insert(bases.end(), members.begin(), members.end());
    return bases;
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
Type::VTableLayout UserDefinedType::ComputePrimaryVTableLayout() {
    return GetVtableData().funcs;
}

std::unique_ptr<Expression> UserDefinedType::FunctionPointerFor(VTableLayout::VirtualFunction entry, unsigned offset) {
    auto name = entry.name;
    auto args = entry.args;
    auto ret = entry.ret;
    if (type->Functions.find(name) == type->Functions.end()) {
        // Could have been inherited from our primary base, if we have one.
        if (GetPrimaryBase()) {
            VTableLayout::VirtualFunctionEntry basentry;
            basentry.abstract = false;
            basentry.function = entry;
            return GetPrimaryBase()->VirtualEntryFor(basentry, offset);
        }
        return nullptr;
    }
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
            auto ty = analyzer.AnalyzeCachedExpression(GetContext(), arg.type)->GetType()->Decay();
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
            llvm::Value* ComputeValue(CodegenContext& con) override final {
                widefunc->EmitCode(con);
                if (offset == 0)
                    return con.module->getFunction(widefunc->GetName());
                auto this_index = (std::size_t)widefunc->GetSignature()->GetReturnType()->IsComplexType(con);
                std::stringstream strstr;
                strstr << "__" << this << offset;
                auto thunk = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(widefunc->GetSignature()->GetLLVMType(con)->getElementType()), llvm::GlobalValue::LinkageTypes::InternalLinkage, strstr.str(), con.module);
                llvm::BasicBlock* bb = llvm::BasicBlock::Create(con.module->getContext(), "entry", thunk);
                llvm::IRBuilder<> irbuilder(bb);
                auto self = std::next(thunk->arg_begin(), widefunc->GetSignature()->GetReturnType()->IsComplexType(con.module));
                auto offset_self = irbuilder.CreateConstGEP1_32(irbuilder.CreatePointerCast(self, llvm::IntegerType::getInt8PtrTy(con.module->getContext())), -offset);
                auto cast_self = irbuilder.CreatePointerCast(offset_self, std::next(con.module->getFunction(widefunc->GetName())->arg_begin(), this_index)->getType());
                std::vector<llvm::Value*> args;
                for (std::size_t i = 0; i < thunk->arg_size(); ++i) {
                    if (i == this_index)
                        args.push_back(cast_self);
                    else
                        args.push_back(std::next(thunk->arg_begin(), i));
                }
                auto call = irbuilder.CreateCall(con.module->getFunction(widefunc->GetName()), args);
                if (call->getType() == llvm::Type::getVoidTy(con.module->getContext()))
                    irbuilder.CreateRetVoid();
                else
                    irbuilder.CreateRet(call);
                return thunk;
            }
        };
        return Wide::Memory::MakeUnique<VTableThunk>(widefunc, offset);
    }
    return nullptr;
}
std::unique_ptr<Expression> UserDefinedType::VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry, unsigned offset) {
    if (auto special = boost::get<VTableLayout::SpecialMember>(&entry.function)) {
        if (*special == VTableLayout::SpecialMember::Destructor) {
            VTableLayout::VirtualFunction func;
            func.name = DestructorName;
            func.args = { analyzer.GetLvalueType(this) };
            func.ret = analyzer.GetVoidType();
            return FunctionPointerFor(func, offset);
        }
        if (*special == VTableLayout::SpecialMember::ItaniumABIDeletingDestructor) {
            VTableLayout::VirtualFunction func;
            func.name = DestructorName;
            func.args = { analyzer.GetLvalueType(this) };
            func.ret = analyzer.GetVoidType();
            return FunctionPointerFor(func, offset);
        }
    }
    return FunctionPointerFor(boost::get<VTableLayout::VirtualFunction>(entry.function), offset);
}

Type* UserDefinedType::GetVirtualPointerType() {
    if (GetBaseData().PrimaryBase)
        return GetBaseData().PrimaryBase->GetVirtualPointerType();
    return analyzer.GetFunctionType(analyzer.GetIntegralType(32, false), {}, false);
}

std::vector<std::pair<Type*, unsigned>> UserDefinedType::GetBasesAndOffsets() {
    std::vector<std::pair<Type*, unsigned>> out;
    for (unsigned i = 0; i < type->bases.size(); ++i) {
        out.push_back(std::make_pair(GetBaseData().bases[i], GetOffset(i)));
    }
    return out;
}
std::vector<Type*> UserDefinedType::GetBases() {
    return GetBaseData().bases;
}
Wide::Util::optional<unsigned> UserDefinedType::GetVirtualFunctionIndex(const AST::Function* func) {
    if (GetVtableData().VTableIndices.find(func) == GetVtableData().VTableIndices.end())
        return Wide::Util::none;
    return GetVtableData().VTableIndices.at(func);
}
bool UserDefinedType::IsA(Type* self, Type* other, Lexer::Access access) {
    if (Type::IsA(self, other, access)) return true;
    if (self == this)
        return analyzer.GetRvalueType(this)->IsA(analyzer.GetRvalueType(self), other, access);
    return false;
}
llvm::Constant* UserDefinedType::GetRTTI(llvm::Module* module) {
    // If we have a Clang type, then use it for compat.
    if (GetBases().size() == 0) {
        return AggregateType::GetRTTI(module);
    }
    std::stringstream stream;
    stream << "struct.__" << this;
    if (auto existing = module->getGlobalVariable(stream.str())) {
        return existing;
    }
    if (GetBases().size() == 1) {
        auto mangledname = GetGlobalString(stream.str(), module);
        auto vtable_name_of_rtti = "_ZTVN10__cxxabiv120__si_class_type_infoE";
        auto vtable = module->getOrInsertGlobal(vtable_name_of_rtti, llvm::Type::getInt8PtrTy(module->getContext()));
        vtable = llvm::ConstantExpr::getInBoundsGetElementPtr(vtable, llvm::ConstantInt::get(llvm::Type::getInt8Ty(module->getContext()), 2));
        vtable = llvm::ConstantExpr::getBitCast(vtable, llvm::Type::getInt8PtrTy(module->getContext()));
        std::vector<llvm::Constant*> inits = { vtable, mangledname, GetBases()[0]->GetRTTI(module) };
        auto ty = llvm::ConstantStruct::getTypeForElements(inits);
        auto rtti = new llvm::GlobalVariable(*module, ty, true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, llvm::ConstantStruct::get(ty, inits), stream.str());
        return rtti;
    }
    // Multiple bases. Yay.
    auto mangledname = GetGlobalString(stream.str(), module);
    auto vtable_name_of_rtti = "_ZTVN10__cxxabiv121__vmi_class_type_infoE";
    auto vtable = module->getOrInsertGlobal(vtable_name_of_rtti, llvm::Type::getInt8PtrTy(module->getContext()));
    vtable = llvm::ConstantExpr::getInBoundsGetElementPtr(vtable, llvm::ConstantInt::get(llvm::Type::getInt8Ty(module->getContext()), 2));
    vtable = llvm::ConstantExpr::getBitCast(vtable, llvm::Type::getInt8PtrTy(module->getContext()));
    std::vector<llvm::Constant*> inits = { vtable, mangledname, GetBases()[0]->GetRTTI(module) };

    // Add flags.
    inits.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(module->getContext()), 0));

    // Add base count
    inits.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(module->getContext()), GetBases().size()));

    // Add one entry for every base.
    for (auto bases : GetBasesAndOffsets()) {
        inits.push_back(bases.first->GetRTTI(module));
        unsigned flags = 0x2 | (bases.second << 8);
        inits.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(module->getContext()), flags));
    }

    auto ty = llvm::ConstantStruct::getTypeForElements(inits);
    auto rtti = new llvm::GlobalVariable(*module, ty, true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, llvm::ConstantStruct::get(ty, inits), stream.str());
    return rtti;
}
bool UserDefinedType::HasDeclaredDynamicFunctions() {
    for (auto overset : type->Functions)
        for (auto func : overset.second->functions)
            if (func->dynamic)
                return true;
    return false;
}
UserDefinedType::BaseData::BaseData(BaseData&& other)
: bases(std::move(other.bases)), PrimaryBase(other.PrimaryBase) {}

UserDefinedType::BaseData& UserDefinedType::BaseData::operator=(BaseData&& other) {
    bases = std::move(other.bases);
    PrimaryBase = other.PrimaryBase;
    return *this;
}
UserDefinedType::VTableData::VTableData(VTableData&& other)
: funcs(std::move(other.funcs))
, VTableIndices(std::move(other.VTableIndices)) {}

UserDefinedType::VTableData& UserDefinedType::VTableData::operator=(VTableData&& other) {
    funcs = std::move(other.funcs);
    VTableIndices = std::move(other.VTableIndices);
    return *this;
}