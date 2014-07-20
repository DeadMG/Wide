#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <functional>
#include <Wide/Lexer/Token.h>
#include <Wide/Parser/ParserError.h>
#include <Wide/Parser/AST.h>
#include <Wide/Util/Memory/MemoryArena.h>
#include <Wide/Util/Ranges/Optional.h>

namespace Wide {
    namespace Parse {
        enum class OutliningType : int {
            Module,
            Function,
            Type
        };
        struct PutbackLexer {
            PutbackLexer(std::function<Wide::Util::optional<Lexer::Token>()> lex) : lex(std::move(lex)) {}
            
            std::function<Wide::Util::optional<Lexer::Token>()> lex;
            std::vector<Lexer::Token> putbacks;
            std::vector<Lexer::Range> locations;

            Wide::Util::optional<Lexer::Token> operator()();
            Lexer::Token operator()(Parse::Error);
            void operator()(Lexer::Token arg);
            Lexer::Range GetLastPosition();
        };
        
        struct Parser {
            PutbackLexer lex;
            Wide::Memory::Arena arena;
            Module GlobalModule;
            std::function<void(std::vector<Wide::Lexer::Range>, Error)> error;
            std::function<void(Lexer::Range, Warning)> warning;
            std::function<void(Lexer::Range, OutliningType)> outlining;

            // All the valid productions in the global module.
            std::unordered_map<Lexer::TokenType, std::function<void(Parser&, Module*, Parse::Access, Lexer::Token& token)>> GlobalModuleTokens;
            // All the valid productions in global module with attributes.
            std::unordered_map<Lexer::TokenType, std::function<void(Parser&, Module*, Parse::Access, Lexer::Token& token, std::vector<Attribute> attributes)>> GlobalModuleAttributeTokens;
            // All the productions valid in non-global modules only.
            std::unordered_map<Lexer::TokenType, std::function<Parse::Access(Parser&, Module*, Parse::Access, Lexer::Token& token)>> ModuleTokens;
            
            // All user-defined overloadable operators that can be overloaded as free functions.
            std::unordered_set<Lexer::TokenType> ModuleOverloadableOperators;
            // All user-defined overloadable operators that can only be overloaded as members.
            std::unordered_set<Lexer::TokenType> MemberOverloadableOperators;
            // All user-defined overloadable operators that can only be overloaded as members and comprise two tokens.
            std::unordered_map<Lexer::TokenType, Lexer::TokenType> MemberDoubleOverloadableOperators;

            // Assignment operators and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<Expression*(Parser&, Expression*)>> AssignmentOperators;

            // Describes the operators and their precedences- all left-associative between assignment and unary
            std::vector<std::unordered_set<Lexer::TokenType>> ExpressionPrecedences;
            
            // Unary operators and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<Expression*(Parser&, Lexer::Token&)>> UnaryOperators;

            // Postfix operators and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<Expression*(Parser&, Expression*, Lexer::Token&)>> PostfixOperators;

            // Primary expressions and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<Expression*(Parser&, Lexer::Token&)>> PrimaryExpressions;

            // Statements and their behaviour
            std::unordered_map<Lexer::TokenType, std::function<Statement*(Parser& p, Lexer::Token& tok)>> Statements;

            // Type 
            std::unordered_map<Lexer::TokenType, std::function<Parse::Access(Parser&, Type*, Parse::Access, Lexer::Token&)>> TypeTokens;
            // Type attribute
            std::unordered_map<Lexer::TokenType, std::function<Parse::Access(Parser&, Type*, Parse::Access, Lexer::Token&, std::vector<Attribute>)>> TypeAttributeTokens;
            // Dynamic members
            std::unordered_map<Lexer::TokenType, std::function<DynamicFunction*(Parser&, Type*, Parse::Access, Lexer::Token&, std::vector<Attribute>)>> DynamicMemberFunctions;

            Parser(std::function<Wide::Util::optional<Lexer::Token>()> l);

            ParserError BadToken(const Lexer::Token& first, Parse::Error err);
            Expression* ParseSubAssignmentExpression(unsigned slot);
            Expression* ParseSubAssignmentExpression(unsigned slot, Expression* Unary);

            Lexer::Token Check(Parse::Error error, Lexer::TokenType tokty);
            template<typename F> Lexer::Token Check(Parse::Error error, F&& f) {
                auto t = lex();
                if (!t)
                    throw ParserError(lex.GetLastPosition(), error);
                if (!f(*t))
                    throw BadToken(*t, error);
                return *t;
            }
            
            Attribute ParseAttribute(Lexer::Token& tok);

            void ParseGlobalModuleContents(Module* m);
            void ParseGlobalModuleLevelDeclaration(Module* m);
            void ParseModuleContents(Module* m, Lexer::Range where);

            void AddTypeToModule(Module* m, std::string name, Type* t, Parse::Access specifier);
            void AddFunctionToModule(Module* m, std::string name, Function* f, Parse::Access specifier);
            void AddUsingToModule(Module* m, std::string name, Using* f, Parse::Access specifier);
            void AddTemplateTypeToModule(Module* m, std::string name, std::vector<FunctionArgument>, Type* t, Parse::Access specifier);
            Module* CreateModule(std::string name, Module* in, Lexer::Range where, Parse::Access access);

            Using* CreateUsing(std::string val, Lexer::Range loc, Expression* expr, Module* p, Parse::Access a); 
            Parse::Access ParseModuleLevelDeclaration(Module* m, Parse::Access a);
            std::vector<Variable*> ParseLambdaCaptures();
            Expression* ParsePrimaryExpression();
            std::vector<Expression*> ParseFunctionArguments();
            Expression* ParsePostfixExpression();
            Expression* ParseUnaryExpression();
            Expression* ParseAssignmentExpression();
            Expression* ParseExpression();
            Statement* ParseStatement();
            std::vector<FunctionArgument> ParseFunctionDefinitionArguments();
            Constructor* ParseConstructor(const Lexer::Token& first, std::vector<Attribute> attrs);
            Function* ParseFunction(const Lexer::Token& first, std::vector<Attribute> attrs);
            Destructor* ParseDestructor(const Lexer::Token& first, std::vector<Attribute> attrs);
            
            Module* ParseQualifiedName(Lexer::Token& first, Module* m, Parse::Access a, Parse::Error err);
            void ParseTypeBody(Type* ty);
            std::vector<Expression*> ParseTypeBases();
            Type* ParseTypeDeclaration(Module* m, Lexer::Range loc, Lexer::Token& ident, std::vector<Attribute>& attrs);
        };
    }
}
