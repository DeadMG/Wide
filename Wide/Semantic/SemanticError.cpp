#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>

using namespace Wide;
using namespace Semantic;

AnalyzerError::AnalyzerError(Analyzer& a, Lexer::Range loc, std::string err)
    : Error(loc, err), a(&a)
{
    a.errors.insert(this);
}
Error::Error(Lexer::Range loc, std::string err)
    : where(loc), msg(err) {}
AnalyzerError::AnalyzerError(const AnalyzerError& other)
    : a(other.a), Error(other) {
    a->errors.insert(this);
}
AnalyzerError::AnalyzerError(AnalyzerError&& other)
    : a(other.a), Error(std::move(other)) {
    a->errors.insert(this);
}
AnalyzerError::~AnalyzerError() {
    disconnect();
}
void AnalyzerError::disconnect() {
    if (a) {
        a->errors.erase(this);
        a = nullptr;
    }
}