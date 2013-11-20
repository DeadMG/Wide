#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <Wide/Lexer/Token.h>
#include <Wide/Parser/ParserError.h>
#include <Wide/Util/Ranges/Optional.h>

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
                Lexer::TokenType::Dereference,
                Lexer::TokenType::Or,
                Lexer::TokenType::LeftShift,
                Lexer::TokenType::RightShift,
                Lexer::TokenType::Xor,
                Lexer::TokenType::OpenBracket,
                Lexer::TokenType::Plus
            };
            return std::unordered_set<Lexer::TokenType>(std::begin(tokens), std::end(tokens));
        }());
        
        // Tokens which introduce module-level scope productions
        static const std::unordered_set<Lexer::TokenType> ModuleGrammarIntroducers([]() -> std::unordered_set<Lexer::TokenType> {
            Lexer::TokenType tokens[] = {
                Lexer::TokenType::Module,
                Lexer::TokenType::Using,
                Lexer::TokenType::Type,
                Lexer::TokenType::Identifier,
                Lexer::TokenType::Operator
            };
            return std::unordered_set<Lexer::TokenType>(std::begin(tokens), std::end(tokens));
        }());
         
        template<typename T> struct ExprType {
            typedef typename std::decay<T>::type::ExpressionType type;
        };
        template<typename T> struct StmtType {        
            typedef typename std::decay<T>::type::StatementType type;
        };
        template<typename Token, typename Lex> ParserError BadToken(Lex&& lex, Token&& first, Error err) {
            // Put it back so that the recovery functions might match it successfully.
            lex(first);            
            // Now throw it.
            return ParserError(first.GetLocation(), lex.GetLastPosition(), err);
        }
        template<typename Lex> auto Check(Lex&& lex, Error error, Lexer::TokenType tokty) -> decltype(lex()) {
            if (!lex)
                throw ParserError(lex.GetLastPosition(), error);
            auto t = lex();
            if (t.GetType() != tokty)
                throw BadToken(lex, t, error);
            return t;
        }
        template<typename Lex, typename F> auto Check(Lex&& lex, Error error, F&& f) -> decltype(lex()) {
            if (!lex)
                throw ParserError(lex.GetLastPosition(), error);
            auto t = lex();
            if (!f(t))
                throw BadToken(lex, t, error);
            return t;
        }
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseExpression(Lex&& lex, Sema&& sema);
        template<typename Lex, typename Sema, typename Group, typename Loc> void ParseFunctionArguments(Lex&& lex, Sema&& sema, Group&& group);
        template<typename Lex, typename Sema, typename Loc> auto ParseFunctionDefinitionArguments(Lex&& lex, Sema&& sema, Loc&& open) -> decltype(sema.CreateFunctionArgumentGroup());
        template<typename Lex, typename Sema> typename StmtType<Sema>::type ParseStatement(Lex&& lex, Sema&& sema);
        template<typename Lex, typename Sema, typename Ty> void ParseTypeBody(Lex&& lex, Sema&& sema, Ty&& ty);
        template<typename Lex, typename Sema, typename Caps, typename Loc> void ParseLambdaCaptures(Lex&& lex, Sema&& sema, Caps&& caps, Loc&& loc) {
            auto tok = lex();
            while(true) {
                if (tok.GetType() != Lexer::TokenType::Identifier)
                    throw std::runtime_error("Expected identifier to introduce a lambda capture.");
                auto varassign = lex();
                if (varassign.GetType() != Lexer::TokenType::VarCreate)
                    throw std::runtime_error("Expected := after identifer when parsing lambda capture.");
                auto init = ParseExpression(lex, sema);
                sema.AddCaptureToGroup(caps, sema.CreateVariable(tok.GetValue(), init, tok.GetLocation() + sema.GetLocation(init)));
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
            if (!lex)
                throw ParserError(lex.GetLastPosition(), Error::ExpressionNoBeginning);
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::OpenBracket) {
                try {
                    auto expr = ParseExpression(lex, sema);
                    Check(lex, Error::ParenthesisedExpressionNoCloseBracket, Lexer::TokenType::CloseBracket);
                    return std::move(expr);
                } catch(ParserError& e) {
                    if (!lex)
                        throw;
                    auto recov = lex();
                    if (recov.GetType() == Lexer::TokenType::CloseBracket) {
                        sema.Error(e.recover_where(), e.error());
                        return sema.CreateError(recov.GetLocation() + t.GetLocation());
                    }
                    lex(recov);
                    throw;
                }
            }
            if (t.GetType() == Lexer::TokenType::Identifier)
                return sema.CreateIdentifier(t.GetValue(), t.GetLocation());
            if (t.GetType() == Lexer::TokenType::String)
                return sema.CreateString(t.GetValue(), t.GetLocation());
            if (t.GetType() == Lexer::TokenType::Integer)
                return sema.CreateInteger(t.GetValue(), t.GetLocation());
            if (t.GetType() == Lexer::TokenType::This)
                return sema.CreateThis(t.GetLocation());
            if (t.GetType() == Lexer::TokenType::Function) {
                auto args = sema.CreateFunctionArgumentGroup();
                try {
                    args = ParseFunctionDefinitionArguments(lex, sema, Check(lex, Error::LambdaNoOpenBracket, Lexer::TokenType::OpenBracket).GetLocation());
                } catch(ParserError& e) {
                    if (!lex) throw;
                    auto tok = lex();
                    if (tok.GetType() != Lexer::TokenType::OpenSquareBracket && tok.GetType() != Lexer::TokenType::OpenCurlyBracket)
                        throw;
                    lex(tok);
                    sema.Error(e.recover_where(), e.error());
                }

                auto pos = t.GetLocation();
                auto grp = sema.CreateStatementGroup();
                auto tok = lex();
                auto caps = sema.CreateCaptureGroup();
                bool defaultref = false;
                if (tok.GetType() == Lexer::TokenType::OpenSquareBracket) {
                    auto opensquare = tok.GetLocation();
                    tok = lex();
                    if (tok.GetType() == Lexer::TokenType::And) {
                        defaultref = true;
                        tok = lex();
                        if (tok.GetType() == Lexer::TokenType::Comma) {
                            ParseLambdaCaptures(lex, sema, caps, opensquare);
                            tok = lex();
                        }
                        else if (tok.GetType() == Lexer::TokenType::CloseSquareBracket) {
                            tok = lex();
                        }
                        else 
                            throw std::runtime_error("Expected ] or , after [& in a lambda capture.");
                    } else {
                        lex(tok);
                        ParseLambdaCaptures(lex, sema, caps, opensquare);
                        tok = lex();
                    }
                }
                if (tok.GetType() != Lexer::TokenType::OpenCurlyBracket)
                    throw BadToken(lex, tok, Error::LambdaNoOpenCurly);
                auto opencurly = tok.GetLocation();
                tok = lex();
                while(tok.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                    lex(tok);
                    sema.AddStatementToGroup(grp, ParseStatement(lex, sema));
                    tok = lex();
                }
                return sema.CreateLambda(std::move(args), std::move(grp), pos + tok.GetLocation(), defaultref, std::move(caps));
            }
            if (t.GetType() == Lexer::TokenType::Type) {                
                auto ty = sema.CreateType("anonymous", Check(lex, Error::TypeExpressionNoCurly, Lexer::TokenType::OpenCurlyBracket).GetLocation());
                ParseTypeBody(lex, sema, ty);
                return ty;
            }
           
            throw BadToken(lex, t, Error::ExpressionNoBeginning);
        }
        
        template<typename Lex, typename Sema, typename Group> void ParseFunctionArguments(Lex&& lex, Sema&& sema, Group&& group) {
            try {
                sema.AddExpressionToGroup(group, ParseExpression(lex, sema));
            } catch(ParserError& e) {
                if (!lex) throw;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::CloseBracket) {
                    sema.Error(e.recover_where(), e.error());
                    return;
                }
                if (t.GetType() == Lexer::TokenType::Comma) {
                    sema.Error(e.recover_where(), e.error());
                    return ParseFunctionArguments(lex, sema, group);
                }
                lex(t);
                throw;
            }
            if (!lex)
                throw ParserError(lex.GetLastPosition(), Error::FunctionArgumentNoBracketOrComma);
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::Comma)
                return ParseFunctionArguments(lex, sema, std::forward<Group>(group));
            if (t.GetType() == Lexer::TokenType::CloseBracket)
                return;
            lex(t);
            throw ParserError(lex.GetLastPosition(), Error::FunctionArgumentNoBracketOrComma);
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParsePostfixExpression(Lex&& lex, Sema&& sema) {
            auto expr = ParsePrimaryExpression(lex, sema);
            while(true) {
                if (!lex)
                    return expr;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Dot) {
                    Check(lex, Error::MemberAccessNoIdentifierOrDestructor, [&](decltype(lex())& tok) {
                        if (tok.GetType() == Lexer::TokenType::Identifier) {                           
                            expr = sema.CreateMemberAccess(tok.GetValue(), std::move(expr), sema.GetLocation(expr) + tok.GetLocation());
                            return true;
                        }
                        if (tok.GetType() != Lexer::TokenType::Negate)
                            return false;
                        Check(lex, Error::MemberAccessNoTypeAfterNegate, Lexer::TokenType::Type);
                        expr = sema.CreateMemberAccess("~type", std::move(expr), sema.GetLocation(expr) + tok.GetLocation());
                        return true;
                    });
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::PointerAccess) {
                    Check(lex, Error::PointerAccessNoIdentifierOrDestructor, [&](decltype(lex())& tok) {
                        if (tok.GetType() == Lexer::TokenType::Identifier) {                           
                            expr = sema.CreatePointerAccess(tok.GetValue(), std::move(expr), sema.GetLocation(expr) + tok.GetLocation());
                            return true;
                        }
                        if (tok.GetType() != Lexer::TokenType::Negate)
                            return false;
                        Check(lex, Error::PointerAccessNoTypeAfterNegate, Lexer::TokenType::Type);
                        expr = sema.CreatePointerAccess("~type", std::move(expr), sema.GetLocation(expr) + tok.GetLocation());
                        return true;
                    });
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
                    expr = sema.CreateFunctionCall(std::move(expr), std::move(argexprs), sema.GetLocation(expr) + lex.GetLastPosition());
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Exclaim) {
                    Check(lex, Error::NoOpenBracketAfterExclaim, Lexer::TokenType::OpenBracket);
                    auto argexprs = sema.CreateExpressionGroup();
                    t = lex();
                    if (t.GetType() != Lexer::TokenType::CloseBracket) {
                        lex(t);
                        ParseFunctionArguments(lex, sema, argexprs);
                    }
                    expr = sema.CreateMetaFunctionCall(std::move(expr), std::move(argexprs), sema.GetLocation(expr) + t.GetLocation());
                    continue;
                }
                // Did not recognize either of these, so put it back and return the final result.
                lex(t);
                return std::move(expr);
            }
        }

        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseUnaryExpression(Lex&& lex, Sema&& sema) {
            // Even if this token is not a unary operator, primary requires at least one token.
            // So just fail right away if there are no more tokens here.
            if (!lex)
                throw ParserError(lex.GetLastPosition(), Error::ExpressionNoBeginning);
            auto tok = lex();
            if (tok.GetType() == Lexer::TokenType::Dereference) {
                auto expr = ParseUnaryExpression(lex, sema);
                return sema.CreateDereference(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::Negate) {
                auto expr = ParseUnaryExpression(lex, sema);
                return sema.CreateNegate(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::Increment) {
                auto expr = ParseUnaryExpression(lex, sema);
                return sema.CreatePrefixIncrement(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::Decrement) {
                auto expr = ParseUnaryExpression(lex, sema);
                return sema.CreatePrefixDecrement(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::And) {
                auto expr = ParseUnaryExpression(lex, sema);
                return sema.CreateAddressOf(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            lex(tok);
            return ParsePostfixExpression(lex, sema);
        }

        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseMultiplicativeExpression(Expr e, Lex&& lex, Sema&& sema) {
            auto lhs = std::move(e);
            while(true) {
                if (!lex)
                    return lhs;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Dereference) {
                    auto rhs = ParseUnaryExpression(lex, sema);
                    lhs = sema.CreateMultiply(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Divide) {
                    auto rhs = ParseUnaryExpression(lex, sema);
                    lhs = sema.CreateDivision(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Modulo) {
                    auto rhs = ParseUnaryExpression(lex, sema);
                    lhs = sema.CreateModulus(std::move(lhs), std::move(rhs));
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
                if (!lex)
                    return lhs;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Plus) {
                    auto rhs = ParseMultiplicativeExpression(lex, sema);
                    lhs = sema.CreateAddition(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Minus) {
                    auto rhs = ParseMultiplicativeExpression(lex, sema);
                    lhs = sema.CreateSubtraction(std::move(lhs), std::move(rhs));
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
                if (!lex)
                    return lhs;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::LeftShift) {
                    auto rhs = ParseAdditiveExpression(lex, sema);
                    lhs = sema.CreateLeftShift(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::RightShift) {
                    auto rhs = ParseAdditiveExpression(lex, sema);
                    lhs = sema.CreateRightShift(std::move(lhs), std::move(rhs));
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
                if (!lex)
                    return lhs;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::LT) {
                    auto rhs = ParseShiftExpression(lex, sema);
                    lhs = sema.CreateLT(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::LTE) {
                    auto rhs = ParseShiftExpression(lex, sema);
                    lhs = sema.CreateLTE(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::GT) {
                    auto rhs = ParseShiftExpression(lex, sema);
                    lhs = sema.CreateGT(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::GTE) {
                    auto rhs = ParseShiftExpression(lex, sema);
                    lhs = sema.CreateGTE(std::move(lhs), std::move(rhs));
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
                if (!lex)
                    return lhs;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::EqCmp) {
                    auto rhs = ParseRelationalExpression(lex, sema);
                    lhs = sema.CreateEqCmp(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::NotEqCmp) {
                    auto rhs = ParseRelationalExpression(lex, sema);
                    lhs = sema.CreateNotEqCmp(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseEqualityExpression(Lex&& lex, Sema&& sema) {
            return ParseEqualityExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }
        
        template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseAndExpression(Postfix&& fix, Lex&& lex, Sema&& sema) {
            auto lhs = ParseEqualityExpression(std::forward<Postfix>(fix), lex, sema);
            while(true) {
                if (!lex)
                    return lhs;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::And) {
                    auto rhs = ParseEqualityExpression(lex, sema);
                    lhs = sema.CreateAnd(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAndExpression(Lex&& lex, Sema&& sema) {
            return ParseAndExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }
        
        template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseXorExpression(Postfix&& fix, Lex&& lex, Sema&& sema) {
            auto lhs = ParseAndExpression(std::forward<Postfix>(fix), lex, sema);
            while(true) {
                if (!lex)
                    return lhs;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Xor) {
                    auto rhs = ParseAndExpression(lex, sema);
                    lhs = sema.CreateXor(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseXorExpression(Lex&& lex, Sema&& sema) {
            return ParseXorExpression(ParseUnaryExpression(lex, sema), lex, sema);
        }
        
        template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseOrExpression(Postfix&& fix, Lex&& lex, Sema&& sema) {
            auto lhs = ParseXorExpression(std::forward<Postfix>(fix), lex, sema);
            while(true) {
                if (!lex)
                    return lhs;
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Or) {
                    auto rhs = ParseXorExpression(lex, sema);
                    lhs = sema.CreateOr(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }

        const std::unordered_set<Lexer::TokenType> AssignmentOperators = []() -> std::unordered_set<Lexer::TokenType> {
            std::unordered_set<Lexer::TokenType> ret;
            ret.insert(Lexer::TokenType::Assignment);
            ret.insert(Lexer::TokenType::MinusAssign);
            ret.insert(Lexer::TokenType::PlusAssign);
            ret.insert(Lexer::TokenType::AndAssign);
            ret.insert(Lexer::TokenType::OrAssign);
            ret.insert(Lexer::TokenType::MulAssign);
            ret.insert(Lexer::TokenType::ModAssign);
            ret.insert(Lexer::TokenType::DivAssign);
            ret.insert(Lexer::TokenType::XorAssign);
            ret.insert(Lexer::TokenType::RightShiftAssign);
            ret.insert(Lexer::TokenType::LeftShiftAssign);
            return ret;
        }();

        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAssignmentExpression(Lex&& lex, Sema&& sema) {
            auto lhs = ParseUnaryExpression(lex, sema);
            // Somebody's gonna be disappointed because an expression is not a valid end of program.
            // But we don't know who or what they're looking for, so just wait and let them fail.
            // Same strategy for all expression types.
            if (!lex)
                return lhs;
            auto t = lex();
            if (AssignmentOperators.find(t.GetType()) != AssignmentOperators.end()) {
                return sema.CreateAssignment(std::move(lhs), ParseAssignmentExpression(lex, sema), t.GetType());
            }
            lex(t);
            return ParseOrExpression(std::move(lhs), lex, sema);
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseExpression(Lex&& lex, Sema&& sema) {
            return ParseAssignmentExpression(lex, sema);
        }

        template<typename Lex, typename Sema, typename Token> auto ParseVariableStatement(Lex&& lex, Sema&& sema, Token&& t) 
        -> decltype(sema.CreateVariable(t.GetValue(), typename ExprType<Sema>::type(), t.GetLocation())) {
            // Expect to have already seen :=
            auto expr = ParseExpression(lex, sema);
            auto semi = Check(lex, Error::VariableStatementNoSemicolon, Lexer::TokenType::Semicolon);
            return sema.CreateVariable(t.GetValue(), std::move(expr), t.GetLocation() + semi.GetLocation());
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
                next = Check(lex, Error::ReturnNoSemicolon, Lexer::TokenType::Semicolon);
                return sema.CreateReturn(std::move(expr), t.GetLocation() + next.GetLocation());
            }
            // If "if", then we're good.
            if (t.GetType() == Lexer::TokenType::If) {
                Check(lex, Error::IfNoOpenBracket, Lexer::TokenType::OpenBracket);
                auto cond = ParseExpression(lex, sema);
                Check(lex, Error::IfNoCloseBracket, Lexer::TokenType::CloseBracket);
                auto true_br = ParseStatement(lex, sema);
                auto next = lex();
                if (next.GetType() == Lexer::TokenType::Else) {
                    auto else_br =  ParseStatement(lex, sema);
                    return sema.CreateIf(cond, true_br, else_br, t.GetLocation() + sema.GetLocation(else_br));
                }
                lex(next);
                return sema.CreateIf(cond, true_br, t.GetLocation() + sema.GetLocation(true_br));
            }
            // If { then compound.
            if (t.GetType() == Lexer::TokenType::OpenCurlyBracket) {
                auto pos = t.GetLocation();
                auto grp = sema.CreateStatementGroup();
                auto t = lex();
                while(t.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                    lex(t);
                    sema.AddStatementToGroup(grp, ParseStatement(lex, sema));
                    t = lex();
                }
                return sema.CreateCompoundStatement(std::move(grp), pos + t.GetLocation());
            }
            if (t.GetType() == Lexer::TokenType::While) {
                Check(lex, Error::WhileNoOpenBracket, Lexer::TokenType::OpenBracket);
                // Check for variable conditions.
                auto ident = lex();
                if (ident.GetType() == Lexer::TokenType::Identifier) {
                    auto var = lex();
                    if (var.GetType() == Lexer::TokenType::VarCreate) {
                        auto expr = ParseExpression(std::forward<Lex>(lex), std::forward<Sema>(sema));
                        auto variable = sema.CreateVariable(ident.GetValue(), std::move(expr), t.GetLocation() + sema.GetLocation(expr));
                        Check(lex, Error::WhileNoCloseBracket, Lexer::TokenType::CloseBracket);
                        auto body = ParseStatement(lex, sema);
                        return sema.CreateWhile(variable, body, t.GetLocation() + sema.GetLocation(body));
                    }
                    lex(var);
                }
                lex(ident);

                auto cond = ParseExpression(lex, sema);
                Check(lex, Error::WhileNoCloseBracket, Lexer::TokenType::CloseBracket);
                auto body = ParseStatement(lex, sema);
                return sema.CreateWhile(cond, body, t.GetLocation() + sema.GetLocation(body));
            }
            // If identifier, check the next for :=
            if (t.GetType() == Lexer::TokenType::Identifier) {
                auto next = lex();
                if (next.GetType() == Lexer::TokenType::VarCreate) {
                    return ParseVariableStatement(lex, sema, t);
                }
                // If it's not := then we're probably just looking at expression statement so put it back.
                lex(next);
            }
            // If continue or break, we're good.
            if (t.GetType() == Lexer::TokenType::Break) {
                auto semi = Check(lex, Error::BreakNoSemicolon, Lexer::TokenType::Semicolon);
                return sema.CreateBreak(t.GetLocation() + semi.GetLocation());
            }
            if (t.GetType() == Lexer::TokenType::Continue) {
                auto semi = Check(lex, Error::ContinueNoSemicolon, Lexer::TokenType::Semicolon);
                return sema.CreateContinue(t.GetLocation() + semi.GetLocation());
            }
            lex(t);
            // Else, expression statement.
            auto expr = ParseExpression(lex, sema);
            Check(lex, Error::ExpressionStatementNoSemicolon, Lexer::TokenType::Semicolon);
            return std::move(expr);
        }
        
        template<typename Lex, typename Sema, typename Loc> auto ParseFunctionDefinitionArguments(Lex&& lex, Sema&& sema, Loc&& loc) -> decltype(sema.CreateFunctionArgumentGroup()) {
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
                    sema.AddArgumentToFunctionGroup(ret, t.GetValue(), t.GetLocation());
                    if (t2.GetType() == Lexer::TokenType::CloseBracket) {
                        break;
                    }
                } else {
                    // Expression-specified argument.
                    lex(t2);
                    lex(t);
                    try {
                        auto ty = ParseExpression(lex, sema);
                        auto ident = Check(lex, Error::FunctionArgumentNoIdentifierOrThis, [](decltype(lex())& tok) {
                            if (tok.GetType() == Lexer::TokenType::Identifier || tok.GetType() == Lexer::TokenType::This)
                                return true;
                            return false;
                        });
                        sema.AddArgumentToFunctionGroup(ret, ident.GetValue(), ident.GetLocation() + sema.GetLocation(ty), ty);
                        auto brace = Check(lex, Error::FunctionArgumentNoBracketOrComma, [&](decltype(lex())& tok) {
                            if (tok.GetType() == Lexer::TokenType::CloseBracket || tok.GetType() == Lexer::TokenType::Comma)
                                return true;
                            return false;
                        });
                        if (brace.GetType() == Lexer::TokenType::CloseBracket) {
                            break;
                        }
                    } catch(ParserError& e) {
                        if (!lex) throw;
                        auto tok = lex();
                        switch(tok.GetType()) {
                        case Lexer::TokenType::Comma:
                            sema.Error(e.recover_where(), e.error());
                            continue;
                        case Lexer::TokenType::CloseBracket:
                            sema.Error(e.recover_where(), e.error());
                            return ret;
                        }
                        lex(tok);
                        throw;
                    }
                }
            }
            return ret;
        }
        
        template<typename Lex, typename Sema, typename Group> auto ParseProlog(Lex&& lex, Sema&& sema, Group&& prolog) -> decltype(lex().GetLocation()) {
            auto t = lex();
            auto loc = t.GetLocation();
            if (t.GetType() == Lexer::TokenType::Prolog) {
                t = Check(lex, Error::PrologNoOpenCurly, Lexer::TokenType::OpenCurlyBracket);
                while(true) {
                    sema.AddStatementToGroup(prolog, ParseStatement(lex, sema));
                    t = lex();
                    if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                        break;
                    }
                    lex(t);
                }
            } else
                lex(t);
            return loc;
        }

        template<typename Lex, typename Sema, typename Token, typename Module, typename Loc> void ParseOverloadedOperator(Lex&& lex, Sema&& sema, Token&& first, Module&& m, Loc&& open) {
            auto group = ParseFunctionDefinitionArguments(lex, sema, std::forward<Loc>(open));
            auto prolog = sema.CreateStatementGroup();
            auto loc = ParseProlog(lex, sema, prolog);
            auto t = Check(lex, Error::OperatorNoCurlyToIntroduceBody, Lexer::TokenType::OpenCurlyBracket);
            auto pos = t.GetLocation();
            auto stmts = sema.CreateStatementGroup();
            t = lex();
            if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                sema.CreateOverloadedOperator(first.GetType(), std::move(stmts), std::move(prolog), loc + t.GetLocation(), m, std::move(group));
                return;
            }
            lex(t);
            while(true) {
                try {
                    sema.AddStatementToGroup(stmts, ParseStatement(lex, sema));
                } catch(ParserError& e) {
                    if (!lex) throw;
                    auto t = lex();
                    switch(t.GetType()) {
                        // If we open a statement:
                    case Lexer::TokenType::While:
                    case Lexer::TokenType::If:
                    case Lexer::TokenType::OpenCurlyBracket:
                    case Lexer::TokenType::Return:
                        // Or an expression:
                    case Lexer::TokenType::Identifier:
                    case Lexer::TokenType::Dereference:
                    case Lexer::TokenType::Negate:
                    case Lexer::TokenType::Increment:
                    case Lexer::TokenType::Decrement:
                    case Lexer::TokenType::And:
                    case Lexer::TokenType::OpenBracket:
                    case Lexer::TokenType::String:
                    case Lexer::TokenType::Integer:
                    case Lexer::TokenType::This:
                    case Lexer::TokenType::Function:
                    case Lexer::TokenType::Type:
                        // Then this could be a valid statement, so attempt recovery.
                        sema.Error(e.recover_where(), e.error());
                        continue;
                    case Lexer::TokenType::CloseCurlyBracket:
                        sema.Error(e.recover_where(), e.error());
                        lex(t);
                        break;
                    default:
                        throw;
                    }
                }
                t = lex();
                if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                    pos = pos + t.GetLocation();
                    break;
                }
                lex(t);
            }
            sema.CreateOverloadedOperator(first.GetType(), std::move(stmts), std::move(prolog), loc + t.GetLocation(), m, std::move(group));
        }

        template<typename Lex, typename Sema, typename Token, typename Module, typename Loc> void ParseFunction(Lex&& lex, Sema&& sema, Token&& first, Module&& m, Loc&& open) 
        {
            // Identifier ( consumed
            // The first two must be () but we can find either prolog then body, or body.
            // Expect ParseFunctionArguments to have consumed the ).
            auto group = ParseFunctionDefinitionArguments(lex, sema, std::forward<Loc>(open));
            auto close = lex.GetLastPosition();
            auto prolog = sema.CreateStatementGroup();
            auto loc = ParseProlog(lex, sema, prolog);
            auto initializers = sema.CreateInitializerGroup();
            auto t = lex();
            if (first.GetType() == Lexer::TokenType::Type) {
                // Constructor- check for initializer list
                while(t.GetType() == Lexer::TokenType::Colon) {
                    auto name = Check(lex, Error::ConstructorNoIdentifierAfterColon, Lexer::TokenType::Identifier);
                    auto open = Check(lex, Error::ConstructorNoBracketAfterMemberName, Lexer::TokenType::OpenBracket);

                    if (!lex)
                        throw ParserError(lex.GetLastPosition(), Error::ConstructorNoBracketOrExpressionAfterMemberName);
                    auto next = lex();
                    if (next.GetType() == Lexer::TokenType::CloseBracket) {
                        // Empty initializer- e.g. : x()
                        sema.AddInitializerToGroup(initializers, sema.CreateVariable(name.GetValue(), t.GetLocation() + next.GetLocation()));
                        continue;
                    }
                    lex(next);
                    try {
                        auto expr = ParseExpression(lex, sema);
                        next = Check(lex, Error::ConstructorNoBracketClosingInitializer, Lexer::TokenType::CloseBracket);
                        sema.AddInitializerToGroup(initializers, sema.CreateVariable(name.GetValue(), std::move(expr), t.GetLocation() + next.GetLocation()));
                    } catch(ParserError& e) {
                        if (!lex) throw;
                        auto t = lex();
                        if (t.GetType() == Lexer::TokenType::CloseBracket) {
                            sema.Error(e.recover_where(), e.error());
                            continue;
                        }
                        lex(t);
                        throw;
                    }
                    t = lex();
                }
            }
            if (t.GetType() != Lexer::TokenType::OpenCurlyBracket)
                throw BadToken(lex, t, Error::FunctionNoCurlyToIntroduceBody);
            auto stmts = sema.CreateStatementGroup();
            auto pos = first.GetLocation();
            if (!lex)
                throw ParserError(lex.GetLastPosition(), Error::FunctionNoClosingCurly);
            t = lex();
            if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                sema.CreateFunction(first.GetValue(), std::move(stmts), std::move(prolog), first.GetLocation() + close, loc + t.GetLocation(), m, std::move(group), std::move(initializers));
                return;
            }
            lex(t);
            while(true) {
                try {
                    sema.AddStatementToGroup(stmts, ParseStatement(lex, sema));
                } catch(ParserError& e) {
                    if (!lex) throw;
                    auto tok = lex();
                    switch(tok.GetType()) {
                        // If we open a statement:
                    case Lexer::TokenType::While:
                    case Lexer::TokenType::If:
                    case Lexer::TokenType::OpenCurlyBracket:
                    case Lexer::TokenType::Return:
                        // Or an expression:
                    case Lexer::TokenType::Identifier:
                    case Lexer::TokenType::Dereference:
                    case Lexer::TokenType::Negate:
                    case Lexer::TokenType::Increment:
                    case Lexer::TokenType::Decrement:
                    case Lexer::TokenType::And:
                    case Lexer::TokenType::OpenBracket:
                    case Lexer::TokenType::String:
                    case Lexer::TokenType::Integer:
                    case Lexer::TokenType::This:
                    case Lexer::TokenType::Function:
                    case Lexer::TokenType::Type:
                        // Then this could be a valid statement, so attempt recovery.
                        sema.Error(e.recover_where(), e.error());
                        continue;
                    }
                    throw;
                }
                if (!lex)
                    throw ParserError(lex.GetLastPosition(), Error::FunctionNoClosingCurly);
                t = lex();
                if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                    pos = pos + t.GetLocation();
                    break;
                }
                lex(t);
            }
            sema.CreateFunction(first.GetValue(), std::move(stmts), std::move(prolog), first.GetLocation() + close, loc + t.GetLocation(), m, std::move(group), std::move(initializers));
        }

        template<typename Lex, typename Sema, typename Module, typename Token> void ParseUsingDefinition(Lex&& lex, Sema&& sema, Module&& m, Token&& first) {
            // We got the "using". Now we have either identifier :=, identifier;, or identifer. We only support identifier := expr right now.
            auto useloc = lex.GetLastPosition();
            auto t = Check(lex, Error::ModuleScopeUsingNoIdentifier, Lexer::TokenType::Identifier);
            auto var = Check(lex, Error::ModuleScopeUsingNoVarCreate, [&](decltype(lex())& curr) {
                if (curr.GetType() == Lexer::TokenType::Assignment) {
                    sema.Warning(curr.GetLocation(), Warning::AssignmentInUsing);
                    return true;
                }
                if (curr.GetType() == Lexer::TokenType::VarCreate)
                    return true;
                return false;
            });
            auto expr = ParseExpression(lex, sema);
            sema.CreateUsing(t.GetValue(),  useloc + sema.GetLocation(expr), std::move(expr), m);
            Check(lex, Error::ModuleScopeUsingNoSemicolon, Lexer::TokenType::Semicolon);
        }
        
        template<typename Lex, typename Sema, typename Module> void ParseModuleContents(Lex&& lex, Sema&& sema, Module&& m);        
        template<typename Lex, typename Sema, typename Module, typename Token> void ParseModuleDeclaration(Lex&& lex, Sema&& sema, Module&& m, Token&& tok) {
            // Already got module
            auto loc = lex.GetLastPosition();
            auto ident = Check(lex, Error::ModuleNoIdentifier, Lexer::TokenType::Identifier);
            auto curly = Check(lex, Error::ModuleNoOpeningBrace, Lexer::TokenType::OpenCurlyBracket);
            auto mod = sema.CreateModule(ident.GetValue(), m, loc + curly.GetLocation());
            ParseModuleContents(lex, sema, mod);
        }

        template<typename Lex, typename Sema, typename Ty> void ParseTypeBody(Lex&& lex, Sema&& sema, Ty&& ty) {
            auto loc = lex.GetLastPosition();
            auto t = lex();
            while(t.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                if (t.GetType() == Lexer::TokenType::Identifier) {
                    // Must be either := for variable or ( for function. Don't support functions yet.
                    Check(lex, Error::TypeScopeExpectedMemberAfterIdentifier, [&](decltype(lex())& tok) {
                        if (tok.GetType() == Lexer::TokenType::VarCreate) {
                            auto var = ParseVariableStatement(lex, sema, t);
                            sema.AddTypeField(ty, var);
                            t = lex();
                            return true;
                        }
                        if (tok.GetType() == Lexer::TokenType::OpenBracket) {
                            ParseFunction(lex, sema, t, ty, tok.GetLocation());
                            t = lex();
                            return true;
                        }
                        return false;
                    });
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Operator) {
                    auto op = Check(lex, Error::NonOverloadableOperator, [&](decltype(lex())& tok) {
                        if (tok.GetType() == Lexer::TokenType::OpenBracket) {
                            Check(lex, Error::OperatorNoCloseBracketAfterOpen, Lexer::TokenType::CloseBracket);
                            return true;
                        }
                        if (OverloadableOperators.find(tok.GetType()) != OverloadableOperators.end())
                            return true;
                        return false;
                    });
                    auto bracket = Check(lex, Error::TypeScopeOperatorNoOpenBracket, Lexer::TokenType::OpenBracket);
                    ParseOverloadedOperator(lex, sema, op, ty, bracket.GetLocation());
                    t = lex();
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Type) {
                    auto open = Check(lex, Error::ConstructorNoOpenBracket, Lexer::TokenType::OpenBracket);
                    ParseFunction(lex, sema, t, ty, open.GetLocation());
                    t = lex();
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Negate) {
                    auto next = Check(lex, Error::DestructorNoType, Lexer::TokenType::Type);
                    auto open = Check(lex, Error::DestructorNoOpenBracket, Lexer::TokenType::OpenBracket);
                    ParseFunction(lex, sema, t, ty, open.GetLocation());
                    t = lex();
                    continue;
                }
                throw BadToken(lex, t, Error::ModuleScopeTypeExpectedMemberOrTerminate);
            }
            sema.SetTypeEndLocation(loc + t.GetLocation(), ty);
        }

        template<typename Lex, typename Sema, typename Module, typename Loc> void ParseTypeDeclaration(Lex&& lex, Sema&& sema, Module&& m, Loc&& loc) {
            auto ident = Check(lex, Error::ModuleScopeTypeNoIdentifier, Lexer::TokenType::Identifier);
            auto t = Check(lex, Error::ModuleScopeTypeNoCurlyBrace, [&](decltype(lex())& curr) -> bool {
                if (curr.GetType() == Lexer::TokenType::OpenCurlyBracket)
                    return true;
                if (curr.GetType() == Lexer::TokenType::OpenBracket) {
                    // Insert an extra lex here so that future code can recover the function.
                    lex(curr);
                    lex(ident);
                    throw ParserError(curr.GetLocation(), lex.GetLastPosition(), Error::ModuleScopeTypeIdentifierButFunction);
                }
                return false;
            });
            auto ty = sema.CreateType(ident.GetValue(), m, loc + t.GetLocation());
            return ParseTypeBody(lex, sema, ty);
        }
                
        template<typename Lex, typename Sema, typename Module> void ParseModuleLevelDeclaration(Lex&& lex, Sema&& sema, Module&& m) {
            try {
                auto token = lex();
                if (token.GetType() == Lexer::TokenType::Identifier) {
                    auto t = Check(lex, Parser::Error::ModuleScopeFunctionNoOpenBracket, Lexer::TokenType::OpenBracket);
                    ParseFunction(lex, sema, token, std::forward<Module>(m), t.GetLocation());
                    return;
                }
                if (token.GetType() == Lexer::TokenType::Using) {
                    ParseUsingDefinition(lex, sema, std::forward<Module>(m), token);
                    return;
                }
                if (token.GetType() == Lexer::TokenType::Module) {
                    ParseModuleDeclaration(lex, sema, std::forward<Module>(m), token);
                    return;
                }
                if (token.GetType() == Lexer::TokenType::Type) {
                    ParseTypeDeclaration(lex, sema, std::forward<Module>(m), token.GetLocation());
                    return;
                }
                /*if (token.GetType() == Lexer::TokenType::Semicolon) {
                    return ParseModuleLevelDeclaration(lex, sema, std::forward<Module>(m));
                }*/
                if (token.GetType() == Lexer::TokenType::Operator) {
                    if (!lex)
                        throw ParserError(token.GetLocation(), Error::NoOperatorFound);
                    auto op = lex();
                    if (op.GetType() == Lexer::TokenType::OpenBracket) {
                        auto loc = op.GetLocation();
                        if (lex) {
                            auto tok = lex();
                            if (tok.GetType() == Lexer::TokenType::CloseBracket)
                                loc = loc + tok.GetLocation();
                            else
                                lex(tok);
                        }
                        // What to do here? There could well be a perfectly valid function body sitting here.
                        throw ParserError(loc, Error::GlobalFunctionCallOperator);
                    }
                    if (OverloadableOperators.find(op.GetType()) == OverloadableOperators.end())
                        throw BadToken(lex, op, Error::NonOverloadableOperator);
                    auto bracket = Check(lex, Error::ModuleScopeOperatorNoOpenBracket, Lexer::TokenType::OpenBracket);
                    ParseOverloadedOperator(lex, sema, op, std::forward<Module>(m), bracket.GetLocation());
                    return;
                }
                throw ParserError(token.GetLocation(), Error::UnrecognizedTokenModuleScope);
            } catch(ParserError& e) {
                if (lex) {
                    auto t = lex();
                    switch(t.GetType()) {
                    case Lexer::TokenType::Module:
                    case Lexer::TokenType::Operator:
                    case Lexer::TokenType::Type:
                    case Lexer::TokenType::Identifier:
                    case Lexer::TokenType::Using:
                        lex(t);
                        sema.Error(e.recover_where(), e.error());
                        return ParseModuleLevelDeclaration(lex, sema, m);
                    }
                    lex(t);
                }
                throw;
            }
        }
        
        template<typename Lex, typename Sema, typename Module> void ParseGlobalModuleContents(Lex&& lex, Sema&& sema, Module&& m) {
            typedef typename std::decay<decltype(*lex())>::type token_type;
            typedef typename std::decay<decltype(lex()->GetLocation())>::type location_type;
            
            struct AssumeLexer {
                typename std::decay<Lex>::type* lex;
                mutable std::deque<token_type> putbacks;
                mutable std::deque<location_type> locations;

                token_type operator()() {
                    if (!putbacks.empty()) {
                        auto val = std::move(putbacks.back());
                        putbacks.pop_back();
                        locations.push_back(val.GetLocation());
                        return val;
                    }
                    auto val = (*lex)();
                    if (!val) throw std::runtime_error("Encountered unexpected end of input.");
                    locations.push_back(val->GetLocation());
                    return std::move(*val);
                }
                void operator()(token_type arg) {
                    locations.pop_back();
                    putbacks.push_back(std::move(arg));
                }
                explicit operator bool() const {
                    if (!putbacks.empty()) return true;
                    auto val = (*lex)();
                    if (!val)
                        return false;
                    putbacks.push_back(std::move(*val));
                    return true;
                }
                Lexer::Range GetLastPosition() {
                    return locations.back();
                }
            };
            AssumeLexer lexer;
            lexer.lex = &lex;
            while (lexer) {
                try {
                    ParseModuleLevelDeclaration(lexer, sema, m);
                } catch(ParserError& e) {
                    sema.Error(e.where(), e.error());
                    continue;
                }
            }
        }

        template<typename Lex, typename Sema, typename Module> void ParseModuleContents(Lex&& lex, Sema&& sema, Module&& m) {
            auto first = lex.GetLastPosition();
            while(true) {
                if (!lex)
                    throw ParserError(lex.GetLastPosition(), Error::ModuleRequiresTerminatingCurly);
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                    sema.SetModuleEndLocation(m, first + t.GetLocation());
                    return;
                }
                lex(t);
                try {
                    ParseModuleLevelDeclaration(lex, sema, m);
                } catch(ParserError& e) {
                    if (!lex) throw;
                    auto t = lex();
                    if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                        sema.Error(e.recover_where(), e.error());
                        sema.SetModuleEndLocation(m, first + t.GetLocation());
                        return;
                    }
                    lex(t);
                    throw;
                }
            }
        }
    }
}
