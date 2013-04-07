#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "../Lexer/Token.h"
#include "../Util/MakeUnique.h"

// Lexing interface:
// operator() for a token
// explicit operator bool for if more tokens
// operator()(Token t) for putback.

// Token interface:
// Movable
// t.GetType() returns TokenType enumeration.

// Semantic interface:
// Expression type - A movable type which represents an expression. Isn't created directly.
// Statement type - A movable type which represents a statement. Isn't created directly. A statement is implicitly constructible from an rvalue expression.
// ModuleLevelDeclaration type - A movable type which represents any definition at module level.
// CreateStatementGroup() - creates a movable type which can hold multiple instances of the statement type. push_back should add an rvalue of type statement.
// CreateExpressionGroup() - creates a movable type which can hold multiple instances of the expression type. push_back should add an rvalue of type expression.
// CreateIdentExpression(std::string val) - creates an identifier expression, where the identifier is the argument.
// CreateStringExpression(std::string val) - creates a string expression, where the value of the string is the argument.
// CreateMemberAccessExpression(std::string mem, Expression obj) - creates a member access expression, where the "mem" member of "obj" is accessed.
// CreateLeftShiftExpression(Expression lhs, Expression rhs) - creates a left shift expression.
// CreateFunction(std::string name, StatementGroup group) - creates a function 
// CreateFunctionCallExpression(Expression, ExpressionGroup) - creates a function call expr, where 1 is object and 2 is arguments.
// CreateReturn(Expression) - return expr;
// CreateReturn() - return;
// CreateIntegerExpression(std::string);

namespace Wide {
    template<typename T> struct ExprType {
        typedef typename std::decay<decltype(*std::declval<T>().CreateExpressionGroup().begin())>::type type;
    };
    template<typename T> struct StmtType {        
        typedef typename std::decay<decltype(*std::declval<T>().CreateStatementGroup().begin())>::type type;
    };
    template<typename Lex, typename Sema> typename ExprType<Sema>::type ParsePrimaryExpression(Lex&& lex, Sema&& sema) {
        // ident
        // string
        // ( expression )
        auto t = lex();
        if (t.GetType() == Lexer::TokenType::OpenBracket) {
            auto expr = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
            t = lex(); // Consume close bracket
            if (t.GetType() != Lexer::TokenType::CloseBracket)
                throw std::runtime_error("Found ( expression but no closing ) in ParsePrimaryExpression.");
            return std::move(expr);
        }
        if (t.GetType() == Lexer::TokenType::Identifier)
            return sema.CreateIdentExpression(t.GetValue(), t.GetLocation());
        if (t.GetType() == Lexer::TokenType::String)
            return sema.CreateStringExpression(t.GetValue(), t.GetLocation());
        if (t.GetType() == Lexer::TokenType::Integer)
            return sema.CreateIntegerExpression(t.GetValue(), t.GetLocation());
        throw std::runtime_error("Expected expression, but could not find the start of an expression.");
    }

    template<typename Lex, typename Sema, typename Group> void ParseFunctionArguments(Lex&& lex, Sema&& sema, Group&& group) {
        group.push_back(ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema)));
        auto t = lex();
        if (t.GetType() == Lexer::TokenType::Comma)
            return ParseFunctionArguments(std::forward<Lex>(lex), std::forward<Sema>(sema), std::forward<Group>(group));
        if (t.GetType() == Lexer::TokenType::CloseBracket)
            return;
        throw std::runtime_error("Encountered unexpected token after function argument- was expecting comma or close bracket.");
    }

    template<typename Lex, typename Sema> typename ExprType<Sema>::type ParsePostfixExpression(Lex&& lex, Sema&& sema) {
        auto expr = ParsePrimaryExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
        while(true) {
           auto t = lex();
           if (t.GetType() == Lexer::TokenType::Dot) {
               expr = sema.CreateMemberAccessExpression(lex().GetValue(), std::move(expr), expr->GetLocation() + t.GetLocation());
               continue;
           }
           if (t.GetType() == Lexer::TokenType::OpenBracket) {
               auto argexprs = sema.CreateExpressionGroup();
               t = lex();
               if (t.GetType() != Lexer::TokenType::CloseBracket) {
                   lex(t);
                   ParseFunctionArguments(std::forward<Lex>(lex), std::forward<Sema>(sema), argexprs);
               }
               expr = sema.CreateFunctionCallExpression(std::move(expr), std::move(argexprs), expr->GetLocation() + t.GetLocation());
               continue;
           }
           if (t.GetType() == Lexer::TokenType::Exclaim) {
               t = lex();               
               if (t.GetType() != Lexer::TokenType::OpenBracket)
                   throw std::runtime_error("Expected ( after !.");
               auto argexprs = sema.CreateExpressionGroup();
               t = lex();
               if (t.GetType() != Lexer::TokenType::CloseBracket) {
                   lex(t);
                   ParseFunctionArguments(std::forward<Lex>(lex), std::forward<Sema>(sema), argexprs);
               }
               expr = sema.CreateMetaFunctionCallExpression(std::move(expr), std::move(argexprs), expr->GetLocation() + t.GetLocation());
               continue;
           }
           // Did not recognize either of these, so put it back and return the final result.
           lex(t);
           return std::move(expr);
        }
    }

    template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseShiftExpression(Expr e, Lex&& lex, Sema&& sema) {
        auto lhs = std::move(e);
        while(true) {
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::LeftShift) {
                auto rhs = ParsePostfixExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateLeftShiftExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            if (t.GetType() == Lexer::TokenType::RightShift) {
                auto rhs = ParsePostfixExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateRightShiftExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            lex(t);
            return std::move(lhs);
        }
    }
    
    template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseShiftExpression(Lex&& lex, Sema&& sema) {
        return ParseShiftExpression(ParsePostfixExpression(std::forward<Lex>(lex), std::forward<Sema>(sema)), std::forward<Lex>(lex), std::forward<Sema>(sema));
    }
    
    template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseRelationalExpression(Expr e, Lex&& lex, Sema&& sema) {
        auto lhs = ParseShiftExpression(std::move(e), std::forward<Lex>(lex), std::forward<Sema>(sema));
        while(true) {
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::LT) {
                auto rhs = ParseShiftExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateLTExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            if (t.GetType() == Lexer::TokenType::LTE) {
                auto rhs = ParseShiftExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateLTEExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            if (t.GetType() == Lexer::TokenType::GT) {
                auto rhs = ParseShiftExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateGTExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            if (t.GetType() == Lexer::TokenType::GTE) {
                auto rhs = ParseShiftExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateGTEExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            lex(t);
            return std::move(lhs);
        }
    }
    
    template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseRelationalExpression(Lex&& lex, Sema&& sema) {
        return ParseRelationalExpression(ParsePostfixExpression(std::forward<Lex>(lex), std::forward<Sema>(sema)), std::forward<Lex>(lex), std::forward<Sema>(sema));
    }

    template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseEqualityExpression(Expr e, Lex&& lex, Sema&& sema) {
        auto lhs = ParseRelationalExpression(std::move(e), std::forward<Lex>(lex), std::forward<Sema>(sema));
        while(true) {
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::EqCmp) {
                auto rhs = ParseRelationalExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateEqCmpExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            if (t.GetType() == Lexer::TokenType::NotEqCmp) {
                auto rhs = ParseRelationalExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateNotEqCmpExpression(std::move(lhs), std::move(rhs));
                continue;
            }v
            lex(t);
            return std::move(lhs);
        }
    }
    
    template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseEqualityExpression(Lex&& lex, Sema&& sema) {
        return ParseEqualityExpression(ParsePostfixExpression(std::forward<Lex>(lex), std::forward<Sema>(sema)), std::forward<Lex>(lex), std::forward<Sema>(sema));
    }
    
    template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseXorExpression(Postfix&& fix, Lex&& lex, Sema&& sema) {
        auto lhs = ParseEqualityExpression(std::forward<Postfix>(fix), std::forward<Lex>(lex), std::forward<Sema>(sema));
        while(true) {
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::Xor) {
                auto rhs = ParseEqualityExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateXorExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            lex(t);
            return std::move(lhs);
        }
    }
    
    template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseXorExpression(Lex&& lex, Sema&& sema) {
        return ParseXorExpression(ParsePostfixExpression(std::forward<Lex>(lex), std::forward<Sema>(sema)), std::forward<Lex>(lex), std::forward<Sema>(sema));
    }

    template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseAndExpression(Postfix&& fix, Lex&& lex, Sema&& sema) {
        auto lhs = ParseXorExpression(std::forward<Postfix>(fix), std::forward<Lex>(lex), std::forward<Sema>(sema));
        while(true) {
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::And) {
                auto rhs = ParseXorExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateAndExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            lex(t);
            return std::move(lhs);
        }
    }

    template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAndExpression(Lex&& lex, Sema&& sema) {
        return ParseAndExpression(ParsePostfixExpression(std::forward<Lex>(lex), std::forward<Sema>(sema)), std::forward<Lex>(lex), std::forward<Sema>(sema));
    }

    template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseOrExpression(Postfix&& fix, Lex&& lex, Sema&& sema) {
        auto lhs = ParseAndExpression(std::forward<Postfix>(fix), std::forward<Lex>(lex), std::forward<Sema>(sema));
        while(true) {
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::Or) {
                auto rhs = ParseAndExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                lhs = sema.CreateOrExpression(std::move(lhs), std::move(rhs));
                continue;
            }
            lex(t);
            return std::move(lhs);
        }
    }

    template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAssignmentExpression(Lex&& lex, Sema&& sema) {
        auto lhs = ParsePostfixExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
        auto t = lex();
        if (t.GetType() == Lexer::TokenType::Assignment) {
            auto rhs = ParseAssignmentExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
            return sema.CreateAssignmentExpression(std::move(lhs), std::move(rhs));
        }
        lex(t);
        return ParseOrExpression(std::move(lhs), std::forward<Lex>(lex), std::forward<Sema>(sema));
    }

    template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseExpression(Lex&& lex, Sema&& sema) {
        return ParseAssignmentExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
    }

    template<typename Lex, typename Sema> typename StmtType<Sema>::type ParseStatement(Lex&& lex, Sema&& sema) {
        // Check first token- if it is return, then parse return statement.
        auto t = lex();
        if (t.GetType() == Lexer::TokenType::Return) {
            auto next = lex(); // Check next token for ;
            if (next.GetType() == Lexer::TokenType::Semicolon) {
                return sema.CreateReturn(t.GetLocation() + next.GetLocation());
            }
            // If it wasn't ; then expect expression.
            lex(next);
            auto expr = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
            next = lex();
            if (next.GetType() != Lexer::TokenType::Semicolon)
                throw std::runtime_error("Expected semicolon after expression.");
            return sema.CreateReturn(std::move(expr), t.GetLocation() + next.GetLocation());
        }
        // If identifier, check the next for :=
        if (t.GetType() == Lexer::TokenType::Identifier) {
            auto next = lex();
            if (next.GetType() == Lexer::TokenType::VarCreate) {
                auto expr = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                auto semi = lex();
                if (semi.GetType() != Lexer::TokenType::Semicolon)
                    throw std::runtime_error("Expected semicolon after variable definition.");
                return sema.CreateVariableStatement(t.GetValue(), std::move(expr), t.GetLocation() + semi.GetLocation());
            }
            lex(next);
        }
        // If "if", then we're good.
        if (t.GetType() == Lexer::TokenType::If) {
            auto check = lex();
            if (check.GetType() != Lexer::TokenType::OpenBracket)
                throw std::runtime_error("Expected ( after if.");
            auto cond = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
            check = lex();
            if (check.GetType() != Lexer::TokenType::CloseBracket)
                throw std::runtime_error("Expected ) after if condition.");
            auto true_br = ParseStatement(std::forward<Lex>(lex), std::forward<Sema>(sema));
            auto next = lex();
            if (next.GetType() == Lexer::TokenType::Else)
                return sema.CreateIfStatement(cond, true_br, ParseStatement(std::forward<Lex>(lex), std::forward<Sema>(sema)));
            lex(next);
            return sema.CreateIfStatement(cond, true_br);
        }
        // If { then compound.
        if (t.GetType() == Lexer::TokenType::OpenCurlyBracket) {
            auto grp = sema.CreateStatementGroup();
            auto t = lex();
            while(t.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                lex(t);
                grp.push_back(ParseStatement(std::forward<Lex>(lex), std::forward<Sema>(sema)));
                t = lex();
            }
            return sema.CreateCompoundStatement(std::move(grp));
        }
        // If "while" then while.
        if (t.GetType() == Lexer::TokenType::While) {            
            auto check = lex();
            if (check.GetType() != Lexer::TokenType::OpenBracket)
                throw std::runtime_error("Expected ( after while.");
            auto cond = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
            check = lex();
            if (check.GetType() != Lexer::TokenType::CloseBracket)
                throw std::runtime_error("Expected ) after while condition.");
            auto body = ParseStatement(std::forward<Lex>(lex), std::forward<Sema>(sema));
            return sema.CreateWhileStatement(cond, body);
        }
        lex(t);
        // Else, expression statement.
        auto expr = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
        t = lex();
        if (t.GetType() != Lexer::TokenType::Semicolon)
            throw std::runtime_error("Expected semicolon after expression.");
        return std::move(expr);
    }

    template<typename Lex, typename Sema> auto ParseFunctionArguments(Lex&& lex, Sema&& sema) -> decltype(sema.CreateFunctionArgumentGroup()) {
        auto ret = sema.CreateFunctionArgumentGroup();
        auto t = lex();
        if (t.GetType() == Lexer::TokenType::CloseBracket)
            return ret;
        lex(t);
        // At least one argument.
        while(true) {
            auto t = lex();
            auto t2 = lex();
            if (t.GetType() == Lexer::TokenType::Identifier && (t2.GetType() == Lexer::TokenType::Comma || t2.GetType() == Lexer::TokenType::CloseBracket)) {
                // Type-deduced argument.
                sema.AddArgumentToFunctionGroup(ret, t.GetValue());
                if (t2.GetType() == Lexer::TokenType::CloseBracket)
                    break;
            } else {
                // Expression-specified argument.
                lex(t2);
                lex(t);
                auto ty = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                auto ident = lex();
                if (ident.GetType() != Lexer::TokenType::Identifier)
                    throw std::runtime_error("Expected identifier after expression when parsing function argument.");
                sema.AddArgumentToFunctionGroup(ret, ident.GetValue(), ty);
                t2 = lex();
                if (t2.GetType() == Lexer::TokenType::CloseBracket)
                    break;
                if (t2.GetType() != Lexer::TokenType::Comma)
                    throw std::runtime_error("Expected , or ) after function argument.");
            }
        }
        return ret;
    }

    template<typename Lex, typename Sema, typename Token, typename Module> void ParseFunction(Lex&& lex, Sema&& sema, Token&& first, Module&& m) 
    {
        // Identifier consumed
        // The first two must be () but we can find either prolog then body, or body.
        // Expect ParseFunctionArguments to have consumed the ).
        lex();
        auto group = ParseFunctionArguments(std::forward<Lex>(lex), std::forward<Sema>(sema));
        auto t = lex();
        auto prolog = sema.CreateStatementGroup();
        if (t.GetType() == Lexer::TokenType::Prolog) {
            t = lex();
            if (t.GetType() != Lexer::TokenType::OpenCurlyBracket)
                throw std::runtime_error("Expected { after prolog.");            
            while(true) {
                prolog.push_back(ParseStatement(std::forward<Lex>(lex), std::forward<Sema>(sema)));
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                    break;
                }
                lex(t);
            }
            // Consume the open curly of the body.
            t = lex();
        }
        if (t.GetType() != Lexer::TokenType::OpenCurlyBracket) {
            throw std::runtime_error("Expected prolog or { after function arguments.");
        }
        auto stmts = sema.CreateStatementGroup();
        auto pos = first.GetLocation();
        t = lex();
        if (t.GetType() == Lexer::TokenType::CloseCurlyBracket)
            sema.CreateFunction(first.GetValue(), std::move(stmts), std::move(prolog), pos + t.GetLocation(), m, std::move(group));
        lex(t);
        while(true) {
            stmts.push_back(ParseStatement(std::forward<Lex>(lex), std::forward<Sema>(sema)));
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                pos = pos + t.GetLocation();
                break;
            }
            lex(t);
        }
        sema.CreateFunction(first.GetValue(), std::move(stmts), std::move(prolog), pos, m, std::move(group));
    }

    template<typename Lex, typename Sema> auto ParseQualifiedName(Lex&& lex, Sema&& sema) -> decltype(sema.CreateQualifiedName()) {
        auto t = lex();
        if (t.GetType() != Lexer::TokenType::Identifier)
            throw std::runtime_error("Expected at least one identifier in qualified name.");
        auto qname = sema.CreateQualifiedName();
        while(t.GetType() == Lexer::TokenType::Identifier) {
            sema.AddNameToQualifiedName(qname, t.GetValue());
            t = lex();
            if (t.GetType() != Lexer::TokenType::Dot) {
                lex(t);
                return qname;
            }
            t = lex();
        }
        throw std::runtime_error("A qualified name can only be terminated with an identifier, not a dot.");
    }

    template<typename Lex, typename Sema, typename Module> 
    void ParseUsingDefinition(Lex&& lex, Sema&& sema, Module&& m) {
        // We got the "using". Now we have either identifier :=, identifier;, or identifer. We only support identifier := qualifiedname right now.
        auto t = lex();
        if (t.GetType() != Lexer::TokenType::Identifier) {
            throw std::runtime_error("All forms of using require an identifier after using.");
        }
        auto val = t.GetValue();
        t = lex();
        if (t.GetType() != Lexer::TokenType::VarCreate) {
            throw std::runtime_error("Don't support non-assigning usings right now.");
        }
        auto def = sema.CreateUsingDefinition(std::move(val), ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema)), m);
        t = lex();
        if (t.GetType() != Lexer::TokenType::Semicolon)
            throw std::runtime_error("Expected semicolon after using.");
    }

    //template<typename Lex, typename Sema, typename Module> void ParseModuleContents(Lex&& lex, Sema&& sema, Module&& m);

    template<typename Lex, typename Sema, typename Module> void ParseModuleDeclaration(Lex&& lex, Sema&& sema, Module&& m) {
        // Already got module
        auto ident = lex();
        if (ident.GetType() != Lexer::TokenType::Identifier)
            throw std::runtime_error("Expected identifier after module.");
        auto mod = sema.CreateModule(ident.GetValue(), m);
        auto curly = lex();
        if (curly.GetType() != Lexer::TokenType::OpenCurlyBracket)
            throw std::runtime_error("Expected { after identifier when parsing module.");
        ParseModuleContents(std::forward<Lex>(lex), std::forward<Sema>(sema), mod);
    }

    template<typename Lex, typename Sema, typename Module> void ParseModuleLevelDeclaration(Lex&& lex, Sema&& sema, Module&& m) {
        auto token = lex();
        if (token.GetType() == Lexer::TokenType::Identifier) {
            ParseFunction(std::forward<Lex>(lex), std::forward<Sema>(sema), token, std::forward<Module>(m));
            return;
        }
        if (token.GetType() == Lexer::TokenType::Using) {
            ParseUsingDefinition(std::forward<Lex>(lex), std::forward<Sema>(sema), std::forward<Module>(m));
            return;
        }
        if (token.GetType() == Lexer::TokenType::Module) {
            ParseModuleDeclaration(std::forward<Lex>(lex), std::forward<Sema>(sema), std::forward<Module>(m));
            return;
        }
        throw std::runtime_error("Only support function, using, or module right now.");
    }
        
    template<typename Lex, typename Sema, typename Module> void ParseModuleContents(Lex&& lex, Sema&& sema, Module&& m) {
        // Should really be refactored later into ParseGlobalModuleContents and ParseModuleDeclaration
        while(lex) {
            ParseModuleLevelDeclaration(std::forward<Lex>(lex), std::forward<Sema>(sema), m);
            if (lex) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::CloseCurlyBracket)
                    return;
                lex(t);
            }
        }
    }
}