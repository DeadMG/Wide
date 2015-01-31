#include <Wide/Parser/Parser.h>

using namespace Wide;
using namespace Parse;

Lexer::Token PutbackLexer::GetLastToken() {
    return tokens.back();
}

void PutbackLexer::operator()(Lexer::Token arg) {
    tokens.pop_back();
    putbacks.push_back(std::move(arg));
}

Wide::Util::optional<Lexer::Token> PutbackLexer::operator()() {
    if (!putbacks.empty()) {
        auto val = std::move(putbacks.back());
        putbacks.pop_back();
        tokens.push_back(val);
        return val;
    }
    auto val = lex();
    if (val)
        tokens.push_back(*val);
    return std::move(val);
}
Lexer::Token PutbackLexer::operator()(Lexer::TokenType required) {
    auto&& val = (*this)();
    if (!val)
        throw Error(tokens.back(), Wide::Util::none, { required });
    if (val->GetType() != required)
        throw Error(tokens[tokens.size() - 2], *val, { required });
    return *val;
}
Lexer::Token PutbackLexer::operator()(std::unordered_set<Lexer::TokenType> required) {
    auto val = (*this)();
    if (!val)
        throw Error(tokens.back(), Wide::Util::none, required);
    if (required.find(val->GetType()) == required.end())
        throw Error(tokens[tokens.size() - 2], *val, required );
    return *val;
}

Parser::Parser(std::function<Wide::Util::optional<Lexer::Token>()> l)
: lex(l), GlobalModule() 
{
    outlining = [](Lexer::Range r, Parse::OutliningType out) {};

    ModuleOverloadableOperators = std::initializer_list<Parse::OperatorName> {
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

    MemberOverloadableOperators = std::initializer_list<Parse::OperatorName> {
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
    
    for (auto&& ty : { &Lexer::TokenTypes::LeftShiftAssign, &Lexer::TokenTypes::RightShiftAssign, &Lexer::TokenTypes::MulAssign, &Lexer::TokenTypes::PlusAssign, &Lexer::TokenTypes::MinusAssign, 
        &Lexer::TokenTypes::OrAssign, &Lexer::TokenTypes::AndAssign, &Lexer::TokenTypes::XorAssign, &Lexer::TokenTypes::DivAssign, &Lexer::TokenTypes::ModAssign, &Lexer::TokenTypes::Assignment }) {
        AssignmentOperators[ty] = [ty](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> lhs) {
            return Wide::Memory::MakeUnique<BinaryExpression>(std::move(lhs), p.ParseAssignmentExpression(imp), ty);
        };
    }

    for (auto&& op : { &Lexer::TokenTypes::Star, &Lexer::TokenTypes::Negate, &Lexer::TokenTypes::Increment, &Lexer::TokenTypes::Decrement, &Lexer::TokenTypes::And }) {
        UnaryOperators[op] = [op](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& token) {
            auto&& subexpr = p.ParseUnaryExpression(imp);
            return Wide::Memory::MakeUnique<UnaryExpression>(std::move(subexpr), op, token.GetLocation() + subexpr->location);
        };
    }

    ModuleTokens[&Lexer::TokenTypes::Private] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& token) {
        p.lex(&Lexer::TokenTypes::Colon);
        return Parse::Access::Private;
    };

    ModuleTokens[&Lexer::TokenTypes::Public] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& token) {
        p.lex(&Lexer::TokenTypes::Colon);
        return Parse::Access::Public;
    };
    
    GlobalModuleTokens[&Lexer::TokenTypes::Import] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& token) {
        // import y;
        // import y hiding x, y, z;
        auto&& expr = p.ParseExpression(imp);
        auto&& semi = p.lex({ &Lexer::TokenTypes::Semicolon, &Lexer::TokenTypes::Hiding });
        std::vector<Parse::Name> hidings;
        if (semi.GetType() == &Lexer::TokenTypes::Semicolon)
            return std::make_shared<Import>(std::move(expr), std::vector<Parse::Name>(), std::move(imp), hidings);
        // Hiding
        while (true) {
            Parse::Name name;
            auto&& lead = p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator });
            if (lead.GetType() == &Lexer::TokenTypes::Operator)
                name = p.ParseOperatorName(p.GetAllOperators());
            else
                name = lead.GetValue();
            hidings.push_back(name);
            auto&& next = p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::Semicolon });
            if (next.GetType() == &Lexer::TokenTypes::Semicolon)
                return std::make_shared<Import>(std::move(expr), std::vector<Parse::Name>(), imp, hidings);
        }
    };

    GlobalModuleTokens[&Lexer::TokenTypes::From] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& token) {
        // from x import y, z;
        auto&& expr = p.ParseExpression(imp);
        p.lex(&Lexer::TokenTypes::Import);
        // Import only these
        std::vector<Parse::Name> names;
        while (true) {
            Parse::Name name;
            auto&& lead = p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator });
            if (lead.GetType() == &Lexer::TokenTypes::Operator)
                name = p.ParseOperatorName(p.GetAllOperators());
            else
                name = lead.GetValue();
            names.push_back(name);
            auto&& next = p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::Semicolon });
            if (next.GetType() == &Lexer::TokenTypes::Semicolon)
                return std::make_shared<Import>(std::move(expr), names, imp, std::vector<Parse::Name>());
        }
    };

    GlobalModuleTokens[&Lexer::TokenTypes::Module] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& module) {
        auto&& ident = p.lex(&Lexer::TokenTypes::Identifier);
        auto&& maybedot = p.lex({ &Lexer::TokenTypes::Dot, &Lexer::TokenTypes::OpenCurlyBracket });
        if (maybedot.GetType() == &Lexer::TokenTypes::Dot)
            m = p.ParseQualifiedName(ident, m, a, { &Lexer::TokenTypes::Identifier }, { &Lexer::TokenTypes::OpenCurlyBracket });
        else
            p.lex(maybedot);
        auto&& curly = p.lex(&Lexer::TokenTypes::OpenCurlyBracket);
        auto&& mod = p.CreateModule(ident.GetValue(), m, module.GetLocation() + curly.GetLocation(), a);
        p.ParseModuleContents(mod, curly.GetLocation(), imp);
        return imp;
    };

    GlobalModuleTokens[&Lexer::TokenTypes::Template] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& templat) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& args = p.ParseFunctionDefinitionArguments(imp);
        auto&& attrs = std::vector<Attribute>();
        auto&& token = p.lex({ &Lexer::TokenTypes::OpenSquareBracket, &Lexer::TokenTypes::Type });
        while (token.GetType() == &Lexer::TokenTypes::OpenSquareBracket) {
            attrs.push_back(p.ParseAttribute(token, imp));
            token = p.lex({ &Lexer::TokenTypes::OpenSquareBracket, &Lexer::TokenTypes::Type });
        }
        auto&& ident = p.lex(&Lexer::TokenTypes::Identifier);
        auto&& ty = p.ParseTypeDeclaration(templat.GetLocation(), imp, ident, std::move(attrs));
        p.AddTemplateTypeToModule(m, ident.GetValue(), templat.GetLocation() + ty->GetLocation(), std::move(args), std::move(ty), a);
        return imp;
    };

    GlobalModuleTokens[&Lexer::TokenTypes::Using] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& token) {
        auto&& useloc = p.lex.GetLastToken().GetLocation();
        auto&& t = p.lex(&Lexer::TokenTypes::Identifier);
        auto&& var = p.lex(&Lexer::TokenTypes::VarCreate);
        auto&& expr = p.ParseExpression(imp);
        auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
        auto&& use = std::make_shared<Using>(std::move(expr), token.GetLocation() + semi.GetLocation());
        p.AddUsingToModule(m, t.GetValue(), std::move(use), a);
        return imp;
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::OpenSquareBracket] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& token, std::vector<Attribute> attributes) {
        // Another attribute, just add it to the list.
        attributes.push_back(p.ParseAttribute(token, imp));
        auto&& next = p.lex(GetExpectedTokenTypesFromMap(p.GlobalModuleAttributeTokens));
        return p.GlobalModuleAttributeTokens[next.GetType()](p, m, a, imp, next, std::move(attributes));
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Dot] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& token, std::vector<Attribute> attributes) {
        auto&& next = p.lex(&Lexer::TokenTypes::Identifier);
        return p.GlobalModuleAttributeTokens[&Lexer::TokenTypes::Identifier](p, m, a, imp, next, std::move(attributes));
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Identifier] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& ident, std::vector<Attribute> attributes) {
        auto&& maybedot = p.lex({ &Lexer::TokenTypes::Dot, &Lexer::TokenTypes::OpenBracket });
        if (maybedot.GetType() == &Lexer::TokenTypes::Dot)
            m = p.ParseQualifiedName(ident, m, a, { &Lexer::TokenTypes::Operator, &Lexer::TokenTypes::Identifier }, { &Lexer::TokenTypes::OpenBracket });
        else
            p.lex(maybedot);
        if (ident.GetType() == &Lexer::TokenTypes::Identifier) {
            auto&& t = p.lex(&Lexer::TokenTypes::OpenBracket);
            auto&& func = p.ParseFunction(ident, imp, std::move(attributes));
            p.AddFunctionToModule(m, ident.GetValue(), std::move(func), a);
            return;
        }
        auto&& name = p.ParseOperatorName(p.ModuleOverloadableOperators);
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& func = p.ParseFunction(ident, imp, std::move(attributes));
        m->OperatorOverloads[name][a].insert(std::move(func));
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Type] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& typ, std::vector<Attribute> attributes) {
        // Could be exported constructor.
        auto&& next = p.lex({ &Lexer::TokenTypes::OpenBracket, &Lexer::TokenTypes::Identifier });
        if (next.GetType() == &Lexer::TokenTypes::OpenBracket) {
            auto&& func = p.ParseConstructor(typ, imp, std::move(attributes));
            m->constructor_decls.insert(std::move(func));
            return;
        }
        auto&& ty = p.ParseTypeDeclaration(typ.GetLocation(), imp, next, std::move(attributes));
        p.AddTypeToModule(m, next.GetValue(), std::move(ty), a);
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Operator] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import>imp, Lexer::Token& tok, std::vector<Attribute> attrs) {
        auto&& name = p.ParseOperatorName(p.ModuleOverloadableOperators);
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& func = p.ParseFunction(tok, imp, std::move(attrs));
        m->OperatorOverloads[name][a].insert(std::move(func));
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Negate] = [](Parser& p, Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& token, std::vector<Attribute> attributes) {
        m->destructor_decls.insert(p.ParseDestructor(token, imp, std::move(attributes)));
    };
    
    PostfixOperators[&Lexer::TokenTypes::OpenSquareBracket] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) {
        auto&& index = p.ParseExpression(imp);
        auto&& close = p.lex(&Lexer::TokenTypes::CloseSquareBracket);
        return Wide::Memory::MakeUnique<Index>(std::move(e), std::move(index), e->location + close.GetLocation());
    };

    PostfixOperators[&Lexer::TokenTypes::Dot] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) -> std::unique_ptr<Expression> {
        auto&& t = p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator, &Lexer::TokenTypes::Negate });
        if (t.GetType() == &Lexer::TokenTypes::Identifier)
            return Wide::Memory::MakeUnique<MemberAccess>(t.GetValue(), std::move(e), e->location + t.GetLocation(), t.GetLocation());
        if (t.GetType() == &Lexer::TokenTypes::Operator)
            return Wide::Memory::MakeUnique<MemberAccess>(p.ParseOperatorName(p.GetAllOperators()), std::move(e), e->location + t.GetLocation(), t.GetLocation());
        auto&& typ = p.lex(&Lexer::TokenTypes::Type);
        auto&& open = p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& close = p.lex(&Lexer::TokenTypes::CloseBracket);
        return Wide::Memory::MakeUnique<DestructorAccess>(std::move(e), e->location + close.GetLocation());
    };
    
    PostfixOperators[&Lexer::TokenTypes::PointerAccess] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) -> std::unique_ptr<Expression> {
        auto&& t = p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator, &Lexer::TokenTypes::Negate });
        if (t.GetType() == &Lexer::TokenTypes::Identifier)
            return Wide::Memory::MakeUnique<PointerMemberAccess>(t.GetValue(), std::move(e), e->location + t.GetLocation(), t.GetLocation());
        if (t.GetType() == &Lexer::TokenTypes::Operator)
            return Wide::Memory::MakeUnique<MemberAccess>(p.ParseOperatorName(p.GetAllOperators()), std::move(e), e->location + t.GetLocation(), t.GetLocation());
        auto&& typ = p.lex(&Lexer::TokenTypes::Type);
        return Wide::Memory::MakeUnique<PointerDestructorAccess>(std::move(e), e->location + typ.GetLocation());
    };

    PostfixOperators[&Lexer::TokenTypes::Increment] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) {
        return Wide::Memory::MakeUnique<Increment>(std::move(e), e->location + token.GetLocation(), true);
    };

    PostfixOperators[&Lexer::TokenTypes::Decrement] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) {
        return Wide::Memory::MakeUnique<Decrement>(std::move(e), e->location + token.GetLocation(), true);
    };

    PostfixOperators[&Lexer::TokenTypes::OpenBracket] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) {
        auto&& args = p.ParseFunctionArguments(imp);
        return Wide::Memory::MakeUnique<FunctionCall>(std::move(e), std::move(args), e->location + p.lex.GetLastToken().GetLocation());
    };

    PostfixOperators[&Lexer::TokenTypes::QuestionMark] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) {
        return Wide::Memory::MakeUnique<BooleanTest>(std::move(e), e->location + token.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::OpenCurlyBracket] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        std::vector<std::unique_ptr<Expression>> exprs;
        auto&& expected = p.GetExpressionBeginnings();
        expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);        
        auto&& terminator = p.lex(expected);
        while (terminator.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
            p.lex(terminator);
            exprs.push_back(p.ParseExpression(imp));
            terminator = p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseCurlyBracket });
            if (terminator.GetType() == &Lexer::TokenTypes::Comma)
                terminator = p.lex(p.GetExpressionBeginnings());
        }
        return Wide::Memory::MakeUnique<Tuple>(std::move(exprs), t.GetLocation() + terminator.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::String] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        return Wide::Memory::MakeUnique<String>(t.GetValue(), t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Integer] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        return Wide::Memory::MakeUnique<Integer>(t.GetValue(), t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::This] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        return Wide::Memory::MakeUnique<This>(t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::True] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        return Wide::Memory::MakeUnique<True>(t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::False] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        return Wide::Memory::MakeUnique<False>(t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Operator] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        return Wide::Memory::MakeUnique<Identifier>(p.ParseOperatorName(p.GetAllOperators()), imp, t.GetLocation() + p.lex.GetLastToken().GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Decltype] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& expr = p.ParseExpression(imp);
        auto&& close = p.lex(&Lexer::TokenTypes::CloseBracket);
        return Wide::Memory::MakeUnique<Decltype>(std::move(expr), t.GetLocation() + close.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Typeid] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& expr = p.ParseExpression(imp);
        auto&& close = p.lex(&Lexer::TokenTypes::CloseBracket);
        return Wide::Memory::MakeUnique<Typeid>(std::move(expr), t.GetLocation() + close.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::DynamicCast] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& expr1 = p.ParseExpression(imp);
        p.lex(&Lexer::TokenTypes::Comma);
        auto&& expr2 = p.ParseExpression(imp);
        auto&& close = p.lex(&Lexer::TokenTypes::CloseBracket);
        return Wide::Memory::MakeUnique<DynamicCast>(std::move(expr1), std::move(expr2), t.GetLocation() + close.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Identifier] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) -> std::unique_ptr<Expression> {
        auto&& maybe_lambda = p.lex();
        if (!maybe_lambda || maybe_lambda->GetType() != &Lexer::TokenTypes::Lambda) {
            if (maybe_lambda) p.lex(*maybe_lambda);
            return Wide::Memory::MakeUnique<Identifier>(t.GetValue(), imp, t.GetLocation());
        }
        auto&& expr = p.ParseExpression(imp);
        std::vector<std::unique_ptr<Statement>> stmts;
        stmts.push_back({ Wide::Memory::MakeUnique<Return>(std::move(expr), expr->location) });
        std::vector<FunctionArgument> args;
        args.push_back({ t.GetLocation(), t.GetValue(), nullptr, nullptr });
        return Wide::Memory::MakeUnique<Lambda>(std::move(stmts), std::move(args), t.GetLocation() + expr->location, false, std::vector<Variable>());
    };

    PrimaryExpressions[&Lexer::TokenTypes::OpenBracket] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) -> std::unique_ptr<Expression> {
        auto&& expected = p.GetExpressionBeginnings();
        assert(expected.find(&Lexer::TokenTypes::CloseBracket) == expected.end() && "Defined extension that used ) to begin an expression, which would be ambiguous.");
        expected.insert(&Lexer::TokenTypes::CloseBracket);
        auto&& tok = p.lex(expected);
        if (tok.GetType() == &Lexer::TokenTypes::CloseBracket) {
            p.lex(&Lexer::TokenTypes::Lambda);
            auto&& expr = p.ParseExpression(imp);
            std::vector<std::unique_ptr<Statement>> stmts;
            stmts.push_back({ Wide::Memory::MakeUnique<Return>(std::move(expr), expr->location) });
            return Wide::Memory::MakeUnique<Lambda>(std::move(stmts), std::vector<FunctionArgument>(), t.GetLocation() + expr->location, false, std::vector<Variable>());
        }
        p.lex(tok);
        auto&& expr = p.ParseExpression(imp);
        p.lex(&Lexer::TokenTypes::CloseBracket);
        return std::move(expr);
    };

    PrimaryExpressions[&Lexer::TokenTypes::Function] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) -> std::unique_ptr<Expression> {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& args = p.ParseFunctionDefinitionArguments(imp);
        auto&& pos = t.GetLocation();
        auto&& grp = std::vector<std::unique_ptr<Statement>>();
        auto&& caps = std::vector<Variable>();
        bool defaultref = false;
        auto&& tok = p.lex({ &Lexer::TokenTypes::OpenSquareBracket, &Lexer::TokenTypes::OpenCurlyBracket });
        if (tok.GetType() == &Lexer::TokenTypes::OpenSquareBracket) {
            auto&& opensquare = tok.GetLocation();
            tok = p.lex({ &Lexer::TokenTypes::And, &Lexer::TokenTypes::Identifier });
            if (tok.GetType() == &Lexer::TokenTypes::And) {
                defaultref = true;
                tok = p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseSquareBracket });
                if (tok.GetType() == &Lexer::TokenTypes::Comma)
                    caps = p.ParseLambdaCaptures(imp);
                tok = p.lex(&Lexer::TokenTypes::OpenCurlyBracket);
            } else {
                p.lex(tok);
                caps = p.ParseLambdaCaptures(imp);
                tok = p.lex(&Lexer::TokenTypes::OpenCurlyBracket);
            }
        }
        auto&& opencurly = tok.GetLocation();
        auto&& expected = p.GetStatementBeginnings();
        expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        tok = p.lex(expected);
        while (tok.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
            p.lex(tok);
            grp.push_back(p.ParseStatement(imp));
            tok = p.lex(expected);
        }
        return Wide::Memory::MakeUnique<Lambda>(std::move(grp), std::move(args), pos + tok.GetLocation(), defaultref, std::move(caps));
    };

    PrimaryExpressions[&Lexer::TokenTypes::Dot] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) -> std::unique_ptr<Expression> {
        // May be in the form ".t", say.
        auto&& next = p.lex();
        if (!next)
            return Wide::Memory::MakeUnique<GlobalModuleReference>(t.GetLocation());
        if (next->GetType() == &Lexer::TokenTypes::Identifier)
            return Wide::Memory::MakeUnique<MemberAccess>(next->GetValue(), Wide::Memory::MakeUnique<GlobalModuleReference>(t.GetLocation()), t.GetLocation() + next->GetLocation(), next->GetLocation());
        if (next->GetType() == &Lexer::TokenTypes::Operator)
            return Wide::Memory::MakeUnique<MemberAccess>(p.ParseOperatorName(p.GetAllOperators()), Wide::Memory::MakeUnique<GlobalModuleReference>(t.GetLocation()), t.GetLocation() + p.lex.GetLastToken().GetLocation(), next->GetLocation() + p.lex.GetLastToken().GetLocation());
        p.lex(*next);
        return Wide::Memory::MakeUnique<GlobalModuleReference>(t.GetLocation());
    };

    PrimaryExpressions[&Lexer::TokenTypes::Type] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) -> std::unique_ptr<Expression> {
        auto&& bases = p.ParseTypeBases(imp);
        auto&& ty = Wide::Memory::MakeUnique<Type>(std::move(bases), p.lex(&Lexer::TokenTypes::OpenCurlyBracket).GetLocation(), std::vector<Attribute>());
        p.ParseTypeBody(ty.get(), imp);
        return std::move(ty);
    };

    Statements[&Lexer::TokenTypes::Return] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        auto&& expected = p.GetExpressionBeginnings();
        expected.insert(&Lexer::TokenTypes::Semicolon);
        auto&& next = p.lex(expected); // Check next token for ;
        if (next.GetType() == &Lexer::TokenTypes::Semicolon)
            return Wide::Memory::MakeUnique<Return>(t.GetLocation() + next.GetLocation());
        // If it wasn't ; then expect expression.
        p.lex(next);
        auto&& expr = p.ParseExpression(imp);
        next = p.lex(&Lexer::TokenTypes::Semicolon);
        return Wide::Memory::MakeUnique<Return>(std::move(expr), t.GetLocation() + next.GetLocation());
    };

    Statements[&Lexer::TokenTypes::If] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        // Check for variable conditions
        auto&& else_expected = p.GetStatementBeginnings();
        else_expected.insert(&Lexer::TokenTypes::Else);
        else_expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        auto&& ident = p.lex(p.GetExpressionBeginnings());
        if (ident.GetType() == &Lexer::TokenTypes::Identifier) {
            auto&& expected = p.GetIdentifierFollowups();
            expected.insert(&Lexer::TokenTypes::VarCreate);
            expected.insert(&Lexer::TokenTypes::CloseBracket);
            auto&& var = p.lex(expected);
            if (var.GetType() == &Lexer::TokenTypes::VarCreate) {
                auto&& expr = p.ParseExpression(imp);
                auto&& variable = Wide::Memory::MakeUnique<Variable>(std::vector<Variable::Name>{{ ident.GetValue(), t.GetLocation() + expr->location }}, std::move(expr), ident.GetLocation(), nullptr);
                p.lex(&Lexer::TokenTypes::CloseBracket);
                auto&& body = p.ParseStatement(imp);
                auto&& next = p.lex(else_expected);
                if (next.GetType() == &Lexer::TokenTypes::Else) {
                    auto&& else_br = p.ParseStatement(imp);
                    return Wide::Memory::MakeUnique<If>(std::move(variable), std::move(body), std::move(else_br), t.GetLocation() + body->location);
                }
                p.lex(next);
                return Wide::Memory::MakeUnique<If>(std::move(variable), std::move(body), nullptr, t.GetLocation() + body->location);
            }
            p.lex(var);
        }
        p.lex(ident);
        auto&& cond = p.ParseExpression(imp);
        p.lex(&Lexer::TokenTypes::CloseBracket);
        auto&& true_br = p.ParseStatement(imp);
        auto&& next = p.lex(else_expected);
        if (next.GetType() == &Lexer::TokenTypes::Else) {
            auto&& else_br = p.ParseStatement(imp);
            return Wide::Memory::MakeUnique<If>(std::move(cond), std::move(true_br), std::move(else_br), t.GetLocation() + else_br->location);
        }
        p.lex(next);
        return Wide::Memory::MakeUnique<If>(std::move(cond), std::move(true_br), nullptr, t.GetLocation() + true_br->location);
    };

    Statements[&Lexer::TokenTypes::OpenCurlyBracket] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        auto&& pos = t.GetLocation();
        auto&& expected = p.GetStatementBeginnings();
        expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        auto&& next = p.lex(expected);
        std::vector<std::unique_ptr<Statement>> stmts;
        while (next.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
            p.lex(next);
            stmts.push_back(p.ParseStatement(imp));
            next = p.lex(expected);
        }
        return Wide::Memory::MakeUnique<CompoundStatement>(std::move(stmts), pos + t.GetLocation());
    };

    Statements[&Lexer::TokenTypes::While] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        // Check for variable conditions.
        auto&& ident = p.lex(p.GetExpressionBeginnings());
        if (ident.GetType() == &Lexer::TokenTypes::Identifier) {
            auto&& expected = p.GetIdentifierFollowups();
            expected.insert(&Lexer::TokenTypes::VarCreate);
            expected.insert(&Lexer::TokenTypes::CloseBracket);
            auto&& var = p.lex(expected);
            if (var.GetType() == &Lexer::TokenTypes::VarCreate) {
                auto&& expr = p.ParseExpression(imp);
                auto&& variable = Wide::Memory::MakeUnique<Variable>(std::vector<Variable::Name>{{ ident.GetValue(), t.GetLocation() + expr->location }}, std::move(expr), ident.GetLocation(), nullptr);
                p.lex(&Lexer::TokenTypes::CloseBracket);
                auto&& body = p.ParseStatement(imp);
                return Wide::Memory::MakeUnique<While>(std::move(body), std::move(variable), t.GetLocation() + body->location);
            }
            p.lex(var);
        }
        p.lex(ident);
        auto&& cond = p.ParseExpression(imp);
        p.lex(&Lexer::TokenTypes::CloseBracket);
        auto&& body = p.ParseStatement(imp);
        return Wide::Memory::MakeUnique<While>(std::move(body), std::move(cond), t.GetLocation() + body->location);
    };

    Statements[&Lexer::TokenTypes::Identifier] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t)-> std::unique_ptr<Statement> {
        std::vector<Parse::Variable::Name> names = { { t.GetValue(), t.GetLocation() } };
        auto&& expected = p.GetIdentifierFollowups();
        expected.insert(&Lexer::TokenTypes::VarCreate);
        expected.insert(&Lexer::TokenTypes::Comma);
        expected.insert(&Lexer::TokenTypes::Colon);
        auto&& next = p.lex(expected);
        if (next.GetType() != &Lexer::TokenTypes::VarCreate && next.GetType() != &Lexer::TokenTypes::Comma && next.GetType() != &Lexer::TokenTypes::Colon) {
            p.lex(next);
            p.lex(t);
            auto&& expr = p.ParseExpression(imp);
            p.lex(&Lexer::TokenTypes::Semicolon);
            return std::move(expr);
        } else {
            while (next.GetType() == &Lexer::TokenTypes::Comma) {
                auto&& ident = p.lex(&Lexer::TokenTypes::Identifier);
                names.push_back({ ident.GetValue(), ident.GetLocation() });
                next = p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::VarCreate, &Lexer::TokenTypes::Colon });
            }
            std::unique_ptr<Expression> type;
            if (next.GetType() == &Lexer::TokenTypes::Colon) {
                type = p.ParseExpression(imp);
                p.lex(&Lexer::TokenTypes::VarCreate);
            }
            auto&& init = p.ParseExpression(imp);
            auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
            return Wide::Memory::MakeUnique<Variable>(std::move(names), std::move(init), t.GetLocation() + semi.GetLocation(), std::move(type));
        }
    };

    Statements[&Lexer::TokenTypes::Break] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
        return Wide::Memory::MakeUnique<Break>(t.GetLocation() + semi.GetLocation());
    };

    Statements[&Lexer::TokenTypes::Continue] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t)  {
        auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
        return Wide::Memory::MakeUnique<Continue>(t.GetLocation() + semi.GetLocation());
    };

    Statements[&Lexer::TokenTypes::Throw] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        auto&& expected = p.GetExpressionBeginnings();
        expected.insert(&Lexer::TokenTypes::Semicolon);
        auto&& next = p.lex(expected);
        if (next.GetType() == &Lexer::TokenTypes::Semicolon)
            return Wide::Memory::MakeUnique<Throw>(t.GetLocation() + next.GetLocation());
        p.lex(next);
        auto&& expr = p.ParseExpression(imp);
        auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
        return Wide::Memory::MakeUnique<Throw>(t.GetLocation() + semi.GetLocation(), std::move(expr));
    };

    Statements[&Lexer::TokenTypes::Try] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        auto&& open = p.lex(&Lexer::TokenTypes::OpenCurlyBracket);
        auto&& stmts = std::vector<std::unique_ptr<Statement>>();
        auto&& expected = p.GetStatementBeginnings();
        expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        auto&& catch_expected = p.GetStatementBeginnings();
        catch_expected.insert(&Lexer::TokenTypes::Catch);
        // Could also be end-of-scope next.
        catch_expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        auto&& next = p.lex(expected);
        while (next.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
            p.lex(next);
            stmts.push_back(p.ParseStatement(imp));
            next = p.lex(expected);
        }
        auto&& compound = Wide::Memory::MakeUnique<CompoundStatement>(std::move(stmts), open.GetLocation() + t.GetLocation());
        // Catches- there must be at least one.
        auto&& catches = std::vector<Catch>();
        auto&& catch_ = p.lex(&Lexer::TokenTypes::Catch);
        while (catch_.GetType() == &Lexer::TokenTypes::Catch) {
            p.lex(&Lexer::TokenTypes::OpenBracket);
            next = p.lex({ &Lexer::TokenTypes::Ellipsis, &Lexer::TokenTypes::Identifier });
            auto&& catch_stmts = std::vector<std::unique_ptr<Statement>>();
            if (next.GetType() == &Lexer::TokenTypes::Ellipsis) {
                p.lex(&Lexer::TokenTypes::CloseBracket);
                p.lex(&Lexer::TokenTypes::OpenCurlyBracket);
                next = p.lex(expected);
                while (next.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
                    p.lex(next);
                    catch_stmts.push_back(p.ParseStatement(imp));
                    next = p.lex(expected);
                }
                catches.push_back(Catch{ std::move(catch_stmts) });
                catch_ = p.lex(catch_expected);
                break;
            }
            auto&& name = next.GetValue();
            p.lex(&Lexer::TokenTypes::Colon);
            auto&& type = p.ParseExpression(imp);
            p.lex(&Lexer::TokenTypes::CloseBracket);
            p.lex(&Lexer::TokenTypes::OpenCurlyBracket);
            next = p.lex(expected);
            while (next.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
                p.lex(next);
                catch_stmts.push_back(p.ParseStatement(imp));
                next = p.lex(expected);
            }
            catches.push_back(Catch{ std::move(catch_stmts), name, std::move(type) });
            catch_ = p.lex(catch_expected);
        }
        p.lex(catch_);
        return Wide::Memory::MakeUnique<TryCatch>(std::move(compound), std::move(catches), t.GetLocation() + next.GetLocation());
    };
    
    TypeTokens[&Lexer::TokenTypes::Public] = [](Parser& p, Type* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        p.lex(&Lexer::TokenTypes::Colon);
        return Parse::Access::Public;
    };
    TypeTokens[&Lexer::TokenTypes::Private] = [](Parser& p, Type* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        p.lex(&Lexer::TokenTypes::Colon);
        return Parse::Access::Private;
    };
    TypeTokens[&Lexer::TokenTypes::Protected] = [](Parser& p, Type* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        p.lex(&Lexer::TokenTypes::Colon);
        return Parse::Access::Protected;
    };
    TypeTokens[&Lexer::TokenTypes::Using] = [](Parser& p, Type* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        auto&& ident = p.lex(&Lexer::TokenTypes::Identifier);
        p.lex(&Lexer::TokenTypes::VarCreate);
        auto&& expr = p.ParseExpression(imp);
        auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
        auto&& use = Wide::Memory::MakeUnique<Using>(std::move(expr), tok.GetLocation() + semi.GetLocation());
        if (t->nonvariables.find(ident.GetValue()) != t->nonvariables.end())
            throw std::runtime_error("Found using, but there was already an overload set there.");
        t->nonvariables[ident.GetValue()] = std::make_pair(access, std::move(use));
        return access;
    };

    TypeTokens[&Lexer::TokenTypes::From] = [](Parser& p, Type* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        auto&& expr = p.ParseExpression(imp);
        p.lex(&Lexer::TokenTypes::Import);
        std::vector<Parse::Name> names;
        bool constructors = false;
        while (true) {
            auto&& lead = p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator, &Lexer::TokenTypes::Type });
            if (lead.GetType() == &Lexer::TokenTypes::Operator) {
                names.push_back(p.ParseOperatorName(p.GetAllOperators()));
            } else if (lead.GetType() == &Lexer::TokenTypes::Identifier){
                names.push_back(lead.GetValue());
            } else
                constructors = true;
            auto&& next = p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::Semicolon });
            if (next.GetType() == &Lexer::TokenTypes::Semicolon) {
                t->imports.push_back(std::make_tuple(std::move(expr), names, constructors));
                return access;
            }
        }
    };

    TypeAttributeTokens[&Lexer::TokenTypes::OpenSquareBracket] = [](Parser& p, Type* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attributes) {
        attributes.push_back(p.ParseAttribute(tok, imp));
        auto&& next = p.lex(GetExpectedTokenTypesFromMap(p.TypeAttributeTokens));
        return p.TypeAttributeTokens[next.GetType()](p, t, access, imp, next, std::move(attributes));
    };

    TypeAttributeTokens[&Lexer::TokenTypes::Dynamic] = [](Parser& p, Type* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attributes) {
        auto&& intro = p.lex(GetExpectedTokenTypesFromMap(p.DynamicMemberFunctions));
        auto&& func = p.DynamicMemberFunctions[intro.GetType()](p, t, access, imp, intro, std::move(attributes));
        func->dynamic = true;
        return access;
    };

    TypeAttributeTokens[&Lexer::TokenTypes::Identifier] = [](Parser& p, Type* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attributes) {
        auto&& next = p.lex({ &Lexer::TokenTypes::VarCreate, &Lexer::TokenTypes::Colon, &Lexer::TokenTypes::OpenBracket });
        if (next.GetType() == &Lexer::TokenTypes::VarCreate) {
            auto&& init = p.ParseExpression(imp);
            auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
            t->variables.push_back(MemberVariable(tok.GetValue(), std::move(init), access, tok.GetLocation() + semi.GetLocation(), std::move(attributes), nullptr));
        } else if (next.GetType() == &Lexer::TokenTypes::Colon) {
            auto&& type = p.ParseExpression(imp);
            auto&& ass = p.lex({ &Lexer::TokenTypes::Assignment, &Lexer::TokenTypes::Semicolon });
            if (ass.GetType() == &Lexer::TokenTypes::Semicolon) {
                t->variables.push_back(MemberVariable(tok.GetValue(), nullptr, access, tok.GetLocation() + ass.GetLocation(), std::move(attributes), std::move(type)));
                return access;
            }
            auto&& init = p.ParseExpression(imp);
            auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
            t->variables.push_back(MemberVariable(tok.GetValue(), std::move(init), access, tok.GetLocation() + semi.GetLocation(), std::move(attributes), std::move(type)));
        } else {
            auto&& func = p.ParseFunction(tok, imp, std::move(attributes));
            auto&& overset = boost::get<OverloadSet<std::unique_ptr<Function>>>(&t->nonvariables[tok.GetValue()]);
            if (!overset) throw std::runtime_error("Found overload set but there was already a property by that name.");
            (*overset)[access].insert(std::move(func));
        }
        return access;
    };
    
    TypeAttributeTokens[&Lexer::TokenTypes::Type] = [](Parser& p, Type* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attributes) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& con = p.ParseConstructor(tok, imp, std::move(attributes));
        t->constructor_decls[access].insert(std::move(con));
        return access;
    };

    TypeAttributeTokens[&Lexer::TokenTypes::Operator] = [](Parser& p, Type* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) {
        auto&& valid_ops = p.ModuleOverloadableOperators;
        valid_ops.insert(p.MemberOverloadableOperators.begin(), p.MemberOverloadableOperators.end());
        auto&& name = p.ParseOperatorName(valid_ops);
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& func = p.ParseFunction(tok, imp, std::move(attrs));
        auto&& overset = boost::get<OverloadSet<std::unique_ptr<Function>>>(&t->nonvariables[name]);
        if (!overset) throw std::runtime_error("Found overload set but there was already a property by that name.");
        (*overset)[a].insert(std::move(func));
        return a;
    };

    TypeAttributeTokens[&Lexer::TokenTypes::Negate] = [](Parser& p, Type* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) {
        t->destructor_decl = p.ParseDestructor(tok, imp, std::move(attrs));
        return a;
    };


    DynamicMemberFunctions[&Lexer::TokenTypes::Identifier] = [](Parser& p, Type* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& func = p.ParseFunction(tok, imp, std::move(attrs));
        auto funcptr = func.get();
        auto&& overset = boost::get<OverloadSet<std::unique_ptr<Function>>>(&t->nonvariables[tok.GetValue()]);
        if (!overset) throw std::runtime_error("Found overload set but there was already a property by that name.");
        (*overset)[a].insert(std::move(func));
        return funcptr;
    };

    DynamicMemberFunctions[&Lexer::TokenTypes::Negate] = [](Parser& p, Type* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) {
        t->destructor_decl = p.ParseDestructor(tok, imp, std::move(attrs));
        return t->destructor_decl.get();
    };

    DynamicMemberFunctions[&Lexer::TokenTypes::Operator] = [](Parser& p, Type* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) -> Function* {
        auto&& valid_ops = p.ModuleOverloadableOperators;
        valid_ops.insert(p.MemberOverloadableOperators.begin(), p.MemberOverloadableOperators.end());
        auto&& name = p.ParseOperatorName(valid_ops);
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& func = p.ParseFunction(tok, imp, std::move(attrs));
        auto funcptr = func.get();
        auto&& overset = boost::get<OverloadSet<std::unique_ptr<Function>>>(&t->nonvariables[name]);
        if (!overset) throw std::runtime_error("Found overload set but there was already a property by that name.");
        (*overset)[a].insert(std::move(func));
        return funcptr;
    };
}
OperatorName Parser::ParseOperatorName(std::unordered_set<OperatorName> valid_ops, OperatorName current, OperatorName valid) {
    if (valid_ops.empty())
        return valid;
    auto&& op = lex();
    if (!op) return valid;
    current.push_back(op->GetType());
    auto&& remaining = GetRemainingValidOperators(valid_ops, current);
    if (remaining.empty()) {
        lex(*op);
        return valid;
    }
    auto&& result = ParseOperatorName(remaining, current, valid);
    if (result == valid) // They did not need our token, so put it back.
        lex(*op);
    return valid;
}
OperatorName Parser::ParseOperatorName(std::unordered_set<OperatorName> valid_ops, OperatorName current) {
    std::unordered_set<Lexer::TokenType> expected;
    for (auto&& name : valid_ops)
        expected.insert(name[current.size()]);
    auto&& op = lex(expected);
    current.push_back(op.GetType());
    auto&& remaining = GetRemainingValidOperators(valid_ops, current);
    if (valid_ops.find(current) != valid_ops.end())
        return ParseOperatorName(remaining, current, current);
    return ParseOperatorName(remaining, current);
}
OperatorName Parser::ParseOperatorName(std::unordered_set<OperatorName> valid_ops) {
    return ParseOperatorName(valid_ops, OperatorName());
}

std::unordered_set<OperatorName> Parser::GetRemainingValidOperators(std::unordered_set<OperatorName> valid, OperatorName current) {
    std::unordered_set<OperatorName> result;
    for (auto&& op : valid) {
        if (op.size() > current.size())
            if (std::equal(op.begin(), op.begin() + current.size(), current.begin()))
                result.insert(op);
    }
    return result;
}
std::unordered_set<OperatorName> Parser::GetAllOperators() {
    auto&& valid = ModuleOverloadableOperators;
    valid.insert(MemberOverloadableOperators.begin(), MemberOverloadableOperators.end());
    return valid;
}
Module* Parser::ParseQualifiedName(Lexer::Token& first, Module* m, Parse::Access a, std::unordered_set<Lexer::TokenType> expected, std::unordered_set<Lexer::TokenType> final) {
    // We have already seen identifier . to enter this method.
    m = CreateModule(first.GetValue(), m, first.GetLocation(), a);
    expected.insert(&Lexer::TokenTypes::Identifier);
    final.insert(&Lexer::TokenTypes::Dot);
    while (true) {
        auto&& ident = lex(expected);
        if (ident.GetType() != &Lexer::TokenTypes::Identifier) return m;
        // If there's a dot, and it was not operator, keep going- else terminate.
        // Don't act on the final whatever
        auto&& dot = lex(final);
        if (dot.GetType() != &Lexer::TokenTypes::Dot) {
            lex(dot);
            first = ident;
            return m;
        }
        m = CreateModule(ident.GetValue(), m, ident.GetLocation(), a);
    }
}

Attribute Parser::ParseAttribute(Lexer::Token& tok, std::shared_ptr<Parse::Import> imp) {
    auto&& initialized = ParseExpression(imp);
    lex(&Lexer::TokenTypes::VarCreate);
    auto&& initializer = ParseExpression(imp);
    auto&& end = lex(&Lexer::TokenTypes::CloseSquareBracket);
    return Attribute(std::move(initialized), std::move(initializer), tok.GetLocation() + end.GetLocation());
}

std::shared_ptr<Parse::Import> Parser::ParseGlobalModuleLevelDeclaration(Module* m, std::shared_ptr<Parse::Import> imp) {
    // Can only get here if ParseGlobalModuleContents found a token, so we know we have at least one.
    auto t = *lex();
    if (GlobalModuleTokens.find(t.GetType()) != GlobalModuleTokens.end())
        return GlobalModuleTokens[t.GetType()](*this, m, Parse::Access::Public, imp, t);
    if (GlobalModuleAttributeTokens.find(t.GetType()) != GlobalModuleAttributeTokens.end()) {
        GlobalModuleAttributeTokens[t.GetType()](*this, m, Parse::Access::Public, imp, t, std::vector<Attribute>());
        return imp;
    }
    std::unordered_set<Wide::Lexer::TokenType> expected;
    for (auto&& pair : GlobalModuleTokens)
        expected.insert(pair.first);
    throw Error(lex.GetLastToken(), t, expected);
}

void Parser::ParseGlobalModuleContents(Module* m, std::shared_ptr<Parse::Import> imp) {
    auto&& t = lex();
    if (t) {
        lex(*t);
        imp = ParseGlobalModuleLevelDeclaration(m, imp);
        return ParseGlobalModuleContents(m, imp);
    }
}
void Parser::ParseModuleContents(Module* m, Lexer::Range first, std::shared_ptr<Parse::Import> imp) {
    auto&& access = Parse::Access::Public;
    while (true) {
        auto&& expected = GetExpectedTokenTypesFromMap(ModuleTokens, GlobalModuleTokens, GlobalModuleAttributeTokens);
        expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        auto&& t = lex(expected);
        if (t.GetType() == &Lexer::TokenTypes::CloseCurlyBracket) {
            outlining(first + t.GetLocation(), OutliningType::Module);
            return;
        }
        lex(t);
        access = ParseModuleLevelDeclaration(m, access, imp);
    }
}
Parse::Access Parser::ParseModuleLevelDeclaration(Module* m, Parse::Access a, std::shared_ptr<Parse::Import> imp) {
    auto t = *lex();
    if (ModuleTokens.find(t.GetType()) != ModuleTokens.end())
        return ModuleTokens[t.GetType()](*this, m, a, imp, t);
    if (GlobalModuleTokens.find(t.GetType()) != GlobalModuleTokens.end()) {
        GlobalModuleTokens[t.GetType()](*this, m, a, imp, t);
        return a;
    }
    assert(GlobalModuleAttributeTokens.find(t.GetType()) != GlobalModuleAttributeTokens.end());
    GlobalModuleAttributeTokens[t.GetType()](*this, m, a, imp, t, std::vector<Attribute>());
    return a;
}
std::unique_ptr<Expression> Parser::ParseExpression(std::shared_ptr<Parse::Import> imp) {
    return ParseAssignmentExpression(imp);
}
std::unique_ptr<Expression> Parser::ParseAssignmentExpression(std::shared_ptr<Parse::Import> imp) {
    auto&& lhs = ParseUnaryExpression(imp);
    // Somebody's gonna be disappointed because an expression is not a valid end of program.
    // But we don't know who or what they're looking for, so just wait and let them fail.
    // Same strategy for all expression types.
    auto&& t = lex();
    if (!t) return std::move(lhs);
    if (AssignmentOperators.find(t->GetType()) != AssignmentOperators.end())
        return AssignmentOperators[t->GetType()](*this, imp, std::move(lhs));
    lex(*t);
    return ParseSubAssignmentExpression(0, std::move(lhs), imp);
}
std::unique_ptr<Expression> Parser::ParsePostfixExpression(std::shared_ptr<Parse::Import> imp) {
    auto&& expr = ParsePrimaryExpression(imp);
    while (true) {
        auto&& t = lex();
        if (!t) return std::move(expr);
        if (PostfixOperators.find(t->GetType()) != PostfixOperators.end()) {
            expr = PostfixOperators[t->GetType()](*this, imp, std::move(expr), *t);
            continue;
        }
        // Did not recognize either of these, so put it back and return the final result.
        lex(*t);
        return std::move(expr);
    }
}
std::unique_ptr<Expression> Parser::ParseUnaryExpression(std::shared_ptr<Parse::Import> imp) {
    // Even if this token is not a unary operator, primary requires at least one token.
    // So just fail right away if there are no more tokens here.
    auto&& tok = lex(GetExpressionBeginnings());
    if (UnaryOperators.find(tok.GetType()) != UnaryOperators.end())
        return UnaryOperators[tok.GetType()](*this, imp, tok);
    lex(tok);
    return ParsePostfixExpression(imp);
}
std::unique_ptr<Expression> Parser::ParseSubAssignmentExpression(unsigned slot, std::shared_ptr<Parse::Import> imp) {
    return ParseSubAssignmentExpression(slot, ParseUnaryExpression(imp), imp);
}
std::unique_ptr<Expression> Parser::ParseSubAssignmentExpression(unsigned slot, std::unique_ptr<Expression> Unary, std::shared_ptr<Parse::Import> imp) {
    if (slot == ExpressionPrecedences.size()) return Unary;
    auto&& lhs = ParseSubAssignmentExpression(slot + 1, std::move(Unary), imp);
    while (true) {
        auto&& t = lex();
        if (!t) return std::move(lhs);
        if (ExpressionPrecedences[slot].find(t->GetType()) != ExpressionPrecedences[slot].end()) {
            auto&& rhs = ParseSubAssignmentExpression(slot + 1, imp);
            lhs = Wide::Memory::MakeUnique<BinaryExpression>(std::move(lhs), std::move(rhs), t->GetType());
            continue;
        }
        lex(*t);
        return std::move(lhs);
    }
}
std::unique_ptr<Expression> Parser::ParsePrimaryExpression(std::shared_ptr<Parse::Import> imp) {
    // ParseUnaryExpression throws if there is no token available so we should be safe here.
    auto&& t = lex(GetExpectedTokenTypesFromMap(PrimaryExpressions));
    return PrimaryExpressions[t.GetType()](*this, imp, t);
}

std::vector<std::unique_ptr<Expression>> Parser::ParseFunctionArguments(std::shared_ptr<Parse::Import> imp) {
    auto&& expected = GetExpressionBeginnings();
    expected.insert(&Lexer::TokenTypes::CloseBracket);
    std::vector<std::unique_ptr<Expression>> result;
    auto&& tok = lex(expected);
    if (tok.GetType() == &Lexer::TokenTypes::CloseBracket)
        return result;
    lex(tok);
    while (true) {
        result.push_back(ParseExpression(imp));
        tok = lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseBracket });
        if (tok.GetType() == &Lexer::TokenTypes::CloseBracket)
            break;
    }
    return result;
}
std::vector<Variable> Parser::ParseLambdaCaptures(std::shared_ptr<Parse::Import> imp) {
    std::vector<Variable> variables;
    auto&& tok = lex(&Lexer::TokenTypes::Identifier);
    while (true) {
        auto&& varassign = lex(&Lexer::TokenTypes::VarCreate);
        auto&& init = ParseExpression(imp);
        variables.push_back(Variable(std::vector<Variable::Name>{{tok.GetValue(), tok.GetLocation() }}, std::move(init), tok.GetLocation() + init->location, nullptr));
        tok = lex({ &Lexer::TokenTypes::CloseSquareBracket, &Lexer::TokenTypes::Comma });
        if (tok.GetType() == &Lexer::TokenTypes::CloseSquareBracket)
            break;
        else
            tok = lex(&Lexer::TokenTypes::Identifier);
    }
    return variables;
}
std::unique_ptr<Statement> Parser::ParseStatement(std::shared_ptr<Parse::Import> imp) {
    auto&& t = lex(GetExpectedTokenTypesFromMap(Statements, UnaryOperators, PrimaryExpressions));
    if (Statements.find(t.GetType()) != Statements.end())
        return Statements[t.GetType()](*this, imp, t);
    // Else, expression statement.
    lex(t);
    auto&& expr = ParseExpression(imp);
    lex(&Lexer::TokenTypes::Semicolon);
    return std::move(expr);
}
std::vector<std::unique_ptr<Expression>> Parser::ParseTypeBases(std::shared_ptr<Parse::Import> imp) {
    auto&& colon = lex({ &Lexer::TokenTypes::Colon, &Lexer::TokenTypes::OpenCurlyBracket });
    auto group = std::vector<std::unique_ptr<Expression>>();
    while (colon.GetType() == &Lexer::TokenTypes::Colon) {
        group.push_back(ParseExpression(imp));
        colon = lex({ &Lexer::TokenTypes::Colon, &Lexer::TokenTypes::OpenCurlyBracket });
    }
    lex(colon);
    return group;
}
std::vector<FunctionArgument> Parser::ParseFunctionDefinitionArguments(std::shared_ptr<Parse::Import> imp) {
    auto ret = std::vector<FunctionArgument>();
    auto&& t = lex({ &Lexer::TokenTypes::CloseBracket, &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::This });
    if (t.GetType() == &Lexer::TokenTypes::CloseBracket)
        return ret;
    lex(t);
    // At least one argument.
    // The form is this or this := expr, then t, or t := expr
    bool first = true;
    while (true) {
        auto&& ident = first
            ? lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::This })
            : lex(&Lexer::TokenTypes::Identifier);
        first = false;
        auto&& t2 = lex({ &Lexer::TokenTypes::VarCreate, &Lexer::TokenTypes::CloseBracket, &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::Colon });
        std::unique_ptr<Expression> type = nullptr;
        std::unique_ptr<Expression> default_value = nullptr;
        if (t2.GetType() == &Lexer::TokenTypes::Colon) {
            type = ParseExpression(imp);
            t2 = lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseBracket, &Lexer::TokenTypes::Assignment });
        }
        if (t2.GetType() == &Lexer::TokenTypes::CloseBracket) {
            ret.push_back(FunctionArgument(ident.GetLocation(), ident.GetValue(), std::move(type), std::move(default_value)));
            break;
        }
        if (t2.GetType() == &Lexer::TokenTypes::Comma) {
            ret.push_back({ ident.GetLocation(), ident.GetValue(), std::move(type), std::move(default_value) });
            continue;
        }
        default_value = ParseExpression(imp);
        ret.push_back(FunctionArgument(ident.GetLocation() + lex.GetLastToken().GetLocation(), ident.GetValue(), std::move(type), nullptr));
        auto&& next = lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseBracket });
        if (next.GetType() == &Lexer::TokenTypes::Comma)
            continue;
        break;
    }
    return ret;
}
std::unique_ptr<Type> Parser::ParseTypeDeclaration(Lexer::Range loc, std::shared_ptr<Parse::Import> imp, Lexer::Token& ident, std::vector<Attribute> attrs) {
    auto&& bases = ParseTypeBases(imp);
    auto&& t = lex(&Lexer::TokenTypes::OpenCurlyBracket);
    auto&& ty = Wide::Memory::MakeUnique<Type>(std::move(bases), loc + t.GetLocation(), std::move(attrs));
    ParseTypeBody(ty.get(), imp);
    return std::move(ty);
}
void Parser::ParseTypeBody(Type* ty, std::shared_ptr<Parse::Import> imp) {
    auto&& loc = lex.GetLastToken().GetLocation();
    auto&& access = Parse::Access::Public;
    auto&& expected = GetExpectedTokenTypesFromMap(TypeTokens, TypeAttributeTokens, DynamicMemberFunctions);
    expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
    auto&& t = lex(expected);
    while (t.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
        if (TypeTokens.find(t.GetType()) != TypeTokens.end())
            access = TypeTokens[t.GetType()](*this, ty, access, imp, t);
        else if (TypeAttributeTokens.find(t.GetType()) != TypeAttributeTokens.end())
            access = TypeAttributeTokens[t.GetType()](*this, ty, access, imp, t, std::vector<Attribute>());
        else
            DynamicMemberFunctions[t.GetType()](*this, ty, access, imp, t, std::vector<Attribute>());
        t = lex(expected);
    }
    outlining(loc + lex.GetLastToken().GetLocation(), OutliningType::Type);
}
std::unique_ptr<Function> Parser::ParseFunction(const Lexer::Token& first, std::shared_ptr<Parse::Import> imp, std::vector<Attribute> attrs) {
    auto&& args = ParseFunctionDefinitionArguments(imp);
    // Gotta be := or {
    auto&& next = lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Colon, &Lexer::TokenTypes::Default, &Lexer::TokenTypes::Delete });
    if (next.GetType() == &Lexer::TokenTypes::Default) {
        auto&& func = Wide::Memory::MakeUnique<Function>(std::vector<std::unique_ptr<Statement>>(), first.GetLocation() + next.GetLocation(), std::move(args), nullptr, std::move(attrs));
        func->defaulted = true;
        return std::move(func);
    }
    std::unique_ptr<Expression> explicit_return = nullptr;
    if (next.GetType() == &Lexer::TokenTypes::Colon) {
        explicit_return = ParseExpression(imp);
        next = lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Abstract });
    }
    if (next.GetType() == &Lexer::TokenTypes::Delete) {
        auto&& func = Wide::Memory::MakeUnique<Function>(std::vector<std::unique_ptr<Statement>>(), first.GetLocation() + next.GetLocation(), std::move(args), std::move(explicit_return), std::move(attrs));
        func->deleted = true;
        return std::move(func);;
    }
    if (next.GetType() == &Lexer::TokenTypes::Abstract) {
        auto&& func = Wide::Memory::MakeUnique<Function>(std::vector<std::unique_ptr<Statement>>(), first.GetLocation() + next.GetLocation(), std::move(args), std::move(explicit_return), std::move(attrs));
        func->abstract = true;
        func->dynamic = true; // abstract implies dynamic.
        return std::move(func);;
    }
    auto&& expected = GetStatementBeginnings();
    expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
    std::vector<std::unique_ptr<Statement>> statements;
    auto&& t = lex(expected);
    while (t.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
        lex(t);
        statements.push_back(ParseStatement(imp));
        t = lex(expected);
    }
    return Wide::Memory::MakeUnique<Function>(std::move(statements), first.GetLocation() + t.GetLocation(), std::move(args), std::move(explicit_return), std::move(attrs));
}
std::unique_ptr<Constructor> Parser::ParseConstructor(const Lexer::Token& first, std::shared_ptr<Parse::Import> imp, std::vector<Attribute> attrs) {
    auto&& args = ParseFunctionDefinitionArguments(imp);
    // Gotta be : or { or default
    auto&& colon_or_open = lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Colon, &Lexer::TokenTypes::Default, &Lexer::TokenTypes::Delete });
    if (colon_or_open.GetType() == &Lexer::TokenTypes::Default) {
        auto&& con = Wide::Memory::MakeUnique<Constructor>(std::vector<std::unique_ptr<Statement>>(), first.GetLocation() + colon_or_open.GetLocation(), std::move(args), std::vector<VariableInitializer>(), std::move(attrs));
        con->defaulted = true;
        return std::move(con);
    }
    if (colon_or_open.GetType() == &Lexer::TokenTypes::Delete) {
        auto&& con = Wide::Memory::MakeUnique<Constructor>(std::vector<std::unique_ptr<Statement>>(), first.GetLocation() + colon_or_open.GetLocation(), std::move(args), std::vector<VariableInitializer>(), std::move(attrs));
        con->deleted = true;
        return std::move(con);
    }
    std::vector<VariableInitializer> initializers;
    while (colon_or_open.GetType() == &Lexer::TokenTypes::Colon) {
        auto&& expected = GetExpressionBeginnings();
        expected.insert(&Lexer::TokenTypes::Type);
        auto&& next = lex(expected);
        if (next.GetType() == &Lexer::TokenTypes::Type) {
            // Delegating constructor.
            lex(&Lexer::TokenTypes::VarCreate);
            auto&& initializer = ParseExpression(imp);
            initializers.push_back({ Wide::Memory::MakeUnique<Identifier>("type", imp, next.GetLocation()), std::move(initializer), next.GetLocation() + initializer->location });
            colon_or_open = lex(&Lexer::TokenTypes::OpenCurlyBracket);
            break;
        }
        lex(next);
        auto&& initialized = ParseExpression(imp);
        lex(&Lexer::TokenTypes::VarCreate);
        auto&& initializer = ParseExpression(imp);
        initializers.push_back({ std::move(initialized), std::move(initializer), colon_or_open.GetLocation() + initializer->location });
        colon_or_open = lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Colon });
    }
    // Gotta be { by this point.
    std::vector<std::unique_ptr<Statement>> statements;
    auto&& expected = GetStatementBeginnings();
    expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
    auto&& t = lex(expected);
    while (t.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
        lex(t);
        statements.push_back(ParseStatement(imp));
        t = lex(expected);
    }
    return Wide::Memory::MakeUnique<Constructor>(std::move(statements), first.GetLocation() + t.GetLocation(), std::move(args), std::move(initializers), std::move(attrs));
}
std::unique_ptr<Destructor> Parser::ParseDestructor(const Lexer::Token& first, std::shared_ptr<Parse::Import> imp, std::vector<Attribute> attrs) {
    // ~ type ( ) { stuff }
    lex(&Lexer::TokenTypes::Type);
    lex(&Lexer::TokenTypes::OpenBracket);
    lex(&Lexer::TokenTypes::CloseBracket);
    auto&& default_or_open = lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Default });
    if (default_or_open.GetType() == &Lexer::TokenTypes::Default) {
        return Wide::Memory::MakeUnique<Destructor>(std::vector<std::unique_ptr<Statement>>(), first.GetLocation() + default_or_open.GetLocation(), std::move(attrs), true);
    }
    std::vector<std::unique_ptr<Statement>> body;
    auto&& expected = GetStatementBeginnings();
    expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
    auto&& t = lex(expected);
    while (t.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
        lex(t);
        body.push_back(ParseStatement(imp));
        t = lex(expected);
    }
    return Wide::Memory::MakeUnique<Destructor>(std::move(body), first.GetLocation() + t.GetLocation(), std::move(attrs), false);
}

void Parser::AddTypeToModule(Module* m, std::string name, std::shared_ptr<Type> t, Parse::Access specifier) {
    if (m->named_decls.find(name) != m->named_decls.end())
        throw std::runtime_error("Tried to insert a type into a module but already found something there.");
    m->named_decls[name] = std::make_pair(specifier, t);
}
void Parser::AddUsingToModule(Module* m, std::string name, std::shared_ptr<Using> u, Parse::Access specifier) {
    if (m->named_decls.find(name) != m->named_decls.end())
        throw std::runtime_error("Tried to insert a using into a module but already found something there.");
    m->named_decls[name] = std::make_pair(specifier, u);
}
void Parser::AddFunctionToModule(Module* m, std::string name, std::shared_ptr<Function> f, Parse::Access specifier) {
    if (m->named_decls.find(name) == m->named_decls.end())
        m->named_decls[name] = Wide::Memory::MakeUnique<ModuleOverloadSet<Function>>();
    auto&& set = boost::get<std::unique_ptr<MultipleAccessContainer>>(&m->named_decls[name]);
    auto&& overset = dynamic_cast<ModuleOverloadSet<Function>*>(set->get());
    if (!overset)
        throw std::runtime_error("Tried to insert a function into a module but already found something there that was not an overload set.");
    overset->funcs[specifier].insert(f);
}
void Parser::AddTemplateTypeToModule(Module* m, std::string name, Lexer::Range where, std::vector<FunctionArgument> args, std::unique_ptr<Type> t, Parse::Access specifier) {
    if (m->named_decls.find(name) == m->named_decls.end())
        m->named_decls[name] = Wide::Memory::MakeUnique<ModuleOverloadSet<TemplateType>>();
    auto&& set = boost::get<std::unique_ptr<MultipleAccessContainer>>(&m->named_decls[name]);
    auto&& overset = dynamic_cast<ModuleOverloadSet<TemplateType>*>(set->get());
    if (!overset)
        throw std::runtime_error("Tried to insert a template type into a module but already found something there that was not a template overload set.");
    overset->funcs[specifier].insert(std::make_shared<TemplateType>(where, std::move(t), std::move(args)));
}
Module* Parser::CreateModule(std::string name, Module* m, Lexer::Range where, Parse::Access access) {
    if (m->named_decls.find(name) != m->named_decls.end()) {
        auto&& mod = boost::get<std::pair<Parse::Access, Module*>>(&m->named_decls[name]);
        if (!mod)
            throw std::runtime_error("Tried to insert a module into a module but already found something there that was not a module.");
        mod->second->locations.insert(where);
        return mod->second;
    }
    auto&& newmod = Wide::Memory::MakeUnique<Module>();
    auto modptr = newmod.get();
    newmod->locations.insert(where);
    m->named_decls[name] = std::make_pair(access, std::unique_ptr<UniqueAccessContainer>(std::move(newmod)));
    return modptr;
}
Wide::Lexer::Token Parse::Error::GetLastValidToken() {
    return previous;
}
Wide::Util::optional<Wide::Lexer::Token> Parse::Error::GetInvalidToken() {
    return unexpected;
}
std::unordered_set<Wide::Lexer::TokenType> Parse::Error::GetExpectedTokenTypes() {
    return expected;
}

Parse::Error::Error(Wide::Lexer::Token previous, Wide::Util::optional<Wide::Lexer::Token> error, std::unordered_set<Wide::Lexer::TokenType> expected)
    : previous(previous)
    , unexpected(error)
    , expected(expected) 
{
    err = "Unexpected ";
    if (unexpected)
        err += "token: " + *unexpected->GetType() + " at " + to_string(unexpected->GetLocation());
    else
        err += "end of file";
    err += " after " + *previous.GetType() + " at " + to_string(previous.GetLocation());
}
std::unordered_set<Lexer::TokenType> Parser::GetExpressionBeginnings() {
    return GetExpectedTokenTypesFromMap(UnaryOperators, PrimaryExpressions);
}
std::unordered_set<Lexer::TokenType> Parser::GetStatementBeginnings() {
    auto&& base = GetExpectedTokenTypesFromMap(Statements);
    for (auto&& tok : GetExpressionBeginnings())
        base.insert(tok);
    return base;
}
std::unordered_set<Lexer::TokenType> Parser::GetIdentifierFollowups() {
    auto&& expected = GetExpectedTokenTypesFromMap(PostfixOperators, AssignmentOperators);
    expected.insert(&Lexer::TokenTypes::Lambda);
    for (auto&& level : ExpressionPrecedences)
        for (auto&& tok : level)
            expected.insert(tok);
    return expected;
}