#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/SemanticError.h>

#pragma warning(push, 0)
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>
#include <clang/Sema/Sema.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

ConcreteExpression ClangTemplateClass::BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Context c) {
    clang::TemplateArgumentListInfo tl;
    std::vector<Type*> types;
    std::list<clang::IntegerLiteral> literals;
    for(auto&& x : args) {
        if (auto con = dynamic_cast<ConstructorType*>(x.t)) {
            auto clangty = con->GetConstructedType()->GetClangType(*from, *c);
            if (!clangty) throw InvalidTemplateArgument(x.t, c.where, *c);
            auto tysrcinfo = from->GetASTContext().getTrivialTypeSourceInfo(*clangty);
            
            tl.addArgument(clang::TemplateArgumentLoc(clang::TemplateArgument(*clangty), tysrcinfo));
            types.push_back(con->GetConstructedType());
            continue;
        }
        if (auto in = dynamic_cast<IntegralType*>(x.t)) {
            if (auto integral = dynamic_cast<Codegen::IntegralExpression*>(x.Expr)) {
                literals.emplace_back(from->GetASTContext(), llvm::APInt(64, integral->GetValue(), integral->GetSign()), from->GetASTContext().LongLongTy, clang::SourceLocation());
                tl.addArgument(clang::TemplateArgumentLoc(clang::TemplateArgument(&literals.back()), clang::TemplateArgumentLocInfo(&literals.back())));
                types.push_back(x.t);
                continue;
            }
        }
        throw InvalidTemplateArgument(x.t, c.where, *c);
    }
    
    llvm::SmallVector<clang::TemplateArgument, 10> tempargs;
    if (from->GetSema().CheckTemplateArgumentList(tempdecl, tempdecl->getLocation(), tl, false, tempargs))
        throw UnresolvableTemplate(this, types, c.where, *c);

    void* pos = 0;
    auto spec = tempdecl->findSpecialization(tempargs.data(), tempargs.size(), pos);    
    if (spec)    
        return c->GetConstructorType(c->GetClangType(*from, from->GetASTContext().getRecordType(spec)))->BuildValueConstruction({}, c);
    auto loc = from->GetFileEnd();
    if (!spec) {
        spec = clang::ClassTemplateSpecializationDecl::Create(
            from->GetASTContext(), 
            clang::TagTypeKind::TTK_Class, 
            from->GetDeclContext(), 
            loc, 
            loc, 
            tempdecl, 
            tempargs.data(), 
            tempargs.size(), 
            0
        );
        tempdecl->AddSpecialization(spec, pos);
    }
    spec->setLexicalDeclContext(from->GetDeclContext());
    from->GetDeclContext()->addDecl(spec);
    auto tsk = clang::TemplateSpecializationKind::TSK_ExplicitInstantiationDefinition;

    if (!spec->getDefinition())
        if (from->GetSema().InstantiateClassTemplateSpecialization(loc, spec, tsk))
            throw UninstantiableTemplate(c.where);

    return c->GetConstructorType(c->GetClangType(*from, from->GetASTContext().getRecordType(spec)))->BuildValueConstruction({}, c);
}
std::string ClangTemplateClass::explain(Analyzer& a) {
    return GetContext(a)->explain(a) + "." + tempdecl->getName().str();
} 
Type* ClangTemplateClass::GetContext(Analyzer& a) {
    return a.GetClangNamespace(*from, tempdecl->getDeclContext());
}