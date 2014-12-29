#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <functional>
#include <Wide/Lexer/Token.h>
#include <Wide/Parser/AST.h>
#include <Wide/Util/Ranges/Optional.h>

namespace Wide {
    namespace Parse {
        class Error : public std::exception {
            Wide::Lexer::Token previous;
            Wide::Util::optional<Wide::Lexer::Token> unexpected;
            std::unordered_set<Wide::Lexer::TokenType> expected;
            std::string err;
        public:
            Error(Wide::Lexer::Token previous, Wide::Util::optional<Wide::Lexer::Token> error, std::unordered_set<Wide::Lexer::TokenType> expected);
            const char* what() const
#ifndef _MSC_VER
                noexcept
#endif
            { return err.c_str(); };
            Wide::Lexer::Token GetLastValidToken();
            Wide::Util::optional<Wide::Lexer::Token> GetInvalidToken();
            std::unordered_set<Wide::Lexer::TokenType> GetExpectedTokenTypes();
        };
        enum class OutliningType : int {
            Module,
            Function,
            Type
        };
        struct PutbackLexer {
            PutbackLexer(std::function<Wide::Util::optional<Lexer::Token>()> lex) : lex(std::move(lex)) {}
            
            std::function<Wide::Util::optional<Lexer::Token>()> lex;
            std::vector<Lexer::Token> putbacks;
            std::vector<Lexer::Token> tokens;

            Wide::Util::optional<Lexer::Token> operator()();
            Lexer::Token operator()(Lexer::TokenType required);
            Lexer::Token operator()(std::unordered_set<Lexer::TokenType> required);
            void operator()(Lexer::Token arg);
            Lexer::Token GetLastToken();
        };
        
        struct Parser {
            PutbackLexer lex;
            Module GlobalModule;
            std::function<void(Lexer::Range, OutliningType)> outlining;
            
            // All the valid productions in the global module.
            std::unordered_map<Lexer::TokenType, std::function<std::shared_ptr<Parse::Import>(Parser&, Module*, Parse::Access, std::shared_ptr<Parse::Import>, Lexer::Token& token)>> GlobalModuleTokens;
            // All the valid productions in global module with attributes.
            std::unordered_map<Lexer::TokenType, std::function<void(Parser&, Module*, Parse::Access, std::shared_ptr<Parse::Import>, Lexer::Token& token, std::vector<Attribute> attributes)>> GlobalModuleAttributeTokens;
            // All the productions valid in non-global modules only.
            std::unordered_map<Lexer::TokenType, std::function<Parse::Access(Parser&, Module*, Parse::Access, std::shared_ptr<Parse::Import>, Lexer::Token& token)>> ModuleTokens;
            
            // All user-defined overloadable operators that can be overloaded as free functions.
            std::unordered_set<OperatorName> ModuleOverloadableOperators;
            // All user-defined overloadable operators that can only be overloaded as members.
            std::unordered_set<OperatorName> MemberOverloadableOperators;

            // Assignment operators and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<std::unique_ptr<Expression>(Parser&, std::shared_ptr<Parse::Import>, std::unique_ptr<Expression>)>> AssignmentOperators;

            // Describes the operators and their precedences- all left-associative between assignment and unary
            std::vector<std::unordered_set<Lexer::TokenType>> ExpressionPrecedences;
            
            // Unary operators and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<std::unique_ptr<Expression>(Parser&, std::shared_ptr<Parse::Import>, Lexer::Token&)>> UnaryOperators;

            // Postfix operators and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<std::unique_ptr<Expression>(Parser&, std::shared_ptr<Parse::Import>, std::unique_ptr<Expression>, Lexer::Token&)>> PostfixOperators;

            // Primary expressions and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<std::unique_ptr<Expression>(Parser&, std::shared_ptr<Parse::Import> imp, Lexer::Token&)>> PrimaryExpressions;

            // Statements and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<std::unique_ptr<Statement>(Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok)>> Statements;

            // Type 
            std::unordered_map<Lexer::TokenType, std::function<Parse::Access(Parser&, Type*, Parse::Access, std::shared_ptr<Parse::Import> imp, Lexer::Token&)>> TypeTokens;
            // Type attribute
            std::unordered_map<Lexer::TokenType, std::function<Parse::Access(Parser&, Type*, Parse::Access, std::shared_ptr<Parse::Import> imp, Lexer::Token&, std::vector<Attribute>)>> TypeAttributeTokens;
            // Dynamic members
            std::unordered_map<Lexer::TokenType, std::function<DynamicFunction*(Parser&, Type*, Parse::Access, std::shared_ptr<Parse::Import> imp, Lexer::Token&, std::vector<Attribute>)>> DynamicMemberFunctions;

            Parser(std::function<Wide::Util::optional<Lexer::Token>()> l);

            // Parse a valid operator.
            OperatorName ParseOperatorName(std::unordered_set<OperatorName> valid);
            // Parse an operator given a partially-parsed list of tokens.
            OperatorName ParseOperatorName(std::unordered_set<OperatorName> valid_ops, OperatorName);
            // Parse an operator given that we have already found a valid match.
            OperatorName ParseOperatorName(std::unordered_set<OperatorName> valid_ops, OperatorName, OperatorName);
            // Get possible remaining matches.
            std::unordered_set<OperatorName> GetRemainingValidOperators(std::unordered_set<OperatorName>, OperatorName);

            std::unordered_set<Lexer::TokenType> GetExpressionBeginnings();
            std::unordered_set<Lexer::TokenType> GetStatementBeginnings();
            std::unordered_set<Lexer::TokenType> GetIdentifierFollowups();

            template<typename T> static std::unordered_set<Lexer::TokenType> GetExpectedTokenTypesFromMap(T&& t) {
                std::unordered_set<Lexer::TokenType> expected;
                for (auto&& pair : t)
                    expected.insert(pair.first);
                return expected;
            }
            template<typename T, typename... Args> static std::unordered_set<Lexer::TokenType> GetExpectedTokenTypesFromMap(T&& t, Args&&... args) {
                auto rest = GetExpectedTokenTypesFromMap(std::forward<Args>(args)...);
                for (auto&& pair : t)
                    rest.insert(pair.first);                
                return rest;
            }
            
            std::unique_ptr<Expression> ParseSubAssignmentExpression(unsigned slot, std::shared_ptr<Parse::Import> imp);
            std::unique_ptr<Expression> ParseSubAssignmentExpression(unsigned slot, std::unique_ptr<Expression> Unary, std::shared_ptr<Parse::Import> imp);
            std::unordered_set<OperatorName> GetAllOperators();
                        
            Attribute ParseAttribute(Lexer::Token& tok, std::shared_ptr<Parse::Import> imp);

            void ParseGlobalModuleContents(Module* m, std::shared_ptr<Parse::Import> imp = nullptr);
            std::shared_ptr<Parse::Import> ParseGlobalModuleLevelDeclaration(Module* m, std::shared_ptr<Parse::Import> imp);
            void ParseModuleContents(Module* m, Lexer::Range where, std::shared_ptr<Parse::Import> imp);

            void AddTypeToModule(Module* m, std::string name, std::shared_ptr<Type> t, Parse::Access specifier);
            void AddFunctionToModule(Module* m, std::string name, std::shared_ptr<Function> f, Parse::Access specifier);
            void AddUsingToModule(Module* m, std::string name, std::shared_ptr<Using> f, Parse::Access specifier);
            void AddTemplateTypeToModule(Module* m, std::string name, Lexer::Range where, std::vector<FunctionArgument>, std::unique_ptr<Type> t, Parse::Access specifier);
            Module* CreateModule(std::string name, Module* in, Lexer::Range where, Parse::Access access);

            Using* CreateUsing(std::string val, Lexer::Range loc, Expression* expr, Module* p, Parse::Access a); 
            Parse::Access ParseModuleLevelDeclaration(Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp);
            std::vector<Variable> ParseLambdaCaptures(std::shared_ptr<Parse::Import> imp);
            std::unique_ptr<Expression> ParsePrimaryExpression(std::shared_ptr<Parse::Import> imp);
            std::vector<std::unique_ptr<Expression>> ParseFunctionArguments(std::shared_ptr<Parse::Import> imp);
            std::unique_ptr<Expression> ParsePostfixExpression(std::shared_ptr<Parse::Import> imp);
            std::unique_ptr<Expression> ParseUnaryExpression(std::shared_ptr<Parse::Import> imp);
            std::unique_ptr<Expression> ParseAssignmentExpression(std::shared_ptr<Parse::Import> imp);
            std::unique_ptr<Expression> ParseExpression(std::shared_ptr<Parse::Import> imp);
            std::unique_ptr<Statement> ParseStatement(std::shared_ptr<Parse::Import> imp);
            std::vector<FunctionArgument> ParseFunctionDefinitionArguments(std::shared_ptr<Parse::Import> imp);
            std::unique_ptr<Constructor> ParseConstructor(const Lexer::Token& first, std::shared_ptr<Parse::Import> imp, std::vector<Attribute> attrs);
            std::unique_ptr<Function> ParseFunction(const Lexer::Token& first, std::shared_ptr<Parse::Import> imp, std::vector<Attribute> attrs);
            std::unique_ptr<Destructor> ParseDestructor(const Lexer::Token& first, std::shared_ptr<Parse::Import> imp, std::vector<Attribute> attrs);
            
            Module* ParseQualifiedName(Lexer::Token& first, Module* m, Parse::Access a, std::unordered_set<Lexer::TokenType> admissible, std::unordered_set<Lexer::TokenType> final);
            void ParseTypeBody(Type* ty, std::shared_ptr<Parse::Import> imp);
            std::vector<std::unique_ptr<Expression>> ParseTypeBases(std::shared_ptr<Parse::Import> imp);
            std::unique_ptr<Type> ParseTypeDeclaration(Lexer::Range loc, std::shared_ptr<Parse::Import> imp, Lexer::Token& ident, std::vector<Attribute>& attrs);
        };
    }
}
