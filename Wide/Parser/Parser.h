#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <vector>
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
                Lexer::TokenType::Plus,
                Lexer::TokenType::Increment
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

        template<typename Lex> struct AssumeLexer {
            typename std::decay<Lex>::type* lex;
            typedef typename std::decay<decltype(*std::declval<Lex>()())>::type token_type;
            typedef typename std::decay<decltype(std::declval<token_type>().GetLocation())>::type location_type;

            mutable std::vector<token_type> putbacks;
            mutable std::vector<location_type> locations;

            token_type operator()() {
                if(!putbacks.empty()) {
                    auto val = std::move(putbacks.back());
                    putbacks.pop_back();
                    locations.push_back(val.GetLocation());
                    return val;
                }
                auto val = (*lex)();
                if(!val) throw std::runtime_error("Encountered unexpected end of input.");
                locations.push_back(val->GetLocation());
                return std::move(*val);
            }
            void operator()(token_type arg) {
                locations.pop_back();
                putbacks.push_back(std::move(arg));
            }
            explicit operator bool() const {
                if(!putbacks.empty()) return true;
                auto val = (*lex)();
                if(!val)
                    return false;
                putbacks.push_back(std::move(*val));
                return true;
            }
            Lexer::Range GetLastPosition() {
                return locations.back();
            }
        };
        
        template<typename Lex, typename Sema> struct Parser {
            Lex& lex;
            Sema& sema;
            Parser(Lex& l, Sema& s)
                : lex(l), sema(s) {}
            typedef typename std::decay<Sema>::type::StatementType StatementType;
            typedef typename std::decay<Sema>::type::ExpressionType ExpressionType;
            typedef typename std::decay<decltype(std::declval<Lex>()())>::type TokenType;
            typedef typename std::decay<decltype(std::declval<TokenType>().GetLocation())>::type LocationType;
            typedef typename std::decay<decltype(std::declval<TokenType>().GetValue())>::type ValueType;
            typedef typename std::decay<decltype(std::declval<Sema>().GetGlobalModule())>::type ModuleType;

            ParserError BadToken(const TokenType& first, Error err) {
                // Put it back so that the recovery functions might match it successfully.
                lex(first);
                // Now throw it.
                return ParserError(first.GetLocation(), lex.GetLastPosition(), err);
            }

            TokenType Check(Error error, Lexer::TokenType tokty) {
                if(!lex)
                    throw ParserError(lex.GetLastPosition(), error);
                auto t = lex();
                if(t.GetType() != tokty)
                    throw BadToken(t, error);
                return t;
            }
            template<typename F> TokenType Check(Error error, F&& f) {
                if(!lex)
                    throw ParserError(lex.GetLastPosition(), error);
                auto t = lex();
                if(!f(t))
                    throw BadToken(t, error);
                return t;
            }
            template<typename Caps> void ParseLambdaCaptures(Caps&& caps, LocationType loc) {
                auto tok = lex();
                while(true) {
                    if(tok.GetType() != Lexer::TokenType::Identifier)
                        throw std::runtime_error("Expected identifier to introduce a lambda capture.");
                    auto varassign = lex();
                    if(varassign.GetType() != Lexer::TokenType::VarCreate)
                        throw std::runtime_error("Expected := after identifer when parsing lambda capture.");
                    auto init = ParseExpression();
                    sema.AddCaptureToGroup(caps, sema.CreateVariable(tok.GetValue(), init, tok.GetLocation() + sema.GetLocation(init)));
                    tok = lex();
                    if(tok.GetType() == Lexer::TokenType::CloseSquareBracket)
                        break;
                    else if(tok.GetType() == Lexer::TokenType::Comma)
                        tok = lex();
                    else
                        throw std::runtime_error("Expected , or ] after a lambda capture.");
                }
            }

            ExpressionType ParsePrimaryExpression() {
                // ident
                // string
                // { expressions }
                // ( expression )
                if(!lex)
                    throw ParserError(lex.GetLastPosition(), Error::ExpressionNoBeginning);
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::OpenCurlyBracket) {
                    auto group = sema.CreateExpressionGroup();
                    auto terminator = lex();
                    while (terminator.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                        lex(terminator);
                        sema.AddExpressionToGroup(group, ParseExpression());
                        terminator = Check(Error::TupleCommaOrClose, [](decltype(lex())& tok) {
                            if (tok.GetType() == Lexer::TokenType::Comma)
                                return true;
                            if (tok.GetType() == Lexer::TokenType::CloseCurlyBracket)
                                return true;
                            return false;
                        });
                        if (terminator.GetType() == Lexer::TokenType::Comma)
                            terminator = lex();
                    }
                    return sema.CreateTuple(std::move(group), t.GetLocation() + terminator.GetLocation());
                }
                if(t.GetType() == Lexer::TokenType::OpenBracket) {
                    auto tok = lex();
                    if(tok.GetType() == Lexer::TokenType::CloseBracket) {
                        Check(Error::LambdaNoIntroducer, Lexer::TokenType::Lambda);
                        auto expr = ParseExpression();
                        auto stmts = sema.CreateStatementGroup();
                        sema.AddStatementToGroup(stmts, sema.CreateReturn(expr, sema.GetLocation(expr)));
                        return sema.CreateLambda(sema.CreateFunctionArgumentGroup(), std::move(stmts), t.GetLocation() + sema.GetLocation(expr), false, sema.CreateCaptureGroup());
                    }

                    try {
                        lex(tok);
                        auto expr = ParseExpression();
                        Check(Error::ParenthesisedExpressionNoCloseBracket, Lexer::TokenType::CloseBracket);
                        return std::move(expr);
                    } catch(ParserError& e) {
                        if(!lex)
                            throw;
                        auto recov = lex();
                        if(recov.GetType() == Lexer::TokenType::CloseBracket) {
                            sema.Error(e.recover_where(), e.error());
                            return sema.CreateError(recov.GetLocation() + t.GetLocation());
                        }
                        lex(recov);
                        throw;
                    }
                }
                if(t.GetType() == Lexer::TokenType::Identifier) {
                    auto maybe_lambda = lex();
                    if(maybe_lambda.GetType() != Lexer::TokenType::Lambda) {
                        lex(maybe_lambda);
                        return sema.CreateIdentifier(t.GetValue(), t.GetLocation());
                    }
                    auto expr = ParseExpression();
                    auto stmts = sema.CreateStatementGroup();
                    sema.AddStatementToGroup(stmts, sema.CreateReturn(expr, sema.GetLocation(expr)));
                    auto args = sema.CreateFunctionArgumentGroup();
                    sema.AddArgumentToFunctionGroup(args, t.GetValue(), t.GetLocation());
                    return sema.CreateLambda(std::move(args), std::move(stmts), t.GetLocation() + sema.GetLocation(expr), false, sema.CreateCaptureGroup());
                }
                if(t.GetType() == Lexer::TokenType::String)
                    return sema.CreateString(t.GetValue(), t.GetLocation());
                if(t.GetType() == Lexer::TokenType::Integer)
                    return sema.CreateInteger(t.GetValue(), t.GetLocation());
                if(t.GetType() == Lexer::TokenType::This)
                    return sema.CreateThis(t.GetLocation());
                if(t.GetType() == Lexer::TokenType::Function) {
                    auto args = sema.CreateFunctionArgumentGroup();
                    try {
                        args = ParseFunctionDefinitionArguments(Check(Error::LambdaNoOpenBracket, Lexer::TokenType::OpenBracket).GetLocation());
                    } catch(ParserError& e) {
                        if(!lex) throw;
                        auto tok = lex();
                        if(tok.GetType() != Lexer::TokenType::OpenSquareBracket && tok.GetType() != Lexer::TokenType::OpenCurlyBracket)
                            throw;
                        lex(tok);
                        sema.Error(e.recover_where(), e.error());
                    }

                    auto pos = t.GetLocation();
                    auto grp = sema.CreateStatementGroup();
                    auto tok = lex();
                    auto caps = sema.CreateCaptureGroup();
                    bool defaultref = false;
                    if(tok.GetType() == Lexer::TokenType::OpenSquareBracket) {
                        auto opensquare = tok.GetLocation();
                        tok = lex();
                        if(tok.GetType() == Lexer::TokenType::And) {
                            defaultref = true;
                            tok = lex();
                            if(tok.GetType() == Lexer::TokenType::Comma) {
                                ParseLambdaCaptures(caps, opensquare);
                                tok = lex();
                            } else if(tok.GetType() == Lexer::TokenType::CloseSquareBracket) {
                                tok = lex();
                            } else
                                throw std::runtime_error("Expected ] or , after [& in a lambda capture.");
                        } else {
                            lex(tok);
                            ParseLambdaCaptures(caps, opensquare);
                            tok = lex();
                        }
                    }
                    if(tok.GetType() != Lexer::TokenType::OpenCurlyBracket)
                        throw BadToken(tok, Error::LambdaNoOpenCurly);
                    auto opencurly = tok.GetLocation();
                    tok = lex();
                    while(tok.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                        lex(tok);
                        sema.AddStatementToGroup(grp, ParseStatement());
                        tok = lex();
                    }
                    return sema.CreateLambda(std::move(args), std::move(grp), pos + tok.GetLocation(), defaultref, std::move(caps));
                }
                if(t.GetType() == Lexer::TokenType::Type) {
                    auto bases = ParseTypeBases();
                    auto ty = sema.CreateType(bases, Check(Error::TypeExpressionNoCurly, Lexer::TokenType::OpenCurlyBracket).GetLocation(), Lexer::Access::Public);
                    ParseTypeBody(ty);
                    return ty;
                }

                throw BadToken(t, Error::ExpressionNoBeginning);
            }

            template<typename Group> void ParseFunctionArguments(Group&& group) {
                try {
                    sema.AddExpressionToGroup(group, ParseExpression());
                } catch(ParserError& e) {
                    if(!lex) throw;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::CloseBracket) {
                        sema.Error(e.recover_where(), e.error());
                        return;
                    }
                    if(t.GetType() == Lexer::TokenType::Comma) {
                        sema.Error(e.recover_where(), e.error());
                        return ParseFunctionArguments(group);
                    }
                    lex(t);
                    throw;
                }
                if(!lex)
                    throw ParserError(lex.GetLastPosition(), Error::FunctionArgumentNoBracketOrComma);
                auto t = lex();
                if(t.GetType() == Lexer::TokenType::Comma)
                    return ParseFunctionArguments(std::forward<Group>(group));
                if(t.GetType() == Lexer::TokenType::CloseBracket)
                    return;
                lex(t);
                throw ParserError(lex.GetLastPosition(), Error::FunctionArgumentNoBracketOrComma);
            }

            ExpressionType ParsePostfixExpression() {
                auto expr = ParsePrimaryExpression();
                while(true) {
                    if(!lex)
                        return expr;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::Dot) {
                        Check(Error::MemberAccessNoIdentifierOrDestructor, [&](decltype(lex())& tok) {
                            if(tok.GetType() == Lexer::TokenType::Identifier) {
                                expr = sema.CreateMemberAccess(tok.GetValue(), std::move(expr), sema.GetLocation(expr) + tok.GetLocation());
                                return true;
                            }
                            if(tok.GetType() != Lexer::TokenType::Negate)
                                return false;
                            Check(Error::MemberAccessNoTypeAfterNegate, Lexer::TokenType::Type);
                            expr = sema.CreateMemberAccess("~type", std::move(expr), sema.GetLocation(expr) + tok.GetLocation());
                            return true;
                        });
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::PointerAccess) {
                        Check(Error::PointerAccessNoIdentifierOrDestructor, [&](decltype(lex())& tok) {
                            if(tok.GetType() == Lexer::TokenType::Identifier) {
                                expr = sema.CreatePointerAccess(tok.GetValue(), std::move(expr), sema.GetLocation(expr) + tok.GetLocation());
                                return true;
                            }
                            if(tok.GetType() != Lexer::TokenType::Negate)
                                return false;
                            Check(Error::PointerAccessNoTypeAfterNegate, Lexer::TokenType::Type);
                            expr = sema.CreatePointerAccess("~type", std::move(expr), sema.GetLocation(expr) + tok.GetLocation());
                            return true;
                        });
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Increment) {
                        expr = sema.CreatePostfixIncrement(std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Decrement) {
                        expr = sema.CreatePostfixDecrement(std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::OpenBracket) {
                        auto argexprs = sema.CreateExpressionGroup();
                        t = lex();
                        if(t.GetType() != Lexer::TokenType::CloseBracket) {
                            lex(t);
                            ParseFunctionArguments(argexprs);
                        }
                        expr = sema.CreateFunctionCall(std::move(expr), std::move(argexprs), sema.GetLocation(expr) + lex.GetLastPosition());
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Exclaim) {
                        Check(Error::NoOpenBracketAfterExclaim, Lexer::TokenType::OpenBracket);
                        auto argexprs = sema.CreateExpressionGroup();
                        t = lex();
                        if(t.GetType() != Lexer::TokenType::CloseBracket) {
                            lex(t);
                            ParseFunctionArguments(argexprs);
                        }
                        expr = sema.CreateMetaFunctionCall(std::move(expr), std::move(argexprs), sema.GetLocation(expr) + t.GetLocation());
                        continue;
                    }
                    // Did not recognize either of these, so put it back and return the final result.
                    lex(t);
                    return std::move(expr);
                }
            }
            ExpressionType ParseUnaryExpression() {
                // Even if this token is not a unary operator, primary requires at least one token.
                // So just fail right away if there are no more tokens here.
                if(!lex)
                    throw ParserError(lex.GetLastPosition(), Error::ExpressionNoBeginning);
                auto tok = lex();
                if(tok.GetType() == Lexer::TokenType::Dereference) {
                    auto expr = ParseUnaryExpression();
                    return sema.CreateDereference(expr, tok.GetLocation() + sema.GetLocation(expr));
                }
                if(tok.GetType() == Lexer::TokenType::Negate) {
                    auto expr = ParseUnaryExpression();
                    return sema.CreateNegate(expr, tok.GetLocation() + sema.GetLocation(expr));
                }
                if(tok.GetType() == Lexer::TokenType::Increment) {
                    auto expr = ParseUnaryExpression();
                    return sema.CreatePrefixIncrement(expr, tok.GetLocation() + sema.GetLocation(expr));
                }
                if(tok.GetType() == Lexer::TokenType::Decrement) {
                    auto expr = ParseUnaryExpression();
                    return sema.CreatePrefixDecrement(expr, tok.GetLocation() + sema.GetLocation(expr));
                }
                if(tok.GetType() == Lexer::TokenType::And) {
                    auto expr = ParseUnaryExpression();
                    return sema.CreateAddressOf(expr, tok.GetLocation() + sema.GetLocation(expr));
                }
                lex(tok);
                return ParsePostfixExpression();
            }
            ExpressionType ParseMultiplicativeExpression(ExpressionType e) {
                auto lhs = std::move(e);
                while(true) {
                    if(!lex)
                        return lhs;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::Dereference) {
                        auto rhs = ParseUnaryExpression();
                        lhs = sema.CreateMultiply(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Divide) {
                        auto rhs = ParseUnaryExpression();
                        lhs = sema.CreateDivision(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Modulo) {
                        auto rhs = ParseUnaryExpression();
                        lhs = sema.CreateModulus(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    lex(t);
                    return std::move(lhs);
                }
            }
            ExpressionType ParseMultiplicativeExpression() {
                return ParseMultiplicativeExpression(ParseUnaryExpression());
            }
            ExpressionType ParseAdditiveExpression(ExpressionType e) {
                auto lhs = ParseMultiplicativeExpression(std::move(e));
                while(true) {
                    if(!lex)
                        return lhs;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::Plus) {
                        auto rhs = ParseMultiplicativeExpression();
                        lhs = sema.CreateAddition(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Minus) {
                        auto rhs = ParseMultiplicativeExpression();
                        lhs = sema.CreateSubtraction(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    lex(t);
                    return std::move(lhs);
                }
            }
            ExpressionType ParseAdditiveExpression() {
                return ParseAdditiveExpression(ParseUnaryExpression());
            }
            ExpressionType ParseShiftExpression(ExpressionType e) {
                auto lhs = ParseAdditiveExpression(std::move(e));
                while(true) {
                    if(!lex)
                        return lhs;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::LeftShift) {
                        auto rhs = ParseAdditiveExpression();
                        lhs = sema.CreateLeftShift(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::RightShift) {
                        auto rhs = ParseAdditiveExpression();
                        lhs = sema.CreateRightShift(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    lex(t);
                    return std::move(lhs);
                }
            }
            ExpressionType ParseShiftExpression() {
                return ParseShiftExpression(ParseUnaryExpression());
            }
            ExpressionType ParseRelationalExpression(ExpressionType e) {
                auto lhs = ParseShiftExpression(std::move(e));
                while(true) {
                    if(!lex)
                        return lhs;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::LT) {
                        auto rhs = ParseShiftExpression();
                        lhs = sema.CreateLT(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::LTE) {
                        auto rhs = ParseShiftExpression();
                        lhs = sema.CreateLTE(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::GT) {
                        auto rhs = ParseShiftExpression();
                        lhs = sema.CreateGT(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::GTE) {
                        auto rhs = ParseShiftExpression();
                        lhs = sema.CreateGTE(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    lex(t);
                    return std::move(lhs);
                }
            }
            ExpressionType ParseRelationalExpression() {
                return ParseRelationalExpression(ParseUnaryExpression());
            }
            ExpressionType ParseEqualityExpression(ExpressionType e) {
                auto lhs = ParseRelationalExpression(std::move(e));
                while(true) {
                    if(!lex)
                        return lhs;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::EqCmp) {
                        auto rhs = ParseRelationalExpression();
                        lhs = sema.CreateEqCmp(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::NotEqCmp) {
                        auto rhs = ParseRelationalExpression();
                        lhs = sema.CreateNotEqCmp(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    lex(t);
                    return std::move(lhs);
                }
            }
            ExpressionType ParseEqualityExpression() {
                return ParseEqualityExpression(ParseUnaryExpression());
            }
            ExpressionType ParseAndExpression(ExpressionType fix) {
                auto lhs = ParseEqualityExpression(std::move(fix));
                while(true) {
                    if(!lex)
                        return lhs;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::And) {
                        auto rhs = ParseEqualityExpression();
                        lhs = sema.CreateAnd(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    lex(t);
                    return std::move(lhs);
                }
            }
            ExpressionType ParseAndExpression() {
                return ParseAndExpression(ParseUnaryExpression());
            }
            ExpressionType ParseXorExpression(ExpressionType fix) {
                auto lhs = ParseAndExpression(std::move(fix));
                while(true) {
                    if(!lex)
                        return lhs;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::Xor) {
                        auto rhs = ParseAndExpression();
                        lhs = sema.CreateXor(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    lex(t);
                    return std::move(lhs);
                }
            }
            ExpressionType ParseXorExpression() {
                return ParseXorExpression(ParseUnaryExpression());
            }
            ExpressionType ParseOrExpression(ExpressionType fix) {
                auto lhs = ParseXorExpression(std::move(fix));
                while(true) {
                    if(!lex)
                        return lhs;
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::Or) {
                        auto rhs = ParseXorExpression();
                        lhs = sema.CreateOr(std::move(lhs), std::move(rhs));
                        continue;
                    }
                    lex(t);
                    return std::move(lhs);
                }
            }
            ExpressionType ParseAssignmentExpression() {
                auto lhs = ParseUnaryExpression();
                // Somebody's gonna be disappointed because an expression is not a valid end of program.
                // But we don't know who or what they're looking for, so just wait and let them fail.
                // Same strategy for all expression types.
                if(!lex)
                    return lhs;
                auto t = lex();
                if(AssignmentOperators.find(t.GetType()) != AssignmentOperators.end()) {
                    return sema.CreateAssignment(std::move(lhs), ParseAssignmentExpression(), t.GetType());
                }
                lex(t);
                return ParseOrExpression(std::move(lhs));
            }

            ExpressionType ParseExpression() {
                return ParseAssignmentExpression();
            }
            
            auto ParseVariableStatement(TokenType t) -> decltype(sema.CreateVariable(t.GetValue(), std::declval<ExpressionType>(), t.GetLocation())) {
                // Expect to have already seen :=
                auto expr = ParseExpression();
                auto semi = Check(Error::VariableStatementNoSemicolon, Lexer::TokenType::Semicolon);
                return sema.CreateVariable(t.GetValue(), std::move(expr), t.GetLocation() + semi.GetLocation());
            }

            StatementType ParseStatement() {
                // Check first token- if it is return, then parse return statement.
                auto t = lex();
                if(t.GetType() == Lexer::TokenType::Return) {
                    auto next = lex(); // Check next token for ;
                    if(next.GetType() == Lexer::TokenType::Semicolon) {
                        return sema.CreateReturn(t.GetLocation() + next.GetLocation());
                    }
                    // If it wasn't ; then expect expression.
                    lex(next);
                    auto expr = ParseExpression();
                    next = Check(Error::ReturnNoSemicolon, Lexer::TokenType::Semicolon);
                    return sema.CreateReturn(std::move(expr), t.GetLocation() + next.GetLocation());
                }
                // If "if", then we're good.
                if(t.GetType() == Lexer::TokenType::If) {
                    Check(Error::IfNoOpenBracket, Lexer::TokenType::OpenBracket);
                    // Check for variable conditions
                    auto ident = lex();
                    if(ident.GetType() == Lexer::TokenType::Identifier) {
                        auto var = lex();
                        if(var.GetType() == Lexer::TokenType::VarCreate) {
                            auto expr = ParseExpression();
                            auto variable = sema.CreateVariable(ident.GetValue(), std::move(expr), t.GetLocation() + sema.GetLocation(expr));
                            Check(Error::IfNoCloseBracket, Lexer::TokenType::CloseBracket);
                            auto body = ParseStatement();
                            auto next = lex();
                            if(next.GetType() == Lexer::TokenType::Else) {
                                auto else_br = ParseStatement();
                                return sema.CreateIf(variable, body, else_br, t.GetLocation() + sema.GetLocation(body));
                            }
                            lex(next);
                            return sema.CreateIf(variable, body, t.GetLocation() + sema.GetLocation(body));
                        }
                        lex(var);
                    }
                    lex(ident);

                    auto cond = ParseExpression();
                    Check(Error::IfNoCloseBracket, Lexer::TokenType::CloseBracket);
                    auto true_br = ParseStatement();
                    auto next = lex();
                    if(next.GetType() == Lexer::TokenType::Else) {
                        auto else_br = ParseStatement();
                        return sema.CreateIf(cond, true_br, else_br, t.GetLocation() + sema.GetLocation(else_br));
                    }
                    lex(next);
                    return sema.CreateIf(cond, true_br, t.GetLocation() + sema.GetLocation(true_br));
                }
                // If { then compound.
                if(t.GetType() == Lexer::TokenType::OpenCurlyBracket) {
                    auto pos = t.GetLocation();
                    auto grp = sema.CreateStatementGroup();
                    auto t = lex();
                    while(t.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                        lex(t);
                        sema.AddStatementToGroup(grp, ParseStatement());
                        t = lex();
                    }
                    return sema.CreateCompoundStatement(std::move(grp), pos + t.GetLocation());
                }
                if(t.GetType() == Lexer::TokenType::While) {
                    Check(Error::WhileNoOpenBracket, Lexer::TokenType::OpenBracket);
                    // Check for variable conditions.
                    auto ident = lex();
                    if(ident.GetType() == Lexer::TokenType::Identifier) {
                        auto var = lex();
                        if(var.GetType() == Lexer::TokenType::VarCreate) {
                            auto expr = ParseExpression();
                            auto variable = sema.CreateVariable(ident.GetValue(), std::move(expr), t.GetLocation() + sema.GetLocation(expr));
                            Check(Error::WhileNoCloseBracket, Lexer::TokenType::CloseBracket);
                            auto body = ParseStatement();
                            return sema.CreateWhile(variable, body, t.GetLocation() + sema.GetLocation(body));
                        }
                        lex(var);
                    }
                    lex(ident);
                    auto cond = ParseExpression();
                    Check(Error::WhileNoCloseBracket, Lexer::TokenType::CloseBracket);
                    auto body = ParseStatement();
                    return sema.CreateWhile(cond, body, t.GetLocation() + sema.GetLocation(body));
                }
                // If identifier, check the next for := or ,
                if(t.GetType() == Lexer::TokenType::Identifier) {
                    auto group = sema.CreateVariableNameGroup();
                    sema.AddNameToGroup(group, t.GetValue());
                    auto next = lex();
                    if (next.GetType() != Lexer::TokenType::VarCreate && next.GetType() != Lexer::TokenType::Comma)
                        // If it's not := or , then we're probably just looking at expression statement so put it back.
                        lex(next);
                    else {
                        while (next.GetType() == Lexer::TokenType::Comma) {
                            auto ident = Check(Error::VariableListNoIdentifier, Lexer::TokenType::Identifier);
                            sema.AddNameToGroup(group, ident.GetValue());
                            next = lex();
                        }
                        lex(next);
                        Check(Error::VariableListNoInitializer, Lexer::TokenType::VarCreate);
                        auto init = ParseExpression();
                        auto semi = Check(Error::VariableStatementNoSemicolon, Lexer::TokenType::Semicolon);
                        return sema.CreateVariable(std::move(group), init, t.GetLocation() + semi.GetLocation());
                    }
                }
                // If continue or break, we're good.
                if(t.GetType() == Lexer::TokenType::Break) {
                    auto semi = Check(Error::BreakNoSemicolon, Lexer::TokenType::Semicolon);
                    return sema.CreateBreak(t.GetLocation() + semi.GetLocation());
                }
                if(t.GetType() == Lexer::TokenType::Continue) {
                    auto semi = Check(Error::ContinueNoSemicolon, Lexer::TokenType::Semicolon);
                    return sema.CreateContinue(t.GetLocation() + semi.GetLocation());
                }
                lex(t);
                // Else, expression statement.
                auto expr = ParseExpression();
                Check(Error::ExpressionStatementNoSemicolon, Lexer::TokenType::Semicolon);
                return std::move(expr);
            }
            auto ParseFunctionDefinitionArguments(LocationType loc) -> decltype(sema.CreateFunctionArgumentGroup()) {
                auto ret = sema.CreateFunctionArgumentGroup();
                auto t = lex();
                if(t.GetType() == Lexer::TokenType::CloseBracket)
                    return ret;
                lex(t);
                // At least one argument.
                while(true) {
                    auto t = lex();
                    auto t2 = lex();
                    if(t.GetType() == Lexer::TokenType::Identifier && (t2.GetType() == Lexer::TokenType::Comma || t2.GetType() == Lexer::TokenType::CloseBracket)) {
                        // Type-deduced argument.
                        sema.AddArgumentToFunctionGroup(ret, t.GetValue(), t.GetLocation());
                        if(t2.GetType() == Lexer::TokenType::CloseBracket) {
                            break;
                        }
                    } else {
                        // Expression-specified argument.
                        lex(t2);
                        lex(t);
                        try {
                            auto ty = ParseExpression();
                            auto ident = Check(Error::FunctionArgumentNoIdentifierOrThis, [](decltype(lex())& tok) {
                                if(tok.GetType() == Lexer::TokenType::Identifier || tok.GetType() == Lexer::TokenType::This)
                                    return true;
                                return false;
                            });
                            sema.AddArgumentToFunctionGroup(ret, ident.GetValue(), ident.GetLocation() + sema.GetLocation(ty), ty);
                            auto brace = Check(Error::FunctionArgumentNoBracketOrComma, [&](decltype(lex())& tok) {
                                if(tok.GetType() == Lexer::TokenType::CloseBracket || tok.GetType() == Lexer::TokenType::Comma)
                                    return true;
                                return false;
                            });
                            if(brace.GetType() == Lexer::TokenType::CloseBracket) {
                                break;
                            }
                        } catch(ParserError& e) {
                            if(!lex) throw;
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
            template<typename Group> LocationType ParseProlog(Group&& prolog) {
                auto t = lex();
                auto loc = t.GetLocation();
                if(t.GetType() == Lexer::TokenType::Prolog) {
                    t = Check(Error::PrologNoOpenCurly, Lexer::TokenType::OpenCurlyBracket);
                    while(true) {
                        sema.AddStatementToGroup(prolog, ParseStatement());
                        t = lex();
                        if(t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                            break;
                        }
                        lex(t);
                    }
                } else
                    lex(t);
                return loc;
            }
            template<typename Scope> void ParseOverloadedOperator(const TokenType& first, Scope& m, LocationType open, Lexer::Access a) {
                auto group = ParseFunctionDefinitionArguments(open);
                auto prolog = sema.CreateStatementGroup();
                auto loc = ParseProlog(prolog);
                auto t = Check(Error::OperatorNoCurlyToIntroduceBody, Lexer::TokenType::OpenCurlyBracket);
                auto pos = t.GetLocation();
                auto stmts = sema.CreateStatementGroup();
                t = lex();
                if(t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                    sema.CreateOverloadedOperator(first.GetType(), std::move(stmts), std::move(prolog), loc + t.GetLocation(), m, std::move(group), a);
                    return;
                }
                lex(t);
                while(true) {
                    try {
                        sema.AddStatementToGroup(stmts, ParseStatement());
                    } catch(ParserError& e) {
                        if(!lex) throw;
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
                    if(t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                        pos = pos + t.GetLocation();
                        break;
                    }
                    lex(t);
                }
                sema.CreateOverloadedOperator(first.GetType(), std::move(stmts), std::move(prolog), loc + t.GetLocation(), m, std::move(group), a);
            }
            template<typename Scope> void ParseFunction(const TokenType& first, Scope& m, LocationType open, Lexer::Access a)
            {
                // Identifier ( consumed
                // The first two must be () but we can find either prolog then body, or body.
                // Expect ParseFunctionArguments to have consumed the ).
                auto group = ParseFunctionDefinitionArguments(open);
                auto close = lex.GetLastPosition();
                auto prolog = sema.CreateStatementGroup();
                auto loc = ParseProlog(prolog);
                auto initializers = sema.CreateInitializerGroup();
                auto t = lex();
                if(first.GetType() == Lexer::TokenType::Type) {
                    // Constructor- check for initializer list
                    while(t.GetType() == Lexer::TokenType::Colon) {
                        auto name = Check(Error::ConstructorNoIdentifierAfterColon, Lexer::TokenType::Identifier);
                        auto open = Check(Error::ConstructorNoBracketAfterMemberName, Lexer::TokenType::OpenBracket);

                        if(!lex)
                            throw ParserError(lex.GetLastPosition(), Error::ConstructorNoBracketOrExpressionAfterMemberName);
                        auto next = lex();
                        if(next.GetType() == Lexer::TokenType::CloseBracket) {
                            // Empty initializer- e.g. : x()
                            sema.AddInitializerToGroup(initializers, sema.CreateVariable(name.GetValue(), t.GetLocation() + next.GetLocation()));
                            t = lex();
                            continue;
                        }
                        lex(next);
                        try {
                            auto expr = ParseExpression();
                            next = Check(Error::ConstructorNoBracketClosingInitializer, Lexer::TokenType::CloseBracket);
                            sema.AddInitializerToGroup(initializers, sema.CreateVariable(name.GetValue(), std::move(expr), t.GetLocation() + next.GetLocation()));
                        } catch(ParserError& e) {
                            if(!lex) throw;
                            auto t = lex();
                            if(t.GetType() == Lexer::TokenType::CloseBracket) {
                                sema.Error(e.recover_where(), e.error());
                                continue;
                            }
                            lex(t);
                            throw;
                        }
                        t = lex();
                    }
                }
                if(t.GetType() != Lexer::TokenType::OpenCurlyBracket)
                    throw BadToken(t, Error::FunctionNoCurlyToIntroduceBody);
                auto stmts = sema.CreateStatementGroup();
                auto pos = first.GetLocation();
                if(!lex)
                    throw ParserError(lex.GetLastPosition(), Error::FunctionNoClosingCurly);
                t = lex();
                if(t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                    sema.CreateFunction(first.GetValue(), std::move(stmts), std::move(prolog), first.GetLocation() + close, loc + t.GetLocation(), m, std::move(group), std::move(initializers), a);
                    return;
                }
                lex(t);
                while(true) {
                    try {
                        sema.AddStatementToGroup(stmts, ParseStatement());
                    } catch(ParserError& e) {
                        if(!lex) throw;
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
                    if(!lex)
                        throw ParserError(lex.GetLastPosition(), Error::FunctionNoClosingCurly);
                    t = lex();
                    if(t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                        pos = pos + t.GetLocation();
                        break;
                    }
                    lex(t);
                }
                sema.CreateFunction(first.GetValue(), std::move(stmts), std::move(prolog), first.GetLocation() + close, loc + t.GetLocation(), m, std::move(group), std::move(initializers), a);
            }
            void ParseUsingDefinition(ModuleType& m, const TokenType& first, Lexer::Access a) {
                // We got the "using". Now we have either identifier :=, identifier;, or identifer. We only support identifier := expr right now.
                auto useloc = lex.GetLastPosition();
                auto t = Check(Error::ModuleScopeUsingNoIdentifier, Lexer::TokenType::Identifier);
                auto var = Check(Error::ModuleScopeUsingNoVarCreate, [&](decltype(lex())& curr) {
                    if(curr.GetType() == Lexer::TokenType::Assignment) {
                        sema.Warning(curr.GetLocation(), Warning::AssignmentInUsing);
                        return true;
                    }
                    if(curr.GetType() == Lexer::TokenType::VarCreate)
                        return true;
                    return false;
                });
                auto expr = ParseExpression();
                sema.CreateUsing(t.GetValue(), useloc + sema.GetLocation(expr), std::move(expr), m, a);
                Check(Error::ModuleScopeUsingNoSemicolon, Lexer::TokenType::Semicolon);
            }
            ModuleType ParseQualifiedName(TokenType& first, ModuleType m, Lexer::Access a) {
                // We have already seen identifier . to enter this method.
                m = sema.CreateModule(first.GetValue(), m, first.GetLocation(), a);
                while(true) {
                    auto ident = Check(Error::QualifiedNameNoIdentifier, Lexer::TokenType::Identifier);
                    // If there's a dot keep going- else terminate.
                    auto dot = lex();
                    if(dot.GetType() != Lexer::TokenType::Dot) {
                        lex(dot);
                        first = ident;
                        return m;
                    }
                    m = sema.CreateModule(ident.GetValue(), m, ident.GetLocation(), a);
                }
            }
            void ParseModuleDeclaration(ModuleType m, const TokenType& tok, Lexer::Access a) {
                // Already got module
                auto loc = lex.GetLastPosition();
                auto ident = Check(Error::ModuleNoIdentifier, Lexer::TokenType::Identifier);
                auto maybedot = lex();
                if(maybedot.GetType() == Lexer::TokenType::Dot)
                    m = ParseQualifiedName(ident, m, a);
                else
                    lex(maybedot);
                auto curly = Check(Error::ModuleNoOpeningBrace, Lexer::TokenType::OpenCurlyBracket);
                auto mod = sema.CreateModule(ident.GetValue(), m, loc + curly.GetLocation(), a);
                ParseModuleContents(mod);
            }
            template<typename Ty> void ParseTypeBody(Ty&& ty) {
                auto loc = lex.GetLastPosition();
                auto t = lex();
                auto access = Lexer::Access::Public;
                while(t.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                    if (t.GetType() == Lexer::TokenType::Public) {
                        Check(Error::AccessSpecifierNoColon, Lexer::TokenType::Colon);
                        access = Lexer::Access::Public;
                        t = lex();
                        continue;
                    }
                    if (t.GetType() == Lexer::TokenType::Private) {
                        Check(Error::AccessSpecifierNoColon, Lexer::TokenType::Colon);
                        access = Lexer::Access::Private;
                        t = lex();
                        continue;
                    }
                    if (t.GetType() == Lexer::TokenType::Protected) {
                        Check(Error::AccessSpecifierNoColon, Lexer::TokenType::Colon);
                        access = Lexer::Access::Protected;
                        t = lex();
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Identifier) {
                        // Must be either := for variable or ( for function. Don't support functions yet.
                        Check(Error::TypeScopeExpectedMemberAfterIdentifier, [&](decltype(lex())& tok) {
                            if(tok.GetType() == Lexer::TokenType::VarCreate) {
                                auto var = ParseVariableStatement(t);
                                sema.AddTypeField(ty, var, access);
                                t = lex();
                                return true;
                            }
                            if(tok.GetType() == Lexer::TokenType::OpenBracket) {
                                ParseFunction(t, ty, tok.GetLocation(), access);
                                t = lex();
                                return true;
                            }
                            return false;
                        });
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Operator) {
                        auto op = Check(Error::NonOverloadableOperator, [&](decltype(lex())& tok) {
                            if(tok.GetType() == Lexer::TokenType::OpenBracket) {
                                Check(Error::OperatorNoCloseBracketAfterOpen, Lexer::TokenType::CloseBracket);
                                return true;
                            }
                            if(OverloadableOperators.find(tok.GetType()) != OverloadableOperators.end())
                                return true;
                            return false;
                        });
                        auto bracket = Check(Error::TypeScopeOperatorNoOpenBracket, Lexer::TokenType::OpenBracket);
                        ParseOverloadedOperator(op, ty, bracket.GetLocation(), access);
                        t = lex();
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Type) {
                        auto open = Check(Error::ConstructorNoOpenBracket, Lexer::TokenType::OpenBracket);
                        ParseFunction(t, ty, open.GetLocation(), access);
                        t = lex();
                        continue;
                    }
                    if(t.GetType() == Lexer::TokenType::Negate) {
                        auto next = Check(Error::DestructorNoType, Lexer::TokenType::Type);
                        auto open = Check(Error::DestructorNoOpenBracket, Lexer::TokenType::OpenBracket);
                        ParseFunction(t, ty, open.GetLocation(), access);
                        t = lex();
                        continue;
                    }
                    throw BadToken(t, Error::ModuleScopeTypeExpectedMemberOrTerminate);
                }
                sema.SetTypeEndLocation(loc + t.GetLocation(), ty);
            }
            auto ParseTypeBases() -> decltype(sema.CreateExpressionGroup()) {
                auto colon = lex();
                auto group = sema.CreateExpressionGroup();
                while (colon.GetType() == Lexer::TokenType::Colon) {
                    sema.AddExpressionToGroup(group, ParseExpression());
                    colon = lex();
                }
                lex(colon);
                return group;
            }
            auto ParseTypeDeclaration(ModuleType m, LocationType loc, Lexer::Access a, TokenType& ident) -> decltype(sema.CreateType(ParseTypeBases(), loc, a)) {
                auto bases = ParseTypeBases();
                auto t = Check(Error::ModuleScopeTypeNoCurlyBrace, [&](decltype(lex())& curr) -> bool {
                    if(curr.GetType() == Lexer::TokenType::OpenCurlyBracket)
                        return true;
                    if(curr.GetType() == Lexer::TokenType::OpenBracket) {
                        // Insert an extra lex here so that future code can recover the function.
                        lex(curr);
                        lex(ident);
                        throw ParserError(curr.GetLocation(), lex.GetLastPosition(), Error::ModuleScopeTypeIdentifierButFunction);
                    }
                    return false;
                });
                auto ty = sema.CreateType(bases, loc + t.GetLocation(), a);
                ParseTypeBody(ty);
                return ty;
            }
            Lexer::Access ParseModuleLevelDeclaration(ModuleType m, Lexer::Access a) {
                try {
                    auto token = lex();
                    if(token.GetType() == Lexer::TokenType::Identifier) {
                        auto maybedot = lex();
                        if(maybedot.GetType() == Lexer::TokenType::Dot)
                            m = ParseQualifiedName(token, m, a);
                        else
                            lex(maybedot);
                        auto t = Check(Error::ModuleScopeFunctionNoOpenBracket, Lexer::TokenType::OpenBracket);
                        ParseFunction(token, m, t.GetLocation(), a);
                        return a;
                    }
                    if (token.GetType() == Lexer::TokenType::Template){
                        auto args = ParseFunctionDefinitionArguments(Check(Error::TemplateNoArguments, Lexer::TokenType::OpenBracket).GetLocation());
                        auto ident = Check(Error::ModuleScopeTypeNoIdentifier, Lexer::TokenType::Identifier);
                        auto ty = ParseTypeDeclaration(m, token.GetLocation(), a, ident);
                        sema.AddTemplateTypeToModule(m, ident.GetValue(), args, ty);
                        return a;
                    }
                    if(token.GetType() == Lexer::TokenType::Using) {
                        ParseUsingDefinition(m, token, a);
                        return a;
                    }
                    if(token.GetType() == Lexer::TokenType::Module) {
                        ParseModuleDeclaration(m, token, a);
                        return a;
                    }
                    if (token.GetType() == Lexer::TokenType::Type) {
                        auto ident = Check(Error::ModuleScopeTypeNoIdentifier, Lexer::TokenType::Identifier);
                        auto ty = ParseTypeDeclaration(m, token.GetLocation(), a, ident);
                        sema.AddTypeToModule(m, ident.GetValue(), ty);
                        return a;
                    }
                    if (token.GetType() == Lexer::TokenType::Private) {
                        Check(Error::AccessSpecifierNoColon, Lexer::TokenType::Colon);
                        return Lexer::Access::Private;
                    }
                    if (token.GetType() == Lexer::TokenType::Public) {
                        Check(Error::AccessSpecifierNoColon, Lexer::TokenType::Colon);
                        return Lexer::Access::Public;
                    }
                    if (token.GetType() == Lexer::TokenType::Protected) {
                        throw ParserError(token.GetLocation(), Error::ProtectedModuleScope);
                    }
                    /*if (token.GetType() == Lexer::TokenType::Semicolon) {
                    return ParseModuleLevelDeclaration(lex, sema, std::forward<Module>(m));
                    }*/
                    if(token.GetType() == Lexer::TokenType::Operator) {
                        if(!lex)
                            throw ParserError(token.GetLocation(), Error::NoOperatorFound);
                        auto op = lex();
                        if(op.GetType() == Lexer::TokenType::OpenBracket) {
                            auto loc = op.GetLocation();
                            if(lex) {
                                auto tok = lex();
                                if(tok.GetType() == Lexer::TokenType::CloseBracket)
                                    loc = loc + tok.GetLocation();
                                else
                                    lex(tok);
                            }
                            // What to do here? There could well be a perfectly valid function body sitting here.
                            throw ParserError(loc, Error::GlobalFunctionCallOperator);
                        }
                        if(OverloadableOperators.find(op.GetType()) == OverloadableOperators.end())
                            throw BadToken(op, Error::NonOverloadableOperator);
                        auto bracket = Check(Error::ModuleScopeOperatorNoOpenBracket, Lexer::TokenType::OpenBracket);
                        ParseOverloadedOperator(op, m, bracket.GetLocation(), a);
                        return a;
                    }
                    throw ParserError(token.GetLocation(), Error::UnrecognizedTokenModuleScope);
                } catch(ParserError& e) {
                    if(lex) {
                        auto t = lex();
                        switch(t.GetType()) {
                        case Lexer::TokenType::Module:
                        case Lexer::TokenType::Operator:
                        case Lexer::TokenType::Type:
                        case Lexer::TokenType::Identifier:
                        case Lexer::TokenType::Using:
                            lex(t);
                            sema.Error(e.recover_where(), e.error());
                            return ParseModuleLevelDeclaration(m, a);
                        }
                        lex(t);
                    }
                    throw;
                }
            }
            void ParseGlobalModuleContents(ModuleType m) {
                while(lex) {
                    try {
                        ParseModuleLevelDeclaration(m, Lexer::Access::Public);
                    } catch(ParserError& e) {
                        sema.Error(e.where(), e.error());
                        continue;
                    }
                }
            }
            void ParseModuleContents(ModuleType m) {
                auto first = lex.GetLastPosition();
                auto access = Lexer::Access::Public;
                while(true) {
                    if(!lex)
                        throw ParserError(lex.GetLastPosition(), Error::ModuleRequiresTerminatingCurly);
                    auto t = lex();
                    if(t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                        sema.SetModuleEndLocation(m, first + t.GetLocation());
                        return;
                    }
                    lex(t);
                    try {
                        access = ParseModuleLevelDeclaration(m, access);
                    } catch(ParserError& e) {
                        if(!lex) throw;
                        auto t = lex();
                        if(t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                            sema.Error(e.recover_where(), e.error());
                            sema.SetModuleEndLocation(m, first + t.GetLocation());
                            return;
                        }
                        lex(t);
                        throw;
                    }
                }
            }
        };
    }
}
