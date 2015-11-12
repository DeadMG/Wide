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

boost::optional<Lexer::Token> PutbackLexer::operator()() {
    if (!putbacks.empty()) {
        auto val = std::move(putbacks.back());
        putbacks.pop_back();
        tokens.push_back(val);
        return val;
    }
    auto val = lex();
    if (val) {
        tokens.push_back(*val);
        return *val;
    }
    return boost::none;
}
template<typename F> auto PutbackLexer::operator()(Lexer::TokenType required, F f) -> decltype(f(std::declval<Wide::Lexer::Token&>())) {
    return (*this)(std::initializer_list<Lexer::TokenType>({ required }), f);
}
template<typename F> auto PutbackLexer::operator()(Lexer::TokenType required, Lexer::TokenType terminator, F f) -> decltype(f(std::declval<Wide::Lexer::Token&>())) {
    return (*this)(std::initializer_list<Lexer::TokenType>({ required }), std::initializer_list<Lexer::TokenType>({ terminator }), f);
}
template<typename F> auto PutbackLexer::operator()(std::unordered_set<Lexer::TokenType> required, F f) -> decltype(f(std::declval<Wide::Lexer::Token&>())) {
    return (*this)(required, std::unordered_set<Lexer::TokenType>(), f);
}
template<typename F> auto PutbackLexer::operator()(std::unordered_set<Lexer::TokenType> required, Lexer::TokenType terminator, F f) -> decltype(f(std::declval<Wide::Lexer::Token&>())) {
    return (*this)(required, std::unordered_set<Lexer::TokenType>({ terminator }), f);
}
template<typename F> auto PutbackLexer::operator()(std::unordered_set<Lexer::TokenType> required, std::unordered_set<Lexer::TokenType> terminators, F f) -> decltype(f(std::declval<Wide::Lexer::Token&>())) {
    auto val = (*this)();
    if (!val)
        throw Error(tokens.back(), Wide::Util::none, required);
    if (required.find(val->GetType()) == required.end())
        throw Error(tokens[tokens.size() - 2], *val, required);
    try {
        return f(*val);
    }
    catch (Error& e) {
        if (auto tok = e.GetInvalidToken()) {
            //if (required.find(tok->GetType()) != required.end())
            //    return f(*tok);
            // Find the terminator. Before he kills John Connor.
            if (!terminators.empty()) {
                while (auto next = (*this)()) {
                    if (terminators.find(next->GetType()) != terminators.end())
                        return f(*next);
                }
            }
        }
        
        throw;
    }
}
Lexer::Range PutbackLexer::operator()(Lexer::TokenType required) {
    return (*this)(std::unordered_set<Lexer::TokenType>({required}));
}
Lexer::Range PutbackLexer::operator()(std::unordered_set<Lexer::TokenType> required) {
    return (*this)(required, [](Lexer::Token& t) { return t.GetLocation(); });
}
namespace {
    template<typename Ret, typename F> Ret RecursiveWhile(F f) {
        return f([&]() -> Ret {
            return RecursiveWhile<Ret>(f);
        });
    }
}

Parser::Parser(std::function<Wide::Util::optional<Lexer::Token>()> l)
: lex(l)
{
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

    ModuleTokens[&Lexer::TokenTypes::Private] = [](Parser& p, ModuleParseState state, Lexer::Token& token) {
        p.lex(&Lexer::TokenTypes::Colon);
        return ModuleParseResult{
            ModuleParseState{
                state.imp,
                Parse::Access::Private
            },
            Wide::Util::none
        };
    };

    ModuleTokens[&Lexer::TokenTypes::Public] = [](Parser& p, ModuleParseState state, Lexer::Token& token) {
        p.lex(&Lexer::TokenTypes::Colon);
        return ModuleParseResult{
            ModuleParseState {
                state.imp,
                Parse::Access::Public
            },
            Wide::Util::none
        };
    };
    
    GlobalModuleTokens[&Lexer::TokenTypes::Import] = [](Parser& p, ModuleParseState state, Lexer::Token& token) {
        // import y;
        // import y hiding x, y, z;
        auto&& expr = p.ParseExpression(state.imp);
        return p.lex({ &Lexer::TokenTypes::Semicolon, &Lexer::TokenTypes::Hiding }, [&](Wide::Lexer::Token& semi) {
            std::vector<Parse::Name> hidings;
            if (semi.GetType() == &Lexer::TokenTypes::Semicolon)
                return ModuleParseResult{
                    ModuleParseState{
                    std::make_shared<Import>(std::move(expr), std::vector<Parse::Name>(), std::move(state.imp), hidings),
                    state.access
                }
            };
            return RecursiveWhile<ModuleParseResult>([&](auto continuation) {
                Parse::Name name;
                return p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator }, [&](Lexer::Token& lead) {
                    if (lead.GetType() == &Lexer::TokenTypes::Operator)
                        name = p.ParseOperatorName(p.GetAllOperators());
                    else
                        name = lead.GetValue();
                    hidings.push_back(name);
                    return p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::Semicolon }, [&](Lexer::Token& next) {
                        if (next.GetType() == &Lexer::TokenTypes::Semicolon)
                            return ModuleParseResult{
                                ModuleParseState{
                                    std::make_shared<Import>(std::move(expr), std::vector<Parse::Name>(), state.imp, hidings),
                                    state.access
                                }
                            };
                        return continuation();
                    });
                });
            });
        });
    };

    GlobalModuleTokens[&Lexer::TokenTypes::From] = [](Parser& p, ModuleParseState state, Lexer::Token& token) {
        // from x import y, z;
        auto&& expr = p.ParseExpression(state.imp);
        p.lex(&Lexer::TokenTypes::Import);
        // Import only these
        std::vector<Parse::Name> names;
        return RecursiveWhile<ModuleParseResult>([&](auto continuation) {
            Parse::Name name;
            return p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator }, [&](Lexer::Token& lead) {
                if (lead.GetType() == &Lexer::TokenTypes::Operator)
                    name = p.ParseOperatorName(p.GetAllOperators());
                else
                    name = lead.GetValue();
                names.push_back(name);
                return p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::Semicolon }, [&](Lexer::Token& next) {
                    if (next.GetType() == &Lexer::TokenTypes::Semicolon)
                        return ModuleParseResult{
                            ModuleParseState{
                                std::make_shared<Import>(std::move(expr), names, state.imp, std::vector<Parse::Name>()),
                                state.access
                            }
                        };
                    return continuation();
                });
            });
        });
    };

    GlobalModuleTokens[&Lexer::TokenTypes::Module] = [](Parser& p, ModuleParseState state, Lexer::Token& module) {
        return p.lex(&Lexer::TokenTypes::Identifier, [&](Lexer::Token& ident) {
            return ModuleParseResult{
                state,
                ModuleMember{
                    ModuleMember::NamedMember{
                        ident.GetValue(),
                        ModuleMember::NamedMember::UniqueMember{
                            std::make_pair(state.access, p.ParseModule(ident.GetLocation(), state, module))
                        }
                    }
                }
            };
        });
    };

    GlobalModuleTokens[&Lexer::TokenTypes::Template] = [](Parser& p, ModuleParseState state, Lexer::Token& templat) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& args = p.ParseFunctionDefinitionArguments(state.imp);
        auto&& attrs = std::vector<Attribute>();
        return RecursiveWhile<ModuleParseResult>([&](auto continuation) {
            return p.lex({ &Lexer::TokenTypes::OpenSquareBracket, &Lexer::TokenTypes::Type }, [&](Lexer::Token& tok) {
                if (tok.GetType() == &Lexer::TokenTypes::OpenSquareBracket) {
                    attrs.push_back(p.ParseAttribute(tok, state.imp));
                    return continuation();
                }
                return p.lex(&Lexer::TokenTypes::Identifier, [&](Lexer::Token& ident) {
                    auto&& ty = p.ParseTypeDeclaration(tok.GetLocation(), state.imp, ident, std::move(attrs));
                    return ModuleParseResult{
                        state,
                        ModuleMember{
                            ModuleMember::NamedMember{
                                ident.GetValue(),
                                ModuleMember::NamedMember::SharedMember{
                                    state.access,
                                    std::move(ty)
                                }
                            }
                        }
                    };
                });
            });
        });
    };

    GlobalModuleTokens[&Lexer::TokenTypes::Using] = [](Parser& p, ModuleParseState state, Lexer::Token& token) {
        p.lex.GetLastToken().GetLocation();
        return p.lex(&Lexer::TokenTypes::Identifier, [&](Lexer::Token& t) {
            p.lex(&Lexer::TokenTypes::VarCreate);
            auto&& expr = p.ParseExpression(state.imp);
            auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
            auto&& use = std::make_shared<Using>(std::move(expr), token.GetLocation() + semi);
            return ModuleParseResult{
                state,
                ModuleMember{
                    ModuleMember::NamedMember{
                        t.GetValue(),
                        ModuleMember::NamedMember::SharedMember{
                            state.access,
                            std::move(use)
                        }
                    }
                }
            };
        });
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::OpenSquareBracket] = [](Parser& p, ModuleParseState state, Lexer::Token& token, std::vector<Attribute> attributes) {
        // Another attribute, just add it to the list.
        attributes.push_back(p.ParseAttribute(token, state.imp));
        return p.lex(GetExpectedTokenTypesFromMap(p.GlobalModuleAttributeTokens), [&](Lexer::Token& next) {
            return p.GlobalModuleAttributeTokens[next.GetType()](p, state, next, std::move(attributes));
        });
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Dot] = [](Parser& p, ModuleParseState state, Lexer::Token& token, std::vector<Attribute> attributes) {
        return p.lex(&Lexer::TokenTypes::Identifier, [&](Lexer::Token& next) {
            return p.GlobalModuleAttributeTokens[&Lexer::TokenTypes::Identifier](p, state, next, std::move(attributes));
        });
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Identifier] = [](Parser& p, ModuleParseState state, Lexer::Token& ident, std::vector<Attribute> attributes) {
        return p.lex({ &Lexer::TokenTypes::Dot, &Lexer::TokenTypes::OpenBracket }, [&](Lexer::Token& maybedot) {
            if (maybedot.GetType() == &Lexer::TokenTypes::OpenBracket) {
                auto&& func = p.ParseFunction(ident, state.imp, std::move(attributes));
                auto set = Wide::Memory::MakeUnique<ModuleOverloadSet<Wide::Parse::Function>>();
                set->funcs[state.access].insert(std::move(func));
                return ModuleParseResult{
                    state,
                    ModuleMember{
                        ModuleMember::NamedMember{
                            ident.GetValue(),
                            ModuleMember::NamedMember::MultiAccessMember{
                                std::move(set)
                            }
                        }
                    }
                };
            }
            return ModuleParseResult{
                state,
                ModuleMember{
                    ModuleMember::NamedMember{
                        ident.GetValue(),
                        ModuleMember::NamedMember::UniqueMember{
                            state.access,
                            p.ParseModuleFunction(ident.GetLocation(), state, std::move(attributes))
                        }
                    }
                }
            };
        });
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Type] = [](Parser& p, ModuleParseState state, Lexer::Token& typ, std::vector<Attribute> attributes) {
        // Could be exported constructor.
        return p.lex({ &Lexer::TokenTypes::OpenBracket, &Lexer::TokenTypes::Identifier }, [&](Lexer::Token& next) {
            if (next.GetType() == &Lexer::TokenTypes::OpenBracket) {
                return ModuleParseResult{
                    state,
                    ModuleMember{
                        ModuleMember::ConstructorDecl{
                            p.ParseConstructor(typ, state.imp, std::move(attributes))
                        }
                    }
                };
            }
            return ModuleParseResult{
                state,
                ModuleMember{
                    ModuleMember::NamedMember{
                        next.GetValue(),
                        ModuleMember::NamedMember::SharedMember{
                            std::make_pair(state.access, p.ParseTypeDeclaration(typ.GetLocation(), state.imp, next, std::move(attributes)))
                        }
                    }
                }
            };
        });
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Operator] = [](Parser& p, ModuleParseState state, Lexer::Token& tok, std::vector<Attribute> attrs) {
        auto&& name = p.ParseOperatorName(p.ModuleOverloadableOperators);
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& func = p.ParseFunction(tok, state.imp, std::move(attrs));
        return ModuleParseResult{
            state,
            ModuleMember {
                ModuleMember::OperatorOverload {
                    name,
                    state.access,
                    std::move(func)
                }
            }
        };
    };

    GlobalModuleAttributeTokens[&Lexer::TokenTypes::Negate] = [](Parser& p, ModuleParseState state, Lexer::Token& token, std::vector<Attribute> attributes) {
        return ModuleParseResult {
            state,
            ModuleMember{
                ModuleMember::DestructorDecl {
                    p.ParseDestructor(token, state.imp, std::move(attributes))
                }
            }
        };
    };
    
    PostfixOperators[&Lexer::TokenTypes::OpenSquareBracket] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) {
        auto&& index = p.ParseExpression(imp);
        auto&& close = p.lex(&Lexer::TokenTypes::CloseSquareBracket);
        return Wide::Memory::MakeUnique<Index>(std::move(e), std::move(index), e->location + close);
    };

    PostfixOperators[&Lexer::TokenTypes::Dot] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) -> std::unique_ptr<Expression> {
        return p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator, &Lexer::TokenTypes::Negate }, [&](Lexer::Token& t) -> std::unique_ptr<Expression> {
            if (t.GetType() == &Lexer::TokenTypes::Identifier)
                return Wide::Memory::MakeUnique<MemberAccess>(t.GetValue(), std::move(e), e->location + t.GetLocation(), t.GetLocation());
            if (t.GetType() == &Lexer::TokenTypes::Operator)
                return Wide::Memory::MakeUnique<MemberAccess>(p.ParseOperatorName(p.GetAllOperators()), std::move(e), e->location + t.GetLocation(), t.GetLocation());
            p.lex(&Lexer::TokenTypes::Type);
            p.lex(&Lexer::TokenTypes::OpenBracket);
            auto&& close = p.lex(&Lexer::TokenTypes::CloseBracket);
            return Wide::Memory::MakeUnique<DestructorAccess>(std::move(e), e->location + close);
        });
    };
    
    PostfixOperators[&Lexer::TokenTypes::PointerAccess] = [](Parser& p, std::shared_ptr<Parse::Import> imp, std::unique_ptr<Expression> e, Lexer::Token& token) -> std::unique_ptr<Expression> {
        return p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator, &Lexer::TokenTypes::Negate }, [&](Lexer::Token& t) -> std::unique_ptr<Expression> {
            if (t.GetType() == &Lexer::TokenTypes::Identifier)
                return Wide::Memory::MakeUnique<PointerMemberAccess>(t.GetValue(), std::move(e), e->location + t.GetLocation(), t.GetLocation());
            if (t.GetType() == &Lexer::TokenTypes::Operator)
                return Wide::Memory::MakeUnique<MemberAccess>(p.ParseOperatorName(p.GetAllOperators()), std::move(e), e->location + t.GetLocation(), t.GetLocation());
            return p.lex(&Lexer::TokenTypes::Type, [&](Lexer::Token& typ) {
                return Wide::Memory::MakeUnique<PointerDestructorAccess>(std::move(e), e->location + typ.GetLocation());
            });
        });
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
        return RecursiveWhile<std::unique_ptr<Expression>>([&](auto continuation) {
            return p.lex(expected, [&](Lexer::Token& terminator) {
                if (terminator.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
                    p.lex(terminator);
                    exprs.push_back(p.ParseExpression(imp));
                    return p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseCurlyBracket }, [&](Lexer::Token& next) {
                        if (next.GetType() == &Lexer::TokenTypes::Comma)
                            return continuation();
                        return std::unique_ptr<Expression>(Wide::Memory::MakeUnique<Tuple>(std::move(exprs), t.GetLocation() + terminator.GetLocation()));
                    });
                }
                return std::unique_ptr<Expression>(Wide::Memory::MakeUnique<Tuple>(std::move(exprs), t.GetLocation() + terminator.GetLocation()));
            });
        });
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
        return p.lex(&Lexer::TokenTypes::CloseBracket, [&](Lexer::Token& close) {
            return Wide::Memory::MakeUnique<Decltype>(std::move(expr), t.GetLocation() + close.GetLocation());
        });
    };

    PrimaryExpressions[&Lexer::TokenTypes::Typeid] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& expr = p.ParseExpression(imp);
        return p.lex(&Lexer::TokenTypes::CloseBracket, [&](Lexer::Token& close) {
            return Wide::Memory::MakeUnique<Typeid>(std::move(expr), t.GetLocation() + close.GetLocation());
        });
    };

    PrimaryExpressions[&Lexer::TokenTypes::DynamicCast] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& expr1 = p.ParseExpression(imp);
        p.lex(&Lexer::TokenTypes::Comma);
        auto&& expr2 = p.ParseExpression(imp);
        return p.lex(&Lexer::TokenTypes::CloseBracket, [&](Lexer::Token& close) {
            return Wide::Memory::MakeUnique<DynamicCast>(std::move(expr1), std::move(expr2), t.GetLocation() + close.GetLocation());
        });
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
        return p.lex(expected, [&](Lexer::Token& tok) -> std::unique_ptr<Expression> {
            if (tok.GetType() == &Lexer::TokenTypes::CloseBracket) {
                p.lex(&Lexer::TokenTypes::Lambda);
                auto expr = p.ParseExpression(imp);
                std::vector<std::unique_ptr<Statement>> stmts;
                auto loc = expr->location;
                stmts.push_back({ Wide::Memory::MakeUnique<Return>(std::move(expr), loc) });
                return Wide::Memory::MakeUnique<Lambda>(std::move(stmts), std::vector<FunctionArgument>(), t.GetLocation() + loc, false, std::vector<Variable>());
            }
            p.lex(tok);
            auto expr = p.ParseExpression(imp);
            p.lex(&Lexer::TokenTypes::CloseBracket);
            return expr;
        });
    };

    PrimaryExpressions[&Lexer::TokenTypes::Function] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) -> std::unique_ptr<Expression> {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& args = p.ParseFunctionDefinitionArguments(imp);
        auto&& pos = t.GetLocation();
        auto&& grp = std::vector<std::unique_ptr<Statement>>();
        auto&& caps = std::vector<Variable>();
        bool defaultref = false;
        auto handlecurly = [&](Lexer::Token& tok) {
            auto&& expected = p.GetStatementBeginnings();
            expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
            return RecursiveWhile<std::unique_ptr<Expression>>([&](auto continuation) {
                return p.lex(expected, [&](Lexer::Token& tok) {
                    if (tok.GetType() == &Lexer::TokenTypes::CloseCurlyBracket)
                        return std::unique_ptr<Expression>(Wide::Memory::MakeUnique<Lambda>(std::move(grp), std::move(args), pos + tok.GetLocation(), defaultref, std::move(caps)));
                    p.lex(tok);
                    grp.push_back(p.ParseStatement(imp));
                    return continuation();
                });
            });            
        };
        return p.lex({ &Lexer::TokenTypes::OpenSquareBracket, &Lexer::TokenTypes::OpenCurlyBracket }, [&](Lexer::Token& tok) {
            if (tok.GetType() == &Lexer::TokenTypes::OpenSquareBracket) {
                return p.lex({ &Lexer::TokenTypes::And, &Lexer::TokenTypes::Identifier }, [&](Lexer::Token& tok) {
                    if (tok.GetType() == &Lexer::TokenTypes::And) {
                        defaultref = true;
                        return p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseSquareBracket }, [&](Lexer::Token& tok) {
                            if (tok.GetType() == &Lexer::TokenTypes::Comma)
                                caps = p.ParseLambdaCaptures(imp);
                            return p.lex(&Lexer::TokenTypes::OpenCurlyBracket, [&](Lexer::Token& curly) {
                                return handlecurly(curly);
                            });
                        });
                    } else {
                        p.lex(tok);
                        caps = p.ParseLambdaCaptures(imp);
                        return p.lex(&Lexer::TokenTypes::OpenCurlyBracket, [&](Lexer::Token& curly) {
                            return handlecurly(curly);
                        });
                    }
                });
            }
            return handlecurly(tok);
        });
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
        return p.lex(&Lexer::TokenTypes::OpenCurlyBracket, [&](Lexer::Token& open) {
            TypeMembers members;
            auto close = p.ParseTypeBody(&members, imp);
            auto&& ty = Wide::Memory::MakeUnique<Type>(members, std::vector<Attribute>(), t.GetLocation(), open.GetLocation(), close, Wide::Util::none);
            return std::move(ty);
        });
    };

    Statements[&Lexer::TokenTypes::Return] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        auto&& expected = p.GetExpressionBeginnings();
        expected.insert(&Lexer::TokenTypes::Semicolon);
        return p.lex(expected, [&](Lexer::Token& next) {// Check next token for ;
            if (next.GetType() == &Lexer::TokenTypes::Semicolon)
                return Wide::Memory::MakeUnique<Return>(t.GetLocation() + next.GetLocation());
            // If it wasn't ; then expect expression.
            p.lex(next);
            auto&& expr = p.ParseExpression(imp);
            return p.lex(&Lexer::TokenTypes::Semicolon, [&](Lexer::Token& next) {
                return Wide::Memory::MakeUnique<Return>(std::move(expr), t.GetLocation() + next.GetLocation());
            });            
        }); 
    };

    Statements[&Lexer::TokenTypes::If] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        // Check for variable conditions
        auto&& else_expected = p.GetStatementBeginnings();
        else_expected.insert(&Lexer::TokenTypes::Else);
        else_expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        return p.lex(p.GetExpressionBeginnings(), [&](Lexer::Token& ident) {
            auto handle_expression_condition = [&] {
                p.lex(ident);
                auto&& cond = p.ParseExpression(imp);
                p.lex(&Lexer::TokenTypes::CloseBracket);
                auto&& true_br = p.ParseStatement(imp);
                return p.lex(else_expected, [&](Lexer::Token& next) {
                    if (next.GetType() == &Lexer::TokenTypes::Else) {
                        auto&& else_br = p.ParseStatement(imp);
                        return Wide::Memory::MakeUnique<If>(std::move(cond), std::move(true_br), std::move(else_br), t.GetLocation() + else_br->location);
                    }
                    p.lex(next);
                    return Wide::Memory::MakeUnique<If>(std::move(cond), std::move(true_br), nullptr, t.GetLocation() + true_br->location);
                });
            };
            if (ident.GetType() == &Lexer::TokenTypes::Identifier) {
                auto&& expected = p.GetIdentifierFollowups();
                expected.insert(&Lexer::TokenTypes::VarCreate);
                expected.insert(&Lexer::TokenTypes::CloseBracket);
                return p.lex(expected, [&](Lexer::Token& var) {
                    if (var.GetType() == &Lexer::TokenTypes::VarCreate) {
                        auto&& expr = p.ParseExpression(imp);
                        auto&& variable = Wide::Memory::MakeUnique<Variable>(std::vector<Variable::Name>{ { ident.GetValue(), t.GetLocation() + expr->location }}, std::move(expr), ident.GetLocation(), nullptr);
                        p.lex(&Lexer::TokenTypes::CloseBracket);
                        auto&& body = p.ParseStatement(imp);
                        return p.lex(else_expected, [&](Lexer::Token& next) {
                            if (next.GetType() == &Lexer::TokenTypes::Else) {
                                auto&& else_br = p.ParseStatement(imp);
                                return Wide::Memory::MakeUnique<If>(std::move(variable), std::move(body), std::move(else_br), t.GetLocation() + body->location);
                            }
                            p.lex(next);
                            return Wide::Memory::MakeUnique<If>(std::move(variable), std::move(body), nullptr, t.GetLocation() + body->location);
                        });
                    }
                    p.lex(var);
                    return handle_expression_condition();
                });
            }
            return handle_expression_condition();
        });
    };

    Statements[&Lexer::TokenTypes::OpenCurlyBracket] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        auto&& pos = t.GetLocation();
        auto&& expected = p.GetStatementBeginnings();
        expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        std::vector<std::unique_ptr<Statement>> stmts;
        return RecursiveWhile<std::unique_ptr<Statement>>([&](auto continuation) {
            return p.lex(expected, [&](Lexer::Token& next) {
                if (next.GetType() == &Lexer::TokenTypes::CloseCurlyBracket)
                    return std::unique_ptr<Statement>(Wide::Memory::MakeUnique<CompoundStatement>(std::move(stmts), pos + t.GetLocation()));
                p.lex(next);
                stmts.push_back(p.ParseStatement(imp));
                return continuation();
            });
        });
    };

    Statements[&Lexer::TokenTypes::While] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        // Check for variable conditions.
        return p.lex(p.GetExpressionBeginnings(), [&](Lexer::Token& ident) {
            auto handle_expression_condition = [&] {
                p.lex(ident);
                auto&& cond = p.ParseExpression(imp);
                p.lex(&Lexer::TokenTypes::CloseBracket);
                auto&& body = p.ParseStatement(imp);
                return Wide::Memory::MakeUnique<While>(std::move(body), std::move(cond), t.GetLocation() + body->location);
            };
            if (ident.GetType() == &Lexer::TokenTypes::Identifier) {
                auto&& expected = p.GetIdentifierFollowups();
                expected.insert(&Lexer::TokenTypes::VarCreate);
                expected.insert(&Lexer::TokenTypes::CloseBracket);
                return p.lex(expected, [&](Lexer::Token& var) {
                    if (var.GetType() == &Lexer::TokenTypes::VarCreate) {
                        auto&& expr = p.ParseExpression(imp);
                        auto&& variable = Wide::Memory::MakeUnique<Variable>(std::vector<Variable::Name>{ { ident.GetValue(), t.GetLocation() + expr->location }}, std::move(expr), ident.GetLocation(), nullptr);
                        p.lex(&Lexer::TokenTypes::CloseBracket);
                        auto&& body = p.ParseStatement(imp);
                        return Wide::Memory::MakeUnique<While>(std::move(body), std::move(variable), t.GetLocation() + body->location);
                    }
                    p.lex(var);
                    return handle_expression_condition();
                });
            }
            return handle_expression_condition();
        });
    };

    Statements[&Lexer::TokenTypes::Identifier] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t)-> std::unique_ptr<Statement> {
        std::vector<Parse::Variable::Name> names = { { t.GetValue(), t.GetLocation() } };
        auto&& expected = p.GetIdentifierFollowups();
        expected.insert(&Lexer::TokenTypes::VarCreate);
        expected.insert(&Lexer::TokenTypes::Comma);
        expected.insert(&Lexer::TokenTypes::Colon);
        return p.lex(expected, [&](Lexer::Token& next) {
            if (next.GetType() != &Lexer::TokenTypes::VarCreate && next.GetType() != &Lexer::TokenTypes::Comma && next.GetType() != &Lexer::TokenTypes::Colon) {
                p.lex(next);
                p.lex(t);
                auto&& expr = p.ParseExpression(imp);
                p.lex(&Lexer::TokenTypes::Semicolon);
                return std::unique_ptr<Statement>(std::move(expr));
            } else {
                p.lex(next);
                return RecursiveWhile<std::unique_ptr<Statement>>([&](auto continuation) {
                    return p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::VarCreate, &Lexer::TokenTypes::Colon }, [&](Lexer::Token& next) {
                        if (next.GetType() == &Lexer::TokenTypes::Comma) {
                            return p.lex(&Lexer::TokenTypes::Identifier, [&](Lexer::Token& ident) {
                                names.push_back({ ident.GetValue(), ident.GetLocation() });
                                return continuation();
                            });
                        }
                        std::unique_ptr<Expression> type;
                        if (next.GetType() == &Lexer::TokenTypes::Colon) {
                            type = p.ParseExpression(imp);
                            p.lex(&Lexer::TokenTypes::VarCreate);
                        }
                        auto&& init = p.ParseExpression(imp);
                        auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
                        return std::unique_ptr<Statement>(Wide::Memory::MakeUnique<Variable>(std::move(names), std::move(init), t.GetLocation() + semi, std::move(type)));
                    });
                });
            }
        });
    };

    Statements[&Lexer::TokenTypes::Break] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        return p.lex(&Lexer::TokenTypes::Semicolon, [&](Lexer::Token& semi) {
            return Wide::Memory::MakeUnique<Break>(t.GetLocation() + semi.GetLocation());
        });
    };

    Statements[&Lexer::TokenTypes::Continue] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t)  {
        return p.lex(&Lexer::TokenTypes::Semicolon, [&](Lexer::Token& semi) {
            return Wide::Memory::MakeUnique<Continue>(t.GetLocation() + semi.GetLocation());
        });
    };

    Statements[&Lexer::TokenTypes::Throw] = [](Parser& p, std::shared_ptr<Parse::Import> imp, Lexer::Token& t) {
        auto&& expected = p.GetExpressionBeginnings();
        expected.insert(&Lexer::TokenTypes::Semicolon);
        return p.lex(expected, [&](Lexer::Token& next) {
            if (next.GetType() == &Lexer::TokenTypes::Semicolon)
                return Wide::Memory::MakeUnique<Throw>(t.GetLocation() + next.GetLocation());
            p.lex(next);
            auto&& expr = p.ParseExpression(imp);
            return p.lex(&Lexer::TokenTypes::Semicolon, [&](Lexer::Token& semi) {
                return Wide::Memory::MakeUnique<Throw>(t.GetLocation() + semi.GetLocation(), std::move(expr));
            });
        });
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
        return RecursiveWhile<std::unique_ptr<TryCatch>>([&](auto continuation) {
            return p.lex(expected, [&](Lexer::Token& next) {
                if (next.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
                    p.lex(next);
                    stmts.push_back(p.ParseStatement(imp));
                    return continuation();
                }
                auto&& compound = Wide::Memory::MakeUnique<CompoundStatement>(std::move(stmts), open + t.GetLocation());
                // Catches- there must be at least one.
                auto&& catches = std::vector<Catch>();
                return RecursiveWhile<std::unique_ptr<TryCatch>>([&](auto continuation) {
                    auto current_catch_expected = catches.empty() ? std::unordered_set<Lexer::TokenType>({ &Lexer::TokenTypes::Catch }) : catch_expected;
                    return p.lex(current_catch_expected, [&](Lexer::Token& catch_) {
                        if (catch_.GetType() != &Lexer::TokenTypes::Catch) {
                            p.lex(catch_);
                            return Wide::Memory::MakeUnique<TryCatch>(std::move(compound), std::move(catches), t.GetLocation() + next.GetLocation());
                        }
                        p.lex(&Lexer::TokenTypes::OpenBracket);
                        auto&& catch_stmts = std::vector<std::unique_ptr<Statement>>();
                        return p.lex({ &Lexer::TokenTypes::Ellipsis, &Lexer::TokenTypes::Identifier }, [&](Lexer::Token& next) {
                            if (next.GetType() == &Lexer::TokenTypes::Ellipsis) {
                                p.lex(&Lexer::TokenTypes::CloseBracket);
                                p.lex(&Lexer::TokenTypes::OpenCurlyBracket);
                                return RecursiveWhile<std::unique_ptr<TryCatch>>([&](auto continuation) {
                                    return p.lex(expected, [&](Lexer::Token& next) {
                                        if (next.GetType() == &Lexer::TokenTypes::CloseCurlyBracket) {
                                            catches.push_back(Catch{ std::move(catch_stmts) });
                                            return Wide::Memory::MakeUnique<TryCatch>(std::move(compound), std::move(catches), t.GetLocation() + next.GetLocation());
                                        }
                                        p.lex(next);
                                        catch_stmts.push_back(p.ParseStatement(imp));
                                        return continuation();
                                    });
                                });
                            }                            
                            auto name = next.GetValue();
                            p.lex(&Lexer::TokenTypes::Colon);
                            auto&& type = p.ParseExpression(imp);
                            p.lex(&Lexer::TokenTypes::CloseBracket);
                            p.lex(&Lexer::TokenTypes::OpenCurlyBracket);
                            return RecursiveWhile<std::unique_ptr<TryCatch>>([&](auto nested_continuation) {
                                return p.lex(expected, [&](Lexer::Token& next) {
                                    if (next.GetType() == &Lexer::TokenTypes::CloseCurlyBracket) {
                                        catches.push_back(Catch{ std::move(catch_stmts), name, std::move(type) });
                                        return continuation();
                                    }
                                    p.lex(next);
                                    catch_stmts.push_back(p.ParseStatement(imp));
                                    return nested_continuation();
                                });
                            });
                        });
                    });
                });
            });
        });
    };
    
    TypeTokens[&Lexer::TokenTypes::Public] = [](Parser& p, TypeMembers* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        p.lex(&Lexer::TokenTypes::Colon);
        return Parse::Access::Public;
    };
    TypeTokens[&Lexer::TokenTypes::Private] = [](Parser& p, TypeMembers* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        p.lex(&Lexer::TokenTypes::Colon);
        return Parse::Access::Private;
    };
    TypeTokens[&Lexer::TokenTypes::Protected] = [](Parser& p, TypeMembers* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        p.lex(&Lexer::TokenTypes::Colon);
        return Parse::Access::Protected;
    };
    TypeTokens[&Lexer::TokenTypes::Using] = [](Parser& p, TypeMembers* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        return p.lex(&Lexer::TokenTypes::Identifier, [&](Lexer::Token& ident) {
            p.lex(&Lexer::TokenTypes::VarCreate);
            auto&& expr = p.ParseExpression(imp);
            return p.lex(&Lexer::TokenTypes::Semicolon, [&](Lexer::Token& semi) {
                auto&& use = Wide::Memory::MakeUnique<Using>(std::move(expr), tok.GetLocation() + semi.GetLocation());
                if (t->nonvariables.find(ident.GetValue()) != t->nonvariables.end())
                    throw std::runtime_error("Found using, but there was already an overload set there.");
                t->nonvariables[ident.GetValue()] = std::make_pair(access, std::move(use));
                return access;
            });
        });
    };

    TypeTokens[&Lexer::TokenTypes::From] = [](Parser& p, TypeMembers* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok) {
        auto&& expr = p.ParseExpression(imp);
        p.lex(&Lexer::TokenTypes::Import);
        std::vector<Parse::Name> names;
        bool constructors = false;
        return RecursiveWhile<Parse::Access>([&](auto continuation) {
            return p.lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator, &Lexer::TokenTypes::Type }, [&](Lexer::Token& lead) {
                if (lead.GetType() == &Lexer::TokenTypes::Operator) {
                    names.push_back(p.ParseOperatorName(p.GetAllOperators()));
                } else if (lead.GetType() == &Lexer::TokenTypes::Identifier) {
                    names.push_back(lead.GetValue());
                } else
                    constructors = true;
                return p.lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::Semicolon }, [&](Lexer::Token& next) {
                    if (next.GetType() == &Lexer::TokenTypes::Semicolon) {
                        t->imports.push_back(std::make_tuple(std::move(expr), names, constructors));
                        return access;
                    }
                    return continuation();
                });
            });
        });
    };

    TypeAttributeTokens[&Lexer::TokenTypes::OpenSquareBracket] = [](Parser& p, TypeMembers* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attributes) {
        attributes.push_back(p.ParseAttribute(tok, imp));
        return p.lex(GetExpectedTokenTypesFromMap(p.TypeAttributeTokens), [&](Lexer::Token& next) {
            return p.TypeAttributeTokens[next.GetType()](p, t, access, imp, next, std::move(attributes));
        });
    };

    TypeAttributeTokens[&Lexer::TokenTypes::Dynamic] = [](Parser& p, TypeMembers* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attributes) {
        return p.lex(GetExpectedTokenTypesFromMap(p.DynamicMemberFunctions), [&](Lexer::Token& intro) {
            auto&& func = p.DynamicMemberFunctions[intro.GetType()](p, t, access, imp, intro, std::move(attributes));
            func->dynamic = true;
            return access;
        });
    };

    TypeAttributeTokens[&Lexer::TokenTypes::Identifier] = [](Parser& p, TypeMembers* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attributes) {
        return p.lex({ &Lexer::TokenTypes::VarCreate, &Lexer::TokenTypes::Colon, &Lexer::TokenTypes::OpenBracket }, [&](Lexer::Token& next) {
            if (next.GetType() == &Lexer::TokenTypes::VarCreate) {
                auto&& init = p.ParseExpression(imp);
                auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
                t->variables.push_back(MemberVariable(tok.GetValue(), std::move(init), access, tok.GetLocation() + semi, std::move(attributes), nullptr));
                return access;
            } else if (next.GetType() == &Lexer::TokenTypes::Colon) {
                auto&& type = p.ParseExpression(imp);
                return p.lex({ &Lexer::TokenTypes::Assignment, &Lexer::TokenTypes::Semicolon }, [&](Lexer::Token& ass) {
                    if (ass.GetType() == &Lexer::TokenTypes::Semicolon) {
                        t->variables.push_back(MemberVariable(tok.GetValue(), nullptr, access, tok.GetLocation() + ass.GetLocation(), std::move(attributes), std::move(type)));
                        return access;
                    }
                    auto&& init = p.ParseExpression(imp);
                    auto&& semi = p.lex(&Lexer::TokenTypes::Semicolon);
                    t->variables.push_back(MemberVariable(tok.GetValue(), std::move(init), access, tok.GetLocation() + semi, std::move(attributes), std::move(type)));
                    return access;
                });
            } else {
                auto&& func = p.ParseFunction(tok, imp, std::move(attributes));
                auto&& overset = boost::get<OverloadSet<std::unique_ptr<Function>>>(&t->nonvariables[tok.GetValue()]);
                if (!overset) throw std::runtime_error("Found overload set but there was already a property by that name.");
                (*overset)[access].insert(std::move(func));
                return access;
            }
        });
    };
    
    TypeAttributeTokens[&Lexer::TokenTypes::Type] = [](Parser& p, TypeMembers* t, Parse::Access access, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attributes) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& con = p.ParseConstructor(tok, imp, std::move(attributes));
        t->constructor_decls[access].insert(std::move(con));
        return access;
    };

    TypeAttributeTokens[&Lexer::TokenTypes::Operator] = [](Parser& p, TypeMembers* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) {
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

    TypeAttributeTokens[&Lexer::TokenTypes::Negate] = [](Parser& p, TypeMembers* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) {
        t->destructor_decl = p.ParseDestructor(tok, imp, std::move(attrs));
        return a;
    };


    DynamicMemberFunctions[&Lexer::TokenTypes::Identifier] = [](Parser& p, TypeMembers* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) {
        p.lex(&Lexer::TokenTypes::OpenBracket);
        auto&& func = p.ParseFunction(tok, imp, std::move(attrs));
        auto funcptr = func.get();
        auto&& overset = boost::get<OverloadSet<std::unique_ptr<Function>>>(&t->nonvariables[tok.GetValue()]);
        if (!overset) throw std::runtime_error("Found overload set but there was already a property by that name.");
        (*overset)[a].insert(std::move(func));
        return funcptr;
    };

    DynamicMemberFunctions[&Lexer::TokenTypes::Negate] = [](Parser& p, TypeMembers* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) {
        t->destructor_decl = p.ParseDestructor(tok, imp, std::move(attrs));
        return t->destructor_decl.get();
    };

    DynamicMemberFunctions[&Lexer::TokenTypes::Operator] = [](Parser& p, TypeMembers* t, Parse::Access a, std::shared_ptr<Parse::Import> imp, Lexer::Token& tok, std::vector<Attribute> attrs) -> Function* {
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
    return lex(expected, [&](Lexer::Token& op) {
        current.push_back(op.GetType());
        auto&& remaining = GetRemainingValidOperators(valid_ops, current);
        if (valid_ops.find(current) != valid_ops.end())
            return ParseOperatorName(remaining, current, current);
        return ParseOperatorName(remaining, current);
    });
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
Attribute Parser::ParseAttribute(Lexer::Token& tok, std::shared_ptr<Parse::Import> imp) {
    auto&& initialized = ParseExpression(imp);
    lex(&Lexer::TokenTypes::VarCreate);
    auto&& initializer = ParseExpression(imp);
    auto end = lex(&Lexer::TokenTypes::CloseSquareBracket);
    return Attribute(std::move(initialized), std::move(initializer), tok.GetLocation() + end);
}

ModuleParseResult Parser::ParseGlobalModuleLevelDeclaration(std::shared_ptr<Parse::Import> imp) {
    // Can only get here if ParseGlobalModuleContents found a token, so we know we have at least one.
    return lex(GetExpectedTokenTypesFromMap(GlobalModuleTokens, GlobalModuleAttributeTokens), [&](Lexer::Token& t) {
        if (GlobalModuleTokens.find(t.GetType()) != GlobalModuleTokens.end())
            return GlobalModuleTokens[t.GetType()](*this, { imp, Parse::Access::Public }, t);
        return GlobalModuleAttributeTokens[t.GetType()](*this, { imp, Parse::Access::Public }, t, std::vector<Attribute>());
    });
}

std::vector<ModuleMember> Parser::ParseGlobalModuleContents(std::shared_ptr<Parse::Import> imp) {
    std::vector<ModuleMember> members;
    auto t = lex();
    while (t) {
        lex(*t);
        auto result = ParseGlobalModuleLevelDeclaration(imp);
        if (result.member)
            members.push_back(std::move(*result.member));
        imp = result.newstate.imp;
        t = lex();
    }
    return members;
}
ModuleParse Parser::ParseModuleContents(std::shared_ptr<Parse::Import> imp) {
    auto&& access = Parse::Access::Public;
    std::vector<ModuleMember> members;
    return RecursiveWhile<ModuleParse>([&](std::function<ModuleParse()> continuation) {
        auto expected = GetExpectedTokenTypesFromMap(ModuleTokens, GlobalModuleTokens, GlobalModuleAttributeTokens);
        expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        return lex(expected, [&](Lexer::Token& t) {
            if (t.GetType() == &Lexer::TokenTypes::CloseCurlyBracket) {
                return ModuleParse{ std::move(members), t.GetLocation() };
            }
            lex(t);
            auto result = ParseModuleLevelDeclaration({ imp, access });
            if (result.member)
                members.push_back(std::move(*result.member));
            access = result.newstate.access;
            imp = result.newstate.imp;
            return continuation();
        });
    });
}
ModuleParseResult Parser::ParseModuleLevelDeclaration(ModuleParseState state) {
    return lex(GetExpectedTokenTypesFromMap(ModuleTokens, GlobalModuleTokens, GlobalModuleAttributeTokens), [&](Lexer::Token& t) {
        if (ModuleTokens.find(t.GetType()) != ModuleTokens.end())
            return ModuleTokens[t.GetType()](*this, state, t);
        if (GlobalModuleTokens.find(t.GetType()) != GlobalModuleTokens.end())
            return GlobalModuleTokens[t.GetType()](*this, state, t);
        assert(GlobalModuleAttributeTokens.find(t.GetType()) != GlobalModuleAttributeTokens.end());
        return GlobalModuleAttributeTokens[t.GetType()](*this, state, t, std::vector<Attribute>());
    });
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
    return lex(GetExpressionBeginnings(), [&](Lexer::Token& tok) {
        if (UnaryOperators.find(tok.GetType()) != UnaryOperators.end())
            return UnaryOperators[tok.GetType()](*this, imp, tok);
        lex(tok);
        return ParsePostfixExpression(imp);
    });
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
    return lex(GetExpectedTokenTypesFromMap(PrimaryExpressions), [&](Lexer::Token& t) {
        return PrimaryExpressions[t.GetType()](*this, imp, t);
    });
}

std::vector<std::unique_ptr<Expression>> Parser::ParseFunctionArguments(std::shared_ptr<Parse::Import> imp) {
    auto&& expected = GetExpressionBeginnings();
    expected.insert(&Lexer::TokenTypes::CloseBracket);
    std::vector<std::unique_ptr<Expression>> result;
    return lex(expected, [&, this](Wide::Lexer::Token& tok) {
        if (tok.GetType() == &Lexer::TokenTypes::CloseBracket)
            return std::move(result);
        lex(tok);
        return RecursiveWhile<std::vector<std::unique_ptr<Expression>>>([&, this](auto continuation) {
            result.push_back(this->ParseExpression(imp));
            return lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseBracket }, [&](Lexer::Token& tok) {
                if (tok.GetType() == &Lexer::TokenTypes::CloseBracket)
                    return std::move(result);
                return continuation();
            });
        });
    });
}
std::vector<Variable> Parser::ParseLambdaCaptures(std::shared_ptr<Parse::Import> imp) {
    std::vector<Variable> variables;
    return RecursiveWhile<std::vector<Variable>>([&](std::function<std::vector<Variable>()> continuation) {
        return lex(&Lexer::TokenTypes::Identifier, [&](Lexer::Token& ident) {
            lex(&Lexer::TokenTypes::VarCreate);
            auto&& init = ParseExpression(imp);
            return lex({ &Lexer::TokenTypes::CloseSquareBracket, &Lexer::TokenTypes::Comma }, [&](Lexer::Token& tok) {
                variables.push_back(Variable(std::vector<Variable::Name>{ { ident.GetValue(), ident.GetLocation() }}, std::move(init), tok.GetLocation() + init->location, nullptr));
                if (tok.GetType() == &Lexer::TokenTypes::CloseSquareBracket)
                    return std::move(variables);
                return continuation();
            });
        });
    });
}
std::unique_ptr<Statement> Parser::ParseStatement(std::shared_ptr<Parse::Import> imp) {
    return lex(GetExpectedTokenTypesFromMap(Statements, UnaryOperators, PrimaryExpressions), [&](Lexer::Token& t) -> std::unique_ptr<Statement> {
        if (Statements.find(t.GetType()) != Statements.end())
            return Statements[t.GetType()](*this, imp, t);
        // Else, expression statement.
        lex(t);
        auto&& expr = ParseExpression(imp);
        lex(&Lexer::TokenTypes::Semicolon);
        return std::move(expr);
    });
}
std::vector<std::unique_ptr<Expression>> Parser::ParseTypeBases(std::shared_ptr<Parse::Import> imp) {
    auto group = std::vector<std::unique_ptr<Expression>>();
    return RecursiveWhile<std::vector<std::unique_ptr<Expression>>>([&](std::function<std::vector<std::unique_ptr<Expression>>()> continuation){
        return lex({ &Lexer::TokenTypes::Colon, &Lexer::TokenTypes::OpenCurlyBracket }, [&](Lexer::Token& colon) {
            if (colon.GetType() == &Lexer::TokenTypes::Colon) {
                group.push_back(ParseExpression(imp));
                return continuation();
            }
            lex(colon);
            return std::move(group);
        });
    });
}
std::vector<FunctionArgument> Parser::ParseFunctionDefinitionArguments(std::shared_ptr<Parse::Import> imp) {
    auto ret = std::vector<FunctionArgument>();
    auto type = std::unique_ptr<Expression>();
    auto default_value = std::unique_ptr<Expression>();
    return lex({ &Lexer::TokenTypes::CloseBracket, &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::This }, [&](Lexer::Token& t) {
        if (t.GetType() == &Lexer::TokenTypes::CloseBracket)
            return std::move(ret);
        lex(t);
        // At least one argument.
        // The form is this or this : expr, then t, or t : expr
        bool first = true;
        return RecursiveWhile<std::vector<FunctionArgument>>([&](std::function<std::vector<FunctionArgument>()> continuation) {
            std::unordered_set<Lexer::TokenType> expected = { &Lexer::TokenTypes::Identifier };
            if (first)
                expected.insert(&Lexer::TokenTypes::This);
            return lex(expected, [&](Lexer::Token& ident) {
                first = false;
                auto handle_noncolon = [&](Lexer::Token& t2) {
                    if (t2.GetType() == &Lexer::TokenTypes::CloseBracket) {
                        ret.push_back(FunctionArgument(ident.GetLocation(), ident.GetValue(), std::move(type), std::move(default_value)));
                        return std::move(ret);
                    }
                    if (t2.GetType() == &Lexer::TokenTypes::Comma) {
                        ret.push_back({ ident.GetLocation(), ident.GetValue(), std::move(type), std::move(default_value) });
                        return continuation();
                    }
                    default_value = ParseExpression(imp);
                    ret.push_back(FunctionArgument(ident.GetLocation() + lex.GetLastToken().GetLocation(), ident.GetValue(), std::move(type), nullptr));
                    return lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseBracket }, [&](Lexer::Token& next) {
                        if (next.GetType() == &Lexer::TokenTypes::Comma)
                            return continuation();
                        return std::move(ret);
                    });
                };
                return lex({ &Lexer::TokenTypes::VarCreate, &Lexer::TokenTypes::CloseBracket, &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::Colon }, [&](Lexer::Token& t2) {
                    if (t2.GetType() == &Lexer::TokenTypes::Colon) {
                        type = ParseExpression(imp);
                        return lex({ &Lexer::TokenTypes::Comma, &Lexer::TokenTypes::CloseBracket, &Lexer::TokenTypes::VarCreate }, [&](Lexer::Token& t2) {
                            return handle_noncolon(t2);
                        });
                    }
                    return handle_noncolon(t2);
                });
            });
        });
    });
}
std::unique_ptr<Type> Parser::ParseTypeDeclaration(Lexer::Range tloc, std::shared_ptr<Parse::Import> imp, Lexer::Token& ident, std::vector<Attribute> attrs) {
    auto&& bases = ParseTypeBases(imp);
    return lex(&Lexer::TokenTypes::OpenCurlyBracket, [&](Lexer::Token& t) {
        TypeMembers members;
        auto close = ParseTypeBody(&members, imp);
        auto&& ty = Wide::Memory::MakeUnique<Type>(members, std::move(attrs), tloc, t.GetLocation(), close, ident.GetLocation());
        return std::move(ty);
    });
}
Lexer::Range Parser::ParseTypeBody(TypeMembers* ty, std::shared_ptr<Parse::Import> imp) {
    auto&& access = Parse::Access::Public;
    auto&& expected = GetExpectedTokenTypesFromMap(TypeTokens, TypeAttributeTokens, DynamicMemberFunctions);
    expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
    return RecursiveWhile<Lexer::Range>([&](std::function<Lexer::Range()> continuation) {
        return lex(expected, [&](Lexer::Token& t) {
            if (t.GetType() == &Lexer::TokenTypes::CloseCurlyBracket)
                return t.GetLocation();
            if (TypeTokens.find(t.GetType()) != TypeTokens.end())
                access = TypeTokens[t.GetType()](*this, ty, access, imp, t);
            else if (TypeAttributeTokens.find(t.GetType()) != TypeAttributeTokens.end())
                access = TypeAttributeTokens[t.GetType()](*this, ty, access, imp, t, std::vector<Attribute>());
            else
                DynamicMemberFunctions[t.GetType()](*this, ty, access, imp, t, std::vector<Attribute>());
            return continuation();
        });
    });
}
std::unique_ptr<Function> Parser::ParseFunction(const Lexer::Token& first, std::shared_ptr<Parse::Import> imp, std::vector<Attribute> attrs) {
    auto&& args = ParseFunctionDefinitionArguments(imp);
    // Gotta be := or {
    std::unique_ptr<Expression> explicit_return = nullptr;
    auto handle_nocolon = [&](Lexer::Token& next) {
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
        return RecursiveWhile<std::unique_ptr<Function>>([&](std::function<std::unique_ptr<Function>()> continuation) {
            return lex(expected, &Lexer::TokenTypes::CloseCurlyBracket, [&](Lexer::Token& t) {
                if (t.GetType() == &Lexer::TokenTypes::CloseCurlyBracket)
                    return Wide::Memory::MakeUnique<Function>(std::move(statements), first.GetLocation() + t.GetLocation(), std::move(args), std::move(explicit_return), std::move(attrs));
                lex(t);
                statements.push_back(ParseStatement(imp));
                return continuation();
            });
        });        
    };
    return lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Colon, &Lexer::TokenTypes::Default, &Lexer::TokenTypes::Delete }, [&](Lexer::Token& next) {
        if (next.GetType() == &Lexer::TokenTypes::Default) {
            auto&& func = Wide::Memory::MakeUnique<Function>(std::vector<std::unique_ptr<Statement>>(), first.GetLocation() + next.GetLocation(), std::move(args), nullptr, std::move(attrs));
            func->defaulted = true;
            return std::move(func);
        }
        if (next.GetType() == &Lexer::TokenTypes::Colon) {
            explicit_return = ParseExpression(imp);
            return lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Abstract }, [&](Lexer::Token& next) {
                return handle_nocolon(next);
            });
        }
        return handle_nocolon(next);
    });
}
std::unique_ptr<Constructor> Parser::ParseConstructor(const Lexer::Token& first, std::shared_ptr<Parse::Import> imp, std::vector<Attribute> attrs) {
    auto&& args = ParseFunctionDefinitionArguments(imp);
    // Gotta be : or { or default
    return lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Colon, &Lexer::TokenTypes::Default, &Lexer::TokenTypes::Delete }, [&](Lexer::Token& colon_or_open) {
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
        lex(colon_or_open);
        return RecursiveWhile<std::unique_ptr<Constructor>>([&](std::function<std::unique_ptr<Constructor>()> continuation) {
            return lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Colon }, [&](Lexer::Token& colon_or_open) {
                auto&& expected = GetExpressionBeginnings();
                expected.insert(&Lexer::TokenTypes::Type);
                return lex(expected, [&](Lexer::Token& next) {
                    if (next.GetType() == &Lexer::TokenTypes::Type) {
                        // Delegating constructor.
                        lex(&Lexer::TokenTypes::VarCreate);
                        auto&& initializer = ParseExpression(imp);
                        initializers.push_back({ Wide::Memory::MakeUnique<Identifier>("type", imp, next.GetLocation()), std::move(initializer), next.GetLocation() + initializer->location });
                        lex(&Lexer::TokenTypes::OpenCurlyBracket);
                        // Gotta be { by this point.
                        std::vector<std::unique_ptr<Statement>> statements;
                        auto&& expected = GetStatementBeginnings();
                        expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
                        return RecursiveWhile<std::unique_ptr<Constructor>>([&](std::function<std::unique_ptr<Constructor>()> continuation) {
                            return lex(expected, [&](Lexer::Token& t) {
                                if (t.GetType() != &Lexer::TokenTypes::CloseCurlyBracket) {
                                    lex(t);
                                    statements.push_back(ParseStatement(imp));
                                    return continuation();
                                }
                                return Wide::Memory::MakeUnique<Constructor>(std::move(statements), first.GetLocation() + t.GetLocation(), std::move(args), std::move(initializers), std::move(attrs));
                            });
                        });
                    }
                    lex(next);
                    auto&& initialized = ParseExpression(imp);
                    lex(&Lexer::TokenTypes::VarCreate);
                    auto&& initializer = ParseExpression(imp);
                    initializers.push_back({ std::move(initialized), std::move(initializer), colon_or_open.GetLocation() + initializer->location });
                    return continuation();
                });
            });
        });
    });
}
std::unique_ptr<Destructor> Parser::ParseDestructor(const Lexer::Token& first, std::shared_ptr<Parse::Import> imp, std::vector<Attribute> attrs) {
    // ~ type ( ) { stuff }
    lex(&Lexer::TokenTypes::Type);
    lex(&Lexer::TokenTypes::OpenBracket);
    lex(&Lexer::TokenTypes::CloseBracket);
    return lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Default }, [&](Lexer::Token& default_or_open) {
        if (default_or_open.GetType() == &Lexer::TokenTypes::Default) {
            return Wide::Memory::MakeUnique<Destructor>(std::vector<std::unique_ptr<Statement>>(), first.GetLocation() + default_or_open.GetLocation(), std::move(attrs), true);
        }
        std::vector<std::unique_ptr<Statement>> body;
        auto&& expected = GetStatementBeginnings();
        expected.insert(&Lexer::TokenTypes::CloseCurlyBracket);
        return RecursiveWhile<std::unique_ptr<Destructor>>([&](std::function<std::unique_ptr<Destructor>()> continuation) {
            return lex(expected, [&](Lexer::Token& t) {
                if (t.GetType() == &Lexer::TokenTypes::CloseBracket)
                    return Wide::Memory::MakeUnique<Destructor>(std::move(body), first.GetLocation() + t.GetLocation(), std::move(attrs), false);
                lex(t);
                body.push_back(ParseStatement(imp));
                return continuation();
            }); 
        });
    });
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

void Parser::AddMemberToModule(Module* m, ModuleMember member) {
    Module temp(Wide::Util::none);
    if (auto con = boost::get<ModuleMember::ConstructorDecl>(&member.member)) {
        temp.constructor_decls.insert(std::move(con->constructor));
    }
    if (auto des = boost::get<ModuleMember::DestructorDecl>(&member.member)) {
        temp.destructor_decls.insert(std::move(des->destructor));
    }
    if (auto op = boost::get<ModuleMember::OperatorOverload>(&member.member)) {
        temp.OperatorOverloads[op->name][op->access].insert(std::move(op->function));
    }
    if (auto named = boost::get<ModuleMember::NamedMember>(&member.member)) {
        temp.named_decls[named->name] = std::move(named->member);
    }
    m->unify(temp);
}
std::unique_ptr<Module> Parser::ParseModule(Lexer::Range where, ModuleParseState state, Lexer::Token& module) {
     return lex({ &Lexer::TokenTypes::OpenCurlyBracket, &Lexer::TokenTypes::Dot }, [&](Lexer::Token& token) {
        if (token.GetType() == &Lexer::TokenTypes::OpenCurlyBracket) {
            auto contents = ParseModuleContents(state.imp);
            auto mod = Wide::Memory::MakeUnique<Module>(ModuleLocation::LongForm{
                module.GetLocation(),
            where,
                token.GetLocation(),
                contents.CloseCurly
            });
            for (auto&& content : contents.results)
                AddMemberToModule(mod.get(), std::move(content));
            return mod;
        }
        return lex(&Lexer::TokenTypes::Identifier, [&](Lexer::Token& next) {
            auto nested = ParseModule(next.GetLocation(), state, module);
            auto mod = Wide::Memory::MakeUnique<Module>(ModuleLocation::ShortForm{ where });
            mod->named_decls[next.GetValue()] = std::make_pair(Access::Public, std::move(nested));
            return mod;
        });
    });
}
std::unique_ptr<Module> Parser::ParseModuleFunction(Lexer::Range where, ModuleParseState state, std::vector<Attribute> attributes)
{
    // When we got here, we found "identifier ." at module scope.
    return lex({ &Lexer::TokenTypes::Identifier, &Lexer::TokenTypes::Operator }, [&](Lexer::Token& token) {
        auto result = GlobalModuleAttributeTokens[token.GetType()](*this, state, token, std::move(attributes));
        auto mod = std::make_unique<Module>(ModuleLocation::ShortForm{ where });
        AddMemberToModule(mod.get(), std::move(*result.member));
        return mod;
    });
}
