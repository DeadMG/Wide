#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/Reference.h>
#include <unordered_map>

#pragma warning(push, 0)
#include <clang/AST/TypeOrdering.h>
#include <clang/Frontend/CodeGenOptions.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/DeclGroup.h>
#include <clang/Lex/HeaderSearchOptions.h>
#pragma warning(pop)

namespace Wide {
    namespace Semantic {         
        clang::ExprValueKind GetKindOfType(Type* t) {
            if (dynamic_cast<Semantic::LvalueType*>(t))
                return clang::ExprValueKind::VK_LValue;
            else 
                return clang::ExprValueKind::VK_RValue;
        }
    }
    namespace ClangUtil {
        clang::TargetInfo* CreateTargetInfoFromTriple(clang::DiagnosticsEngine& engine, std::string triple) {
            clang::TargetOptions& target = *new clang::TargetOptions();
            target.Triple = triple;
            auto targetinfo = clang::TargetInfo::CreateTargetInfo(engine, &target);
            targetinfo->setCXXABI(clang::TargetCXXABI::GenericItanium);
            return targetinfo;
        } 
        std::size_t ClangTypeHasher::operator()(clang::QualType t) const {
            return llvm::DenseMapInfo<clang::QualType>::getHashValue(t);
        }       
    }
}