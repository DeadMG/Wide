#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>

using namespace Wide;
using namespace Semantic;

NoMember::NoMember(Type* what, Type* con, std::string name, Lexer::Range where, Analyzer& a)
: Error(where), which(what), context(con), member(name) {
    msg = "Could not access member \"" + member + "\" of " + which->explain(a) + " from " + context->explain(a) + ".";
}
NotAType::NotAType(Type* what, Lexer::Range loc, Analyzer& a)
: Error(loc), real_type(what) {
    msg = "Found expression of type " + real_type->explain(a) + " instead of a type.";
}
CantFindHeader::CantFindHeader(std::string path, std::vector<std::string> paths, Lexer::Range loc)
: Error(loc), which(path), includes(paths) {
    msg ="Could not find header " + which + ".";
}