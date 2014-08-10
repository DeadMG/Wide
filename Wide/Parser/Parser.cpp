#include <Wide/Parser/Parser.h>

using namespace Wide;
using namespace Parse;

Lexer::Range PutbackLexer::GetLastPosition() {
    return locations.back();
}

void PutbackLexer::operator()(Lexer::Token arg) {
    locations.pop_back();
    putbacks.push_back(std::move(arg));
}

Wide::Util::optional<Lexer::Token> PutbackLexer::operator()() {
    if (!putbacks.empty()) {
        auto val = std::move(putbacks.back());
        putbacks.pop_back();
        locations.push_back(val.GetLocation());
        return val;
    }
    auto val = lex();
    if (val)
        locations.push_back(val->GetLocation());
    return std::move(val);
}

Lexer::Token PutbackLexer::operator()(Parse::Error err) {
    auto tok = (*this)();
    if (!tok)
        throw ParserError(GetLastPosition(), err);
    return *tok;
}

Parser::Parser(std::function<Wide::Util::optional<Lexer::Token>()> l)
: lex(l), GlobalModule() 
{
    error = [](std::vector<Lexer::Range> r, Parse::Error e) {};
    warning = [](Lexer::Range r, Parse::Warning w) {};
    outlining = [](Lexer::Range r, Parse::OutliningType out) {};

    ModuleOverloadableOperators = std::initializer_list<std::vector<Wide::Lexer::TokenType>> {
       { { &Lexer::TokenTypes::LeftShift  } },
       { { &Lexer::TokenTypes::RightShift  } },
       { { &Lexer::TokenTypes::EqCmp  } },
       { { &Lexer::TokenTypes::NotEqCmp  } },
       { { &Lexer::TokenTypes::Star  } },
       { { &Lexer::TokenTypes::Negate  } },
       { { &Lexer::TokenTypes::Plus  } },
       { { &Lexer::TokenTypes::Increment  } },
       { { &Lexer::TokenTypes::Decrement  } },
       { { &Lexer::TokenTypes::Minus  } },
       { { &Lexer::TokenTypes::LT  } },
       { { &Lexer::TokenTypes::LTE  } },
       { { &Lexer::TokenTypes::GT  } },
       { { &Lexer::TokenTypes::GTE  } },
       { { &Lexer::TokenTypes::Or  } },
       { { &Lexer::TokenTypes::And  } },
       { { &Lexer::TokenTypes::Xor  } },
       { { &Lexer::TokenTypes::Divide  } },
       { { &Lexer::TokenTypes::Modulo  } },
    };

    MemberOverloadableOperators = std::initializer_list<std::vector<Wide::Lexer::TokenType>> {
        { &Lexer::TokenTypes::LeftShiftAssign },
        { &Lexer::TokenTypes::RightShiftAssign },
        { &Lexer::TokenTypes::MulAssign },
        { &Lexer::TokenTypes::PlusAssign },
        { &Lexer::TokenTypes::MinusAssign },
        { &Lexer::TokenTypes::OrAssign },
        { &Lexer::TokenTypes::AndAssign },
        { &Lexer::TokenTypes::XorAssign },
        { &Lexer::TokenTypes::DivAssign },
        { &Lexer::TokenTypes::ModAssign },
        { &Lexer::TokenTypes::Assignment },
        { &Lexer::TokenTypes::QuestionMark },
        { &Lexer::TokenTypes::OpenBracket, &Lexer::TokenTypes::CloseBracket },
        { &Lexer::TokenTypes::OpenSquareBracket, &Lexer::TokenTypes::CloseSquareBracket },
    };

    ExpressionPrecedences = {
        { &Lexer::TokenTypes::Or },
        { &Lexer::TokenTypes::Xor },
        { &Lexer::TokenTypes::And },
        { &Lexer::TokenTypes::EqCmp, &Lexer::TokenTypes::NotEqCmp },
        { &Lexer::TokenTypes::LT, &Lexer::TokenTypes::LTE, &Lexer::TokenTypes::GT, &Lexer::TokenTypes::GTE },
        { &Lexer::TokenTypes::LeftShift, &Lexer::TokenTypes::RightShift },
        { &Lexer::TokenTypes::Plus, &Lexer::TokenTypes::Minus },
        { &Lexer::TokenTypes::Star, &Lexer::TokenTypes::Divide, &Lexer::TokenTypes::Modulo },
    };
    
    for (auto ty : { &Lexer::TokenTypes::LeftShiftAssign, &Lexer::TokenTypes::RightShiftAssign, &Lexer::TokenTypes::MulAssign, &Lexer::TokenTypes::PlusAssign, &Lexer::TokenTypes::MinusAssign, 
        &Lexer::TokenTypes::OrAssign, &Lexer::TokenTypes::AndAssign, &Lexer::TokenTypes::XorAssign, &Lexer::TokenTypes::DivAssign, &Lexer::TokenTypes::ModAssign, &Lexer::TokenTypes::Assignment }) {
        AssignmentOperators[ty] = [ty](Parser& p, Expression* lhs) {
            return p.arena.Allocate<BinaryExpression>(lhs, p.ParseAssignmentExpression(), ty);
        };
    }

    for (auto op : { &Lexer::TokenTypes::Star, &Lexer::TokenTypes::Negate, &Lexer::TokenTypes::Increment, &Lexer::TokenTypes::Decrement, &Lexer::TokenTypes::And }) {
        UnaryOperators[op] = [op](Parser& p, Lexer::Token& token) {
            auto subexpr = p.ParseUnaryExpression();
            return p.arena.Allocate<UnaryExpression>(subexpr, op, token.GetLocation() + subexpr->location);
        };
    }

    ModuleTokens[&Lexer::TokenTypes::Private] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& token) {
        p.Check(Error::AccessSpecifierNoColon, &Lexer::TokenTypes::Colon);
        return Parse::Access::Private;
    };

    ModuleTokens[&Lexer::TokenTypes::Public] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& token) {
        p.Check(Error::AccessSpecifierNoColon, &Lexer::TokenTypes::Colon);
        return Parse::Access::Public;
    };

    ModuleTokens[&Lexer::TokenTypes::Protected] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& token) {
        throw ParserError(token.GetLocation(), Error::ProtectedModuleScope);
        return a; // Quiet error
    };    

    GlobalModuleTokens[&Lexer::TokenTypes::Module] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& module) {
        auto ident = p.Check(Error::ModuleNoIdentifier, &Lexer::TokenTypes::Identifier);
        auto maybedot = p.lex(Parse::Error::ModuleNoOpeningBrace);
        if (maybedot.GetType() == &Lexer::TokenTypes::Dot)
            m = p.ParseQualifiedName(ident, m, a, Parse::Error::ModuleNoOpeningBrace);
        else
            p.lex(maybedot);
        if (ident.GetType() == &Lexer::TokenTypes::Operator) throw p.BadToken(ident, Parse::Error::QualifiedNameNoIdentifier);
        auto curly = p.Check(Error::ModuleNoOpeningBrace, &Lexer::TokenTypes::OpenCurlyBracket);
        auto mod = p.CreateModule(ident.GetValue(), m, module.GetLocation() + curly.GetLocation(), a);
        p.ParseModuleContents(mod, curly.GetLocation());
    };

    GlobalModuleTokens[&Lexer::TokenTypes::Template] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& templat) {
        p.Check(Error::TemplateNoArguments, &Lexer::TokenTypes::OpenBracket);
        auto args = p.ParseFunctionDefinitionArguments();
        auto attrs = std::vector<Attribute>();
        auto token = p.lex(Error::ModuleScopeTemplateNoType);
        while (token.GetType() == &Lexer::TokenTypes::OpenSquareBracket) {
            attrs.push_back(p.ParseAttribute(token));
            token = p.lex(Error::ModuleScopeTemplateNoType);
        }
        p.lex(token);
        auto type = p.Check(Error::ModuleScopeTemplateNoType, &Lexer::TokenTypes::Type);
        auto ident = p.Check(Error::ModuleScopeTypeNoIdentifier, &Lexer::TokenTypes::Identifier);
        auto ty = p.ParseTypeDeclaration(m, templat.GetLocation(), ident, attrs);
        p.AddTemplateTypeToModule(m, ident.GetValue(), args, ty, a);
    };

    GlobalModuleTokens[&Lexer::TokenTypes::Using] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& token) {
        auto useloc = p.lex.GetLastPosition();
        auto t = p.Check(Error::ModuleScopeUsingNoIdentifier, &Lexer::TokenTypes::Identifier);
        auto var = p.Check(Error::ModuleScopeUsingNoVarCreate, [&](Lexer::Token& curr) {
            if (curr.GetType() == &Lexer::TokenTypes::Assignment) {
                p.warning(curr.GetLocation(), Warning::AssignmentInUsing);
                return true;
            }
            if (curr.GetType() == &Lexer::TokenTypes::VarCreate)
                return true;
            return false;
        });
        auto expr = p.ParseExpression();
        auto semi = p.Check(Error::ModuleScopeUsingNoSemicolon, &Lexer::TokenTypes::Semicolon);
        auto use = p.arena.Allocate<Using>(expr, token.GetLocation() + semi.GetLocation());
        p.AddUsingToModule(m, t.GetValue(), use, a);
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::OpenSquareBracket] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& token, std::vector<Attribute> attributes) {
        // Another attribute, just add it to the list.
        attributes.push_back(p.ParseAttribute(token));
        auto next = p.lex(Error::AttributeNoEnd);
        if (p.GlobalModuleAttributeTokens.find(next.GetType()) == p.GlobalModuleAttributeTokens.end())
            throw p.BadToken(next, Error::AttributeNoEnd);
        return p.GlobalModuleAttributeTokens[next.GetType()](p, m, a, next, attributes);
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Identifier] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& ident, std::vector<Attribute> attributes) {
        auto maybedot = p.lex(Parse::Error::ModuleNoOpeningBrace);
        if (maybedot.GetType() == &Lexer::TokenTypes::Dot)
            m = p.ParseQualifiedName(ident, m, a, Parse::Error::ModuleNoOpeningBrace);
        else
            p.lex(maybedot);
        if (ident.GetType() == &Lexer::TokenTypes::Identifier) {
            auto t = p.Check(Error::ModuleScopeFunctionNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
            auto func = p.ParseFunction(ident, attributes);
            p.AddFunctionToModule(m, ident.GetValue(), func, a);
            return;
        }
        auto name = p.ParseOperatorName(p.ModuleOverloadableOperators);
        p.Check(Error::ModuleScopeFunctionNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        auto func = p.ParseFunction(ident, attributes);
        m->OperatorOverloads[name][a].insert(func);
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Type] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& typ, std::vector<Attribute> attributes) {
        // Could be exported constructor.
        auto next = p.lex(Error::ModuleScopeTypeNoIdentifier);
        if (next.GetType() == &Lexer::TokenTypes::OpenBracket) {
            auto func = p.ParseConstructor(typ, attributes);
            m->constructor_decls.insert(func);
            return;
        }
        if (next.GetType() != &Lexer::TokenTypes::Identifier) throw p.BadToken(next, Error::ModuleScopeTypeNoIdentifier);
        auto ty = p.ParseTypeDeclaration(m, typ.GetLocation(), next, attributes);
        p.AddTypeToModule(m, next.GetValue(), ty, a);
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Operator] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& tok, std::vector<Attribute> attrs) {
        auto name = p.ParseOperatorName(p.ModuleOverloadableOperators);
        p.Check(Error::ModuleScopeFunctionNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        auto func = p.ParseFunction(tok, attrs);
        m->OperatorOverloads[name][a].insert(func);
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Negate] = [](Parser& p, Module* m, Parse::Access a, Lexer::Token& token, std::vector<Attribute> attributes) {
        auto des = p.ParseDestructor(token, attributes);
        m->destructor_decls.insert(des);
    };
    
    PostfixOperators[&Lexer::TokenTypes::OpenSquareBracket] = [](Parser& p, Expression* e, Lexer::Token& token) {
        auto index = p.ParseExpression();
        auto close = p.Check(Error::IndexNoCloseBracket, &Lexer::TokenTypes::CloseSquareBracket);
        return p.arena.Allocate<Index>(e, index, e->location + close.GetLocation());
    };

    PostfixOperators[&Lexer::TokenTypes::Dot] = [](Parser& p, Expression* e, Lexer::Token& token) -> Expression* {
        auto t = p.lex(Error::MemberAccessNoIdentifierOrDestructor);
        if (t.GetType() == &Lexer::TokenTypes::Identifier)
            return p.arena.Allocate<MemberAccess>(t.GetValue(), e, e->location + t.GetLocation(), t.GetLocation());
        if (t.GetType() == &Lexer::TokenTypes::Operator)
            return p.arena.Allocate<MemberAccess>(p.ParseOperatorName(p.GetAllOperators()), e, e->location + t.GetLocation(), t.GetLocation());
        if (t.GetType() == &Lexer::TokenTypes::Negate) {
            auto typ = p.Check(Error::MemberAccessNoTypeAfterNegate, &Lexer::TokenTypes::Type);
            auto open = p.Check(Error::DestructorNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
            auto close = p.Check(Error::DestructorNoOpenBracket, &Lexer::TokenTypes::CloseBracket);
            return p.arena.Allocate<DestructorAccess>(e, e->location + close.GetLocation());
        }
        throw p.BadToken(t, Error::MemberAccessNoIdentifierOrDestructor);
        return nullptr; // shut up warning
    };
    
    PostfixOperators[&Lexer::TokenTypes::PointerAccess] = [](Parser& p, Expression* e, Lexer::Token& token) -> Expression* {
        auto t = p.lex(Error::PointerAccessNoIdentifierOrDestructor);
        if (t.GetType() == &Lexer::TokenTypes::Identifier)
            return p.arena.Allocate<PointerMemberAccess>(t.GetValue(), e, e->location + t.GetLocation(), t.GetLocation());
        if (t.GetType() == &Lexer::TokenTypes::Operator)
            return p.arena.Allocate<MemberAccess>(p.ParseOperatorName(p.GetAllOperators()), e, e->location + t.GetLocation(), t.GetLocation());
        if (t.GetType() == &Lexer::TokenTypes::Negate) {
            auto typ = p.Check(Error::PointerAccessNoTypeAfterNegate, &Lexer::TokenTypes::Type);
            return p.arena.Allocate<PointerDestructorAccess>(e, e->location + typ.GetLocation());
        }
        throw p.BadToken(t, Error::PointerAccessNoIdentifierOrDestructor);
        return nullptr; // shut up warning
    };

    PostfixOperators[&Lexer::TokenTypes::Increment] = [](Parser& p, Expression* e, Lexer::Token& token) {
        return p.arena.Allocate<Increment>(e, e->location + token.GetLocation(), true);
    };

    PostfixOperators[&Lexer::TokenTypes::Decrement] = [](Parser& p, Expression* e, Lexer::Token& token) {
        return p.arena.Allocate<Decrement>(e, e->location + token.GetLocation(), true);
    };

    PostfixOperators[&Lexer::TokenTypes::OpenBracket] = [](Parser& p, Expression* e, Lexer::Token& token) {
        std::vector<Expression*> args;
        auto t = p.lex(Error::FunctionArgumentNoIdentifierOrThis);
        if (t.GetType() != &Lexer::TokenTypes::CloseBracket) {
            p.lex(t);
            args = p.ParseFunctionArguments();
        }
        return p.arena.Allocate<FunctionCall>(e, std::move(args), e->location + p.lex.GetLastPosition());
    };

    PostfixOperators[&Lexer::TokenTypes::QuestionMark] = [](Parser& p, Expression* e, Lexer::Token& token) {
        return p.arena.Allocate<BooleanTest>(e, e->location + token.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::OpenCurlyBracket] = [](Parser& p, Lexer::Token& t) {
        std::vector<Expression*> exprs;
        auto terminator = p.lex(Error::TupleCommaOrClose);
        while (terminator.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
            p.lex(terminator);
            exprs.push_back(p.ParseExpression());
            terminator = p.Check(Error::TupleCommaOrClose, [](Lexer::Token& tok) {
                if (tok.GetType() == &Lexer::TokenTypes::Comma)
                    return true;
                if (tok.GetType() == &Lexer::TokenTypes::CloseCurlyBracket)
                    return true;
                return false;
            });
            if (terminator.GetType() == &Lexer::TokenTypes::Comma)
                terminator = p.lex(Error::TupleCommaOrClose);
        }
        return p.arena.Allocate<Tuple>(std::move(exprs), t.GetLocation() + terminator.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::String] = [](Parser& p, Lexer::Token& t) {
        return p.arena.Allocate<String>(t.GetValue(), t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Integer] = [](Parser& p, Lexer::Token& t) {
        return p.arena.Allocate<Integer>(t.GetValue(), t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::This] = [](Parser& p, Lexer::Token& t) {
        return p.arena.Allocate<This>(t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::True] = [](Parser& p, Lexer::Token& t) {
        return p.arena.Allocate<True>(t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::False] = [](Parser& p, Lexer::Token& t) {
        return p.arena.Allocate<False>(t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Operator] = [](Parser& p, Lexer::Token& t) {
        return p.arena.Allocate<Identifier>(p.ParseOperatorName(p.GetAllOperators()), t.GetLocation() + p.lex.GetLastPosition());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Decltype] = [](Parser& p, Lexer::Token& t) {
        p.Check(Error::DecltypeNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        auto expr = p.ParseExpression();
        auto close = p.Check(Error::DecltypeNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
        return p.arena.Allocate<Decltype>(expr, t.GetLocation() + close.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Typeid] = [](Parser& p, Lexer::Token& t) {
        p.Check(Error::TypeidNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        auto expr = p.ParseExpression();
        auto close = p.Check(Error::TypeidNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
        return p.arena.Allocate<Typeid>(expr, t.GetLocation() + close.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::DynamicCast] = [](Parser& p, Lexer::Token& t) {
        p.Check(Error::DynamicCastNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        auto expr1 = p.ParseExpression();
        p.Check(Error::DynamicCastNoComma, &Lexer::TokenTypes::Comma);
        auto expr2 = p.ParseExpression();
        auto close = p.Check(Error::DynamicCastNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
        return p.arena.Allocate<DynamicCast>(expr1, expr2, t.GetLocation() + close.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Identifier] = [](Parser& p, Lexer::Token& t) -> Expression* {
        auto maybe_lambda = p.lex();
        if (!maybe_lambda || maybe_lambda->GetType() != &Lexer::TokenTypes::Lambda) {
            if (maybe_lambda) p.lex(*maybe_lambda);
            return p.arena.Allocate<Identifier>(t.GetValue(), t.GetLocation());
        }
        auto expr = p.ParseExpression();
        std::vector<Statement*> stmts = { p.arena.Allocate<Return>(expr, expr->location) };
        std::vector<FunctionArgument> args = { { t.GetLocation(), t.GetValue() } };
        return p.arena.Allocate<Lambda>(std::move(stmts), std::move(args), t.GetLocation() + expr->location, false, std::vector<Variable*>());
    };

    PrimaryExpressions[&Lexer::TokenTypes::OpenBracket] = [](Parser& p, Lexer::Token& t) -> Expression* {
        auto tok = p.lex(Error::ParenthesisedExpressionNoCloseBracket);
        if (tok.GetType() == &Lexer::TokenTypes::CloseBracket) {
            p.Check(Error::LambdaNoIntroducer, &Lexer::TokenTypes::Lambda);
            auto expr = p.ParseExpression();
            std::vector<Statement*> stmts = { p.arena.Allocate<Return>(expr, expr->location) };
            return p.arena.Allocate<Lambda>(std::move(stmts), std::vector<FunctionArgument>(), t.GetLocation() + expr->location, false, std::vector<Variable*>());
        }
        p.lex(tok);
        auto expr = p.ParseExpression();
        p.Check(Error::ParenthesisedExpressionNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
        return std::move(expr);
    };

    PrimaryExpressions[&Lexer::TokenTypes::Function] = [](Parser& p, Lexer::Token& t) -> Expression* {
        p.Check(Error::LambdaNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        auto args = p.ParseFunctionDefinitionArguments();
        auto pos = t.GetLocation();
        auto grp = std::vector<Statement*>();
        auto caps = std::vector<Variable*>();
        bool defaultref = false;
        auto tok = p.lex(Error::LambdaNoOpenCurly);
        if (tok.GetType() == &Lexer::TokenTypes::OpenSquareBracket) {
            auto opensquare = tok.GetLocation();
            tok = p.lex(Error::LambdaNoOpenCurly);
            if (tok.GetType() == &Lexer::TokenTypes::And) {
                defaultref = true;
                tok = p.lex(Error::LambdaNoOpenCurly);
                if (tok.GetType() == &Lexer::TokenTypes::Comma) {
                    caps = p.ParseLambdaCaptures();
                    tok = p.lex(Error::LambdaNoOpenCurly);
                }
                else if (tok.GetType() == &Lexer::TokenTypes::CloseSquareBracket) {
                    tok = p.lex(Error::LambdaNoOpenCurly);
                }
                else
                    throw std::runtime_error("Expected ] or , after [& in a lambda capture.");
            } else {
                p.lex(tok);
                caps = p.ParseLambdaCaptures();
                tok = p.lex(Error::LambdaNoOpenCurly);
            }
        }
        if (tok.GetType() != &Lexer::TokenTypes::OpenCurlyBracket)
            throw p.BadToken(tok, Error::LambdaNoOpenCurly);
        auto opencurly = tok.GetLocation();
        tok = p.lex(Error::LambdaNoOpenCurly);
        while (tok.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
            p.lex(tok);
            grp.push_back(p.ParseStatement());
            tok = p.lex(Error::LambdaNoOpenCurly);
        }
        return p.arena.Allocate<Lambda>(std::move(grp), std::move(args), pos + tok.GetLocation(), defaultref, std::move(caps));
    };

    PrimaryExpressions[&Lexer::TokenTypes::Type] = [](Parser& p, Lexer::Token& t) -> Expression* {
        auto bases = p.ParseTypeBases();
        auto ty = p.arena.Allocate<Type>(bases, p.Check(Error::TypeExpressionNoCurly, &Lexer::TokenTypes::OpenCurlyBracket).GetLocation(), std::vector<Attribute>());
        p.ParseTypeBody(ty);
        return ty;
    };

    Statements[&Lexer::TokenTypes::Return] = [](Parser& p, Lexer::Token& t) {
        auto next = p.lex(Error::ReturnNoSemicolon); // Check next token for ;
        if (next.GetType() == &Lexer::TokenTypes::Semicolon)
            return p.arena.Allocate<Return>(t.GetLocation() + next.GetLocation());        
        // If it wasn't ; then expect expression.
        p.lex(next);
        auto expr = p.ParseExpression();
        next = p.Check(Error::ReturnNoSemicolon, &Lexer::TokenTypes::Semicolon);
        return p.arena.Allocate<Return>(expr, t.GetLocation() + next.GetLocation());
    };

    Statements[&Lexer::TokenTypes::If] = [](Parser& p, Lexer::Token& t) {
        p.Check(Error::IfNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        // Check for variable conditions
        auto ident = p.lex(Error::IfNoOpenBracket);
        if (ident.GetType() == &Lexer::TokenTypes::Identifier) {
            auto var = p.lex(Error::IfNoOpenBracket);
            if (var.GetType() == &Lexer::TokenTypes::VarCreate) {
                auto expr = p.ParseExpression();
                auto variable = p.arena.Allocate<Variable>(std::vector<Variable::Name>{{ ident.GetValue(), t.GetLocation() + expr->location }}, std::move(expr), ident.GetLocation());
                p.Check(Error::IfNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
                auto body = p.ParseStatement();
                auto next = p.lex(Error::IfNoOpenBracket);
                if (next.GetType() == &Lexer::TokenTypes::Else) {
                    auto else_br = p.ParseStatement();
                    return p.arena.Allocate<If>(variable, body, else_br, t.GetLocation() + body->location);
                }
                p.lex(next);
                return p.arena.Allocate<If>(variable, body, nullptr, t.GetLocation() + body->location);
            }
            p.lex(var);
        }
        p.lex(ident);

        auto cond = p.ParseExpression();
        p.Check(Error::IfNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
        auto true_br = p.ParseStatement();
        auto next = p.lex(Error::IfNoOpenBracket);
        if (next.GetType() == &Lexer::TokenTypes::Else) {
            auto else_br = p.ParseStatement();
            return p.arena.Allocate<If>(cond, true_br, else_br, t.GetLocation() + else_br->location);
        }
        p.lex(next);
        return p.arena.Allocate<If>(cond, true_br, nullptr, t.GetLocation() + true_br->location);
    };

    Statements[&Lexer::TokenTypes::OpenCurlyBracket] = [](Parser& p, Lexer::Token& t) {
        auto pos = t.GetLocation();
        auto next = p.lex(Error::IfNoOpenBracket);
        std::vector<Statement*> stmts;
        while (next.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
            p.lex(next);
            stmts.push_back(p.ParseStatement());
            next = p.lex(Error::IfNoOpenBracket);
        }
        return p.arena.Allocate<CompoundStatement>(std::move(stmts), pos + t.GetLocation());
    };

    Statements[&Lexer::TokenTypes::While] = [](Parser& p, Lexer::Token& t) {
        p.Check(Error::WhileNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        // Check for variable conditions.
        auto ident = p.lex(Error::WhileNoOpenBracket);
        if (ident.GetType() == &Lexer::TokenTypes::Identifier) {
            auto var = p.lex(Error::WhileNoOpenBracket);
            if (var.GetType() == &Lexer::TokenTypes::VarCreate) {
                auto expr = p.ParseExpression();
                auto variable = p.arena.Allocate<Variable>(std::vector<Variable::Name>{{ ident.GetValue(), t.GetLocation() + expr->location }}, std::move(expr), ident.GetLocation());
                p.Check(Error::WhileNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
                auto body = p.ParseStatement();
                return p.arena.Allocate<While>(body, variable, t.GetLocation() + body->location);
            }
            p.lex(var);
        }
        p.lex(ident);
        auto cond = p.ParseExpression();
        p.Check(Error::WhileNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
        auto body = p.ParseStatement();
        return p.arena.Allocate<While>(body, cond, t.GetLocation() + body->location);
    };

    Statements[&Lexer::TokenTypes::Identifier] = [](Parser& p, Lexer::Token& t) -> Statement* {
        std::vector<Parse::Variable::Name> names = { { t.GetValue(), t.GetLocation() } };
        auto next = p.lex(Error::VariableListNoIdentifier);
        if (next.GetType() != &Lexer::TokenTypes::VarCreate && next.GetType() != &Lexer::TokenTypes::Comma) {
            p.lex(next);
            p.lex(t);
            auto expr = p.ParseExpression();
            p.Check(Error::ExpressionStatementNoSemicolon, &Lexer::TokenTypes::Semicolon);
            return expr;
        } else {
            while (next.GetType() == &Lexer::TokenTypes::Comma) {
                auto ident = p.Check(Error::VariableListNoIdentifier, &Lexer::TokenTypes::Identifier);
                names.push_back({ ident.GetValue(), ident.GetLocation() });
                next = p.lex(Error::VariableListNoIdentifier);
            }
            p.lex(next);
            p.Check(Error::VariableListNoInitializer, &Lexer::TokenTypes::VarCreate);
            auto init = p.ParseExpression();
            auto semi = p.Check(Error::VariableStatementNoSemicolon, &Lexer::TokenTypes::Semicolon);
            return p.arena.Allocate<Variable>(std::move(names), init, t.GetLocation() + semi.GetLocation());
        }
    };

    Statements[&Lexer::TokenTypes::Break] = [](Parser& p, Lexer::Token& t) -> Statement* {
        auto semi = p.Check(Error::BreakNoSemicolon, &Lexer::TokenTypes::Semicolon);
        return p.arena.Allocate<Break>(t.GetLocation() + semi.GetLocation());
    };

    Statements[&Lexer::TokenTypes::Continue] = [](Parser& p, Lexer::Token& t) -> Statement* {
        auto semi = p.Check(Error::ContinueNoSemicolon, &Lexer::TokenTypes::Semicolon);
        return p.arena.Allocate<Continue>(t.GetLocation() + semi.GetLocation());
    };

    Statements[&Lexer::TokenTypes::Throw] = [](Parser& p, Lexer::Token& t) -> Statement* {
        auto next = p.lex(Error::ThrowNoSemicolon);
        if (next.GetType() == &Lexer::TokenTypes::Semicolon)
            return p.arena.Allocate<Throw>(t.GetLocation() + next.GetLocation());
        p.lex(next);
        auto expr = p.ParseExpression();
        auto semi = p.Check(Error::ThrowNoSemicolon, &Lexer::TokenTypes::Semicolon);
        return p.arena.Allocate<Throw>(t.GetLocation() + semi.GetLocation(), expr);
    };

    Statements[&Lexer::TokenTypes::Try] = [](Parser& p, Lexer::Token& t) -> Statement* {
        auto open = p.Check(Error::TryNoOpenCurly, &Lexer::TokenTypes::OpenCurlyBracket);
        auto stmts = std::vector<Statement*>();
        auto next = p.lex(Error::TryNoOpenCurly);
        while (next.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
            p.lex(next);
            stmts.push_back(p.ParseStatement());
            next = p.lex(Error::TryNoOpenCurly);
        }
        auto compound = p.arena.Allocate<CompoundStatement>(std::move(stmts), open.GetLocation() + t.GetLocation());
        // Catches- there must be at least one.
        auto catches = std::vector<Catch>();
        auto catch_ = p.Check(Error::TryNoCatch, &Lexer::TokenTypes::Catch);
        while (catch_.GetType() == &Lexer::TokenTypes::Catch) {
            p.Check(Error::CatchNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
            next = p.lex(Error::CatchNoOpenBracket);
            auto catch_stmts = std::vector<Statement*>();
            if (next.GetType() == &Lexer::TokenTypes::Ellipsis) {
                p.Check(Error::CatchAllNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
                p.Check(Error::CatchAllNoOpenCurly, &Lexer::TokenTypes::OpenCurlyBracket);
                next = p.lex(Error::CatchNoOpenBracket);
                while (next.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
                    p.lex(next);
                    catch_stmts.push_back(p.ParseStatement());
                    next = p.lex(Error::CatchNoOpenBracket);
                }
                catches.push_back(Catch{ catch_stmts });
                catch_ = p.lex(Error::CatchNoOpenBracket);
                break;
            }
            if (next.GetType() != &Lexer::TokenTypes::Identifier)
                throw p.BadToken(next, Error::CatchNoIdentifier);
            auto name = next.GetValue();
            p.Check(Error::CatchNoVarCreate, &Lexer::TokenTypes::VarCreate);
            auto type = p.ParseExpression();
            p.Check(Error::CatchNoCloseBracket, &Lexer::TokenTypes::CloseBracket);
            p.Check(Error::CatchNoOpenCurly, &Lexer::TokenTypes::OpenCurlyBracket);
            next = p.lex(Error::CatchNoIdentifier);
            while (next.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
                p.lex(next);
                catch_stmts.push_back(p.ParseStatement());
                next = p.lex(Error::CatchNoIdentifier);
            }
            catches.push_back(Catch{ catch_stmts, name, type });
            catch_ = p.lex(Error::CatchNoIdentifier);
        }
        p.lex(catch_);
        return p.arena.Allocate<TryCatch>(compound, catches, t.GetLocation() + next.GetLocation());
    };
    
    TypeTokens[&Lexer::TokenTypes::Public] = [](Parser& p, Type* t, Parse::Access access, Lexer::Token& tok) {
        p.Check(Error::AccessSpecifierNoColon, &Lexer::TokenTypes::Colon);
        return Parse::Access::Public;
    };
    TypeTokens[&Lexer::TokenTypes::Private] = [](Parser& p, Type* t, Parse::Access access, Lexer::Token& tok) {
        p.Check(Error::AccessSpecifierNoColon, &Lexer::TokenTypes::Colon);
        return Parse::Access::Private;
    };
    TypeTokens[&Lexer::TokenTypes::Protected] = [](Parser& p, Type* t, Parse::Access access, Lexer::Token& tok) {
        p.Check(Error::AccessSpecifierNoColon, &Lexer::TokenTypes::Colon);
        return Parse::Access::Protected;
    };
    TypeAttributeTokens[&Lexer::TokenTypes::OpenSquareBracket] = [](Parser& p, Type* t, Parse::Access access, Lexer::Token& tok, std::vector<Attribute> attributes) {
        attributes.push_back(p.ParseAttribute(tok));
        auto next = p.lex(Error::AttributeNoEnd);
        if (p.TypeAttributeTokens.find(next.GetType()) == p.TypeAttributeTokens.end())
            throw p.BadToken(next, Error::AttributeNoEnd);
        return p.TypeAttributeTokens[next.GetType()](p, t, access, next, attributes);
    };

    TypeAttributeTokens[&Lexer::TokenTypes::Dynamic] = [](Parser& p, Type* t, Parse::Access access, Lexer::Token& tok, std::vector<Attribute> attributes) {
        auto intro = p.lex(Error::TypeScopeExpectedIdentifierAfterDynamic);
        if (p.DynamicMemberFunctions.find(intro.GetType()) != p.DynamicMemberFunctions.end()) {
            auto func = p.DynamicMemberFunctions[intro.GetType()](p, t, access, intro, attributes);
            func->dynamic = true;
            return access;
        }
        throw p.BadToken(intro, Error::TypeScopeExpectedIdentifierAfterDynamic);
        return access;
    };

    TypeAttributeTokens[&Lexer::TokenTypes::Identifier] = [](Parser& p, Type* t, Parse::Access access, Lexer::Token& tok, std::vector<Attribute> attributes) {
        auto next = p.lex(Error::TypeScopeExpectedMemberAfterIdentifier);
        if (next.GetType() == &Lexer::TokenTypes::VarCreate) {
            auto init = p.ParseExpression();
            auto semi = p.Check(Error::VariableStatementNoSemicolon, &Lexer::TokenTypes::Semicolon);
            t->variables.push_back(MemberVariable(tok.GetValue(), init, access, tok.GetLocation() + semi.GetLocation(), attributes));
        } else if (next.GetType() == &Lexer::TokenTypes::OpenBracket) {
            auto func = p.ParseFunction(tok, attributes);
            t->functions[tok.GetValue()][access].insert(func);
        } else
            throw p.BadToken(next, Error::TypeScopeExpectedMemberAfterIdentifier);
        return access;
    };
    
    TypeAttributeTokens[&Lexer::TokenTypes::Type] = [](Parser& p, Type* t, Parse::Access access, Lexer::Token& tok, std::vector<Attribute> attributes) {
        p.Check(Error::ConstructorNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        auto con = p.ParseConstructor(tok, attributes);
        t->constructor_decls[access].insert(con);
        return access;
    };

    DynamicMemberFunctions[&Lexer::TokenTypes::Identifier] = [](Parser& p, Type* t, Parse::Access a, Lexer::Token& tok, std::vector<Attribute> attrs) {
        p.Check(Error::TypeScopeExpectedIdentifierAfterDynamic, &Lexer::TokenTypes::OpenBracket);
        auto func = p.ParseFunction(tok, attrs);
        t->functions[tok.GetValue()][a].insert(func);
        return func;
    };

    DynamicMemberFunctions[&Lexer::TokenTypes::Negate] = [](Parser& p, Type* t, Parse::Access a, Lexer::Token& tok, std::vector<Attribute> attrs) {
        auto des = p.ParseDestructor(tok, attrs);
        t->destructor_decl = des;
        return des;
    };

    DynamicMemberFunctions[&Lexer::TokenTypes::Operator] = [](Parser& p, Type* t, Parse::Access a, Lexer::Token& tok, std::vector<Attribute> attrs) -> Function* {
        auto valid_ops = p.ModuleOverloadableOperators;
        valid_ops.insert(p.MemberOverloadableOperators.begin(), p.MemberOverloadableOperators.end());
        auto name = p.ParseOperatorName(valid_ops);
        p.Check(Error::ModuleScopeFunctionNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
        auto func = p.ParseFunction(tok, attrs);
        t->functions[name][a].insert(func);
        return func;
    };
}
OperatorName Parser::ParseOperatorName(std::unordered_set<OperatorName> valid_ops, OperatorName current, OperatorName valid) {
    if (valid_ops.empty())
        return valid;
    auto op = lex(Error::TypeScopeOperatorNoOpenBracket);
    current.push_back(op.GetType());
    auto remaining = GetRemainingValidOperators(valid_ops, current);
    if (remaining.empty()) {
        lex(op);
        return valid;
    }
    auto result = ParseOperatorName(remaining, current, valid);
    if (result == valid) // They did not need our token, so put it back.
        lex(op);
    return valid;
}
OperatorName Parser::ParseOperatorName(std::unordered_set<OperatorName> valid_ops, OperatorName current) {
    auto op = lex(Error::TypeScopeOperatorNoOpenBracket);
    current.push_back(op.GetType());
    auto remaining = GetRemainingValidOperators(valid_ops, current);
    if (valid_ops.find(current) != valid_ops.end())
        return ParseOperatorName(remaining, current, current);
    if (remaining.empty())
        throw BadToken(op, Error::NonOverloadableOperator);
    return ParseOperatorName(remaining, current);
}
OperatorName Parser::ParseOperatorName(std::unordered_set<OperatorName> valid_ops) {
    return ParseOperatorName(valid_ops, OperatorName());
}

std::unordered_set<OperatorName> Parser::GetRemainingValidOperators(std::unordered_set<OperatorName> valid, OperatorName current) {
    std::unordered_set<OperatorName> result;
    for (auto op : valid) {
        if (op.size() > current.size())
            if (std::equal(op.begin(), op.begin() + current.size(), current.begin()))
                result.insert(op);
    }
    return result;
}
std::unordered_set<OperatorName> Parser::GetAllOperators() {
    auto valid = ModuleOverloadableOperators;
    valid.insert(MemberOverloadableOperators.begin(), MemberOverloadableOperators.end());
    return valid;
}
Module* Parser::ParseQualifiedName(Lexer::Token& first, Module* m, Parse::Access a, Parse::Error err) {
    // We have already seen identifier . to enter this method.
    m = CreateModule(first.GetValue(), m, first.GetLocation(), a);
    while (true) {
        auto ident = Check(Error::QualifiedNameNoIdentifier, [](const Lexer::Token& t) { return t.GetType() == &Lexer::TokenTypes::Identifier || t.GetType() == &Lexer::TokenTypes::Operator; });
        if (ident.GetType() == &Lexer::TokenTypes::Operator) return m;
        // If there's a dot, and it was not operator, keep going- else terminate.
        // Don't act on the final identifier or operator.
        auto dot = lex(err);
        if (dot.GetType() != &Lexer::TokenTypes::Dot) {
            lex(dot);
            first = ident;
            return m;
        }
        m = CreateModule(ident.GetValue(), m, ident.GetLocation(), a);
    }
}

Attribute Parser::ParseAttribute(Lexer::Token& tok) {
    auto initialized = ParseExpression();
    Check(Error::AttributeNoVarCreate, &Lexer::TokenTypes::VarCreate);
    auto initializer = ParseExpression();
    auto end = Check(Error::AttributeNoEnd, &Lexer::TokenTypes::CloseSquareBracket);
    return Attribute(initialized, initializer, tok.GetLocation() + end.GetLocation());
}

ParserError Parser::BadToken(const Lexer::Token& first, Parse::Error err) {
    // Put it back so that the recovery functions might match it successfully.
    lex(first);
    // Now throw it.
    return ParserError(first.GetLocation(), lex.GetLastPosition(), err);
}

Lexer::Token Parser::Check(Parse::Error error, Lexer::TokenType tokty) {
    auto t = lex();
    if (!t)
        throw ParserError(lex.GetLastPosition(), error);
    if (t->GetType() != tokty)
        throw BadToken(*t, error);
    return *t;
}

void Parser::ParseGlobalModuleLevelDeclaration(Module* m) {
    // Can only get here if ParseGlobalModuleContents found a token, so we know we have at least one.
    auto t = *lex();
    if (GlobalModuleTokens.find(t.GetType()) != GlobalModuleTokens.end())
        return GlobalModuleTokens[t.GetType()](*this, m, Parse::Access::Public, t);
    if (GlobalModuleAttributeTokens.find(t.GetType()) != GlobalModuleAttributeTokens.end())
        return GlobalModuleAttributeTokens[t.GetType()](*this, m, Parse::Access::Public, t, std::vector<Attribute>());
    throw ParserError(t.GetLocation(), Error::UnrecognizedTokenModuleScope);
}

void Parser::ParseGlobalModuleContents(Module* m) {
    auto t = lex();
    if (t) {
        lex(*t);
        ParseGlobalModuleLevelDeclaration(m);
        return ParseGlobalModuleContents(m);
    }
}
void Parser::ParseModuleContents(Module* m, Lexer::Range first) {
    auto access = Parse::Access::Public;
    while (true) {
        auto t = lex(Error::ModuleRequiresTerminatingCurly);
        if (t.GetType() == &Lexer::TokenTypes::CloseCurlyBracket) {
            outlining(first + t.GetLocation(), OutliningType::Module);
            return;
        }
        lex(t);
        access = ParseModuleLevelDeclaration(m, access);
    }
}
Parse::Access Parser::ParseModuleLevelDeclaration(Module* m, Parse::Access a) {
    auto t = lex(Error::ModuleRequiresTerminatingCurly);
    if (GlobalModuleTokens.find(t.GetType()) != GlobalModuleTokens.end()) {
        GlobalModuleTokens[t.GetType()](*this, m, a, t);
        return a;
    }
    if (GlobalModuleAttributeTokens.find(t.GetType()) != GlobalModuleAttributeTokens.end()) {
        GlobalModuleAttributeTokens[t.GetType()](*this, m, a, t, std::vector<Attribute>());
        return a;
    }
    if (ModuleTokens.find(t.GetType()) != ModuleTokens.end())
        return ModuleTokens[t.GetType()](*this, m, a, t);
    throw ParserError(t.GetLocation(), Error::UnrecognizedTokenModuleScope);
}
Expression* Parser::ParseExpression() {
    return ParseAssignmentExpression();
}
Expression* Parser::ParseAssignmentExpression() {
    auto lhs = ParseUnaryExpression();
    // Somebody's gonna be disappointed because an expression is not a valid end of program.
    // But we don't know who or what they're looking for, so just wait and let them fail.
    // Same strategy for all expression types.
    auto t = lex();
    if (!t) return lhs;
    if (AssignmentOperators.find(t->GetType()) != AssignmentOperators.end())
        return AssignmentOperators[t->GetType()](*this, lhs); 
    lex(*t);
    return ParseSubAssignmentExpression(0, lhs);
}
Expression* Parser::ParsePostfixExpression() {
    auto expr = ParsePrimaryExpression();
    while (true) {
        auto t = lex();
        if (!t) return expr;
        if (PostfixOperators.find(t->GetType()) != PostfixOperators.end()) {
            expr = PostfixOperators[t->GetType()](*this, expr, *t);
            continue;
        }
        // Did not recognize either of these, so put it back and return the final result.
        lex(*t);
        return expr;
    }
}
Expression* Parser::ParseUnaryExpression() {
    // Even if this token is not a unary operator, primary requires at least one token.
    // So just fail right away if there are no more tokens here.
    auto tok = lex(Error::ExpressionNoBeginning);
    if (UnaryOperators.find(tok.GetType()) != UnaryOperators.end())
        return UnaryOperators[tok.GetType()](*this, tok);
    lex(tok);
    return ParsePostfixExpression();
}
Expression* Parser::ParseSubAssignmentExpression(unsigned slot) {
    return ParseSubAssignmentExpression(slot, ParseUnaryExpression());
}
Expression* Parser::ParseSubAssignmentExpression(unsigned slot, Expression* Unary) {
    if (slot == ExpressionPrecedences.size()) return Unary;
    auto lhs = ParseSubAssignmentExpression(slot + 1, Unary);
    while (true) {
        auto t = lex();
        if (!t) return lhs;
        if (ExpressionPrecedences[slot].find(t->GetType()) != ExpressionPrecedences[slot].end()) {
            auto rhs = ParseSubAssignmentExpression(slot + 1);
            lhs = arena.Allocate<BinaryExpression>(lhs, rhs, t->GetType());
            continue;
        }
        lex(*t);
        return lhs;
    }
}
Expression* Parser::ParsePrimaryExpression() {
    // ParseUnaryExpression throws if there is no token available so we should be safe here.
    auto t = lex(Error::ExpressionNoBeginning);
    if (PrimaryExpressions.find(t.GetType()) != PrimaryExpressions.end())
        return PrimaryExpressions[t.GetType()](*this, t);
    throw BadToken(t, Error::ExpressionNoBeginning);
}

std::vector<Expression*> Parser::ParseFunctionArguments() {
    std::vector<Expression*> result;
    auto tok = lex(Error::FunctionArgumentNoBracketOrComma);
    if (tok.GetType() == &Lexer::TokenTypes::CloseBracket)
        return result;
    lex(tok);
    while (true) {
        result.push_back(ParseExpression());
        tok = Check(Error::FunctionArgumentNoBracketOrComma, [](Lexer::Token& tok) { return tok.GetType() == &Lexer::TokenTypes::Comma || tok.GetType() == &Lexer::TokenTypes::CloseBracket; });
        if (tok.GetType() == &Lexer::TokenTypes::CloseBracket)
            break;
    }
    return result;
}
std::vector<Variable*> Parser::ParseLambdaCaptures() {
    std::vector<Variable*> variables;
    auto tok = lex(Error::LambdaNoIntroducer);
    while (true) {
        if (tok.GetType() != &Lexer::TokenTypes::Identifier)
            throw std::runtime_error("Expected identifier to introduce a lambda capture.");
        auto varassign = Check(Error::LambdaNoIntroducer, &Lexer::TokenTypes::VarCreate);
        auto init = ParseExpression();
        variables.push_back(arena.Allocate<Variable>(std::vector<Variable::Name>{{tok.GetValue(), tok.GetLocation() }}, init, tok.GetLocation() + init->location));
        tok = lex(Error::LambdaNoIntroducer);
        if (tok.GetType() == &Lexer::TokenTypes::CloseSquareBracket)
            break;
        else if (tok.GetType() == &Lexer::TokenTypes::Comma)
            tok = lex(Error::LambdaNoIntroducer);
        else
            throw std::runtime_error("Expected , or ] after a lambda capture.");
    }
    return variables;
}
Statement* Parser::ParseStatement() {
    auto t = lex(Error::BreakNoSemicolon);
    if (Statements.find(t.GetType()) != Statements.end())
        return Statements[t.GetType()](*this, t);
    // Else, expression statement.
    lex(t);
    auto expr = ParseExpression();
    Check(Error::ExpressionStatementNoSemicolon, &Lexer::TokenTypes::Semicolon);
    return std::move(expr);
}
std::vector<Expression*> Parser::ParseTypeBases() {
    auto colon = lex(Error::BreakNoSemicolon);
    auto group = std::vector<Expression*>();
    while (colon.GetType() == &Lexer::TokenTypes::Colon) {
        group.push_back(ParseExpression());
        colon = lex(Error::BreakNoSemicolon);
    }
    lex(colon);
    return group;
}
std::vector<FunctionArgument> Parser::ParseFunctionDefinitionArguments() {
    auto ret = std::vector<FunctionArgument>();
    auto t = lex(Error::FunctionArgumentNoBracketOrComma);
    if (t.GetType() == &Lexer::TokenTypes::CloseBracket)
        return ret;
    lex(t);
    // At least one argument.
    // The form is this or this := expr, then t, or t := expr
    bool first = true;
    while (true) {
        auto ident = first
            ? Check(Error::FunctionArgumentNoIdentifierOrThis, [](const Lexer::Token& tok) { return tok.GetType() == &Lexer::TokenTypes::Identifier || tok.GetType() == &Lexer::TokenTypes::This; })
            : Check(Error::FunctionArgumentOnlyFirstThis, &Lexer::TokenTypes::Identifier);
        first = false;
        auto t2 = lex(Error::FunctionArgumentNoBracketOrComma);
        if (t2.GetType() == &Lexer::TokenTypes::CloseBracket) {
            ret.push_back(FunctionArgument(ident.GetLocation(), ident.GetValue()));
            break;
        }
        if (t2.GetType() == &Lexer::TokenTypes::VarCreate) {
            auto type = ParseExpression();
            ret.push_back(FunctionArgument(ident.GetLocation() + type->location, ident.GetValue(), type));
            auto next = lex(Error::FunctionArgumentNoBracketOrComma);
            if (next.GetType() == &Lexer::TokenTypes::Comma)
                continue;
            if (next.GetType() == &Lexer::TokenTypes::CloseBracket)
                break;
            throw BadToken(next, Error::FunctionArgumentNoBracketOrComma);
        }
        if (t2.GetType() == &Lexer::TokenTypes::Comma) {
            ret.push_back({ ident.GetLocation(), ident.GetValue() });
            continue;
        }
        throw BadToken(t2, Error::FunctionArgumentNoBracketOrComma);
    }
    return ret;
}
Type* Parser::ParseTypeDeclaration(Module* m, Lexer::Range loc, Lexer::Token& ident, std::vector<Attribute>& attrs) {
    auto bases = ParseTypeBases();
    auto t = Check(Error::ModuleScopeTypeNoCurlyBrace, [&](Lexer::Token& curr) -> bool {
        if (curr.GetType() == &Lexer::TokenTypes::OpenCurlyBracket)
            return true;
        if (curr.GetType() == &Lexer::TokenTypes::OpenBracket) {
            // Insert an extra lex here so that future code can recover the function.
            lex(curr);
            lex(ident);
            throw ParserError(curr.GetLocation(), lex.GetLastPosition(), Error::ModuleScopeTypeIdentifierButFunction);
        }
        return false;
    });
    auto ty = arena.Allocate<Type>(bases, loc + t.GetLocation(), attrs);
    ParseTypeBody(ty);
    return ty;
}
void Parser::ParseTypeBody(Type* ty) {
    auto loc = lex.GetLastPosition();
    auto t = lex(Error::TypeExpectedBracketAfterIdentifier);
    auto access = Parse::Access::Public;
    while (t.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
        if (TypeTokens.find(t.GetType()) != TypeTokens.end())
            access = TypeTokens[t.GetType()](*this, ty, access, t);
        else if (TypeAttributeTokens.find(t.GetType()) != TypeAttributeTokens.end())
            access = TypeAttributeTokens[t.GetType()](*this, ty, access, t, std::vector<Attribute>());
        else if (DynamicMemberFunctions.find(t.GetType()) != DynamicMemberFunctions.end())
            DynamicMemberFunctions[t.GetType()](*this, ty, access, t, std::vector<Attribute>());
        else
            throw BadToken(t, Error::TypeExpectedBracketAfterIdentifier);
        t = lex(Error::TypeExpectedBracketAfterIdentifier);
    }
    outlining(loc + lex.GetLastPosition(), OutliningType::Type);
}
Function* Parser::ParseFunction(const Lexer::Token& first, std::vector<Attribute> attrs) {
    auto args = ParseFunctionDefinitionArguments();
    // Gotta be := or {
    auto next = Check(Error::FunctionNoCurlyToIntroduceBody, [](Lexer::Token& tok) { return
        tok.GetType() == &Lexer::TokenTypes::OpenCurlyBracket ||
        tok.GetType() == &Lexer::TokenTypes::VarCreate ||
        tok.GetType() == &Lexer::TokenTypes::Default ||
        tok.GetType() == &Lexer::TokenTypes::Delete;
    });
    if (next.GetType() == &Lexer::TokenTypes::Default) {
        auto func = arena.Allocate<Function>(std::vector<Statement*>(), first.GetLocation() + next.GetLocation(), std::move(args), nullptr, attrs);
        func->defaulted = true;
        return func;
    }
    Expression* explicit_return = nullptr;
    if (next.GetType() == &Lexer::TokenTypes::VarCreate) {
        explicit_return = ParseExpression();
        next = Check(Error::FunctionNoCurlyToIntroduceBody, [](Lexer::Token& tok) { return tok.GetType() == &Lexer::TokenTypes::OpenCurlyBracket || tok.GetType() == &Lexer::TokenTypes::Abstract; });
    }
    if (next.GetType() == &Lexer::TokenTypes::Delete) {
        auto func = arena.Allocate<Function>(std::vector<Statement*>(), first.GetLocation() + next.GetLocation(), std::move(args), explicit_return, attrs);
        func->deleted = true;
        return func;
    }
    if (next.GetType() == &Lexer::TokenTypes::Abstract) {
        auto func = arena.Allocate<Function>(std::vector<Statement*>(), first.GetLocation() + next.GetLocation(), std::move(args), explicit_return, attrs);
        func->abstract = true;
        func->dynamic = true; // abstract implies dynamic.
        return func;
    }
    std::vector<Statement*> statements;
    auto t = lex(Error::FunctionNoClosingCurly);
    while (t.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
        lex(t);
        statements.push_back(ParseStatement());
        t = lex(Error::FunctionNoClosingCurly);
    }
    return arena.Allocate<Function>(std::move(statements), first.GetLocation() + t.GetLocation(), std::move(args), explicit_return, attrs);
}
Constructor* Parser::ParseConstructor(const Lexer::Token& first, std::vector<Attribute> attrs) {
    auto args = ParseFunctionDefinitionArguments();
    // Gotta be : or { or default
    auto colon_or_open = Check(Error::FunctionNoCurlyToIntroduceBody, [](Lexer::Token& tok) { return
        tok.GetType() == &Lexer::TokenTypes::OpenCurlyBracket ||
        tok.GetType() == &Lexer::TokenTypes::Colon ||
        tok.GetType() == &Lexer::TokenTypes::Default ||
        tok.GetType() == &Lexer::TokenTypes::Delete;
    });
    if (colon_or_open.GetType() == &Lexer::TokenTypes::Default) {
        auto con = arena.Allocate<Constructor>(std::vector<Statement*>(), first.GetLocation() + colon_or_open.GetLocation(), std::move(args), std::vector<VariableInitializer>(), attrs);
        con->defaulted = true;
        return con;
    }
    if (colon_or_open.GetType() == &Lexer::TokenTypes::Delete) {
        auto con = arena.Allocate<Constructor>(std::vector<Statement*>(), first.GetLocation() + colon_or_open.GetLocation(), std::move(args), std::vector<VariableInitializer>(), attrs);
        con->deleted = true;
        return con;
    }
    std::vector<VariableInitializer> initializers;
    while (colon_or_open.GetType() == &Lexer::TokenTypes::Colon) {
        auto initialized = ParseExpression();
        Check(Error::ConstructorInitializerNoVarCreate, &Lexer::TokenTypes::VarCreate);
        auto initializer = ParseExpression();
        initializers.push_back({ initialized, initializer, colon_or_open.GetLocation() + initializer->location });
        colon_or_open = Check(Error::FunctionNoCurlyToIntroduceBody, [](Lexer::Token& tok) { return tok.GetType() == &Lexer::TokenTypes::OpenCurlyBracket || tok.GetType() == &Lexer::TokenTypes::Colon; });
    }
    // Gotta be { by this point.
    std::vector<Statement*> statements;
    auto t = lex(Error::FunctionNoClosingCurly);
    while (t.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
        lex(t);
        statements.push_back(ParseStatement());
        t = lex(Error::FunctionNoClosingCurly);
    }
    return arena.Allocate<Constructor>(std::move(statements), first.GetLocation() + t.GetLocation(), std::move(args), std::move(initializers), attrs);
}
Destructor* Parser::ParseDestructor(const Lexer::Token& first, std::vector<Attribute> attrs) {
    // ~ type ( ) { stuff }
    Check(Error::DestructorNoType, &Lexer::TokenTypes::Type);
    Check(Error::DestructorNoOpenBracket, &Lexer::TokenTypes::OpenBracket);
    Check(Error::DestructorNoOpenBracket, &Lexer::TokenTypes::CloseBracket);
    auto default_or_open = Check(Error::DestructorNoOpenBracket, [](Lexer::Token& t) { return t.GetType() == &Lexer::TokenTypes::OpenCurlyBracket || t.GetType() == &Lexer::TokenTypes::Default; });
    if (default_or_open.GetType() == &Lexer::TokenTypes::Default) {
        return arena.Allocate<Destructor>(std::vector<Statement*>(), first.GetLocation() + default_or_open.GetLocation(), attrs, true);
    }
    std::vector<Statement*> body;
    auto t = lex(Error::DecltypeNoCloseBracket);
    while (t.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
        lex(t);
        body.push_back(ParseStatement());
        t = lex(Error::DecltypeNoCloseBracket);
    }
    return arena.Allocate<Destructor>(std::move(body), first.GetLocation() + t.GetLocation(), attrs, false);
}

void Parser::AddTypeToModule(Module* m, std::string name, Type* t, Parse::Access specifier) {
    if (m->named_decls.find(name) != m->named_decls.end())
        throw ParserError(t->location, Error::TypeidNoCloseBracket);
    m->named_decls[name] = std::make_pair(specifier, t);
}
void Parser::AddUsingToModule(Module* m, std::string name, Using* u, Parse::Access specifier) {
    if (m->named_decls.find(name) != m->named_decls.end())
        throw ParserError(u->location, Error::TypeidNoCloseBracket);
    m->named_decls[name] = std::make_pair(specifier, u);
}
void Parser::AddFunctionToModule(Module* m, std::string name, Function* f, Parse::Access specifier) {
    if (m->named_decls.find(name) == m->named_decls.end())
        m->named_decls[name] = std::unordered_map<Parse::Access, std::unordered_set<Function*>>();
    auto overset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<Function*>>>(&m->named_decls[name]);
    if (!overset)
        throw ParserError(f->where, Error::TypeExpectedBracketAfterIdentifier);
    (*overset)[specifier].insert(f);
}
void Parser::AddTemplateTypeToModule(Module* m, std::string name, std::vector<FunctionArgument> args, Type* t, Parse::Access specifier) {
    if (m->named_decls.find(name) == m->named_decls.end())
        m->named_decls[name] = std::unordered_map<Parse::Access, std::unordered_set<TemplateType*>>();
    auto overset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<TemplateType*>>>(&m->named_decls[name]);
    if (!overset)
        throw ParserError(t->location, Error::TypeExpectedBracketAfterIdentifier);
    (*overset)[specifier].insert(arena.Allocate<TemplateType>(t, args));
}
Module* Parser::CreateModule(std::string name, Module* m, Lexer::Range where, Parse::Access access) {
    if (m->named_decls.find(name) != m->named_decls.end()) {
        auto mod = boost::get<std::pair<Parse::Access, Module*>>(&m->named_decls[name]);
        if (!mod)
            throw ParserError(where, Error::TypeExpectedBracketAfterIdentifier);
        mod->second->locations.push_back(where);
        return mod->second;
    }
    auto newmod = arena.Allocate<Module>();
    newmod->locations.push_back(where);
    m->named_decls[name] = std::make_pair(access, newmod);
    return newmod;
}