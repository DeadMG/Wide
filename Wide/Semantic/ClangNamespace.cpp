#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangOverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Util/MakeUnique.h>

#pragma warning(push, 0)

#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>
#include <clang/AST/UnresolvedSet.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Expression ClangNamespace::AccessMember(Expression val, std::string name, Analyzer& a) {        
    clang::LookupResult lr(
        from->GetSema(), 
        clang::DeclarationNameInfo(clang::DeclarationName(from->GetIdentifierInfo(name)), clang::SourceLocation()),
        clang::Sema::LookupNameKind::LookupOrdinaryName);

    if (!from->GetSema().LookupQualifiedName(lr, con))
        throw std::runtime_error("Attempted to access a member of a Clang namespace, but Clang could not find the name.");
    if (lr.isAmbiguous())
        throw std::runtime_error("Attempted to access a member of a Clang namespace, but Clang said the lookup was ambiguous.");
    if (lr.isSingleResult()) {
        auto result = lr.getFoundDecl()->getCanonicalDecl();
        // If result is a function, namespace, or variable decl, we're good. Else, cry like a little girl. A LITTLE GIRL.
        if (auto fundecl = llvm::dyn_cast<clang::FunctionDecl>(result)) {
            std::vector<Type*> args;
            for(auto decl = fundecl->param_begin(); decl != fundecl->param_end(); ++decl) {
                args.push_back(a.GetClangType(*from, (*decl)->getType()));
            }
            return Expression(a.GetFunctionType(a.GetClangType(*from, fundecl->getResultType()), args), a.gen->CreateFunctionValue(from->MangleName(fundecl)));
        }
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(result)) {
            return Expression(a.AsLvalueType(a.GetClangType(*from, vardecl->getType())), a.gen->CreateGlobalVariable(from->MangleName(vardecl)));
        }
        if (auto namedecl = llvm::dyn_cast<clang::NamespaceDecl>(result)) {
            return a.GetConstructorType(a.GetClangNamespace(*from, namedecl))->BuildValueConstruction(a);
        }
        if (auto typedefdecl = llvm::dyn_cast<clang::TypeDecl>(result)) {
            return a.GetConstructorType(a.GetClangType(*from, from->GetASTContext().getTypeDeclType(typedefdecl)))->BuildValueConstruction(a);
        }
        if (auto tempdecl = llvm::dyn_cast<clang::ClassTemplateDecl>(result)) {
            return a.GetClangTemplateClass(*from, tempdecl)->BuildValueConstruction(a);
        }
        throw std::runtime_error("Found a decl but didn't know how to interpret it.");
    }
    auto ptr = Wide::Memory::MakeUnique<clang::UnresolvedSet<8>>();
    clang::UnresolvedSet<8>& us = *ptr;
    for(auto it = lr.begin(); it != lr.end(); ++it) {
        us.addDecl(*it);
    }
    return Expression(a.arena.Allocate<ClangOverloadSet>(std::move(ptr), from, nullptr), nullptr);
}