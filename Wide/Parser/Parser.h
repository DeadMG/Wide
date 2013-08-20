#pragma once

#include <string>
#include <unordered_map>
#include <memory>
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

        enum ExpressionContext {
            ModuleUsing,
            TypeMemberVariable,
            IfCondition,
            WhileCondition,
            FunctionLocalVariable,
            ExpressionStatement,
            FunctionArgument,
            FunctionArgumentType,
            LambdaCapture,
            ConstructorInitializer,
        };
         
        template<typename T> struct ExprType {
            typedef typename std::decay<T>::type::ExpressionType type;
        };
        template<typename T> struct StmtType {        
            typedef typename std::decay<T>::type::StatementType type;
        };
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseExpression(Lex&& lex, Sema&& sema, ExpressionContext);
        template<typename Lex, typename Sema, typename Group, typename Loc> void ParseFunctionArguments(Lex&& lex, Sema&& sema, Group&& group, ExpressionContext context);
        template<typename Lex, typename Sema, typename Loc> auto ParseFunctionArguments(Lex&& lex, Sema&& sema, Loc&& open) -> decltype(sema.CreateFunctionArgumentGroup());
        template<typename Lex, typename Sema> typename StmtType<Sema>::type ParseStatement(Lex&& lex, Sema&& sema);
        template<typename Lex, typename Sema, typename Ty> void ParseTypeBody(Lex&& lex, Sema&& sema, Ty&& ty, Wide::Util::optional<ExpressionContext>);
        template<typename Lex, typename Sema, typename Caps, typename Loc> void ParseLambdaCaptures(Lex&& lex, Sema&& sema, Caps&& caps, Loc&& loc) {
            auto tok = lex();
            while(true) {
                if (tok.GetType() != Lexer::TokenType::Identifier)
                    throw std::runtime_error("Expected identifier to introduce a lambda capture.");
                auto varassign = lex();
                if (varassign.GetType() != Lexer::TokenType::VarCreate)
                    throw std::runtime_error("Expected := after identifer when parsing lambda capture.");
                auto init = ParseExpression(lex, sema, ExpressionContext::LambdaCapture);
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
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParsePrimaryExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            // ident
            // string
            // ( expression )
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::OpenBracket) {
                auto expr = ParseExpression(lex, sema, context);
                auto close = lex(); // Consume close bracket
                if (close.GetType() != Lexer::TokenType::CloseBracket)
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
                auto args = ParseFunctionArguments(lex, sema, open.GetLocation());

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
                    throw std::runtime_error("Expected { after function(args).");
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
                auto curly = lex();
                if (curly.GetType() != Lexer::TokenType::OpenCurlyBracket)
                    throw std::runtime_error("Expected { after type in a type expression.");
                auto ty = sema.CreateType("__unnamed", curly.GetLocation());
                ParseTypeBody(lex, sema, ty, context);
                return ty;
            }
            /*if (t.GetType() == Lexer::TokenType::Auto) {
                return sema.CreateAutoExpression(t.GetLocation());
            }*/
           
            throw std::runtime_error("Expected expression, but could not find the start of an expression.");
        }
        
        template<typename Lex, typename Sema, typename Group> void ParseFunctionArguments(Lex&& lex, Sema&& sema, Group&& group, ExpressionContext context) {
            sema.AddExpressionToGroup(group, ParseExpression(lex, sema, ExpressionContext::FunctionArgument));
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::Comma)
                return ParseFunctionArguments(lex, sema, std::forward<Group>(group), context);
            if (t.GetType() == Lexer::TokenType::CloseBracket) {
                return;
            }
            throw std::runtime_error("Encountered unexpected token after function argument- was expecting comma or close bracket.");
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParsePostfixExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto expr = ParsePrimaryExpression(lex, sema, context);
            while(true) {
               auto t = lex();
               if (t.GetType() == Lexer::TokenType::Dot) {
                   auto t = lex();
                   if (t.GetType() == Lexer::TokenType::Negate) {
                       auto next = lex();
                       if (next.GetType() == Lexer::TokenType::Type) {
                           expr = sema.CreateMemberAccessExpression("~type", std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                           continue;
                       }
                   }
                   if (t.GetType() != Lexer::TokenType::Identifier)
                       throw std::runtime_error("Attempted to access a member through expr.ident, but didn't find ident or ~type after ->.");
                   expr = sema.CreateMemberAccessExpression(t.GetValue(), std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                   continue;
               }
               if (t.GetType() == Lexer::TokenType::PointerAccess) {
                   auto t = lex();
                   if (t.GetType() == Lexer::TokenType::Negate) {
                       auto next = lex();
                       if (next.GetType() == Lexer::TokenType::Type) {
                           expr = sema.CreatePointerAccessExpression("~type", std::move(expr), sema.GetLocation(expr) + t.GetLocation());
                           continue;
                       }
                   }
                   if (t.GetType() != Lexer::TokenType::Identifier)
                       throw std::runtime_error("Attempted to access a member through expr->ident, but didn't find ident or ~type after ->.");
                   expr = sema.CreatePointerAccessExpression(t.GetValue(), std::move(expr), sema.GetLocation(expr) + t.GetLocation());
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
                       ParseFunctionArguments(lex, sema, argexprs, context);
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
                       ParseFunctionArguments(lex, sema, argexprs, context);
                   }
                   expr = sema.CreateMetaFunctionCallExpression(std::move(expr), std::move(argexprs), sema.GetLocation(expr) + t.GetLocation());
                   continue;
               }
               // Did not recognize either of these, so put it back and return the final result.
               lex(t);
               return std::move(expr);
            }
        }

        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseUnaryExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto tok = lex();
            if (tok.GetType() == Lexer::TokenType::Dereference) {
                auto expr = ParseUnaryExpression(lex, sema, context);
                return sema.CreateDereferenceExpression(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::Negate) {
                auto expr = ParseUnaryExpression(lex, sema, context);
                return sema.CreateNegateExpression(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::Increment) {
                auto expr = ParseUnaryExpression(lex, sema, context);
                return sema.CreatePrefixIncrement(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::Decrement) {
                auto expr = ParseUnaryExpression(lex, sema, context);
                return sema.CreatePrefixDecrement(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            if (tok.GetType() == Lexer::TokenType::And) {
                auto expr = ParseUnaryExpression(lex, sema, context);
                return sema.CreateAddressOf(expr, tok.GetLocation() + sema.GetLocation(expr));
            }
            lex(tok);
            return ParsePostfixExpression(lex, sema, context);
        }

        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseMultiplicativeExpression(Expr e, Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto lhs = std::move(e);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Dereference) {
                    auto rhs = ParseUnaryExpression(lex, sema, context);
                    lhs = sema.CreateMultiplyExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
				if (t.GetType() == Lexer::TokenType::Divide) {
                    auto rhs = ParseUnaryExpression(lex, sema, context);
                    lhs = sema.CreateDivisionExpression(std::move(lhs), std::move(rhs));
                    continue;
				}
				if (t.GetType() == Lexer::TokenType::Modulo) {
                    auto rhs = ParseUnaryExpression(lex, sema, context);
                    lhs = sema.CreateModulusExpression(std::move(lhs), std::move(rhs));
                    continue;
				}
                lex(t);
                return std::move(lhs);
            }
        }
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseMultiplicativeExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            return ParseMultiplicativeExpression(ParseUnaryExpression(lex, sema, context), lex, sema, context);
        }

        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseAdditiveExpression(Expr e, Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto lhs = ParseMultiplicativeExpression(std::move(e), lex, sema, context);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Plus) {
                    auto rhs = ParseMultiplicativeExpression(lex, sema, context);
                    lhs = sema.CreateAdditionExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
				if (t.GetType() == Lexer::TokenType::Minus) {
					auto rhs = ParseMultiplicativeExpression(lex, sema, context);
					lhs = sema.CreateSubtractionExpression(std::move(lhs), std::move(rhs));
					continue;
				}
                lex(t);
                return std::move(lhs);
            }
        }
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAdditiveExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            return ParseAdditiveExpression(ParseUnaryExpression(lex, sema, context), lex, sema, context);
        }
                
        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseShiftExpression(Expr e, Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto lhs = ParseAdditiveExpression(std::move(e), lex, sema, context);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::LeftShift) {
                    auto rhs = ParseAdditiveExpression(lex, sema, context);
                    lhs = sema.CreateLeftShiftExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::RightShift) {
                    auto rhs = ParseAdditiveExpression(lex, sema, context);
                    lhs = sema.CreateRightShiftExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseShiftExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            return ParseShiftExpression(ParseUnaryExpression(lex, sema, context), lex, sema, context);
        }
        
        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseRelationalExpression(Expr e, Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto lhs = ParseShiftExpression(std::move(e), lex, sema, context);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::LT) {
                    auto rhs = ParseShiftExpression(lex, sema, context);
                    lhs = sema.CreateLTExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::LTE) {
                    auto rhs = ParseShiftExpression(lex, sema, context);
                    lhs = sema.CreateLTEExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::GT) {
                    auto rhs = ParseShiftExpression(lex, sema, context);
                    lhs = sema.CreateGTExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::GTE) {
                    auto rhs = ParseShiftExpression(lex, sema, context);
                    lhs = sema.CreateGTEExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseRelationalExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            return ParseRelationalExpression(ParseUnaryExpression(lex, sema, context), lex, sema, context);
        }
        
        template<typename Lex, typename Sema, typename Expr> typename ExprType<Sema>::type ParseEqualityExpression(Expr e, Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto lhs = ParseRelationalExpression(std::move(e), lex, sema, context);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::EqCmp) {
                    auto rhs = ParseRelationalExpression(lex, sema, context);
                    lhs = sema.CreateEqCmpExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::NotEqCmp) {
                    auto rhs = ParseRelationalExpression(lex, sema, context);
                    lhs = sema.CreateNotEqCmpExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseEqualityExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            return ParseEqualityExpression(ParseUnaryExpression(lex, sema, context), lex, sema, context);
        }
        
        template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseAndExpression(Postfix&& fix, Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto lhs = ParseEqualityExpression(std::forward<Postfix>(fix), lex, sema, context);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::And) {
                    auto rhs = ParseEqualityExpression(lex, sema, context);
                    lhs = sema.CreateAndExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAndExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            return ParseAndExpression(ParseUnaryExpression(lex, sema, context), lex, sema, context);
        }
        
        template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseXorExpression(Postfix&& fix, Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto lhs = ParseAndExpression(std::forward<Postfix>(fix), lex, sema, context);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Xor) {
                    auto rhs = ParseAndExpression(lex, sema, context);
                    lhs = sema.CreateXorExpression(std::move(lhs), std::move(rhs));
                    continue;
                }
                lex(t);
                return std::move(lhs);
            }
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseXorExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            return ParseXorExpression(ParseUnaryExpression(lex, sema, context), lex, sema, context);
        }
        
        template<typename Lex, typename Sema, typename Postfix> typename ExprType<Sema>::type ParseOrExpression(Postfix&& fix, Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto lhs = ParseXorExpression(std::forward<Postfix>(fix), lex, sema, context);
            while(true) {
                auto t = lex();
                if (t.GetType() == Lexer::TokenType::Or) {
                    auto rhs = ParseXorExpression(lex, sema, context);
                    lhs = sema.CreateOrExpression(std::move(lhs), std::move(rhs));
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

		template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseAssignmentExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            auto lhs = ParseUnaryExpression(lex, sema, context);
            auto t = lex();
			if (AssignmentOperators.find(t.GetType()) != AssignmentOperators.end()) {
                return sema.CreateAssignmentExpression(std::move(lhs), ParseAssignmentExpression(lex, sema, context), t.GetType());
			}
            lex(t);
            return ParseOrExpression(std::move(lhs), lex, sema, context);
        }
        
        template<typename Lex, typename Sema> typename ExprType<Sema>::type ParseExpression(Lex&& lex, Sema&& sema, ExpressionContext context) {
            return ParseAssignmentExpression(lex, sema, context);
        }

        template<typename Lex, typename Sema, typename Token> auto ParseVariableStatement(Lex&& lex, Sema&& sema, Token&& t, ExpressionContext context) 
        -> decltype(sema.CreateVariableStatement(t.GetValue(), typename ExprType<Sema>::type(), t.GetLocation())) {
            // Expect to have already seen :=
            auto expr = ParseExpression(lex, sema, context);
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
                auto expr = ParseExpression(lex, sema, ExpressionContext::FunctionLocalVariable);
                next = lex();
                if (next.GetType() != Lexer::TokenType::Semicolon)
                    throw std::runtime_error("Expected semicolon after expression.");
                return sema.CreateReturn(std::move(expr), t.GetLocation() + next.GetLocation());
            }
            // If identifier, check the next for :=
            if (t.GetType() == Lexer::TokenType::Identifier) {
                auto next = lex();
                if (next.GetType() == Lexer::TokenType::VarCreate) {
                    return ParseVariableStatement(lex, sema, t, ExpressionContext::FunctionLocalVariable);
                }
                lex(next);
            }
            // If "if", then we're good.
            if (t.GetType() == Lexer::TokenType::If) {
                auto check = lex();
                if (check.GetType() != Lexer::TokenType::OpenBracket)
                    throw std::runtime_error("Expected ( after if.");
                auto open = check.GetLocation();

                auto cond = ParseExpression(lex, sema, ExpressionContext::IfCondition);
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
                    sema.AddStatementToGroup(grp, ParseStatement(lex, sema));
                    t = lex();
                }
                return sema.CreateCompoundStatement(std::move(grp), pos + t.GetLocation());
            }
            // If "while" then while.
            if (t.GetType() == Lexer::TokenType::While) {            
                auto check = lex();
                if (check.GetType() != Lexer::TokenType::OpenBracket)
                    throw std::runtime_error("Expected ( after while.");
                auto open = check.GetLocation();
                auto cond = ParseExpression(lex, sema, ExpressionContext::WhileCondition);
                check = lex();
                if (check.GetType() != Lexer::TokenType::CloseBracket)
                    throw std::runtime_error("Expected ) after while condition.");
                auto body = ParseStatement(lex, sema);
                return sema.CreateWhileStatement(cond, body, t.GetLocation() + sema.GetLocation(body));
            }
            lex(t);
            // Else, expression statement.
            auto expr = ParseExpression(lex, sema, ExpressionContext::ExpressionStatement);
            t = lex();
            if (t.GetType() != Lexer::TokenType::Semicolon)
                throw std::runtime_error("Expected semicolon after expression.");
            return std::move(expr);
        }
        
        template<typename Lex, typename Sema, typename Loc> auto ParseFunctionArguments(Lex&& lex, Sema&& sema, Loc&& loc) -> decltype(sema.CreateFunctionArgumentGroup()) {
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
                    if (t2.GetType() == Lexer::TokenType::CloseBracket) {
                        break;
                    }
                } else {
                    // Expression-specified argument.
                    lex(t2);
                    lex(t);
                    auto ty = ParseExpression(lex, sema, ExpressionContext::FunctionArgumentType);
                    auto ident = lex();
                    if (ident.GetType() != Lexer::TokenType::Identifier && ident.GetType() != Lexer::TokenType::This)
                        throw std::runtime_error("Expected identifier after expression when parsing function argument.");
                    sema.AddArgumentToFunctionGroup(ret, ident.GetValue(), ty);
                    t2 = lex();
                    if (t2.GetType() == Lexer::TokenType::CloseBracket) {
                        break;
                    }
                    if (t2.GetType() != Lexer::TokenType::Comma)
                        throw std::runtime_error("Expected , or ) after function argument.");
                }
            }
            return ret;
        }
        
		template<typename Lex, typename Sema, typename Group> auto ParseProlog(Lex&& lex, Sema&& sema, Group&& prolog) -> decltype(lex().GetLocation()) {
            auto t = lex();
            auto loc = t.GetLocation();
            if (t.GetType() == Lexer::TokenType::Prolog) {
                t = lex();
                if (t.GetType() != Lexer::TokenType::OpenCurlyBracket)
                    throw std::runtime_error("Expected { after prolog.");            
                while(true) {
                    sema.AddStatementToGroup(prolog, ParseStatement(lex, sema));
                    t = lex();
                    if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                        break;
                    }
                    lex(t);
                }
                // Consume the close curly of the end of the prolog, leaving { for the next section to find.
                t = lex();
            } else
				lex(t);
			return loc;
		}
		template<typename Lex, typename Sema, typename Token, typename Module, typename Loc> void ParseOverloadedOperator(Lex&& lex, Sema&& sema, Token&& first, Module&& m, Loc&& open) {
            auto group = ParseFunctionArguments(lex, sema, std::forward<Loc>(open));
            auto prolog = sema.CreateStatementGroup();
			auto loc = ParseProlog(lex, sema, prolog);
			auto t = lex();
			auto pos = t.GetLocation();
            if (t.GetType() != Lexer::TokenType::OpenCurlyBracket) {
                throw std::runtime_error("Expected { after function arguments or prolog");
            }
            auto stmts = sema.CreateStatementGroup();
            t = lex();
            if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                sema.CreateOverloadedOperator(first.GetType(), std::move(stmts), std::move(prolog), loc + t.GetLocation(), m, std::move(group));
                return;
            }
            lex(t);
            while(true) {
                sema.AddStatementToGroup(stmts, ParseStatement(lex, sema));
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
            auto group = ParseFunctionArguments(lex, sema, std::forward<Loc>(open));
            auto prolog = sema.CreateStatementGroup();
			auto loc = ParseProlog(lex, sema, prolog);
            auto initializers = sema.CreateInitializerGroup();
			auto t = lex();
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
                        sema.AddInitializerToGroup(initializers, sema.CreateVariableStatement(name.GetValue(), t.GetLocation() + next.GetLocation()));
                        continue;
                    }
                    lex(next);
                    auto expr = ParseExpression(lex, sema, ExpressionContext::ConstructorInitializer);
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
            case Lexer::TokenType::Negate:
                val = "~type";
                break;
            }
            if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                sema.CreateFunction(val, std::move(stmts), std::move(prolog), loc + t.GetLocation(), m, std::move(group), std::move(initializers));
                return;
            }
            lex(t);
            while(true) {
                sema.AddStatementToGroup(stmts, ParseStatement(lex, sema));
                t = lex();
                if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                    pos = pos + t.GetLocation();
                    break;
                }
                lex(t);
            }
            sema.CreateFunction(val, std::move(stmts), std::move(prolog), loc + t.GetLocation(), m, std::move(group), std::move(initializers));
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
                    void(); //std::cout << "Warning: = used in using instead of :=. Treating as :=.\n";
                else
                    throw std::runtime_error("Don't support non-assigning usings right now.");
            }
            sema.CreateUsingDefinition(std::move(val), ParseExpression(lex, sema, ExpressionContext::ModuleUsing), m);
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
            auto curly = lex();
            if (curly.GetType() != Lexer::TokenType::OpenCurlyBracket)
                throw std::runtime_error("Expected { after identifier when parsing module.");
            auto mod = sema.CreateModule(ident.GetValue(), m, curly.GetLocation());
            ParseModuleContents(lex, sema, mod);
        }

        template<typename Lex, typename Sema, typename Ty> void ParseTypeBody(Lex&& lex, Sema&& sema, Ty&& ty, Wide::Util::optional<ExpressionContext> context) {
            auto t = lex();
            while(t.GetType() != Lexer::TokenType::CloseCurlyBracket) {
                if (t.GetType() == Lexer::TokenType::Identifier) {
                    // Must be either := for variable or ( for function. Don't support functions yet.
                    if (!lex) {
                        throw UnrecoverableError(t.GetLocation(), Error::TypeScopeExpectedMemberAfterIdentifier);
                    }
                    auto next = lex();
                    if (next.GetType() == Lexer::TokenType::VarCreate) {
                        auto var = ParseVariableStatement(lex, sema, t, ExpressionContext::TypeMemberVariable);
                        sema.AddTypeField(ty, var);
                        t = lex();
                        continue;
                    }
                    if (next.GetType() == Lexer::TokenType::OpenBracket) {
                        ParseFunction(lex, sema, t, ty, next.GetLocation());
                        t = lex();
                        continue;
                    }

                }
                if (t.GetType() == Lexer::TokenType::Operator) {
                    auto op = lex();
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
                    ParseFunction(lex, sema, op, ty, bracket.GetLocation());
                    t = lex();
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Type) {
                    auto open = lex();
                    if (open.GetType() != Lexer::TokenType::OpenBracket)
                        throw std::runtime_error("Expected ( after type to introduce a constructor at type scope.");
                    ParseFunction(lex, sema, t, ty, open.GetLocation());
                    t = lex();
                    continue;
                }
                if (t.GetType() == Lexer::TokenType::Negate) {
                    auto next = lex();
                    if (next.GetType() != Lexer::TokenType::Type)
                        throw std::runtime_error("Expected to find type after ~ at type scope.");
                    auto open = lex();
                    if (open.GetType() != Lexer::TokenType::OpenBracket)
                        throw std::runtime_error("Expected ( after type to introduce a constructor at type scope.");
                    ParseFunction(lex, sema, t, ty, open.GetLocation());
                    t = lex();
                    continue;
                }
                // Error handling based on context.
                if (!context // module m { type t { 
                 || *context == ExpressionContext::ModuleUsing // using x := type {
                 || *context == ExpressionContext::TypeMemberVariable // type { x := type {
                 ) {
                     if (ModuleGrammarIntroducers.find(t.GetType()) != ModuleGrammarIntroducers.end()) {
                         lex(t);
                         sema.Error(sema.GetLocation(ty), Error::ModuleScopeTypeRequiresTerminatingCurly);
                         return;
                     }
                     throw UnrecoverableError(sema.GetLocation(ty), Error::ModuleRequiresTerminatingCurly);
                }
                if (*context == ExpressionContext::IfCondition
                 || *context == ExpressionContext::WhileCondition
                 || *context == ExpressionContext::FunctionLocalVariable) {
                    // Expect a statement next or ).
                    if (t.GetType() == Lexer::TokenType::CloseBracket
                     || t.GetType() == Lexer::TokenType::If
                     || t.GetType() == Lexer::TokenType::While
                     || t.GetType() == Lexer::TokenType::Return
                     || t.GetType() == Lexer::TokenType::OpenCurlyBracket
                     || t.GetType() == Lexer::TokenType::OpenBracket
                     || t.GetType() == Lexer::TokenType::String
                     || t.GetType() == Lexer::TokenType::Integer
                     || t.GetType() == Lexer::TokenType::This
                     || t.GetType() == Lexer::TokenType::Function
                     || t.GetType() == Lexer::TokenType::Dereference
                     || t.GetType() == Lexer::TokenType::Negate
                     || t.GetType() == Lexer::TokenType::Increment
                     || t.GetType() == Lexer::TokenType::Decrement
                     || t.GetType() == Lexer::TokenType::And) {
                        lex(t);
                        sema.Error(sema.GetLocation(ty), Error::ExpressionScopeTypeRequiresTerminatingCurly);
                        return;
                    }
                }
                throw UnrecoverableError(t.GetLocation(), Error::ExpressionScopeTypeRequiresTerminatingCurly);
            }
            sema.SetTypeEndLocation(t.GetLocation(), ty);
        }

        template<typename Lex, typename Sema, typename Module, typename Loc> void ParseTypeDeclaration(Lex&& lex, Sema&& sema, Module&& m, Loc&& loc) {
            if (!lex) {
                throw UnrecoverableError(loc, Error::ModuleScopeTypeNoIdentifier);
            }
            auto ident = lex();
            if (ident.GetType() != Lexer::TokenType::Identifier) {
                if (ModuleGrammarIntroducers.find(ident.GetType()) != ModuleGrammarIntroducers.end()) {
                    sema.Error(loc, Error::ModuleScopeTypeNoIdentifier);
                    lex(ident);
                    return;
                }
                throw UnrecoverableError(loc, Error::ModuleScopeTypeNoIdentifier);
            }
            if (!lex) {
                throw UnrecoverableError(ident.GetLocation(), Error::ModuleScopeTypeNoCurlyBrace);
            }
            auto t = lex();
            if (t.GetType() != Lexer::TokenType::OpenCurlyBracket) {
                // We can have type x() if the user started to define a type whilst in module scope, before a function.
                // So issue a different error in this context.
                if (t.GetType() == Lexer::TokenType::OpenBracket) {
                    sema.Error(loc, Error::ModuleScopeTypeIdentifierButFunction);
                    lex(t);
                    lex(ident);
                    return;
                }
                if (ModuleGrammarIntroducers.find(t.GetType()) != ModuleGrammarIntroducers.end()) {
                    sema.Error(ident.GetLocation(), Error::ModuleScopeTypeNoCurlyBrace);
                    lex(t);
                    return;
                }
                throw UnrecoverableError(ident.GetLocation(), Error::ModuleScopeTypeNoCurlyBrace);
            }
            auto ty = sema.CreateType(ident.GetValue(), m, t.GetLocation());
            return ParseTypeBody(lex, sema, ty, Wide::Util::none);
        }
        
        template<typename Lex, typename Sema, typename Module> void ParseModuleLevelDeclaration(Lex&& lex, Sema&& sema, Module&& m) {
            auto token = lex();
            if (token.GetType() == Lexer::TokenType::Identifier) {
                if (!lex) {
                    sema.Error(token.GetLocation(), Error::ModuleScopeFunctionNoOpenBracket);
                }
                auto t = lex();
                if (t.GetType() != Lexer::TokenType::OpenBracket) {
                    //
                    // If the user is entering a new function above valid content, the tokens here will be identifier, using, module, type, operator.
                    if (ModuleGrammarIntroducers.find(t.GetType()) == ModuleGrammarIntroducers.end())
                        throw UnrecoverableError(t.GetLocation(), Error::ModuleScopeFunctionNoOpenBracket);
					sema.Error(token.GetLocation(), Error::ModuleScopeFunctionNoOpenBracket);
                    // Else, skip over this apparently partly-written declaration.
                    lex(t);
                    return;
                }
                ParseFunction(lex, sema, token, std::forward<Module>(m), t.GetLocation());
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
                ParseTypeDeclaration(lex, sema, std::forward<Module>(m), token.GetLocation());
                return;
            }
            /*if (token.GetType() == Lexer::TokenType::Semicolon) {
                return ParseModuleLevelDeclaration(lex, sema, std::forward<Module>(m));
            }*/
            if (token.GetType() == Lexer::TokenType::Operator) {
                if (!lex)
                    sema.Error(token.GetLocation(), Error::NoOperatorFound);
                auto op = lex();
                if (op.GetType() == Lexer::TokenType::OpenBracket) {
                    auto loc = op.GetLocation();
                    if (lex) {
                        auto tok = lex();
                        if (tok.GetType() == Lexer::TokenType::CloseBracket) {
                            loc = loc + tok.GetLocation();
                        } else
                            lex(tok);
                    }
                    throw UnrecoverableError(loc, Error::GlobalFunctionCallOperator);
                }
                if (OverloadableOperators.find(op.GetType()) == OverloadableOperators.end()) {
                    auto err = Error::NonOverloadableOperator;
                    if (ModuleGrammarIntroducers.find(op.GetType()) != ModuleGrammarIntroducers.end())
                        err =  Error::NoOperatorFound;                        
                    if (lex) { lex(op); sema.Error(token.GetLocation(), err); return; }
                    throw UnrecoverableError(op.GetLocation(), err);
                }
                if (!lex)
                    sema.Error(op.GetLocation(), Error::ModuleScopeOperatorNoOpenBracket);
                auto bracket = lex();
                if (bracket.GetType() != Lexer::TokenType::OpenBracket) {
                    if (ModuleGrammarIntroducers.find(bracket.GetType()) == ModuleGrammarIntroducers.end())
                        throw UnrecoverableError(op.GetLocation(), ModuleScopeOperatorNoOpenBracket);
                    sema.Error(op.GetLocation(), Error::ModuleScopeOperatorNoOpenBracket);
                    lex(bracket);
                    return;
                }
                ParseOverloadedOperator(lex, sema, op, std::forward<Module>(m), bracket.GetLocation());
                return;
            }
            sema.Error(token.GetLocation(), Error::UnrecognizedTokenModuleScope);
            // Skip this token.
            return;
        }
            

        template<typename Lex, typename Sema, typename Module> void ParseGlobalModuleContents(Lex&& lex, Sema&& sema, Module&& m) {
            typedef typename std::decay<decltype(*lex())>::type token_type;
            
            struct AssumeLexer {
                typename std::decay<Lex>::type* lex;
                mutable std::deque<token_type> putbacks;
                token_type operator()() {
                    if (!putbacks.empty()) {
                        auto val = std::move(putbacks.back());
                        putbacks.pop_back();
                        return val;
                    }
                    auto val = (*lex)();
                    if (!val) throw std::runtime_error("Encountered unexpected end of input.");
                    return std::move(*val);
                }
                void operator()(token_type arg) {
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
                    return lex->GetLastPosition();
                }
            };
            AssumeLexer lexer;
            lexer.lex = &lex;
            while (lexer) {
                ParseModuleLevelDeclaration(lexer, sema, m);
            }
        }

        template<typename Lex, typename Sema, typename Module> void ParseModuleContents(Lex&& lex, Sema&& sema, Module&& m) {
            // Should really be refactored later into ParseGlobalModuleContents and ParseModuleDeclaration
            if (!lex) {
                throw UnrecoverableError(lex.GetLastPosition(), Error::ModuleRequiresTerminatingCurly);
            }
            auto t = lex();
            if (t.GetType() == Lexer::TokenType::CloseCurlyBracket) {
                sema.SetModuleEndLocation(m, t.GetLocation());
                return;
            }
            lex(t);
            ParseModuleLevelDeclaration(lex, sema, m);
            return ParseModuleContents(lex, sema, m);
        }
    }
}