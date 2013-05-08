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
    namespace Parser {
        template<typename T> struct ExprType {
            typedef typename std::decay<decltype(*std::declval<T>().CreateExpressionGroup().begin())>::type type;
        };
        template<typename T> struct StmtType {        
            typedef typename std::decay<decltype(*std::declval<T>().CreateStatementGroup().begin())>::type type;
        };
        template<typename Lex, typename Sema, typename Caps> void ParseLambdaCaptures(Lex&& lex, Sema&& sema, Caps&& caps) {
            auto tok = lex();
            while(true) {
                if (tok.GetType() != Lexer::TokenType::Identifier)
                    throw std::runtime_error("Expected identifier to introduce a lambda capture.");
                auto varassign = lex();
                if (varassign.GetType() != Lexer::TokenType::VarCreate)
                    throw std::runtime_error("Expected := after identifer when parsing lambda capture.");
                sema.AddCaptureToGroup(caps, ParseVariableStatement(std::forward<Lex>(lex), std::forward<Sema>(sema), tok));
                tok = lex();
                if (tok.GetType() == Lexer::TokenType::CloseSquareBracket)
                    break;
                else if (tok.GetType() == Lexer::TokenType::Comma)
                    tok = lex();
                else
                    throw std::runtime_error("Expected , or ] after a lambda capture.");
            }
        }
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
            if (t.GetType() == Lexer::TokenType::This)
                return sema.CreateThisExpression(t.GetLocation());
            if (t.GetType() == Lexer::TokenType::Function) {
                auto open = lex();
                if (open.GetType() != Lexer::TokenType::OpenBracket)
                    throw std::runtime_error("Expected ( after function in expression.");
                auto args = ParseFunctionArguments(std::forward<Lex>(lex), std::forward<Sema>(sema));

                auto pos = t.GetLocation();
                auto grp = sema.CreateStatementGroup();
                auto tok = lex();
                auto caps = sema.CreateCaptureGroup();
                bool defaultref = false;
                if (tok.GetType() == Lexer::TokenType::OpenSquareBracket) {
                    tok = lex();
                    if (tok.GetType() == Lexer::TokenType::And) {
                        defaultref = true;
                        tok = lex();
                        if (tok.GetType() == Lexer::TokenType::Comma)
                            ParseLambdaCaptures(std::forward<Lex>(lex), std::forward<Sema>(sema), caps);
                        else if (tok.GetType() == Lexer::TokenType::CloseSquareBracket)
                            tok = lex();
                        else 
                            throw std::runtime_error("Expected ] or , after [& in a lambda capture.");
                    } else {
                        lex(tok);
                        ParseLambdaCaptures(std::forward<Lex>(lex), std::forward<Sema>(sema), caps);
                    }
                }
                if (tok.GetType() != Lexer::TokenType::OpenCurlyBracket)
                    throw std::runtime_error("Expected { after function(args).");
                tok = lex();
                while(tok.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                    lex(tok);
                    grp.push_back(ParseStatement(std::forward<Lex>(lex), std::forward<Sema>(sema)));
                    tok = lex();
                }
                return sema.CreateLambda(std::move(args), std::move(grp), pos + tok.GetLocation(), defaultref, std::move(caps));
            }
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
                   expr = sema.CreateMemberAccessExpression(lex().GetValue(), std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                   continue;
               }
               if (t.GetType() == Lexer::TokenType::OpenBracket) {
                   auto argexprs = sema.CreateExpressionGroup();
                   t = lex();
                   if (t.GetType() != Lexer::TokenType::CloseBracket) {
                       lex(t);
                       ParseFunctionArguments(std::forward<Lex>(lex), std::forward<Sema>(sema), argexprs);
                   }
                   expr = sema.CreateFunctionCallExpression(std::move(expr), std::move(argexprs), sema.GetLocation(expr) + t.GetLocation());
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
                   expr = sema.CreateMetaFunctionCallExpression(std::move(expr), std::move(argexprs), sema.GetLocation(expr) + t.GetLocation());
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
                }
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

        template<typename Lex, typename Sema, typename Token> auto ParseVariableStatement(Lex&& lex, Sema&& sema, Token&& t) 
        -> decltype(sema.CreateVariableStatement(t.GetValue(), typename ExprType<Sema>::type(), t.GetLocation())) {
            // Expect to have already seen :=
            auto expr = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
            auto semi = lex();
            if (semi.GetType() != Lexer::TokenType::Semicolon)
                throw std::runtime_error("Expected semicolon after variable definition.");
            return sema.CreateVariableStatement(t.GetValue(), std::move(expr), t.GetLocation() + semi.GetLocation());
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
                    return ParseVariableStatement(std::forward<Lex>(lex), std::forward<Sema>(sema), t);
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
                if (next.GetType() == Lexer::TokenType::Else) {
                    auto else_br =  ParseStatement(std::forward<Lex>(lex), std::forward<Sema>(sema));
                    return sema.CreateIfStatement(cond, true_br, else_br, t.GetLocation() + sema.GetLocation(else_br));
                }
                lex(next);
                return sema.CreateIfStatement(cond, true_br, t.GetLocation() + sema.GetLocation(true_br));
            }
            // If { then compound.
            if (t.GetType() == Lexer::TokenType::OpenCurlyBracket) {
                auto pos = t.GetLocation();
                auto grp = sema.CreateStatementGroup();
                auto t = lex();
                while(t.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                    lex(t);
                    grp.push_back(ParseStatement(std::forward<Lex>(lex), std::forward<Sema>(sema)));
                    t = lex();
                }
                return sema.CreateCompoundStatement(std::move(grp), pos + t.GetLocation());
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
                return sema.CreateWhileStatement(cond, body, t.GetLocation() + sema.GetLocation(body));
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
            // Identifier ( consumed
            // The first two must be () but we can find either prolog then body, or body.
            // Expect ParseFunctionArguments to have consumed the ).
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
                // Consume the close curly of the end of the prolog, leaving { for the next section to find.
                t = lex();
            }
            auto initializers = sema.CreateInitializerGroup();
            if (first.GetType() == Lexer::TokenType::Type) {
                // Constructor- check for initializer list
                while(t.GetType() == Lexer::TokenType::Colon) {
                    // Expect identifer ( expression )
                    auto name = lex();
                    if (name.GetType() != Lexer::TokenType::Identifier)
                        throw std::runtime_error("Expected identifier after : to name a member to initialize in a constructor.");
                    auto open = lex();
                    if (open.GetType() != Lexer::TokenType::OpenBracket)
                        throw std::runtime_error("Expected ( after identifier when creating ctor initializer.");
                    auto next = lex();
                    if (next.GetType() == Lexer::TokenType::CloseBracket) {
                        // Empty initializer- e.g. : x()
                        sema.AddInitializerToGroup(initializers, sema.CreateVariableStatement(name.GetValue(), nullptr, t.GetLocation() + next.GetLocation()));
                        continue;
                    }
                    lex(next);
                    auto expr = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                    next = lex();
                    if (next.GetType() != Lexer::TokenType::CloseBracket)
                        throw std::runtime_error("Expected ) to close a member initializer after parsing : identifier ( expression");
                    sema.AddInitializerToGroup(initializers, sema.CreateVariableStatement(name.GetValue(), std::move(expr), t.GetLocation() + next.GetLocation()));
                    t = lex();
                }
            }
            if (t.GetType() != Lexer::TokenType::OpenCurlyBracket) {
                throw std::runtime_error("Expected { after function arguments or prolog");
            }
            auto stmts = sema.CreateStatementGroup();
            auto pos = first.GetLocation();
            t = lex();
            if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                sema.CreateFunction(first.GetValue(), std::move(stmts), std::move(prolog), pos + t.GetLocation(), m, std::move(group), std::move(initializers));
                return;
            }
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
            sema.CreateFunction(first.GetValue(), std::move(stmts), std::move(prolog), pos, m, std::move(group), std::move(initializers));
        }

        template<typename Lex, typename Sema, typename Module> 
        void ParseUsingDefinition(Lex&& lex, Sema&& sema, Module&& m) {
            // We got the "using". Now we have either identifier :=, identifier;, or identifer. We only support identifier := expr right now.
            auto t = lex();
            if (t.GetType() != Lexer::TokenType::Identifier) {
                throw std::runtime_error("All forms of using require an identifier after using.");
            }
            auto val = t.GetValue();
            t = lex();
            if (t.GetType() != Lexer::TokenType::VarCreate) {
                if (t.GetType() == Lexer::TokenType::Assignment)
                    std::cout << "Warning: = used in using instead of :=. Treating as :=.\n";
                else
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

        template<typename Lex, typename Sema, typename Module> void ParseTypeDeclaration(Lex&& lex, Sema&& sema, Module&& m) {
            auto ident = lex();
            if (ident.GetType() != Lexer::TokenType::Identifier)
                throw std::runtime_error("Expected identifier after type.");
            auto t = lex();
            if (t.GetType() != Lexer::TokenType::OpenCurlyBracket)
                throw std::runtime_error("Expected { after identifier.");
            t = lex();
            auto ty = sema.CreateType(ident.GetValue(), m);
            while(t.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                if (t.GetType() == Lexer::TokenType::Identifier) {
                    // Must be either := for variable or ( for function. Don't support functions yet.
                    auto next = lex();
                    if (next.GetType() == Lexer::TokenType::VarCreate) {
                        auto var = ParseVariableStatement(std::forward<Lex>(lex), std::forward<Sema>(sema), t);
                        sema.AddTypeField(ty, var);
                        t = lex();
                        continue;
                    }
                    if (next.GetType() == Lexer::TokenType::OpenBracket) {
                        ParseFunction(std::forward<Lex>(lex), std::forward<Sema>(sema), t, ty);
                        t = lex();
                        continue;
                    }
                }
                if (t.GetType() == Lexer::TokenType::Operator) {
                    auto op = lex();
                    // Only < supported right now
                    static std::unordered_set<Lexer::TokenType> OverloadableOperators([]() -> std::unordered_set<Lexer::TokenType> {
                        Lexer::TokenType tokens[] = {
                            Lexer::TokenType::Assignment,
                            Lexer::TokenType::EqCmp,
                            Lexer::TokenType::NotEqCmp,
                            Lexer::TokenType::GT,
                            Lexer::TokenType::GTE,
                            Lexer::TokenType::LT,
                            Lexer::TokenType::LTE,
                            Lexer::TokenType::And,
                            Lexer::TokenType::Or,
                            Lexer::TokenType::LeftShift,
                            Lexer::TokenType::RightShift,
                            Lexer::TokenType::Xor,
                            Lexer::TokenType::OpenBracket
                        };
                        return std::unordered_set<Lexer::TokenType>(std::begin(tokens), std::end(tokens));
                    }());
                    if (op.GetType() == Lexer::TokenType::OpenBracket) {
                        auto t = lex();
                        if (t.GetType() != Lexer::TokenType::CloseBracket)
                            throw std::runtime_error("Expected ) after operator(.");
                    }
                    if (OverloadableOperators.find(op.GetType()) == OverloadableOperators.end())
                        throw std::runtime_error("This operator was not found on the supported operators list.");
                    auto bracket = lex();
                    if (bracket.GetType() != Lexer::TokenType::OpenBracket)
                        throw std::runtime_error("Expected ( after operator op to designate a type-scope operator overload.");
                    ParseFunction(std::forward<Lex>(lex), std::forward<Sema>(sema), op, ty);
                    t = lex();
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Type) {
                    auto open = lex();
                    if (open.GetType() != Lexer::TokenType::OpenBracket)
                        throw std::runtime_error("Expected ( after type to introduce a constructor at type scope.");
                    ParseFunction(std::forward<Lex>(lex), std::forward<Sema>(sema), t, ty);
                    t = lex();
                    continue;
                }
                throw std::runtime_error("Only member variables supported right now and they open with an identifier.");
            }
        }
        
        template<typename Lex, typename Sema, typename Module> void ParseModuleLevelDeclaration(Lex&& lex, Sema&& sema, Module&& m) {
            auto token = lex();
            if (token.GetType() == Lexer::TokenType::Identifier) {
                auto t = lex();
                if (t.GetType() != Lexer::TokenType::OpenBracket)
                    throw std::runtime_error("Expected ( after identifier to designate a function at module scope.");
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
            if (token.GetType() == Lexer::TokenType::Type) {
                ParseTypeDeclaration(std::forward<Lex>(lex), std::forward<Sema>(sema), std::forward<Module>(m));
                return;
            }
            if (token.GetType() == Lexer::TokenType::Semicolon) {
                std::cout << "Warning: unnecessary semicolon at module scope. Skipping.\n";
                return ParseModuleLevelDeclaration(std::forward<Lex>(lex), std::forward<Sema>(sema), std::forward<Module>(m));
            }
            if (token.GetType() == Lexer::TokenType::Operator) {
                auto op = lex();
                // Only < supported right now
                if (op.GetType() != Lexer::TokenType::LT)
                    throw std::runtime_error("Only < supported as an overloadable operator right now.");
                auto bracket = lex();
                if (bracket.GetType() != Lexer::TokenType::OpenBracket)
                    throw std::runtime_error("Expected ( after operator op to designate a module-scope operator overload.");
                ParseFunction(std::forward<Lex>(lex), std::forward<Sema>(sema), op, std::forward<Module>(m));
                return;
            }
            throw std::runtime_error("Only support function, using, type, operator, or module right now.");
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
}