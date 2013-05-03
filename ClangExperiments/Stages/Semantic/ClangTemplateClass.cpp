#include "ClangTemplateClass.h"
#include "ConstructorType.h"
#include "Analyzer.h"
#include "ClangTU.h"

using namespace Wide;
using namespace Semantic;

#pragma warning(push, 0)

#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>
#include <clang/Sema/Sema.h>

#pragma warning(pop)

Expression ClangTemplateClass::BuildMetaCall(Expression, std::vector<Expression> args, Analyzer& a) {
    clang::TemplateArgumentListInfo tl;
    for(auto&& x : args) {
        auto con = dynamic_cast<ConstructorType*>(x.t);
        if (!con)
            throw std::runtime_error("Only support types as arguments to Clang templates right now.");
        auto clangty = con->GetConstructedType()->GetClangType(*from, a);

        auto tysrcinfo = from->GetASTContext().getTrivialTypeSourceInfo(clangty);
        
        tl.addArgument(clang::TemplateArgumentLoc(clang::TemplateArgument(clangty), tysrcinfo));
    }
    
    llvm::SmallVector<clang::TemplateArgument, 10> tempargs;
    if (from->GetSema().CheckTemplateArgumentList(tempdecl, tempdecl->getLocation(), tl, false, tempargs))
        throw std::runtime_error("Clang could not resolve the template arguments for this template.");

    void* pos = 0;
    auto spec = tempdecl->findSpecialization(tempargs.data(), tempargs.size(), pos);    
    if (spec) {        
        Expression out;
        out.Expr = nullptr;
        out.t = a.GetConstructorType(a.GetClangType(*from, from->GetASTContext().getRecordType(spec)));
        return out;
    }
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
            throw std::runtime_error("Could not instantiate resulting class template specialization.");

    //from->GetSema().InstantiateClassTemplateSpecializationMembers(loc, llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(spec->getDefinition()), tsk);
    
    Expression out;
    out.Expr = nullptr;
    out.t = a.GetConstructorType(a.GetClangType(*from, from->GetASTContext().getRecordType(spec)));
    return out;
}