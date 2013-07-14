#pragma once

#include <unordered_map>
#include <string>
#include <Lexer/Token.h>

namespace Wide {
    namespace Parser {

        enum Error : int {
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
            ModuleScopeTypeRequiresTerminatingCurly,
            ExpressionScopeTypeRequiresTerminatingCurly,
            TypeScopeExpectedMemberAfterIdentifier
        };

        enum Warning {
            SemicolonAfterTypeDefinition
        };

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
                std::make_pair(Error::ModuleRequiresTerminatingCurly, "Expected } to terminate a module." ),
                std::make_pair(Error::ModuleScopeTypeRequiresTerminatingCurly, "Expected } to terminate a type at module scope."),
                std::make_pair(Error::ExpressionScopeTypeRequiresTerminatingCurly, "Expected } to terminate a type as an expression."),
                std::make_pair(Error::TypeScopeExpectedMemberAfterIdentifier, "Expected := or ( after identifier, to denote a member variable or function at type scope.")
            };
            return std::unordered_map<Error, std::string>(std::begin(strings), std::end(strings));
        }());

        class UnrecoverableError : public std::exception {
            Error err;
            Lexer::Range loc;
        public:
            UnrecoverableError(Lexer::Range pos, Error error)
                : err(error), loc(pos) {}
            const char* what() {
                return ErrorStrings.at(err).c_str();
            }
            Lexer::Range where() {
                return loc;
            }
            Error error() {
                return err;
            }
        };
    }
}