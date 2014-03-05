#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Lexer/Token.h>
#include <iostream>
#include <array>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/Lookup.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/DerivedTypes.h>
#include <clang/Sema/Overload.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

namespace std {
    template<> struct hash<clang::AccessSpecifier> {
        std::size_t operator()(clang::AccessSpecifier a) const {
            return std::hash<int>()((int)a);
        }
    };
}
const std::unordered_map<clang::AccessSpecifier, Lexer::Access> AccessMapping = {
    { clang::AccessSpecifier::AS_public, Lexer::Access::Public },
    { clang::AccessSpecifier::AS_private, Lexer::Access::Private },
    { clang::AccessSpecifier::AS_protected, Lexer::Access::Protected },
};

ConcreteExpression GetOverloadSet(clang::NamedDecl* d, ClangTU* from, ConcreteExpression self, Context c) {
    std::unordered_set<clang::NamedDecl*> decls;
    if (d)
        decls.insert(d);
    return c->GetOverloadSet(std::move(decls), from, self.t)->BuildValueConstruction({ self }, c);
}
OverloadSet* GetOverloadSet(clang::LookupResult& lr, ClangTU* from, ConcreteExpression self, Lexer::Access access, Analyzer& a) {
    std::unordered_set<clang::NamedDecl*> decls;
    for (auto decl : lr) {
        if (access < AccessMapping.at(decl->getAccess()))
            continue;
        decls.insert(decl);
    }
    if (decls.empty())
        return nullptr;
    return a.GetOverloadSet(std::move(decls), from, self.t);
}
OverloadSet* GetOverloadSet(clang::LookupResult& lr, ClangTU* from, ConcreteExpression self, Context c) {
    return GetOverloadSet(lr, from, self, GetAccessSpecifier(c, self.t), *c);
}

void ClangType::ProcessImplicitSpecialMember(std::function<bool()> needs, std::function<clang::CXXMethodDecl*()> declare, std::function<void(clang::CXXMethodDecl*)> define, std::function<clang::CXXMethodDecl*()> lookup) {
    if (needs()) {
        auto decl = declare();
        from->GetSema().EvaluateImplicitExceptionSpec(clang::SourceLocation(), decl);
        if (!decl->isDeleted())
            define(decl);
    }
    else {
        auto decl = lookup();
        if (decl && decl->isDefaulted() && !decl->isDeleted()) {
            if (decl->getType()->getAs<clang::FunctionProtoType>()->getExtProtoInfo().ExceptionSpecType == clang::ExceptionSpecificationType::EST_Unevaluated) {
                from->GetSema().EvaluateImplicitExceptionSpec(clang::SourceLocation(), decl);
            }
            if (!decl->doesThisDeclarationHaveABody()) {
                define(decl);
            }
        }
    }
};


ClangType::ClangType(ClangTU* src, clang::QualType t) 
    : from(src), type(t.getCanonicalType()) 
{
    // Declare any special members that need it.    
    // Also fix up their exception spec because for some reason Clang doesn't until you ask it.
    auto recdecl = type->getAsCXXRecordDecl();
    if (!recdecl) return;
    if (!recdecl->hasDefinition()) {
        auto spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(recdecl);
        auto loc = from->GetFileEnd();
        auto tsk = clang::TemplateSpecializationKind::TSK_ExplicitInstantiationDefinition;
        from->GetSema().InstantiateClassTemplateSpecialization(loc, spec, tsk);
        //from->GetSema().InstantiateClassTemplateSpecializationMembers(loc, llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(spec->getDefinition()), tsk);
    }
}
Wide::Util::optional<clang::QualType> ClangType::GetClangType(ClangTU& tu, Analyzer& a) {
    if (&tu != from) return Wide::Util::none;
    return type;
}

Wide::Util::optional<ConcreteExpression> ClangType::AccessMember(ConcreteExpression val, std::string name, Context c) {
    auto access = GetAccessSpecifier(c, this);

    if (val.t->Decay() == val.t)
        val = BuildRvalueConstruction({ val }, c);

    clang::LookupResult lr(
        from->GetSema(), 
        clang::DeclarationNameInfo(clang::DeclarationName(from->GetIdentifierInfo(name)), clang::SourceLocation()),
        clang::Sema::LookupNameKind::LookupOrdinaryName);
    lr.suppressDiagnostics();

    if (!from->GetSema().LookupQualifiedName(lr, type.getCanonicalType()->getAs<clang::TagType>()->getDecl()))
        return Wide::Util::none;
    if (lr.isAmbiguous())
        throw ClangLookupAmbiguous(name, this, c.where, *c);
    if (lr.isSingleResult()) {
        auto declaccess = AccessMapping.at(lr.getFoundDecl()->getAccess());
        if (access < declaccess)
            return Type::AccessMember(val, name, c);

        if (auto field = llvm::dyn_cast<clang::FieldDecl>(lr.getFoundDecl())) {
            auto ty = IsLvalueType(val.t) ? c->GetLvalueType(c->GetClangType(*from, field->getType())) : c->GetRvalueType(c->GetClangType(*from, field->getType()));
            return ConcreteExpression(ty, c->gen->CreateFieldExpression(val.Expr, from->GetFieldNumber(field)));
        }        
        if (auto fun = llvm::dyn_cast<clang::CXXMethodDecl>(lr.getFoundDecl()))
            return GetOverloadSet(fun, from, val, c);
        if (auto ty = llvm::dyn_cast<clang::TypeDecl>(lr.getFoundDecl()))
            return c->GetConstructorType(c->GetClangType(*from, from->GetASTContext().getTypeDeclType(ty)))->BuildValueConstruction({}, c);
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(lr.getFoundDecl())) {    
            return ConcreteExpression(c->GetLvalueType(c->GetClangType(*from, vardecl->getType())), c->gen->CreateGlobalVariable(from->MangleName(vardecl)));
        }
        if (auto tempdecl = llvm::dyn_cast<clang::ClassTemplateDecl>(lr.getFoundDecl()))
            return c->GetClangTemplateClass(*from, tempdecl)->BuildValueConstruction({}, c);
        throw ClangUnknownDecl(name, this, c.where, *c);
    }    
    auto set = GetOverloadSet(lr, from, val, c);
    return set ? set->BuildValueConstruction({ val }, c) : Type::AccessMember(val, name, c);
}

std::function<llvm::Type*(llvm::Module*)> ClangType::GetLLVMType(Analyzer& a) {
    return from->GetLLVMTypeFromClangType(type, a);
}
           
bool ClangType::IsComplexType(Analyzer& a) {
    auto decl = type.getCanonicalType()->getAsCXXRecordDecl();
    return decl && from->IsComplexType(decl);
}

Wide::Codegen::Expression* ClangType::BuildBooleanConversion(ConcreteExpression self, Context c) {
    if (self.t->Decay() == self.t)
        self = BuildRvalueConstruction({ self }, c);
    clang::OpaqueValueExpr ope(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(self.t));
    auto p = from->GetSema().PerformContextuallyConvertToBool(&ope);
    if (!p.get())
        throw std::runtime_error("Attempted to convert an object to bool contextually, but Clang said this was not possible.");
    clang::CallExpr* callexpr;
    if (auto ice = llvm::dyn_cast<clang::ImplicitCastExpr>(p.get())) {
        callexpr = llvm::dyn_cast<clang::CallExpr>(ice->getSubExpr());
    } else {
        callexpr = llvm::dyn_cast<clang::CallExpr>(p.get());
    }
       
    auto fun = callexpr->getDirectCallee();
    if (!fun) {
        std::cout << "Fun was 0.\n";
        llvm::raw_os_ostream out(std::cout);
        callexpr->printPretty(out, 0, clang::PrintingPolicy(from->GetASTContext().getLangOpts()));
        assert(false && "Attempted to contextually convert to bool, but Clang gave back an expression that did not involve a function call.");
    }

    Type* ret = c->GetClangType(*from, fun->getResultType());
    // As a conversion operator, it has to be a member method only taking this as argument.
    std::vector<Type*> types;
    types.insert(types.begin(), self.t);
    auto funty = c->GetFunctionType(ret, types);    
    ConcreteExpression clangfunc(funty, c->gen->CreateFunctionValue(from->MangleName(fun)));
    std::vector<ConcreteExpression> expressions;
    expressions.push_back(self);
    auto e = funty->BuildCall(clangfunc, std::move(expressions), c);

    // The return type should be bool.
    // If the function really returns an i1, the code generator will implicitly patch it up for us.
    if (e.t == c->GetBooleanType())
        return e.Expr;
    throw std::runtime_error("Attempted to contextually convert to bool, but Clang gave back a function that did not return a bool. WTF.");
}

#pragma warning(disable : 4244)
std::size_t ClangType::size(Analyzer& a) {
    return from->GetASTContext().getTypeSizeInChars(type).getQuantity();
}
std::size_t ClangType::alignment(Analyzer& a) {
    return from->GetASTContext().getTypeAlignInChars(type).getQuantity();
}
#pragma warning(default : 4244)
Type* ClangType::GetContext(Analyzer& a) {
    return a.GetClangNamespace(*from, type->getAsCXXRecordDecl()->getDeclContext());
}

namespace std {
    template<> struct iterator_traits<clang::ADLResult::iterator> {
        typedef std::input_iterator_tag iterator_category;
        typedef std::size_t difference_type;
    };
}

OverloadSet* ClangType::CreateADLOverloadSet(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access, Analyzer& a) {
    if (access != Lexer::Access::Public) return CreateADLOverloadSet(what, lhs, rhs, access, a);
    clang::ADLResult res;
    clang::OpaqueValueExpr lhsexpr(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(lhs));
    clang::OpaqueValueExpr rhsexpr(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(rhs));
    std::vector<clang::Expr*> exprs;
    exprs.push_back(&lhsexpr);
    exprs.push_back(&rhsexpr);
    from->GetSema().ArgumentDependentLookup(from->GetASTContext().DeclarationNames.getCXXOperatorName(GetTokenMappings().at(what).first), true, clang::SourceLocation(), exprs, res);
    std::unordered_set<clang::NamedDecl*> decls;
    decls.insert(res.begin(), res.end());
    return a.GetOverloadSet(std::move(decls), from, GetContext(a));
}

OverloadSet* ClangType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType name, Lexer::Access access, Analyzer& a) {
    if (!ProcessedAssignmentOperators && name == Lexer::TokenType::Assignment) {
        ProcessedAssignmentOperators = true;
        auto recdecl = type->getAsCXXRecordDecl();
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitCopyAssignment(); },
            [&]{ return from->GetSema().DeclareImplicitCopyAssignment(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitCopyAssignment(clang::SourceLocation(), decl); },
            [&]{ return from->GetSema().LookupCopyingAssignment(recdecl, 0, false, 0); }
        );
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitMoveAssignment(); },
            [&]{ return from->GetSema().DeclareImplicitMoveAssignment(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitMoveAssignment(clang::SourceLocation(), decl); },
            [&]{ return from->GetSema().LookupMovingAssignment(recdecl, 0, false, 0); }
        );        
    }
    auto opkind = GetTokenMappings().at(name).first;
    clang::LookupResult lr(
        from->GetSema(), 
        from->GetASTContext().DeclarationNames.getCXXOperatorName(opkind), 
        clang::SourceLocation(),
        clang::Sema::LookupNameKind::LookupOrdinaryName
    );
    lr.suppressDiagnostics();
    if (!from->GetSema().LookupQualifiedName(lr, type.getCanonicalType()->getAs<clang::TagType>()->getDecl()))
        return a.GetOverloadSet();

    std::unordered_set<clang::NamedDecl*> decls;
    for (auto decl : lr)
        if (AccessMapping.at(decl->getAccess()) <= access)
            decls.insert(decl);
    return a.GetOverloadSet(std::move(decls), from, self);
}

OverloadSet* ClangType::CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    if (!ProcessedConstructors) {
        auto recdecl = type->getAsCXXRecordDecl();
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitDefaultConstructor(); },
            [&]{ return from->GetSema().DeclareImplicitDefaultConstructor(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitDefaultConstructor(clang::SourceLocation(), static_cast<clang::CXXConstructorDecl*>(decl)); },
            [&]{ return from->GetSema().LookupDefaultConstructor(recdecl); }
        );
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitCopyConstructor(); },
            [&]{ return from->GetSema().DeclareImplicitCopyConstructor(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitCopyConstructor(clang::SourceLocation(), static_cast<clang::CXXConstructorDecl*>(decl)); },
            [&]{ return from->GetSema().LookupCopyingConstructor(recdecl, 0); }
        );
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitMoveConstructor(); },
            [&]{ return from->GetSema().DeclareImplicitMoveConstructor(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitMoveConstructor(clang::SourceLocation(), static_cast<clang::CXXConstructorDecl*>(decl)); },
            [&]{ return from->GetSema().LookupMovingConstructor(recdecl, 0); }
        );
        ProcessedConstructors = true;
    }
    auto cons = from->GetSema().LookupConstructors(type->getAsCXXRecordDecl());
    std::unordered_set<clang::NamedDecl*> decls;
    for (auto con : cons)
        if (AccessMapping.at(con->getAccess()) <= access)
            decls.insert(con);
    auto tupcon = GetTypesForTuple(a) ? TupleInitializable::CreateConstructorOverloadSet(a, Lexer::Access::Public) : a.GetOverloadSet();
    return a.GetOverloadSet(a.GetOverloadSet(decls, from, a.GetLvalueType(this)), tupcon);
}

OverloadSet* ClangType::CreateDestructorOverloadSet(Analyzer& a) {
    if (!ProcessedDestructors) {
        auto recdecl = type->getAsCXXRecordDecl();
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitDestructor(); },
            [&]{ return from->GetSema().DeclareImplicitDestructor(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitDestructor(clang::SourceLocation(), static_cast<clang::CXXDestructorDecl*>(decl)); },
            [&]{ return from->GetSema().LookupDestructor(recdecl); }
        );
        ProcessedDestructors = true;
    }
    auto des = from->GetSema().LookupDestructor(type->getAsCXXRecordDecl());
    if (des->isTrivial())
        return Type::CreateDestructorOverloadSet(a);
    std::unordered_set<clang::NamedDecl*> decls;
    decls.insert(des);
    return a.GetOverloadSet(decls, from, a.GetLvalueType(this));
}
Wide::Util::optional<std::vector<Type*>> ClangType::GetTypesForTuple(Analyzer& a) {
    auto recdecl = type->getAsCXXRecordDecl();
    if (!recdecl) return Wide::Util::none;
    if (recdecl->hasUserDeclaredCopyAssignment()
     || recdecl->hasUserDeclaredCopyConstructor()
     || recdecl->hasUserDeclaredDestructor()
     || recdecl->hasUserDeclaredMoveAssignment()
     || recdecl->hasUserDeclaredMoveConstructor())
        return Wide::Util::none;
    std::vector<Type*> types;
    for (auto it = recdecl->bases_begin(); it != recdecl->bases_end(); ++it) {
        types.push_back(a.GetClangType(*from, it->getType()));
    }
    for (auto it = recdecl->field_begin(); it != recdecl->field_end(); ++it) {
        types.push_back(a.GetClangType(*from, it->getType()));
    }
    return types;
}

ConcreteExpression ClangType::PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a) {
    std::size_t numbases = type->getAsCXXRecordDecl()->bases_end() - type->getAsCXXRecordDecl()->bases_begin();
    Codegen::Expression* fieldexpr = num >= numbases
        ? a.gen->CreateFieldExpression(e.Expr, from->GetFieldNumber(*std::next(type->getAsCXXRecordDecl()->field_begin(), num - numbases)))
        : a.gen->CreateFieldExpression(e.Expr, from->GetBaseNumber(type->getAsCXXRecordDecl(), (type->getAsCXXRecordDecl()->bases_begin() + num)->getType()->getAsCXXRecordDecl()));
    auto ty = GetTypesForTuple(a)->at(num);
    if (!e.t->IsReference())
        return ConcreteExpression(ty, fieldexpr);
    if (e.t->IsReference() && ty->IsReference())
        return ConcreteExpression(ty, a.gen->CreateLoad(fieldexpr));
    if (IsLvalueType(e.t))
        return ConcreteExpression(a.GetLvalueType(ty), fieldexpr);
    return ConcreteExpression(a.GetRvalueType(ty), fieldexpr);
}

InheritanceRelationship ClangType::IsDerivedFrom(Type* other, Analyzer& a) {
    auto otherbase = dynamic_cast<BaseType*>(other);
    if (!otherbase) return InheritanceRelationship::NotDerived;
    auto otherclangty = other->GetClangType(*from, a);
    if (!otherclangty) return InheritanceRelationship::NotDerived;
    auto otherdecl = (*otherclangty)->getAsCXXRecordDecl();
    auto recdecl = type->getAsCXXRecordDecl();
    InheritanceRelationship result = InheritanceRelationship::NotDerived;
    for (auto base = recdecl->bases_begin(); base != recdecl->bases_end(); ++base) {
        if (base->getType()->getAsCXXRecordDecl() == otherdecl) {
            if (result == InheritanceRelationship::NotDerived)
                result = InheritanceRelationship::UnambiguouslyDerived;
            result = InheritanceRelationship::AmbiguouslyDerived;
            continue;
        }
        auto Base = dynamic_cast<BaseType*>(a.GetClangType(*from, base->getType()));
        auto subresult = Base->IsDerivedFrom(other, a);
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
Codegen::Expression* ClangType::AccessBase(Type* other, Codegen::Expression* expr, Analyzer& a) {
    auto recdecl = type->getAsCXXRecordDecl();
    assert(IsDerivedFrom(other, a) == InheritanceRelationship::UnambiguouslyDerived);
    for (auto baseit = recdecl->bases_begin(); baseit != recdecl->bases_end(); ++baseit) {
        auto base = dynamic_cast<BaseType*>(a.GetClangType(*from, baseit->getType()));
        if (base == other)
            return a.gen->CreateFieldExpression(expr, from->GetBaseNumber(recdecl, baseit->getType()->getAsCXXRecordDecl()));
        if (base->IsDerivedFrom(other, a) == InheritanceRelationship::UnambiguouslyDerived)
            return base->AccessBase(other, a.gen->CreateFieldExpression(expr, from->GetBaseNumber(recdecl, baseit->getType()->getAsCXXRecordDecl())), a);
    }
    assert(false);
    return nullptr;
}
std::string ClangType::explain(Analyzer& a) {
    return GetContext(a)->explain(a) + "." + type->getAsCXXRecordDecl()->getName().str();
}