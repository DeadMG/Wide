#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Util/Memory/MakeUnique.h>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>
#include <clang/AST/UnresolvedSet.h>
#include <clang/AST/AST.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::shared_ptr<Expression> ClangNamespace::AccessNamedMember(std::shared_ptr<Expression> t, std::string name, Context c) {
    clang::LookupResult lr(
        from->GetSema(), 
        clang::DeclarationNameInfo(clang::DeclarationName(from->GetIdentifierInfo(name)), clang::SourceLocation()),
        clang::Sema::LookupNameKind::LookupOrdinaryName);

    if (!from->GetSema().LookupQualifiedName(lr, con))
        return nullptr;
    if (lr.isAmbiguous())
        throw ClangLookupAmbiguous(name, t->GetType()->Decay(), c.where);
    if (lr.isSingleResult()) {
        auto result = lr.getFoundDecl()->getCanonicalDecl();
        // If result is a function, namespace, or variable decl, we're good. Else, cry like a little girl. A LITTLE GIRL.
        if (auto fundecl = llvm::dyn_cast<clang::FunctionDecl>(result)) {
            std::unordered_set<clang::NamedDecl*> decls;
            decls.insert(fundecl);
            return BuildChain(std::move(t), analyzer.GetOverloadSet(std::move(decls), from, GetContext())->BuildValueConstruction({}, { this, c.where }));
        }
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(result)) {
            auto object = from->GetObject(vardecl);
            return CreatePrimUnOp(std::move(t), analyzer.GetLvalueType(analyzer.GetClangType(*from, vardecl->getType())), [object](llvm::Value*, CodegenContext& con) {
                return object(con);
            });
        }
        if (auto namedecl = llvm::dyn_cast<clang::NamespaceDecl>(result)) {
            return BuildChain(std::move(t), analyzer.GetClangNamespace(*from, namedecl)->BuildValueConstruction({}, { this, c.where }));
        }
        if (auto typedefdecl = llvm::dyn_cast<clang::TypeDecl>(result)) {
            return BuildChain(std::move(t), analyzer.GetConstructorType(analyzer.GetClangType(*from, from->GetASTContext().getTypeDeclType(typedefdecl)))->BuildValueConstruction({}, { this, c.where }));
        }
        if (auto tempdecl = llvm::dyn_cast<clang::ClassTemplateDecl>(result)) {
            return BuildChain(std::move(t), analyzer.GetClangTemplateClass(*from, tempdecl)->BuildValueConstruction({}, { this, c.where }));
        }
        throw ClangUnknownDecl(name, t->GetType()->Decay(), c.where); 
    }
    std::unordered_set<clang::NamedDecl*> decls;
    decls.insert(lr.begin(), lr.end());
    return BuildChain(std::move(t), analyzer.GetOverloadSet(std::move(decls), from, GetContext())->BuildValueConstruction({}, { this, c.where }));
}

Type* ClangNamespace::GetContext() {
    if (con == from->GetDeclContext())
        return nullptr;
    return analyzer.GetClangNamespace(*from, con->getParent());
}
std::string ClangNamespace::explain() {
    if (con == from->GetDeclContext())
        return "cpp(\"" + from->GetFilename() + "\")";
    auto names = llvm::dyn_cast<clang::NamespaceDecl>(con);
    return GetContext()->explain() + "." + names->getName().str();
}
OverloadSet* ClangNamespace::CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access) {
    clang::LookupResult lr(
        from->GetSema(),
        clang::DeclarationNameInfo(from->GetASTContext().DeclarationNames.getCXXOperatorName(GetTokenMappings().at(what).first), clang::SourceLocation()),
        clang::Sema::LookupNameKind::LookupOrdinaryName);
    if (!from->GetSema().LookupQualifiedName(lr, con))
        return nullptr;
    std::unordered_set<clang::NamedDecl*> decls;
    decls.insert(lr.begin(), lr.end());
    return analyzer.GetOverloadSet(std::move(decls), from, GetContext());
}