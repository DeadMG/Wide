#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Util/Memory/MakeUnique.h>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>
#include <clang/AST/UnresolvedSet.h>
#include <clang/AST/AST.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

Wide::Util::optional<ConcreteExpression> ClangNamespace::AccessMember(ConcreteExpression val, std::string name, Context c) {
    clang::LookupResult lr(
        from->GetSema(), 
        clang::DeclarationNameInfo(clang::DeclarationName(from->GetIdentifierInfo(name)), clang::SourceLocation()),
        clang::Sema::LookupNameKind::LookupOrdinaryName);

    if (!from->GetSema().LookupQualifiedName(lr, con))
        return Wide::Util::none;
    if (lr.isAmbiguous())
        throw ClangLookupAmbiguous(name, val.t, c.where, *c);
    if (lr.isSingleResult()) {
        auto result = lr.getFoundDecl()->getCanonicalDecl();
        // If result is a function, namespace, or variable decl, we're good. Else, cry like a little girl. A LITTLE GIRL.
        if (auto fundecl = llvm::dyn_cast<clang::FunctionDecl>(result)) {
            std::unordered_set<clang::NamedDecl*> decls;
            decls.insert(fundecl);
            return c->GetOverloadSet(std::move(decls), from, GetContext(*c))->BuildValueConstruction({}, c);
        }
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(result)) {
            return ConcreteExpression(c->GetLvalueType(c->GetClangType(*from, vardecl->getType())), c->gen->CreateGlobalVariable(from->MangleName(vardecl)));
        }
        if (auto namedecl = llvm::dyn_cast<clang::NamespaceDecl>(result)) {
            return c->GetClangNamespace(*from, namedecl)->BuildValueConstruction({}, c);
        }
        if (auto typedefdecl = llvm::dyn_cast<clang::TypeDecl>(result)) {
            return c->GetConstructorType(c->GetClangType(*from, from->GetASTContext().getTypeDeclType(typedefdecl)))->BuildValueConstruction({}, c);
        }
        if (auto tempdecl = llvm::dyn_cast<clang::ClassTemplateDecl>(result)) {
            return c->GetClangTemplateClass(*from, tempdecl)->BuildValueConstruction({}, c);
        }
        throw ClangUnknownDecl(name, val.t, c.where, *c); 
    }
    std::unordered_set<clang::NamedDecl*> decls;
    decls.insert(lr.begin(), lr.end());
    return c->GetOverloadSet(std::move(decls), from, GetContext(*c))->BuildValueConstruction({}, c);
}

Type* ClangNamespace::GetContext(Analyzer& a) {
    if (con == from->GetDeclContext())
        return nullptr;
    return a.GetClangNamespace(*from, con->getParent());
}
std::string ClangNamespace::explain(Analyzer& a) {
    if (con == from->GetDeclContext())
        return "cpp(\"" + from->GetFilename() + "\")";
    auto names = llvm::dyn_cast<clang::NamespaceDecl>(con);
    return GetContext(a)->explain(a) + "." + names->getName().str();
}