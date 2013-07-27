#include <Wide/Semantic/NullType.h>
#include <Wide/Semantic/ClangTU.h>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

clang::QualType NullType::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    return TU.GetASTContext().NullPtrTy;
}