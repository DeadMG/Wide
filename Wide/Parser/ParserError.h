#pragma once

#include <Wide/Lexer/Token.h>
#include <unordered_map>
#include <vector>
#include <string>

namespace Wide {
    namespace Parser {
        enum class Error : int {
            ModuleScopeFunctionNoOpenBracket,
            ModuleScopeOperatorNoOpenBracket,
            UnrecognizedTokenModuleScope,
            NonOverloadableOperator,
            NoOperatorFound,
            GlobalFunctionCallOperator,
            ModuleScopeTypeNoIdentifier,
            ModuleScopeTypeNoCurlyBrace,
            ModuleScopeTypeIdentifierButFunction,
            ModuleRequiresTerminatingCurly,
            ModuleScopeTypeExpectedMemberOrTerminate,
            TypeScopeExpectedMemberAfterIdentifier,
            ModuleScopeUsingNoIdentifier,
            ModuleNoIdentifier,
            ModuleNoOpeningBrace,
            ModuleScopeUsingNoVarCreate,
            ModuleScopeUsingNoSemicolon,
            ConstructorNoIdentifierAfterColon,
            ConstructorNoBracketAfterMemberName,
            ConstructorNoBracketClosingInitializer,
            ConstructorNoBracketOrExpressionAfterMemberName,
            FunctionNoCurlyToIntroduceBody,
            FunctionNoClosingCurly,
            OperatorNoCurlyToIntroduceBody,
            OperatorNoCloseBracketAfterOpen,
            TypeScopeOperatorNoOpenBracket,
            ConstructorNoOpenBracket,
            DestructorNoType,
            DestructorNoOpenBracket,
            PrologNoOpenCurly,
            FunctionArgumentNoIdentifierOrThis,
            FunctionArgumentNoBracketOrComma,
            ReturnNoSemicolon,
            IfNoOpenBracket,
            IfNoCloseBracket,
            WhileNoOpenBracket,
            WhileNoCloseBracket,
            ExpressionStatementNoSemicolon,
            VariableStatementNoSemicolon,
            NoOpenBracketAfterExclaim,
            PointerAccessNoIdentifierOrDestructor,
            PointerAccessNoTypeAfterNegate,
            MemberAccessNoIdentifierOrDestructor,
            MemberAccessNoTypeAfterNegate,
            ParenthesisedExpressionNoCloseBracket,
            LambdaNoOpenBracket,
            LambdaNoOpenCurly,
            TypeExpressionNoCurly,
            ExpressionNoBeginning,
            TypeFunctionAlreadyVariable,
            TypeVariableAlreadyFunction,
            BreakNoSemicolon,
            ContinueNoSemicolon,
            QualifiedNameNoIdentifier,
            LambdaNoIntroducer,
            TupleCommaOrClose,
            VariableListNoIdentifier,
            VariableListNoInitializer,
            AccessSpecifierNoColon,
            ProtectedModuleScope,
            TemplateNoArguments,
            ModuleScopeTemplateNoType,
            TypeScopeExpectedIdentifierAfterDynamic,
            TypeExpectedBracketAfterIdentifier,
        };         

        enum class Warning : int {
            SemicolonAfterTypeDefinition,
            AssignmentInUsing
        };
    }
}
#ifndef _MSC_VER
namespace std {
    template<> struct hash<Wide::Parser::Error> {
        std::size_t operator()(Wide::Parser::Error p) const {
            return hash<int>()((int)p);
        }
    };
    template<> struct hash<Wide::Parser::Warning> {
        std::size_t operator()(Wide::Parser::Warning p) const {
            return hash<int>()((int)p);
        }
    };
}
#endif
namespace Wide {
    namespace Parser {
        static const std::unordered_map<Error, std::string> ErrorStrings([]() -> std::unordered_map<Error, std::string> {
            std::pair<Error, std::string> strings[] = {
                std::make_pair(Error::ModuleScopeFunctionNoOpenBracket, "Expected ( after identifier, to denote a function at module scope."),
                std::make_pair(Error::UnrecognizedTokenModuleScope, "Expected module, using, operator, type, or identifier to begin a production at module scope."),
                std::make_pair(Error::NonOverloadableOperator, "Expected an overloadable operator after operator at module scope to denote an operator overload."),
                std::make_pair(Error::NoOperatorFound, "Expected to find an operator after operator to denote an operator overload at module scope."),
                std::make_pair(Error::ModuleScopeOperatorNoOpenBracket, "Expected ( after operator, to denote an operator overload at module scope."),
                std::make_pair(Error::GlobalFunctionCallOperator, "Found operator() at global scope. Function call operator may only be defined at type scope."),
                std::make_pair(Error::ModuleScopeTypeNoIdentifier, "Expected identifier after type, to denote a type at module scope."),
                std::make_pair(Error::ModuleScopeTypeNoCurlyBrace, "Expected { after type identifier at module scope, to denote a type at module scope."),
                std::make_pair(Error::ModuleScopeTypeIdentifierButFunction, "Expected identifier { after type, to denote a type at module scope."),
                std::make_pair(Error::ModuleRequiresTerminatingCurly, "Expected } to terminate a module."),
                std::make_pair(Error::ModuleScopeTypeExpectedMemberOrTerminate, "Expected }, operator, type, ~  or identifier to terminate or define within a type."),
                std::make_pair(Error::TypeScopeExpectedMemberAfterIdentifier, "Expected := or ( after identifier, to denote a member variable or function at type scope."),
                std::make_pair(Error::ModuleScopeUsingNoIdentifier, "Expected an identifier after using, to denote a using at module scope."),
                std::make_pair(Error::ModuleNoIdentifier, "Expected identifier after module, to denote a module at module scope."),
                std::make_pair(Error::ModuleNoOpeningBrace, "Expected { after identifier to denote a module at module scope."),
                std::make_pair(Error::ModuleScopeUsingNoVarCreate, "Expected := after identifier to denote a using at module scope."),
                std::make_pair(Error::ModuleScopeUsingNoSemicolon, "Expected ; after expression to denote a using at module scope."),
                std::make_pair(Error::ConstructorNoIdentifierAfterColon, "Expected an identifier after : to indicate a member for initialization in a constructor initializer list."),
                std::make_pair(Error::ConstructorNoBracketAfterMemberName, "Expected ( after identifier to form a member initializer when parsing a constructor."),
                std::make_pair(Error::ConstructorNoBracketClosingInitializer, "Expected ) to close a member initializer in constructor initializer list."),
                std::make_pair(Error::FunctionNoCurlyToIntroduceBody, "Expected { to introduce a function body after prolog, arguments, or initializer list."),
                std::make_pair(Error::FunctionNoClosingCurly, "Expected } to terminate function body."),
                std::make_pair(Error::ConstructorNoBracketOrExpressionAfterMemberName, "Expected expression or ) to terminate a member initializer."),
                std::make_pair(Error::OperatorNoCurlyToIntroduceBody, "Expected { to introduce a function body after prolog or arguments when parsing overloaded operator"),
                std::make_pair(Error::OperatorNoCloseBracketAfterOpen, "Expected ) after operator( to denote an operator overload at type scope."),
                std::make_pair(Error::TypeScopeOperatorNoOpenBracket, "Expected ( after identifier, to denote an operator overload at type scope."),
                std::make_pair(Error::ConstructorNoOpenBracket, "Expected ( after identifier, to denote a constructor."),
                std::make_pair(Error::DestructorNoType, "Expected type after ~ at member scope to denote a destructor."),
                std::make_pair(Error::DestructorNoOpenBracket, "Expected ( after ~type to denote a destructor."),
                std::make_pair(Error::PrologNoOpenCurly, "Expected { after prolog to denote a prolog statement group at function or operator scope."),
                std::make_pair(Error::FunctionArgumentNoIdentifierOrThis, "Expected identifier or this after expression to denote a function argument."),
                std::make_pair(Error::FunctionArgumentNoBracketOrComma, "Expected ) or , after a function argument to denote the end or continuation of a function argument list"),
                std::make_pair(Error::ReturnNoSemicolon, "Expected ; after return expression for a non-void return statement."),
                std::make_pair(Error::IfNoOpenBracket, "Expected ( after if for if condition to denote if statement."),
                std::make_pair(Error::IfNoCloseBracket, "Expected ) after if(expression to denote an if statement."),
                std::make_pair(Error::WhileNoOpenBracket, "Expected ( after while to denote a while statement."),
                std::make_pair(Error::WhileNoCloseBracket, "Expected ) after while(expression to denote a while statement."),
                std::make_pair(Error::ExpressionStatementNoSemicolon, "Expected ; after expression to denote an expression statement."),
                std::make_pair(Error::VariableStatementNoSemicolon, "Expected ; after identifier := expression to denote a variable statement."),
                std::make_pair(Error::NoOpenBracketAfterExclaim, "Expected ( after ! to denote meta-function call expression."),
                std::make_pair(Error::PointerAccessNoIdentifierOrDestructor, "Expected identifier or ~type after expression-> to denote a pointer member access."),
                std::make_pair(Error::PointerAccessNoTypeAfterNegate, "Expected type after expression->~ to form a destructor call."),
                std::make_pair(Error::MemberAccessNoIdentifierOrDestructor, "Expected identifier or ~type after expression. to denote a member access."),
                std::make_pair(Error::MemberAccessNoTypeAfterNegate, "Expected type after expression.~ to form a destructor call."),
                std::make_pair(Error::ParenthesisedExpressionNoCloseBracket, "Expected ) after (expression to denote a parenthesised expression."),
                std::make_pair(Error::LambdaNoOpenBracket, "Expected ( after function to denote a lambda expression."),
                std::make_pair(Error::LambdaNoOpenCurly, "Expected { after function(arguments) to denote a lambda function expression."),
                std::make_pair(Error::TypeExpressionNoCurly, "Expected { after type to denote an anonymous type expression."),
                std::make_pair(Error::ExpressionNoBeginning, "Expected this, type, function, integer, string, identifier, or bracket to begin an expression."),
                std::make_pair(Error::TypeFunctionAlreadyVariable, "Attempted to add a function to a type, but that identifier was already used to denote a variable."),
                std::make_pair(Error::TypeVariableAlreadyFunction, "Attempted to add a variable to a type, but that identifier was already used to denote a function."),
                std::make_pair(Error::BreakNoSemicolon, "Found a break, but no semicolon afterwards."),
                std::make_pair(Error::ContinueNoSemicolon, "Found a continue, but no semicolon afterwards."),
                std::make_pair(Error::QualifiedNameNoIdentifier, "Found identifier. but did not find another identifier to continue a qualified name"),
                std::make_pair(Error::LambdaNoIntroducer, "Found (), expected => to introduce nullary short-form lambda."),
                std::make_pair(Error::TupleCommaOrClose, "Expected , or } to continue or close a tuple literal"),
                std::make_pair(Error::VariableListNoIdentifier, "Expected an identifier after , to continue a list of variables."),
                std::make_pair(Error::VariableListNoInitializer, "Expected to find := after identifier [, identifier]* to create a variable statement."),
                std::make_pair(Error::AccessSpecifierNoColon, "Expected to find : after private, public, or protected to create an access specifier."),
                std::make_pair(Error::ProtectedModuleScope, "Cannot define module-scope definitions as protected."),
                std::make_pair(Error::TemplateNoArguments, "Expected ( to introduce template arguments after template."),
                std::make_pair(Error::ModuleScopeTemplateNoType, "Expected type after template arguments for a template type."),
                std::make_pair(Error::TypeScopeExpectedIdentifierAfterDynamic, "Expected identifier after dynamic to introduce a dynamic member function."),
                std::make_pair(Error::TypeExpectedBracketAfterIdentifier, "Expected ( after dynamic identifier to introduce dynamic member function."),
            };
            return std::unordered_map<Error, std::string>(std::begin(strings), std::end(strings));
        }());

        static const std::unordered_map<Warning, std::string> WarningStrings([]() -> std::unordered_map<Warning, std::string> {
            std::pair<Warning, std::string> strings[] = {
                std::make_pair(Warning::AssignmentInUsing, "Used = in using, instead of :=. Treated as :=."),
                std::make_pair(Warning::SemicolonAfterTypeDefinition, "Used a semicolon after a type definition. Semicolon is ignored.")
            };
            return std::unordered_map<Warning, std::string>(std::begin(strings), std::end(strings));
        }());

        class ParserError : public std::exception {
            Error err;
            std::vector<Lexer::Range> loc;
            Lexer::Range recoverloc;
        public:
            ParserError(Lexer::Range pos, Error error)
                : err(error), recoverloc(pos) { loc.push_back(pos); }
            ParserError(Lexer::Range pos, Lexer::Range recloc, Error error)
                : err(error), recoverloc(recloc) { loc.push_back(pos); }
            ParserError(Lexer::Range first, std::vector<Lexer::Range> rest, Error error)
                : recoverloc(first), err(error)
            {
                loc.push_back(first);
                for(auto x : rest)
                    loc.push_back(x);
            }
            const char* what() const 
#ifndef _MSC_VER
                noexcept
#endif
            {
                return ErrorStrings.at(err).c_str();
            }
            std::vector<Lexer::Range> where() {
                return loc;
            }
            Error error() {
                return err;
            }
            Lexer::Range recover_where() {
                return recoverloc;
            }
        };
    }
}
