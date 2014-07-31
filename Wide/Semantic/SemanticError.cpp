#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>

using namespace Wide;
using namespace Semantic;

NoMember::NoMember(Type* what, Type* con, Parse::Name name, Lexer::Range where)
: Error(where, "Could not access member \"" + GetNameAsString(name) + "\" of " + what->explain() + " from " + con->explain() + "."), which(what), context(con), member(name) {}

NotAType::NotAType(Type* what, Lexer::Range loc)
: Error(loc, "Found expression of type " + what->explain() + " instead of a type."), real_type(what) {}

CantFindHeader::CantFindHeader(std::string path, std::vector<std::string> paths, Lexer::Range loc)
: Error(loc, "Could not find header " + path + "."), which(path), includes(paths) {}

MacroNotValidExpression::MacroNotValidExpression(std::string expand, std::string nam, Lexer::Range where)
: Error(where, "Macro " + nam + " expanded to \"" + expand + "\" was not a valid expression."), expanded(expand), name(nam) {}

CannotCreateTemporaryFile::CannotCreateTemporaryFile(Lexer::Range where)
: Error(where, "Could not create temporary file.") {}

UnqualifiedLookupFailure::UnqualifiedLookupFailure(Type* con, std::string name, Lexer::Range where)
: Error(where, "Could not find unqualified name \"" + name + "\" from " + con->explain() + "."), context(con), member(name) {}

ClangLookupAmbiguous::ClangLookupAmbiguous(std::string name, Type* what, Lexer::Range where)
: Error(where, "C++ name lookup for \"" + name + "\" was ambiguous in " + what->explain()), name(name), which(what) {}

ClangUnknownDecl::ClangUnknownDecl(std::string name, Type* what, Lexer::Range where)
: Error(where, "C++ name lookup for \"" + name + "\" in " + what->explain() + " produced a declaration that could not be interpreted."), name(name), which(what) {}

InvalidTemplateArgument::InvalidTemplateArgument(Type* t, Lexer::Range where)
: Error(where, "Argument of type " + t->explain() + " was not a valid template argument."), type(t) {}

UnresolvableTemplate::UnresolvableTemplate(Type* temp, std::vector<Type*> args, std::string diag, Lexer::Range where)
: Error(where, "C++ template resolution of " + temp->explain() + " failed.\n" + diag), temp(temp), arguments(args), clangdiag(diag) {}

UninstantiableTemplate::UninstantiableTemplate(Lexer::Range where)
: Error(where, "C++ template instantiation failed.") {}

IncompleteClangType::IncompleteClangType(Type* what, Lexer::Range where)
: Error(where, "C++ type " + what->explain() + " was incomplete."), which(what) {}

CannotTranslateFile::CannotTranslateFile(std::string filepath, Lexer::Range r)
: Error(r, "Clang could not translate file " + filepath) {}

ClangFileParseError::ClangFileParseError(std::string f, std::string e, Lexer::Range where)
: Error(where, "Clang failed to parse \"" + f + "\"\n" + e), filename(f), errors(e) {}

InvalidBase::InvalidBase(Type* t, Lexer::Range where)
: Error(where, "The type " + t->explain() + " is not a valid base class."), base(t) {}

RecursiveMember::RecursiveMember(Type* t, Lexer::Range where)
: Error(where, "The type " + t->explain() + " directly or indirectly contained a member of itself."), base(t) {}

AmbiguousLookup::AmbiguousLookup(std::string name, Type* b1, Type* b2, Lexer::Range where)
: Error(where, "The member " + name + " was found in both " + b1->explain() + " and " + b2->explain()), name(name), base1(b1), base2(b2) {}

NoBooleanConversion::NoBooleanConversion(Type* obj, Lexer::Range where)
: Error(where, "The type " + obj->explain() + " did not have a conversion to bool."), object(obj) {}

AddressOfNonLvalue::AddressOfNonLvalue(Type* obj, Lexer::Range where)
: Error(where, "Attempted to take the address of " + obj->explain() + " which is a non-lvalue."), obj(obj) {}

NoMetaCall::NoMetaCall(Type* what, Lexer::Range where)
: Error(where, "The type " + what->explain() + " has no meta call operator."), which(what) {}

NoMemberToInitialize::NoMemberToInitialize(Type* what, Parse::Name name, Lexer::Range where)
: Error(where, "The type " + what->explain() + " did not contain a member \"" + GetNameAsString(name) + "\" to initialize."), which(what), name(name) {}

ReturnTypeMismatch::ReturnTypeMismatch(Type* new_r, Type* old_r, Lexer::Range where)
: Error(where, "The function had a return type of " + old_r->explain() + " but tried to return " + new_r->explain() + "."), new_ret_type(new_r), existing_ret_type(old_r) {}

VariableTypeVoid::VariableTypeVoid(std::string name, Lexer::Range where)
: Error(where, "The variable \"" + name + "\" was of type void."), name(name) {}

VariableShadowing::VariableShadowing(std::string name, Lexer::Range previous, Lexer::Range where)
: Error(where, "The variable \"" + name + "\" would shadow an existing variable at " + previous + "."), name(name), previousdecl(previous) {}

TupleUnpackWrongCount::TupleUnpackWrongCount(Type* tupty, Lexer::Range where)
: Error(where, "Unpacking " + tupty->explain() + " failed because the variables had the wrong count."), tupletype(tupty) {}

NoControlFlowStatement::NoControlFlowStatement(Lexer::Range where)
: Error(where, "No appropriate control flow statement could be found.") {}

FunctionTypeRecursion::FunctionTypeRecursion(Lexer::Range where)
: Error(where, "The return type of this function had a recursive dependency that could not be computed.") {}

PrologNonAssignment::PrologNonAssignment(Lexer::Range where)
: Error(where, "Prolog statements can only be assignments right now.") {}

PrologAssignmentNotIdentifier::PrologAssignmentNotIdentifier(Lexer::Range where)
: Error(where, "Prolog assignment statement left-hand-sides can only be identifiers.") {}

PrologExportNotAString::PrologExportNotAString(Lexer::Range where)
: Error(where, "Prolog ExportName assignment right-hand-side must be a constant string.") {}

BadMacroExpression::BadMacroExpression(Lexer::Range where, std::string expression)
: Error(where, "Could not interpret macro expression:\n" + expression) {}

BadUsingTarget::BadUsingTarget(Type* con, Lexer::Range where)
: Error(where, "The using target " + con->explain() + " was not a constant expression.") {}

OverloadResolutionFailure::OverloadResolutionFailure(Lexer::Range where)
: Error(where, "Overload resolution failed.") {}