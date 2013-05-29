#include "ClangNamespace.h"
#include "ClangTU.h"
#include "FunctionType.h"
#include "Analyzer.h"
#include "../../Util/MakeUnique.h"
#include "LvalueType.h"
#include "ClangOverloadSet.h"
#include "ConstructorType.h"
#include "../Codegen/Expression.h"
#include "../Codegen/Generator.h"
#include "ClangTemplateClass.h"

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
            Expression out;
            std::vector<Type*> args;
            for(auto decl = fundecl->param_begin(); decl != fundecl->param_end(); ++decl) {
                args.push_back(a.GetClangType(*from, (*decl)->getType()));
            }
            out.t = a.GetFunctionType(a.GetClangType(*from, fundecl->getResultType()), args);
            if (a.gen)
                out.Expr = a.gen->CreateFunctionValue(from->MangleName(fundecl));
            return out;
        }
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(result)) {
            Expression out;
            out.t = a.GetLvalueType(a.GetClangType(*from, vardecl->getType()));
            if (a.gen)
                out.Expr = a.gen->CreateGlobalVariable(from->MangleName(vardecl));
            return out;
        }
        if (auto namedecl = llvm::dyn_cast<clang::NamespaceDecl>(result)) {
            Expression out;
            out.t = a.GetConstructorType(a.GetClangNamespace(*from, namedecl));
            out.Expr = nullptr;
            return out;
        }
        if (auto typedefdecl = llvm::dyn_cast<clang::TypeDecl>(result)) {
            Expression out;
            out.Expr = nullptr;
            out.t = a.GetConstructorType(a.GetClangType(*from, from->GetASTContext().getTypeDeclType(typedefdecl)));
            return out;
        }
        if (auto tempdecl = llvm::dyn_cast<clang::ClassTemplateDecl>(result)) {
            Expression out;
            out.Expr = nullptr;
            out.t = a.GetClangTemplateClass(*from, tempdecl);
            return out;
        }
        throw std::runtime_error("Found a decl but didn't know how to interpret it.");
    }
    auto ptr = Wide::Memory::MakeUnique<clang::UnresolvedSet<8>>();
    clang::UnresolvedSet<8>& us = *ptr;
    for(auto it = lr.begin(); it != lr.end(); ++it) {
        us.addDecl(*it);
    }
    Expression out;
    out.t = a.arena.Allocate<ClangOverloadSet>(std::move(ptr), from, nullptr);
    out.Expr = nullptr;
    return out;

   // throw std::runtime_error("Found a member in a Clang namespace, but it was not of a type that was supported.");
}