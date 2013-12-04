#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/OverloadSet.h>
#include <sstream>

using namespace Wide;
using namespace Semantic;

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/AST.h>
#pragma warning(pop)

Type* Type::GetContext(Analyzer& a) {
    return a.GetGlobalModule();
}
clang::QualType Type::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    throw std::runtime_error("This type has no Clang representation.");
}

DeferredExpression DeferredExpression::BuildDereference(Context c) {
    auto copy = delay;
    return [=](Type* t) {
        return (*copy)(nullptr).BuildDereference(c);
    };
}
DeferredExpression DeferredExpression::BuildNegate(Context c) {
    auto copy = delay;
    return [=](Type* t) {
        return (*copy)(nullptr).BuildNegate(c);
    };
}
DeferredExpression DeferredExpression::AddressOf(Context c) {
    auto copy = delay;
    return [=](Type* t) {
        return (*copy)(nullptr).AddressOf(c);
    };
}
DeferredExpression DeferredExpression::BuildBooleanConversion(Context c) {
    auto copy = delay;
    return [=](Type* t) {
        return ConcreteExpression(c->GetBooleanType(), (*copy)(nullptr).BuildBooleanConversion(c));
    };
}
DeferredExpression DeferredExpression::BuildIncrement(bool b, Context c) {
    auto copy = delay;
    return [=](Type* t) {
        return (*copy)(nullptr).BuildIncrement(b, c);
    };
}
DeferredExpression DeferredExpression::PointerAccessMember(std::string name, Context c) {
    auto copy = delay;
    return DeferredExpression([=](Type* t) {
        auto expr = (*copy)(nullptr);
        auto opt = expr.PointerAccessMember(name, c);
        if (opt)
            return opt->Resolve(t);
        throw std::runtime_error("Attempted to access a member of an object, but that object had no such member.");
    });
}
DeferredExpression DeferredExpression::BuildBinaryExpression(Expression other, Lexer::TokenType what, Context c) {
    auto copy = delay;
    return DeferredExpression([=](Type* t) {
        return (*copy)(nullptr).BuildBinaryExpression(other.Resolve(nullptr), what, c).Resolve(t);
    });
}
DeferredExpression DeferredExpression::BuildBinaryExpression(Expression other, Lexer::TokenType what, std::vector<ConcreteExpression> destructors, Context c) {
    auto copy = delay;
    return DeferredExpression([=](Type* t) {
        return (*copy)(nullptr).BuildBinaryExpression(other.Resolve(nullptr), what, destructors, c).Resolve(t);
    });
}

ConcreteExpression ConcreteExpression::BuildValue(Context c) {
    return t->BuildValue(*this, c);
}
DeferredExpression DeferredExpression::BuildValue(Context c) {
    auto copy = delay;
    return DeferredExpression([=](Type* t) {
        return (*copy)(nullptr).BuildValue(c);
    });
}
Expression Expression::BuildValue(Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildValue(c);
        },
        [&](DeferredExpression& e) {
            return e.BuildValue(c);
        }
    );
    // return expr.t->BuildValue(*this, a);
}

ConcreteExpression ConcreteExpression::BuildIncrement(bool b, Context c) {
    return t->BuildIncrement(*this, b, c);
}

ConcreteExpression ConcreteExpression::BuildNegate(Context c) {
    return t->BuildNegate(*this, c);
}

Expression ConcreteExpression::BuildCall(Expression l, Expression r, Context c) {
    std::vector<Expression> exprs;
    exprs.push_back(l);
    exprs.push_back(r);
    return BuildCall(std::move(exprs), c);
}

Util::optional<Expression> ConcreteExpression::PointerAccessMember(std::string mem, Context c) {
    return t->PointerAccessMember(*this, std::move(mem), c);
}

ConcreteExpression ConcreteExpression::BuildDereference(Context c) {
    return t->BuildDereference(*this, c);
}

Expression ConcreteExpression::BuildCall(ConcreteExpression lhs, ConcreteExpression rhs, Context c) {
    std::vector<ConcreteExpression> exprs;
    exprs.push_back(lhs);
    exprs.push_back(rhs);
    return BuildCall(std::move(exprs), c);
}

Expression ConcreteExpression::BuildCall(ConcreteExpression lhs, Context c) {
    std::vector<ConcreteExpression> exprs;
    exprs.push_back(lhs);
    return BuildCall(std::move(exprs), c);
}

ConcreteExpression ConcreteExpression::AddressOf(Context c) {
    return t->AddressOf(*this, c);
}

Codegen::Expression* ConcreteExpression::BuildBooleanConversion(Context c) {
    return t->BuildBooleanConversion(*this, c);
}

Expression ConcreteExpression::BuildBinaryExpression(ConcreteExpression other, Lexer::TokenType ty, Context c) {
    return t->BuildBinaryExpression(*this, other, ty, c);
}

Expression ConcreteExpression::BuildBinaryExpression(ConcreteExpression rhs, Lexer::TokenType type, std::vector<ConcreteExpression> destructors, Context c) {
    return t->BuildBinaryExpression(*this, rhs, std::move(destructors), type, c);
}

Expression ConcreteExpression::BuildBinaryExpression(Expression l, Lexer::TokenType r, Context c) {
    if (l.contents.type() == typeid(ConcreteExpression)) {
        return BuildBinaryExpression(boost::get<ConcreteExpression>(l.contents), r, c);
    }
    ConcreteExpression self = *this;
    return DeferredExpression([=](Type* t) {
        return self.t->BuildBinaryExpression(self, l.Resolve(nullptr), r, c).Resolve(t);
    });
}

Expression ConcreteExpression::BuildMetaCall(std::vector<Expression> args, Context c) {
    // If they are all concrete, we're on a winner.
    std::vector<ConcreteExpression> concrete;
    auto copy = *this;
    auto localt = t;
    auto t = localt;
    for (auto&& arg : args) {
        if (arg.contents.type() == typeid(ConcreteExpression))
            concrete.push_back(boost::get<ConcreteExpression>(arg.contents));
        else
            return DeferredExpression([copy, c, args, t](Type* result) {
                std::vector<ConcreteExpression> concrete;
                for (auto&& arg : args)
                    concrete.push_back(arg.Resolve(nullptr));
                return t->BuildMetaCall(copy, std::move(concrete), c);
            });
    }
    return t->BuildMetaCall(*this, std::move(concrete), c);
}
Expression ConcreteExpression::BuildCall(std::vector<Expression> arguments, std::vector<ConcreteExpression> destructors, Context c) {
    std::vector<ConcreteExpression> args;
    for(auto&& arg : arguments) {
        auto copy = *this;
        if (arg.contents.type() == typeid(ConcreteExpression))
            args.push_back(boost::get<ConcreteExpression>(arg.contents));
        else
            return DeferredExpression([=](Type* result) {
                std::vector<ConcreteExpression> concrete;
                for (auto&& arg : arguments)
                    concrete.push_back(arg.Resolve(nullptr));
                return t->BuildCall(copy, std::move(concrete), destructors, c).Resolve(result);
            });
    }
    return t->BuildCall(*this, std::move(args), std::move(destructors), c);
}
Expression ConcreteExpression::BuildCall(std::vector<ConcreteExpression> arguments, std::vector<ConcreteExpression> destructors, Context c) {
    return t->BuildCall(*this, std::move(arguments), std::move(destructors), c);
}

DeferredExpression DeferredExpression::BuildCall(std::vector<Expression> args, std::vector<ConcreteExpression> destructors, Context c) {
    auto copy = delay;
    return DeferredExpression([=](Type* result) {
        std::vector<ConcreteExpression> concrete;
        for(auto&& arg : args)
            concrete.push_back(arg.Resolve(nullptr));
        return (*copy)(nullptr).BuildCall(std::move(concrete), destructors, c).Resolve(result);
    });
}

DeferredExpression DeferredExpression::AccessMember(std::string name, Context c) {
    auto copy = delay;
    return DeferredExpression([=](Type* t) {
        auto expr = (*copy)(nullptr);
        auto opt = expr.AccessMember(name, c);
        if (opt)
            return opt->Resolve(t);
        throw std::runtime_error("Attempted to access a member of an object, but that object had no such member.");
    });
}
Wide::Util::optional<Expression> ConcreteExpression::AccessMember(std::string name, Context c) {
    return t->AccessMember(*this, std::move(name), c);
}
Wide::Util::optional<Expression> Expression::AccessMember(std::string name, Context c){
    return VisitContents(
        [&](ConcreteExpression& e) -> Wide::Util::optional<Expression> {
            return e.AccessMember(std::move(name), c);
        },
        [&](DeferredExpression& e) -> Wide::Util::optional<Expression> {
            return e.AccessMember(std::move(name), c);
        }
    );
    //return t->AccessMember(*this, std::move(name), a);
}

Expression Expression::BuildCall(std::vector<Expression> args, std::vector<ConcreteExpression> destructors, Context c) {
    return VisitContents(
        [&](ConcreteExpression& expr) {
            return expr.BuildCall(std::move(args), std::move(destructors), c);
        },
        [&](DeferredExpression& expr) {
            return expr.BuildCall(std::move(args), std::move(destructors), c);
        }
    );
}

Expression ConcreteExpression::BuildCall(std::vector<Expression> args, Context c) {
    // If they are all concrete, we're on a winner.
    std::vector<ConcreteExpression> concrete;
    auto copy = *this;
    for(auto&& arg : args) {
        if (arg.contents.type() == typeid(ConcreteExpression))
            concrete.push_back(boost::get<ConcreteExpression>(arg.contents));
        else
            return DeferredExpression([=](Type* result) {
                std::vector<ConcreteExpression> concrete;
                for (auto&& arg : args)
                    concrete.push_back(arg.Resolve(nullptr));
                return t->BuildCall(copy, std::move(concrete), c).Resolve(result);
            });
    }
    return t->BuildCall(*this, std::move(concrete), c);
}
DeferredExpression DeferredExpression::BuildCall(std::vector<Expression> args, Context c) {
    auto copy = delay;
    return DeferredExpression([=](Type* result) {
        std::vector<ConcreteExpression> concrete;
        for(auto&& arg : args)
            if (arg.contents.type() == typeid(ConcreteExpression))
                concrete.push_back(boost::get<ConcreteExpression>(arg.contents));
            else
                concrete.push_back(boost::get<DeferredExpression>(arg.contents)(nullptr));
        return (*copy)(nullptr).BuildCall(std::move(concrete), c).Resolve(result);
    });
}
Expression ConcreteExpression::BuildCall(std::vector<ConcreteExpression> exprs, Context c) {
    return t->BuildCall(*this, std::move(exprs), c);
}

Expression Expression::BuildCall(std::vector<Expression> args, Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildCall(std::move(args), c);
        },
        [&](DeferredExpression& e) {
            return e.BuildCall(std::move(args), c);
        }
    );
}

Expression ConcreteExpression::BuildCall(Context c) {
    return BuildCall(std::vector<ConcreteExpression>(), c);
}
DeferredExpression DeferredExpression::BuildCall(Context c) {
    return BuildCall(std::vector<Expression>(), c);
}
Expression Expression::BuildCall(Context c) {
    return BuildCall(std::vector<Expression>(), c);
}

ConcreteExpression ConcreteExpression::BuildMetaCall(std::vector<ConcreteExpression> args, Context c) {
    return t->BuildMetaCall(*this, std::move(args), c);
}
DeferredExpression DeferredExpression::BuildMetaCall(std::vector<Expression> args, Context c) {
    auto copy = delay;
    return DeferredExpression([=](Type* t) {
        std::vector<ConcreteExpression> concrete;
        for (auto&& arg : args) {
            concrete.push_back(arg.Resolve(nullptr));
        }
        return (*copy)(nullptr).BuildMetaCall(std::move(concrete), c);
    });
}

Expression Expression::BuildMetaCall(std::vector<Expression> args, Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildMetaCall(std::move(args), c);
        },
        [&](DeferredExpression& e) {
            return e.BuildMetaCall(std::move(args), c);
        }
    );
    //return t->BuildMetaCall(*this, std::move(args), a);
}

Expression Expression::BuildDereference(Context c)  {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildDereference(c);
        },
        [&](DeferredExpression& e) {
            return e.BuildDereference(c);
        }
    );
    //return t->BuildDereference(*this, a);
}
Expression Expression::BuildIncrement(bool postfix, Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildIncrement(postfix, c);
        },
        [&](DeferredExpression& e) {
            return e.BuildIncrement(postfix, c);
        }
    );
    //return t->BuildIncrement(*this, postfix, a);
}  

Wide::Util::optional<Expression> Expression::PointerAccessMember(std::string name, Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Wide::Util::optional<Expression> {
            return e.PointerAccessMember(std::move(name), c);
        },
        [&](DeferredExpression& e) {
            return e.PointerAccessMember(std::move(name), c);
        }
    );
    //return t->PointerAccessMember(*this, std::move(name), a);
}
Expression Expression::AddressOf(Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.AddressOf(c);
        },
        [&](DeferredExpression& e) {
            return e.AddressOf(c);
        }
    );
    //return t->AddressOf(*this, a);
}
Expression Expression::BuildBooleanConversion(Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return ConcreteExpression(c->GetBooleanType(), e.BuildBooleanConversion(c));
        },
        [&](DeferredExpression& e) {
            return e.BuildBooleanConversion(c);
        }
    );
    //return t->BuildBooleanConversion(*this, a);
}
Expression Expression::BuildBinaryExpression(Expression rhs, Lexer::TokenType type, std::vector<ConcreteExpression> destructors, Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildBinaryExpression(rhs, type, std::move(destructors), c);
        },
        [&](DeferredExpression& e) {
            return e.BuildBinaryExpression(rhs, type, std::move(destructors), c);
        }
    );
}
Expression ConcreteExpression::BuildBinaryExpression(Expression rhs, Lexer::TokenType type, std::vector<ConcreteExpression> destructors, Context c) {
    if (rhs.contents.type() == typeid(ConcreteExpression)) {
        return t->BuildBinaryExpression(*this, boost::get<ConcreteExpression>(rhs.contents), std::move(destructors), type, c);
    }
    ConcreteExpression self = *this;
    return DeferredExpression([=](Type* t) {
        return self.t->BuildBinaryExpression(self, rhs.Resolve(nullptr), destructors, type, c).Resolve(t);
    });
    
}
Expression Expression::BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildBinaryExpression(rhs, type, c);
        },
        [&](DeferredExpression& e) {
            return e.BuildBinaryExpression(rhs, type, c);
        }
    );
    //return t->BuildBinaryExpression(*this, rhs, type, a);
}
Expression Expression::BuildNegate(Context c) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildNegate(c);
        },
        [&](DeferredExpression& e) {
            return e.BuildNegate(c);
        }
    );
    //return t->BuildNegate(*this, a);
}
Expression Expression::BuildCall(Expression arg, Context c) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildCall(std::move(args), c);
}
Expression Expression::BuildCall(Expression lhs, Expression rhs, Context c) {
    std::vector<Expression> args;
    args.push_back(lhs);
    args.push_back(rhs);
    return BuildCall(std::move(args), c);
}

ConcreteExpression Type::BuildValueConstruction(ConcreteExpression arg, Context c) {
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildValueConstruction(std::move(args), c);
}
ConcreteExpression Type::BuildRvalueConstruction(ConcreteExpression arg, Context c) {
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildRvalueConstruction(std::move(args), c);
}
ConcreteExpression Type::BuildLvalueConstruction(ConcreteExpression arg, Context c) {
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildLvalueConstruction(std::move(args), c);
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, ConcreteExpression arg, Context c){
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildInplaceConstruction(mem, std::move(args), c);
}
ConcreteExpression Type::BuildValueConstruction(Context c) {
    std::vector<ConcreteExpression> args;
    return BuildValueConstruction(args, c);
}
ConcreteExpression Type::BuildRvalueConstruction(Context c) {
    std::vector<ConcreteExpression> args;
    return BuildRvalueConstruction(args, c);
}
ConcreteExpression Type::BuildLvalueConstruction(Context c) {
    std::vector<ConcreteExpression> args;
    return BuildLvalueConstruction(args, c);
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, Context c) {
    std::vector<ConcreteExpression> args;
    return BuildInplaceConstruction(mem, args, c);
}
Wide::Util::optional<Expression> Type::AccessMember(ConcreteExpression e, std::string name, Context c) {
    if (IsReference())
        return Decay()->AccessMember(e, std::move(name), c);
    if (name == "~type")
        return c->GetNothingFunctorType()->BuildValueConstruction(c);
    return Wide::Util::none;
}
ConcreteExpression Type::BuildValue(ConcreteExpression lhs, Context c) {
    if (IsComplexType())
        throw std::runtime_error("Internal Compiler Error: Attempted to build a complex type into a register.");
    if (lhs.t->IsReference())
        return ConcreteExpression(lhs.t->Decay(), c->gen->CreateLoad(lhs.Expr));
    return lhs;
}            
ConcreteExpression Type::BuildNegate(ConcreteExpression val, Context c) {
    if (IsReference())
        return Decay()->BuildNegate(val, c);
    return ConcreteExpression(c->GetBooleanType(), c->gen->CreateNegateExpression(val.BuildBooleanConversion(c)));
}

static const std::unordered_map<Lexer::TokenType, Lexer::TokenType> Assign = []() -> std::unordered_map<Lexer::TokenType, Lexer::TokenType> {
    std::unordered_map<Lexer::TokenType, Lexer::TokenType> assign;
    assign[Lexer::TokenType::LeftShift] = Lexer::TokenType::LeftShiftAssign;
    assign[Lexer::TokenType::RightShift] = Lexer::TokenType::RightShiftAssign;
    assign[Lexer::TokenType::Minus] = Lexer::TokenType::MinusAssign;
    assign[Lexer::TokenType::Plus] = Lexer::TokenType::PlusAssign;
    assign[Lexer::TokenType::Or] = Lexer::TokenType::OrAssign;
    assign[Lexer::TokenType::And] = Lexer::TokenType::AndAssign;
    assign[Lexer::TokenType::Xor] = Lexer::TokenType::XorAssign;
    assign[Lexer::TokenType::Dereference] = Lexer::TokenType::MulAssign;
    assign[Lexer::TokenType::Modulo] = Lexer::TokenType::ModAssign;
    assign[Lexer::TokenType::Divide] = Lexer::TokenType::DivAssign;
    return assign;
}();

OverloadSet* ConcreteExpression::AccessMember(Lexer::TokenType name, Context c) {
    return t->Decay()->AccessMember(*this, name, c);
}

Expression Type::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, std::vector<ConcreteExpression> destructors, Lexer::TokenType type, Context c) {
    for(auto x : destructors)
        c(x);
    return BuildBinaryExpression(lhs, rhs, type, c);
}

Expression Type::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Context c) {
    if (IsReference())
        return Decay()->BuildBinaryExpression(lhs, rhs, type, c);

    // If this function is entered, it's because the type-specific logic could not resolve the operator.
    // So let us attempt ADL.
    auto adlset = c->GetOverloadSet(lhs.AccessMember(type, c), c->GetOverloadSet(lhs.t->PerformADL(type, lhs.t, rhs.t, c), rhs.t->PerformADL(type, lhs.t, rhs.t, c)));
    std::vector<Type*> arguments;
    arguments.push_back(lhs.t);
    arguments.push_back(rhs.t);
    if (auto call = adlset->Resolve(arguments, *c)) {
        return call->BuildValueConstruction(c).BuildCall(lhs, rhs, c);
    }
    
    // ADL has failed to find us a suitable operator, so fall back to defaults.
    // First implement binary op in terms of op=
    if (Assign.find(type) != Assign.end()) {
        auto lval = BuildLvalueConstruction(lhs, c).BuildBinaryExpression(rhs, Assign.at(type), c).VisitContents(
            [&](ConcreteExpression& expr) -> Expression {
                auto copy = expr;
                copy.t = c->GetRvalueType(expr.t);
                return copy;
            },
            [&](DeferredExpression& expr) {
                auto copy = *expr.delay;
                *expr.delay = [=](Type* context) {
                    auto expr = copy(context);
                    expr.t = c->GetRvalueType(expr.t);
                    return expr;
                };
                return expr;
            }
        );
        return lval;
    }

    // Try fallbacks for relational operators
    // And default assignment for non-complex types.        
    switch(type) {
    case Lexer::TokenType::NotEqCmp:
        return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::EqCmp, c).BuildNegate(c);
    case Lexer::TokenType::EqCmp:
        return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::LT, c).BuildNegate(c).BuildBinaryExpression(rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, c).BuildNegate(c), Lexer::TokenType::And, c);
    case Lexer::TokenType::LTE:
        return rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, c).BuildNegate(c);
    case Lexer::TokenType::GT:
        return rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, c);
    case Lexer::TokenType::GTE:
        return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::LT, c).BuildNegate(c);
    case Lexer::TokenType::Assignment:
        if (!IsComplexType() && lhs.t->Decay() == rhs.t->Decay() && IsLvalueType(lhs.t))
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, rhs.BuildValue(c).Expr));
        break;
    case Lexer::TokenType::Or:
        return ConcreteExpression(c->GetBooleanType(), lhs.BuildBooleanConversion(c)).BuildBinaryExpression(ConcreteExpression(c->GetBooleanType(), rhs.BuildBooleanConversion(c)), Wide::Lexer::TokenType::Or, c);
    case Lexer::TokenType::And:
        return ConcreteExpression(c->GetBooleanType(), lhs.BuildBooleanConversion(c)).BuildBinaryExpression(ConcreteExpression(c->GetBooleanType(), rhs.BuildBooleanConversion(c)), Wide::Lexer::TokenType::And, c);
    }
    throw std::runtime_error("Attempted to build a binary expression; but it could not be found by the type, and a default could not be applied.");
}

ConcreteExpression Type::BuildRvalueConstruction(std::vector<ConcreteExpression> args, Context c) {
    Codegen::Expression* mem = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
    if (!IsComplexType() && args.size() == 1 && args[0].t->Decay() == this) {
        args[0] = args[0].t->BuildValue(args[0], c);
        mem = c->gen->CreateChainExpression(c->gen->CreateStore(mem, args[0].Expr), mem);
    } else
        mem = c->gen->CreateChainExpression(BuildInplaceConstruction(mem, args, c), mem);
    ConcreteExpression out(c->GetRvalueType(this), mem);
    c(out);
    out.steal = true;
    return out;
}

void Context::operator()(ConcreteExpression e) {
    if (RAIIHandler)
        RAIIHandler(e);
}

ConcreteExpression Type::BuildLvalueConstruction(std::vector<ConcreteExpression> args, Context c) {
    Codegen::Expression* mem = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
    if (!IsComplexType() && args.size() == 1 && args[0].t->Decay() == this) {
        args[0] = args[0].BuildValue(c);
        mem = c->gen->CreateChainExpression(c->gen->CreateStore(mem, args[0].Expr), mem);
    } else
        mem = c->gen->CreateChainExpression(BuildInplaceConstruction(mem, args, c), mem);

    ConcreteExpression out(c->GetLvalueType(this), mem);
    c(out);
    out.steal = true;
    return out;
}            
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) {
    if (!IsReference() && !IsComplexType() && args.size() == 1 && args[0].t->Decay() == this)
        return c->gen->CreateStore(mem, args[0].BuildValue(c).Expr);
    throw std::runtime_error("Could not inplace construct this type.");
}

ConcreteExpression Type::BuildValueConstruction(std::vector<ConcreteExpression> args, Context c) {
    if (IsComplexType())
        throw std::runtime_error("Internal compiler error: Attempted to value construct a complex UDT.");
    if (args.size() == 1 && args[0].t == this)
        return args[0];
    auto mem = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
    return ConcreteExpression(this, c->gen->CreateLoad(c->gen->CreateChainExpression(BuildInplaceConstruction(mem, std::move(args), c), mem)));
}
ConcreteExpression Type::AddressOf(ConcreteExpression obj, Context c) {
    // TODO: Remove this restriction, it is not very Wide.
    if (!IsLvalueType(obj.t))
        throw std::runtime_error("Attempted to take the address of something that was not an lvalue.");
    return ConcreteExpression(c->GetPointerType(obj.t->Decay()), obj.Expr);
}

OverloadSet* Type::PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Context c) {
    if (IsReference())
        return Decay()->PerformADL(what, lhs, rhs, c);
    auto context = GetContext(*c);
    if (!context)
        return c->GetOverloadSet();
    return GetContext(*c)->AccessMember(GetContext(*c)->BuildValueConstruction(c), what, c);
}

OverloadSet* Type::AccessMember(ConcreteExpression e, Lexer::TokenType type, Context c) {
    if (IsReference())
        return Decay()->AccessMember(e, type, c);
    return c->GetOverloadSet();
}

std::size_t MetaType::size(Analyzer& a) { return a.gen->GetInt8AllocSize(); }
std::size_t MetaType::alignment(Analyzer& a) { return a.gen->GetDataLayout().getABIIntegerTypeAlignment(8); }

std::function<llvm::Type*(llvm::Module*)> MetaType::GetLLVMType(Analyzer& a) {
    std::stringstream typenam;
    typenam << this;
    auto nam = typenam.str();
    return [=](llvm::Module* mod) -> llvm::Type* {
        if (mod->getTypeByName(nam))
            return mod->getTypeByName(nam);
        return llvm::StructType::create(nam, llvm::IntegerType::getInt8Ty(mod->getContext()), nullptr);
    };
}

Codegen::Expression* MetaType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) {
    if (args.size() > 1)
        throw std::runtime_error("Attempt to construct a type object with too many arguments.");
    if (args.size() == 1 && args[0].t->Decay() != this)
        throw std::runtime_error("Attempt to construct a type object with something other than another instance of that type.");
    return args.size() == 0 ? mem : c->gen->CreateChainExpression(args[0].Expr, mem);
}

ConcreteExpression MetaType::BuildValueConstruction(std::vector<ConcreteExpression> args, Context c) {
    if (args.size() > 1)
        throw std::runtime_error("Attempt to construct a type object with too many arguments.");
    if (args.size() == 1 && args[0].t->Decay() != this)
        throw std::runtime_error("Attempt to construct a type object with something other than another instance of that type.");
    return ConcreteExpression(this, args.size() == 0 ? (Codegen::Expression*)c->gen->CreateNull(GetLLVMType(*c)) : c->gen->CreateChainExpression(args[0].Expr, c->gen->CreateNull(GetLLVMType(*c))));
}

bool Type::IsA(Type* other, Analyzer& a) {
    return other == this;
}