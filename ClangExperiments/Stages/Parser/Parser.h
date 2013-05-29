#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "../Lexer/Token.h"
#include "../Util/MakeUnique.h"

namespace Wide {
    namespace Parser {        
        static const std::unordered_set<Lexer::TokenType> OverloadableOperators([]() -> std::unordered_set<Lexer::TokenType> {
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
        template<typename T> struct ExprType {
            typedef typename std::decay<decltype(*std::declval<T>().CreateExpressionGroup().begin())>::type type;
        };
        template<typename T> struct StmtType {        
            typedef typename std::decay<decltype(*std::declval<T>().CreateStatementGroup().begin())>::type type;
        };
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseExpression(Lex&& lex, Sema&& sema);
        template<typename Lex, typename Sema, typename Group> void ParseFunctionArguments(Lex&& lex, Sema&& sema, Group&& group);
        template<typename Lex, typename Sema> auto ParseFunctionArguments(Lex&& lex, Sema&& sema) -> decltype(sema.CreateFunctionArgumentGroup());
        template<typename Lex, typename Sema> typename StmtType<Sema>::type ParseStatement(Lex&& lex, Sema&& sema);
        template<typename Lex, typename Sema, typename Ty> void ParseTypeBody(Lex&& lex, Sema&& sema, Ty&& ty);
        template<typename Lex, typename Sema, typename Caps> void ParseLambdaCaptures(Lex&& lex, Sema&& sema, Caps&& caps) {
            auto tok = lex();
            while(true) {
                if (tok.GetType() != Lexer::TokenType::Identifier)
                    throw std::runtime_error("Expected identifier to introduce a lambda capture.");
                auto varassign = lex();
                if (varassign.GetType() != Lexer::TokenType::VarCreate)
                    throw std::runtime_error("Expected := after identifer when parsing lambda capture.");
                auto init = ParseExpression(lex, sema);
                sema.AddCaptureToGroup(caps, sema.CreateVariableStatement(tok.GetValue(), init, tok.GetLocation() + sema.GetLocation(init)));
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
                auto expr = ParseExpression(lex, sema);
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
                auto args = ParseFunctionArguments(lex, sema);

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
                        if (tok.GetType() == Lexer::TokenType::Comma) {
                            ParseLambdaCaptures(lex, sema, caps);
                            tok = lex();
                        }
                        else if (tok.GetType() == Lexer::TokenType::CloseSquareBracket)
                            tok = lex();
                        else 
                            throw std::runtime_error("Expected ] or , after [& in a lambda capture.");
                    } else {
                        lex(tok);
                        ParseLambdaCaptures(lex, sema, caps);
                        tok = lex();
                    }
                }
                if (tok.GetType() != Lexer::TokenType::OpenCurlyBracket)
                    throw std::runtime_error("Expected { after function(args).");
                tok = lex();
                while(tok.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                    lex(tok);
                    grp.push_back(ParseStatement(lex, sema));
                    tok = lex();
                }
                return sema.CreateLambda(std::move(args), std::move(grp), pos + tok.GetLocation(), defaultref, std::move(caps));
            }
            if (t.GetType() == Lexer::TokenType::Type) {
                auto ty = sema.CreateType("__unnamed", nullptr);
                if (lex().GetType() != Lexer::TokenType::OpenCurlyBracket)
                    throw std::runtime_error("Expected { after type in expression.");
                ParseTypeBody(lex, sema, ty);
                return ty;
            }
            /*if (t.GetType() == Lexer::TokenType::Auto) {
                return sema.CreateAutoExpression(t.GetLocation());
            }*/
            throw std::runtime_error("Expected expression, but could not find the start of an expression.");
        }
        
        template<typename Lex, typename Sema, typename Group> void ParseFunctionArguments(Lex&& lex, Sema&& sema, Group&& group) {
            group.push_back(ParseExpression(lex, sema));
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::Comma)
                return ParseFunctionArguments(lex, sema, std::forward<Group>(group));
            if (t.GetType() == Lexer::TokenType::CloseBracket)
                return;
            throw std::runtime_error("Encountered unexpected token after function argument- was expecting comma or close bracket.");
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParsePostfixExpression(Lex&& lex, Sema&& sema) {
            auto expr = ParsePrimaryExpression(lex, sema);
            while(true) {
               auto t = lex();
               if (t.GetType() == Lexer::TokenType::Dot) {
                   expr = sema.CreateMemberAccessExpression(lex().GetValue(), std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                   continue;
               }
               if (t.GetType() == Lexer::TokenType::PointerAccess) {
                   expr = sema.CreatePointerAccessExpression(lex().GetValue(), std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                   continue;
               }
               if (t.GetType() == Lexer::TokenType::Increment) {
                   expr = sema.CreatePostfixIncrement(std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                   continue;
               }
               if (t.GetType() == Lexer::TokenType::Decrement) {
                   expr = sema.CreatePostfixDecrement(std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                   continue;
               }
               if (t.GetType() == Lexer::TokenType::OpenBracket) {
                   auto argexprs = sema.CreateExpressionGroup();
                   t = lex();
                   if (t.GetType() != Lexer::TokenType::CloseBracket) {
                       lex(t);
                       ParseFunctionArguments(lex, sema, argexprs);
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
                       ParseFunctionArguments(lex, sema, argexprs);
                   }
                   expr = sema.CreateMetaFunctionCallExpression(std::move(expr), std::move(argexprs), sema.GetLocation(expr) + t.GetLocation());
                   continue;
               }
               // Did not recognize either of these, so put it back and return the final result.
               lex(t);
               return std::move(expr);
            }
        }

        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseUnaryExpression(Lex&& lex, Sema&& sema) {
            auto tok = lex();
            if (tok.GetType() == Lexer::TokenType::Dereference) {
                auto expr = ParseUnaryExpression(lex, sema);
                return sema.CreateDereferenceExpression(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::Negate) {
                auto expr = ParseUnaryExpression(lex, sema);
                return sema.CreateNegateExpression(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::Increment) {
                auto expr = ParseUnaryExpression(lex, sema);
                return sema.CreatePrefixIncrement(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::Decrement) {
                auto expr = ParseUnaryExpression(lex, sema);
                return sema.CreatePrefixDecrement(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            lex(tok);
            return ParsePostfixExpression(lex, sema);
        }

        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseMultiplicativeExpression(Expr e, Lex&& lex, Sema&& sema) {
            auto lhs = std::move(e);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Dereference) {
                    auto rhs = ParseUnaryExpression(lex, sema);
                    lhs = sema.CreateMultiplyExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseMultiplicativeExpression(Lex&& lex, Sema&& sema) {
            return ParseMultiplicativeExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }

        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseAdditiveExpression(Expr e, Lex&& lex, Sema&& sema) {
            auto lhs = ParseMultiplicativeExpression(std::move(e), lex, sema);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Plus) {
                    auto rhs = ParseMultiplicativeExpression(lex, sema);
                    lhs = sema.CreateAdditionExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAdditiveExpression(Lex&& lex, Sema&& sema) {
            return ParseAdditiveExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }
                
        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseShiftExpression(Expr e, Lex&& lex, Sema&& sema) {
            auto lhs = ParseAdditiveExpression(std::move(e), lex, sema);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::LeftShift) {
                    auto rhs = ParseAdditiveExpression(lex, sema);
                    lhs = sema.CreateLeftShiftExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::RightShift) {
                    auto rhs = ParseAdditiveExpression(lex, sema);
                    lhs = sema.CreateRightShiftExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseShiftExpression(Lex&& lex, Sema&& sema) {
            return ParseShiftExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }
        
        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseRelationalExpression(Expr e, Lex&& lex, Sema&& sema) {
            auto lhs = ParseShiftExpression(std::move(e), lex, sema);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::LT) {
                    auto rhs = ParseShiftExpression(lex, sema);
                    lhs = sema.CreateLTExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::LTE) {
                    auto rhs = ParseShiftExpression(lex, sema);
                    lhs = sema.CreateLTEExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::GT) {
                    auto rhs = ParseShiftExpression(lex, sema);
                    lhs = sema.CreateGTExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::GTE) {
                    auto rhs = ParseShiftExpression(lex, sema);
                    lhs = sema.CreateGTEExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseRelationalExpression(Lex&& lex, Sema&& sema) {
            return ParseRelationalExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }
        
        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseEqualityExpression(Expr e, Lex&& lex, Sema&& sema) {
            auto lhs = ParseRelationalExpression(std::move(e), lex, sema);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::EqCmp) {
                    auto rhs = ParseRelationalExpression(lex, sema);
                    lhs = sema.CreateEqCmpExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::NotEqCmp) {
                    auto rhs = ParseRelationalExpression(lex, sema);
                    lhs = sema.CreateNotEqCmpExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseEqualityExpression(Lex&& lex, Sema&& sema) {
            return ParseEqualityExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }
        
        template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseXorExpression(Postfix&& fix, Lex&& lex, Sema&& sema) {
            auto lhs = ParseEqualityExpression(std::forward<Postfix>(fix), lex, sema);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Xor) {
                    auto rhs = ParseEqualityExpression(lex, sema);
                    lhs = sema.CreateXorExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseXorExpression(Lex&& lex, Sema&& sema) {
            return ParseXorExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }
        
        template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseAndExpression(Postfix&& fix, Lex&& lex, Sema&& sema) {
            auto lhs = ParseXorExpression(std::forward<Postfix>(fix), lex, sema);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::And) {
                    auto rhs = ParseXorExpression(lex, sema);
                    lhs = sema.CreateAndExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAndExpression(Lex&& lex, Sema&& sema) {
            return ParseAndExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }
        
        template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseOrExpression(Postfix&& fix, Lex&& lex, Sema&& sema) {
            auto lhs = ParseAndExpression(std::forward<Postfix>(fix), lex, sema);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Or) {
                    auto rhs = ParseAndExpression(lex, sema);
                    lhs = sema.CreateOrExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAssignmentExpression(Lex&& lex, Sema&& sema) {
            auto lhs = ParseUnaryExpression(lex, sema);
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::Assignment) {
                auto rhs = ParseAssignmentExpression(lex, sema);
                return sema.CreateAssignmentExpression(std::move(lhs), std::move(rhs));
            }
            lex(t);
            return ParseOrExpression(std::move(lhs), lex, sema);
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseExpression(Lex&& lex, Sema&& sema) {
            return ParseAssignmentExpression(lex, sema);
        }

        template<typename Lex, typename Sema, typename Token> auto ParseVariableStatement(Lex&& lex, Sema&& sema, Token&& t) 
        -> decltype(sema.CreateVariableStatement(t.GetValue(), typename ExprType<Sema>::type(), t.GetLocation())) {
            // Expect to have already seen :=
            auto expr = ParseExpression(lex, sema);
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
                auto expr = ParseExpression(lex, sema);
                next = lex();
                if (next.GetType() != Lexer::TokenType::Semicolon)
                    throw std::runtime_error("Expected semicolon after expression.");
                return sema.CreateReturn(std::move(expr), t.GetLocation() + next.GetLocation());
            }
            // If identifier, check the next for :=
            if (t.GetType() == Lexer::TokenType::Identifier) {
                auto next = lex();
                if (next.GetType() == Lexer::TokenType::VarCreate) {
                    return ParseVariableStatement(lex, sema, t);
                }
                lex(next);
            }
            // If "if", then we're good.
            if (t.GetType() == Lexer::TokenType::If) {
                auto check = lex();
                if (check.GetType() != Lexer::TokenType::OpenBracket)
                    throw std::runtime_error("Expected ( after if.");
                auto cond = ParseExpression(lex, sema);
                check = lex();
                if (check.GetType() != Lexer::TokenType::CloseBracket)
                    throw std::runtime_error("Expected ) after if condition.");
                auto true_br = ParseStatement(lex, sema);
                auto next = lex();
                if (next.GetType() == Lexer::TokenType::Else) {
                    auto else_br =  ParseStatement(lex, sema);
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
                    grp.push_back(ParseStatement(lex, sema));
                    t = lex();
                }
                return sema.CreateCompoundStatement(std::move(grp), pos + t.GetLocation());
            }
            // If "while" then while.
            if (t.GetType() == Lexer::TokenType::While) {            
                auto check = lex();
                if (check.GetType() != Lexer::TokenType::OpenBracket)
                    throw std::runtime_error("Expected ( after while.");
                auto cond = ParseExpression(lex, sema);
                check = lex();
                if (check.GetType() != Lexer::TokenType::CloseBracket)
                    throw std::runtime_error("Expected ) after while condition.");
                auto body = ParseStatement(lex, sema);
                return sema.CreateWhileStatement(cond, body, t.GetLocation() + sema.GetLocation(body));
            }
            lex(t);
            // Else, expression statement.
            auto expr = ParseExpression(lex, sema);
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
                    auto ty = ParseExpression(lex, sema);
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
            auto group = ParseFunctionArguments(lex, sema);
            auto t = lex();
            auto prolog = sema.CreateStatementGroup();
            if (t.GetType() == Lexer::TokenType::Prolog) {
                t = lex();
                if (t.GetType() != Lexer::TokenType::OpenCurlyBracket)
                    throw std::runtime_error("Expected { after prolog.");            
                while(true) {
                    prolog.push_back(ParseStatement(lex, sema));
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
                    auto expr = ParseExpression(lex, sema);
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
            auto val = first.GetValue();
            switch(first.GetType()) {
            case Lexer::TokenType::Or:
                val = "|";
                break;
            }
            if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                sema.CreateFunction(val, std::move(stmts), std::move(prolog), pos + t.GetLocation(), m, std::move(group), std::move(initializers));
                return;
            }
            lex(t);
            while(true) {
                stmts.push_back(ParseStatement(lex, sema));
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                    pos = pos + t.GetLocation();
                    break;
                }
                lex(t);
            }
            sema.CreateFunction(val, std::move(stmts), std::move(prolog), pos, m, std::move(group), std::move(initializers));
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
            sema.CreateUsingDefinition(std::move(val), ParseExpression(lex, sema), m);
            t = lex();
            if (t.GetType() != Lexer::TokenType::Semicolon)
                throw std::runtime_error("Expected semicolon after using.");
        }
        
        template<typename Lex, typename Sema, typename Module> void ParseModuleContents(Lex&& lex, Sema&& sema, Module&& m);        
        template<typename Lex, typename Sema, typename Module> void ParseModuleDeclaration(Lex&& lex, Sema&& sema, Module&& m) {
            // Already got module
            auto ident = lex();
            if (ident.GetType() != Lexer::TokenType::Identifier)
                throw std::runtime_error("Expected identifier after module.");
            auto mod = sema.CreateModule(ident.GetValue(), m);
            auto curly = lex();
            if (curly.GetType() != Lexer::TokenType::OpenCurlyBracket)
                throw std::runtime_error("Expected { after identifier when parsing module.");
            ParseModuleContents(lex, sema, mod);
        }

        template<typename Lex, typename Sema, typename Ty> void ParseTypeBody(Lex&& lex, Sema&& sema, Ty&& ty) {
            auto t = lex();
            while(t.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                if (t.GetType() == Lexer::TokenType::Identifier) {
                    // Must be either := for variable or ( for function. Don't support functions yet.
                    auto next = lex();
                    if (next.GetType() == Lexer::TokenType::VarCreate) {
                        auto var = ParseVariableStatement(lex, sema, t);
                        sema.AddTypeField(ty, var);
                        t = lex();
                        continue;
                    }
                    if (next.GetType() == Lexer::TokenType::OpenBracket) {
                        ParseFunction(lex, sema, t, ty);
                        t = lex();
                        continue;
                    }
                }
                if (t.GetType() == Lexer::TokenType::Operator) {
                    auto op = lex();
                    // Only < supported right now
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
                    ParseFunction(lex, sema, op, ty);
                    t = lex();
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Type) {
                    auto open = lex();
                    if (open.GetType() != Lexer::TokenType::OpenBracket)
                        throw std::runtime_error("Expected ( after type to introduce a constructor at type scope.");
                    ParseFunction(lex, sema, t, ty);
                    t = lex();
                    continue;
                }
                throw std::runtime_error("Only member variables supported right now and they open with an identifier.");
            }
        }

        template<typename Lex, typename Sema, typename Module> void ParseTypeDeclaration(Lex&& lex, Sema&& sema, Module&& m) {
            auto ident = lex();
            if (ident.GetType() != Lexer::TokenType::Identifier)
                throw std::runtime_error("Expected identifier after type.");
            auto t = lex();
            if (t.GetType() != Lexer::TokenType::OpenCurlyBracket)
                throw std::runtime_error("Expected { after identifier.");
            auto ty = sema.CreateType(ident.GetValue(), m);
            return ParseTypeBody(lex, sema, ty);
        }
        
        template<typename Lex, typename Sema, typename Module> void ParseModuleLevelDeclaration(Lex&& lex, Sema&& sema, Module&& m) {
            auto token = lex();
            if (token.GetType() == Lexer::TokenType::Identifier) {
                auto t = lex();
                if (t.GetType() != Lexer::TokenType::OpenBracket)
                    throw std::runtime_error("Expected ( after identifier to designate a function at module scope.");
                ParseFunction(lex, sema, token, std::forward<Module>(m));
                return;
            }
            if (token.GetType() == Lexer::TokenType::Using) {
                ParseUsingDefinition(lex, sema, std::forward<Module>(m));
                return;
            }
            if (token.GetType() == Lexer::TokenType::Module) {
                ParseModuleDeclaration(lex, sema, std::forward<Module>(m));
                return;
            }
            if (token.GetType() == Lexer::TokenType::Type) {
                ParseTypeDeclaration(lex, sema, std::forward<Module>(m));
                return;
            }
            if (token.GetType() == Lexer::TokenType::Semicolon) {
                std::cout << "Warning: unnecessary semicolon at module scope. Skipping.\n";
                return ParseModuleLevelDeclaration(lex, sema, std::forward<Module>(m));
            }
            if (token.GetType() == Lexer::TokenType::Operator) {
                auto op = lex();
                // Only < supported right now
                if (op.GetType() == Lexer::TokenType::OpenBracket) {
                    auto t = lex();
                    if (t.GetType() != Lexer::TokenType::CloseBracket)
                        throw std::runtime_error("Expected ) after operator(.");
                }
                if (OverloadableOperators.find(op.GetType()) == OverloadableOperators.end())
                    throw std::runtime_error("This operator was not found on the supported operators list.");
                auto bracket = lex();
                if (bracket.GetType() != Lexer::TokenType::OpenBracket)
                    throw std::runtime_error("Expected ( after operator op to designate a module-scope operator overload.");
                ParseFunction(lex, sema, op, std::forward<Module>(m));
                return;
            }
            throw std::runtime_error("Only support function, using, type, operator, or module right now.");
        }
            
        template<typename Lex, typename Sema, typename Module> void ParseModuleContents(Lex&& lex, Sema&& sema, Module&& m) {
            // Should really be refactored later into ParseGlobalModuleContents and ParseModuleDeclaration
            while(lex) {
                ParseModuleLevelDeclaration(lex, sema, m);
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