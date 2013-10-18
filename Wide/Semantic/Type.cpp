#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/OverloadSet.h>

using namespace Wide;
using namespace Semantic;

#pragma warning(push, 0)
#include <clang/AST/AST.h>
#pragma warning(pop)

Type* Type::GetContext(Analyzer& a) {
    return a.GetGlobalModule();
}
clang::QualType Type::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    throw std::runtime_error("This type has no Clang representation.");
}

DeferredExpression DeferredExpression::BuildDereference(Analyzer& a, Lexer::Range where) {
    auto copy = delay;
    return [=, &a](Type* t) {
        return (*copy)(nullptr).BuildDereference(a, where);
    };
}
DeferredExpression DeferredExpression::BuildNegate(Analyzer& a, Lexer::Range where) {
    auto copy = delay;
    return [=, &a](Type* t) {
        return (*copy)(nullptr).BuildNegate(a, where);
    };
}
DeferredExpression DeferredExpression::AddressOf(Analyzer& a, Lexer::Range where) {
    auto copy = delay;
    return [=, &a](Type* t) {
        return (*copy)(nullptr).AddressOf(a, where);
    };
}
DeferredExpression DeferredExpression::BuildBooleanConversion(Analyzer& a, Lexer::Range where) {
    auto copy = delay;
    return [=, &a](Type* t) {
        return ConcreteExpression(a.GetBooleanType(), (*copy)(nullptr).BuildBooleanConversion(a, where));
    };
}
DeferredExpression DeferredExpression::BuildIncrement(bool b, Analyzer& a, Lexer::Range where) {
    auto copy = delay;
    return [=, &a](Type* t) {
        return (*copy)(nullptr).BuildIncrement(b, a, where);
    };
}
DeferredExpression DeferredExpression::PointerAccessMember(std::string name, Analyzer& a, Lexer::Range where) {
    auto copy = delay;
    return DeferredExpression([=, &a](Type* t) {
        auto expr = (*copy)(nullptr);
        auto opt = expr.PointerAccessMember(name, a, where);
        if (opt)
            return *opt;
        throw std::runtime_error("Attempted to access a member of an object, but that object had no such member.");
    });
}
DeferredExpression DeferredExpression::BuildBinaryExpression(Expression other, Lexer::TokenType what, Analyzer& a, Lexer::Range where) {
    auto copy = delay;
    return DeferredExpression([=, &a](Type* t) {
        return (*copy)(nullptr).BuildBinaryExpression(other.Resolve(nullptr), what, a, where);
    });
}

ConcreteExpression ConcreteExpression::BuildValue(Analyzer& a, Lexer::Range where) {
    return t->BuildValue(*this, a, where);
}
DeferredExpression DeferredExpression::BuildValue(Analyzer& a, Lexer::Range where) {
    auto copy = delay;
    return DeferredExpression([=, &a](Type* t) {
        return (*copy)(nullptr).BuildValue(a, where);
    });
}
Expression Expression::BuildValue(Analyzer& a, Lexer::Range where) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildValue(a, where);
        },
        [&](DeferredExpression& e) {
            return e.BuildValue(a, where);
        }
    );
    // return expr.t->BuildValue(*this, a);
}

ConcreteExpression ConcreteExpression::BuildIncrement(bool b, Analyzer& a, Lexer::Range where) {
    return t->BuildIncrement(*this, b, a, where);
}

ConcreteExpression ConcreteExpression::BuildNegate(Analyzer& a, Lexer::Range where) {
    return t->BuildNegate(*this, a, where);
}

Expression ConcreteExpression::BuildCall(Expression l, Expression r, Analyzer& a, Lexer::Range where) {
    std::vector<Expression> exprs;
    exprs.push_back(l);
    exprs.push_back(r);
    return BuildCall(std::move(exprs), a, where);
}

Util::optional<ConcreteExpression> ConcreteExpression::PointerAccessMember(std::string mem, Analyzer& a, Lexer::Range where) {
    return t->PointerAccessMember(*this, std::move(mem), a, where);
}

ConcreteExpression ConcreteExpression::BuildDereference(Analyzer& a, Lexer::Range where) {
    return t->BuildDereference(*this, a, where);
}

Expression ConcreteExpression::BuildCall(ConcreteExpression lhs, ConcreteExpression rhs, Analyzer& a, Lexer::Range where) {
    std::vector<ConcreteExpression> exprs;
    exprs.push_back(lhs);
    exprs.push_back(rhs);
    return BuildCall(std::move(exprs), a, where);
}

Expression ConcreteExpression::BuildCall(ConcreteExpression lhs, Analyzer& a, Lexer::Range where) {
    std::vector<ConcreteExpression> exprs;
    exprs.push_back(lhs);
    return BuildCall(std::move(exprs), a, where);
}

ConcreteExpression ConcreteExpression::AddressOf(Analyzer& a, Lexer::Range where) {
    return t->AddressOf(*this, a, where);
}

Codegen::Expression* ConcreteExpression::BuildBooleanConversion(Analyzer& a, Lexer::Range where) {
    return t->BuildBooleanConversion(*this, a, where);
}

ConcreteExpression ConcreteExpression::BuildBinaryExpression(ConcreteExpression other, Lexer::TokenType ty, Analyzer& a, Lexer::Range where) {
    return t->BuildBinaryExpression(*this, other, ty, a, where);
}

Expression ConcreteExpression::BuildBinaryExpression(Expression l, Lexer::TokenType r, Analyzer& a, Lexer::Range where) {
    if (l.contents.type() == typeid(ConcreteExpression)) {
        return t->BuildBinaryExpression(*this, boost::get<ConcreteExpression>(l.contents), r, a, where);
    }
    ConcreteExpression self = *this;
    return DeferredExpression([=, &a](Type* t) {
        return t->BuildBinaryExpression(self, l.Resolve(nullptr), r, a, where);
    });
}

Expression ConcreteExpression::BuildMetaCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where) {
    // If they are all concrete, we're on a winner.
    std::vector<ConcreteExpression> concrete;
    for (auto arg : args) {
        if (arg.contents.type() == typeid(ConcreteExpression))
            concrete.push_back(boost::get<ConcreteExpression>(arg.contents));
        else
            return DeferredExpression([=, &a](Type* result) {
                std::vector<ConcreteExpression> concrete;
                for (auto arg : args)
                    concrete.push_back(arg.Resolve(nullptr));
                return t->BuildMetaCall(*this, std::move(concrete), a, where);
        });
    }
    return t->BuildMetaCall(*this, std::move(concrete), a, where);
}

DeferredExpression DeferredExpression::AccessMember(std::string name, Analyzer& a, Lexer::Range where) {
    auto copy = delay;
    return DeferredExpression([=, &a](Type* t) {
        auto expr = (*copy)(nullptr);
        auto opt = expr.AccessMember(name, a, where);
        if (opt)
            return *opt;
        throw std::runtime_error("Attempted to access a member of an object, but that object had no such member.");
    });
}
Wide::Util::optional<ConcreteExpression> ConcreteExpression::AccessMember(std::string name, Analyzer& a, Lexer::Range where) {
    return t->AccessMember(*this, std::move(name), a, where);
}
Wide::Util::optional<Expression> Expression::AccessMember(std::string name, Analyzer& a, Lexer::Range where){
    return VisitContents(
        [&](ConcreteExpression& e) -> Wide::Util::optional<Expression> {
            auto opt = e.AccessMember(std::move(name), a, where);
            if (opt)
                return *opt;
            return Wide::Util::none;
        },
        [&](DeferredExpression& e) -> Wide::Util::optional<Expression> {
            return e.AccessMember(std::move(name), a, where);
        }
    );
    //return t->AccessMember(*this, std::move(name), a);
}

Expression ConcreteExpression::BuildCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where) {
    // If they are all concrete, we're on a winner.
    std::vector<ConcreteExpression> concrete;
    for(auto arg : args) {
        if (arg.contents.type() == typeid(ConcreteExpression))
            concrete.push_back(boost::get<ConcreteExpression>(arg.contents));
        else
            return DeferredExpression([=, &a](Type* result) {
                std::vector<ConcreteExpression> concrete;
                for (auto arg : args)
                    concrete.push_back(arg.Resolve(nullptr));
                return t->BuildCall(*this, std::move(concrete), a, where).Resolve(result);
            });
    }
    return t->BuildCall(*this, std::move(concrete), a, where);
}
DeferredExpression DeferredExpression::BuildCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where) {
    return DeferredExpression([=, &a](Type* result) {
        std::vector<ConcreteExpression> concrete;
        for(auto arg : args)
            if (arg.contents.type() == typeid(ConcreteExpression))
                concrete.push_back(boost::get<ConcreteExpression>(arg.contents));
            else
                concrete.push_back(boost::get<DeferredExpression>(arg.contents)(nullptr));
        return (*this)(nullptr).BuildCall(std::move(concrete), a, where).Resolve(result);
    });
}
Expression ConcreteExpression::BuildCall(std::vector<ConcreteExpression> exprs, Analyzer& a, Lexer::Range where) {
    return t->BuildCall(*this, std::move(exprs), a, where);
}

Expression Expression::BuildCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildCall(std::move(args), a, where);
        },
        [&](DeferredExpression& e) {
            return e.BuildCall(std::move(args), a, where);
        }
    );
}

Expression ConcreteExpression::BuildCall(Analyzer& a, Lexer::Range where) {
    return BuildCall(std::vector<ConcreteExpression>(), a, where);
}
DeferredExpression DeferredExpression::BuildCall(Analyzer& a, Lexer::Range where) {
    return BuildCall(std::vector<Expression>(), a, where);
}
Expression Expression::BuildCall(Analyzer& a, Lexer::Range where) {
    return BuildCall(std::vector<Expression>(), a, where);
}

ConcreteExpression ConcreteExpression::BuildMetaCall(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    return t->BuildMetaCall(*this, std::move(args), a, where);
}
DeferredExpression DeferredExpression::BuildMetaCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where) {
    return DeferredExpression([=, &a](Type* t) {
        std::vector<ConcreteExpression> concrete;
        for (auto&& arg : args) {
            concrete.push_back(arg.Resolve(nullptr));
        }
        return (*this)(nullptr).BuildMetaCall(std::move(concrete), a, where);
    });
}

Expression Expression::BuildMetaCall(std::vector<Expression> args, Analyzer& a, Lexer::Range where) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildMetaCall(std::move(args), a, where);
        },
        [&](DeferredExpression& e) {
            return e.BuildMetaCall(std::move(args), a, where);
        }
    );
    //return t->BuildMetaCall(*this, std::move(args), a);
}

Expression Expression::BuildDereference(Analyzer& a, Lexer::Range where)  {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildDereference(a, where);
        },
        [&](DeferredExpression& e) {
            return e.BuildDereference(a, where);
        }
    );
    //return t->BuildDereference(*this, a);
}
Expression Expression::BuildIncrement(bool postfix, Analyzer& a, Lexer::Range where) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildIncrement(postfix, a, where);
        },
        [&](DeferredExpression& e) {
            return e.BuildIncrement(postfix, a, where);
        }
    );
    //return t->BuildIncrement(*this, postfix, a);
}  

Wide::Util::optional<Expression> Expression::PointerAccessMember(std::string name, Analyzer& a, Lexer::Range where) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Wide::Util::optional<Expression> {
            return e.PointerAccessMember(std::move(name), a, where);
        },
        [&](DeferredExpression& e) {
            return e.PointerAccessMember(std::move(name), a, where);
        }
    );
    //return t->PointerAccessMember(*this, std::move(name), a);
}
Expression Expression::AddressOf(Analyzer& a, Lexer::Range where) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.AddressOf(a, where);
        },
        [&](DeferredExpression& e) {
            return e.AddressOf(a, where);
        }
    );
    //return t->AddressOf(*this, a);
}
Expression Expression::BuildBooleanConversion(Analyzer& a, Lexer::Range where) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return ConcreteExpression(a.GetBooleanType(), e.BuildBooleanConversion(a, where));
        },
        [&](DeferredExpression& e) {
            return e.BuildBooleanConversion(a, where);
        }
    );
    //return t->BuildBooleanConversion(*this, a);
}
Expression Expression::BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildBinaryExpression(rhs, type, a, where);
        },
        [&](DeferredExpression& e) {
            return e.BuildBinaryExpression(rhs, type, a, where);
        }
    );
    //return t->BuildBinaryExpression(*this, rhs, type, a);
}
Expression Expression::BuildNegate(Analyzer& a, Lexer::Range where) {
    return VisitContents(
        [&](ConcreteExpression& e) -> Expression {
            return e.BuildNegate(a, where);
        },
        [&](DeferredExpression& e) {
            return e.BuildNegate(a, where);
        }
    );
    //return t->BuildNegate(*this, a);
}
Expression Expression::BuildCall(Expression arg, Analyzer& a, Lexer::Range where) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildCall(std::move(args), a, where);
}
Expression Expression::BuildCall(Expression lhs, Expression rhs, Analyzer& a, Lexer::Range where) {
    std::vector<Expression> args;
    args.push_back(lhs);
    args.push_back(rhs);
    return BuildCall(std::move(args), a, where);
}

ConcreteExpression Type::BuildValueConstruction(ConcreteExpression arg, Analyzer& a, Lexer::Range where) {
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildValueConstruction(std::move(args), a, where);
}
ConcreteExpression Type::BuildRvalueConstruction(ConcreteExpression arg, Analyzer& a, Lexer::Range where) {
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildRvalueConstruction(std::move(args), a, where);
}
ConcreteExpression Type::BuildLvalueConstruction(ConcreteExpression arg, Analyzer& a, Lexer::Range where) {
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildLvalueConstruction(std::move(args), a, where);
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, ConcreteExpression arg, Analyzer& a, Lexer::Range where){
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildInplaceConstruction(mem, std::move(args), a, where);
}
ConcreteExpression Type::BuildValueConstruction(Analyzer& a, Lexer::Range where) {
    std::vector<ConcreteExpression> args;
    return BuildValueConstruction(args, a, where);
}
ConcreteExpression Type::BuildRvalueConstruction(Analyzer& a, Lexer::Range where) {
    std::vector<ConcreteExpression> args;
    return BuildRvalueConstruction(args, a, where);
}
ConcreteExpression Type::BuildLvalueConstruction(Analyzer& a, Lexer::Range where) {
    std::vector<ConcreteExpression> args;
    return BuildLvalueConstruction(args, a, where);
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, Analyzer& a, Lexer::Range where) {
    std::vector<ConcreteExpression> args;
    return BuildInplaceConstruction(mem, args, a, where);
}
Wide::Util::optional<ConcreteExpression> Type::AccessMember(ConcreteExpression e, std::string name, Analyzer& a, Lexer::Range where) {
    if (IsReference())
        return Decay()->AccessMember(e, std::move(name), a, where);
    if (name == "~type")
        return a.GetNothingFunctorType()->BuildValueConstruction(a, where);
    return Wide::Util::none;
}
ConcreteExpression Type::BuildValue(ConcreteExpression lhs, Analyzer& a, Lexer::Range where) {
    if (IsComplexType())
        throw std::runtime_error("Internal Compiler Error: Attempted to build a complex type into a register.");
    if (lhs.t->IsReference())
        return ConcreteExpression(lhs.t->Decay(), a.gen->CreateLoad(lhs.Expr));
    return lhs;
}            
ConcreteExpression Type::BuildNegate(ConcreteExpression val, Analyzer& a, Lexer::Range where) {
    if (IsReference())
        return Decay()->BuildNegate(val, a, where);
    return ConcreteExpression(a.GetBooleanType(), a.gen->CreateNegateExpression(val.BuildBooleanConversion(a, where)));
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

Wide::Util::optional<Wide::Semantic::ConcreteExpression> Type::AccessMember(std::string name, Analyzer& a, Lexer::Range where) {
    return AccessMember(ConcreteExpression(), std::move(name), a, where);
}
Wide::Util::optional<Wide::Semantic::ConcreteExpression> Type::AccessMember(Lexer::TokenType name, Analyzer& a, Lexer::Range where) {
    return AccessMember(ConcreteExpression(), std::move(name), a, where);
}

ConcreteExpression Type::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where) {
    if (IsReference())
        return Decay()->BuildBinaryExpression(lhs, rhs, type, a, where);

    // If this function is entered, it's because the type-specific logic could not resolve the operator.
    // So let us attempt ADL.
    auto ldecls = lhs.t->GetContext(a) ? lhs.t->GetContext(a)->AccessMember(type, a, where) : Wide::Util::none;
    auto rdecls = rhs.t->GetContext(a) ? rhs.t->GetContext(a)->AccessMember(type, a, where) : Wide::Util::none;
    if (ldecls) {
        if (auto loverset = dynamic_cast<OverloadSet*>(ldecls->t->Decay()))
            if (rdecls) {
                if (auto roverset = dynamic_cast<OverloadSet*>(rdecls->t->Decay()))
                    return a.GetOverloadSet(loverset, roverset)->BuildValueConstruction(a, where).BuildCall(lhs, rhs, a, where).Resolve(nullptr);
            } else {
                return loverset->BuildValueConstruction(a, where).BuildCall(lhs, rhs, a, where).Resolve(nullptr);
            }
    } else
        if (rdecls)
            if (auto roverset = dynamic_cast<OverloadSet*>(rdecls->t->Decay()))
                return roverset->BuildValueConstruction(a, where).BuildCall(lhs, rhs, a, where).Resolve(nullptr);

    // At this point, ADL has failed to find an operator and the type also failed to post one.
    // So let us attempt a default implementation for some operators.
    if (Assign.find(type) != Assign.end())
        return BuildLvalueConstruction(lhs, a, where).BuildBinaryExpression(rhs, Assign.at(type), a, where);
    switch(type) {
    case Lexer::TokenType::NotEqCmp:
        return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::EqCmp, a, where).BuildNegate(a, where);
    case Lexer::TokenType::EqCmp:
        return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::LT, a, where).BuildNegate(a, where).BuildBinaryExpression(rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, a, where).BuildNegate(a, where), Lexer::TokenType::And, a, where);
    case Lexer::TokenType::LTE:
        return rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, a, where).BuildNegate(a, where);
    case Lexer::TokenType::GT:
        return rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, a, where);
    case Lexer::TokenType::GTE:
        return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::LT, a, where).BuildNegate(a, where);
    case Lexer::TokenType::Assignment:
        if (!IsComplexType() && lhs.t->Decay() == rhs.t->Decay() && IsLvalueType(lhs.t))
            return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, rhs.BuildValue(a, where).Expr));
    case Lexer::TokenType::Or:
        return ConcreteExpression(a.GetBooleanType(), lhs.BuildBooleanConversion(a, where)).BuildBinaryExpression(ConcreteExpression(a.GetBooleanType(), rhs.BuildBooleanConversion(a, where)), Wide::Lexer::TokenType::Or, a, where);
    case Lexer::TokenType::And:
        return ConcreteExpression(a.GetBooleanType(), lhs.BuildBooleanConversion(a, where)).BuildBinaryExpression(ConcreteExpression(a.GetBooleanType(), rhs.BuildBooleanConversion(a, where)), Wide::Lexer::TokenType::And, a, where);
    }
    throw std::runtime_error("Attempted to build a binary expression; but it could not be found by the type, and a default could not be applied.");
}

ConcreteExpression Type::BuildRvalueConstruction(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    ConcreteExpression out;
    out.t = a.GetRvalueType(this);
    auto mem = a.gen->CreateVariable(GetLLVMType(a), alignment(a));
    if (!IsComplexType() && args.size() == 1 && args[0].t->Decay() == this) {
        args[0] = args[0].t->BuildValue(args[0], a, where);
        out.Expr = a.gen->CreateChainExpression(a.gen->CreateStore(mem, args[0].Expr), mem);
    } else
        out.Expr = a.gen->CreateChainExpression(BuildInplaceConstruction(mem, args, a, where), mem);
    out.steal = true;
    return out;
}

ConcreteExpression Type::BuildLvalueConstruction(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    ConcreteExpression out;
    out.t = a.GetLvalueType(this);
    auto mem = a.gen->CreateVariable(GetLLVMType(a), alignment(a));
    if (!IsComplexType() && args.size() == 1 && args[0].t->Decay() == this) {
        args[0] = args[0].BuildValue(a, where);
        out.Expr = a.gen->CreateChainExpression(a.gen->CreateStore(mem, args[0].Expr), mem);
    } else
        out.Expr = a.gen->CreateChainExpression(BuildInplaceConstruction(mem, args, a, where), mem);    return out;
}            
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    if (!IsReference() && !IsComplexType() && args.size() == 1 && args[0].t->Decay() == this)
        return a.gen->CreateStore(mem, args[0].BuildValue(a, where).Expr);
    throw std::runtime_error("Could not inplace construct this type.");
}

ConcreteExpression Type::BuildValueConstruction(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    if (IsComplexType())
        throw std::runtime_error("Internal compiler error: Attempted to value construct a complex UDT.");
    if (args.size() == 1 && args[0].t == this)
        return args[0];
    auto mem = a.gen->CreateVariable(GetLLVMType(a), alignment(a));
    return ConcreteExpression(this, a.gen->CreateLoad(a.gen->CreateChainExpression(BuildInplaceConstruction(mem, std::move(args), a, where), mem)));
}
ConversionRank Type::RankConversionFrom(Type* from, Analyzer& a) {
    // We only cover the following cases:
    // U to T         - convertible for any U
    // T& to T        - copyable
    // T&& to T       - movable
    // "this" is always the to type. We want to know if we can convert from "from" to "this".

    // U to this&& is just U to this, then this to this&&. T to T&& is always valid- even for something like std::mutex.

    // The default is not convertible but movable and copyable.
    if (from->IsReference(this))
        return ConversionRank::Zero;
    return ConversionRank::None;
}
ConcreteExpression Type::AddressOf(ConcreteExpression obj, Analyzer& a, Lexer::Range where) {
    // TODO: Remove this restriction, it is not very Wide.
    if (!IsLvalueType(obj.t))
        throw std::runtime_error("Attempted to take the address of something that was not an lvalue.");
    return ConcreteExpression(a.GetPointerType(obj.t->Decay()), obj.Expr);
}