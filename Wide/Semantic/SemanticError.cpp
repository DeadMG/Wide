#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>

using namespace Wide;
using namespace Semantic;

Error::Error(Analyzer& a, Lexer::Range loc, std::string err)
    : a(&a), where(loc), msg(err) {
    a.errors.insert(this);
}
Error::Error(const Error& other)
    : a(other.a), where(other.where), msg(other.msg) {
    a->errors.insert(this);
}
Error::Error(Error&& other)
    : a(other.a), where(other.where), msg(other.msg) {
    a->errors.insert(this);
}
Error::~Error() {
    disconnect();
}
void Error::disconnect() {
    if (a) {
        a->errors.erase(this);
        a = nullptr;
    }
}