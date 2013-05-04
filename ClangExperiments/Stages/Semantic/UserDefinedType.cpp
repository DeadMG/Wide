#include "UserDefinedType.h"
#include "Analyzer.h"
#include "../Codegen/Generator.h"
#include "LvalueType.h"
#include "RvalueType.h"
#include "Module.h"
#include "ClangTU.h"
#include "OverloadSet.h"

#include <sstream>

#pragma warning(push, 0)

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/Type.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ASTContext.h>
#include <clang/Sema/Sema.h>
#include <llvm/IR/DerivedTypes.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

bool UserDefinedType::IsComplexType() {
    return iscomplex;
}

UserDefinedType::UserDefinedType(AST::Type* t, Analyzer& a) {
    iscomplex = false;
    // Only assign these at the end of the constructor so that we don't permit something like type t { x := 1; y := x + 1; };
    std::unordered_map<std::string, unsigned> mem;
    type = t;

    for(auto&& decl : t->variables) {
        if (auto var = dynamic_cast<AST::VariableStatement*>(decl)) {
            auto expr = a.AnalyzeExpression(this, var->initializer);
            if (expr.t->IsReference())
                expr.t = expr.t->IsReference();
            member m;
            m.t = expr.t;
            m.num = llvmtypes.size();
            m.llvmty = m.t->GetLLVMType(a);
            m.name = decl->GetName();
            mem[var->name] = llvmtypes.size();
            llvmtypes.push_back(m);
            iscomplex = iscomplex || expr.t->IsComplexType();
        }
    }
    
    std::stringstream stream;
    stream << "struct.__" << this;
    llvmname = stream.str();

    ty = [&](llvm::Module* m) -> llvm::Type* {
        if (m->getTypeByName(llvmname))
            return m->getTypeByName(llvmname);
        std::vector<llvm::Type*> types;
        for(auto&& x : llvmtypes)
            types.push_back(x.llvmty(m));
        if (types.empty())
            types.push_back(llvm::IntegerType::getInt8Ty(m->getContext()));
        return llvm::StructType::create(types, llvmname);
    };

    members = std::move(mem);
}

std::function<llvm::Type*(llvm::Module*)> UserDefinedType::GetLLVMType(Analyzer& a) {
    return ty;
}

Codegen::Expression* UserDefinedType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (args.size() > 1)
        throw std::runtime_error("User-defined types can currently only be copy, move, and default constructed.");
    Codegen::Expression* e = nullptr;
    if (args.size() == 0) {
        for(auto&& x : llvmtypes) {
            auto construct = x.t->BuildInplaceConstruction(a.gen->CreateFieldExpression(mem, x.num), args, a);
            e = e ? a.gen->CreateChainExpression(e, construct) : construct;
        }
    }
    if (args.size() == 1) {
        auto ty = args[0].t;
        if (!ty->IsReference(this)) {
            if (ty == this) {
                if (!IsComplexType())
                    return a.gen->CreateStore(mem, args[0].Expr);
                throw std::runtime_error("Internal compiler error: Found a value of a complex type.");
            }
            throw std::runtime_error("Attempt to construct a user-defined type with something that was not another instance of that type.");
        }
        for(auto&& x : llvmtypes) {
            Type* t;
            if (auto l = dynamic_cast<LvalueType*>(args[0].t)) {
                t = a.GetLvalueType(x.t);
            } else {
                t = a.GetRvalueType(x.t);
            }
            Expression arg(t, a.gen->CreateFieldExpression(args[0].Expr, x.num));
            std::vector<Expression> arguments;
            arguments.push_back(arg);
            auto construct = x.t->BuildInplaceConstruction(a.gen->CreateFieldExpression(mem, x.num), std::move(arguments), a);
            e = e ? a.gen->CreateChainExpression(e, construct) : construct;
        }
    }
    // Can happen if the user declared no members.
    // mem is a safe thing to return as the user will almost always simply create a chain with the memory anyway.
    if (!e) e = mem;
    return e;
}

Expression UserDefinedType::AccessMember(Expression expr, std::string name, Analyzer& a) {
    if (members.find(name) != members.end()) {
        auto member = llvmtypes[members[name]];
        if (expr.t->IsReference()) {
            Expression out;
            out.Expr = a.gen->CreateFieldExpression(expr.Expr, member.num);
            if (auto l = dynamic_cast<LvalueType*>(expr.t)) {
                out.t = a.GetLvalueType(member.t);
            } else {
                out.t = a.GetRvalueType(member.t);
            }
            return out;
        }
        // Codegen currently doesn't permit this.
        throw std::runtime_error("Codegen currently does not permit accessing the members of value typed aggregates.");
    }
    if (type->Functions.find(name) != type->Functions.end()) {
        std::vector<Expression> args;
        args.push_back(expr);
        return a.GetOverloadSet(type->Functions[name], this)->BuildValueConstruction(args, a);
    }
    return a.GetDeclContext(type->higher)->AccessMember(expr, std::move(name), a);
}

Expression UserDefinedType::BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) {
    if (auto val = dynamic_cast<LvalueType*>(lhs.t)) {
        // If we have an overloaded operator, call that.
        if (type->Functions.find("=") != type->Functions.end()) {
            std::vector<Expression> args;
            args.push_back(lhs);
            auto overset = a.GetOverloadSet(type->Functions["="], this)->BuildValueConstruction(std::move(args), a);
            args.push_back(rhs);
            return overset.t->BuildCall(overset, args, a);
        }
        if (!IsComplexType()) {
            Expression out;
            out.t = lhs.t;
            out.Expr = a.gen->CreateStore(lhs.Expr, rhs.t->BuildValue(rhs, a).Expr);
            return out;
        }        
        Expression out;
        auto&& e = out.Expr;

        for(auto&& x : llvmtypes) {
            Type* t;
            if (auto l = dynamic_cast<LvalueType*>(rhs.t)) {
                t = a.GetLvalueType(x.t);
            } else {
                t = a.GetRvalueType(x.t);
            }
            Expression rhs(t, a.gen->CreateFieldExpression(rhs.Expr, x.num));
            Expression lhs(a.GetLvalueType(x.t), a.gen->CreateFieldExpression(lhs.Expr, x.num));

            auto construct = x.t->BuildAssignment(lhs, rhs, a);
            e = e ? a.gen->CreateChainExpression(e, construct.Expr) : construct.Expr;
        }
        // The original expr referred to the memory we were in- return that.
        e = a.gen->CreateChainExpression(e, lhs.Expr);
        out.t = lhs.t;
        return out;
    }
    else
        throw std::runtime_error("Attempt to assign to an rvalue of user-defined type.");
}
clang::QualType UserDefinedType::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    if (clangtypes.find(&TU) != clangtypes.end())
        return clangtypes[&TU];
    
    std::stringstream stream;
    stream << "__" << this;

    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(stream.str()));
    recdecl->startDefinition();

    for(auto&& x : llvmtypes) {
        auto var = clang::FieldDecl::Create(
            TU.GetASTContext(),
            recdecl,
            clang::SourceLocation(),
            clang::SourceLocation(),
            TU.GetIdentifierInfo(x.name),
            x.t->GetClangType(TU, a),
            nullptr,
            nullptr,
            false,
            clang::InClassInitStyle::ICIS_NoInit
        );
        var->setAccess(clang::AccessSpecifier::AS_public);
        recdecl->addDecl(var);
    }
    // Todo: Expose members to Clang.

    recdecl->completeDefinition();
    TU.GetDeclContext()->addDecl(recdecl);
    a.AddClangType(TU.GetASTContext().getTypeDeclType(recdecl), this);
    return clangtypes[&TU] = TU.GetASTContext().getTypeDeclType(recdecl);
}
Expression UserDefinedType::BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    if (type->Functions.find("<") != type->Functions.end()) {
        std::vector<Expression> args;
        args.push_back(lhs);
        auto func = a.GetOverloadSet(type->Functions["<"], this)->BuildValueConstruction(args, a);
        args.pop_back();
        args.push_back(rhs);
        return func.t->BuildCall(func, args, a);
    }
    // May also match a global operator<
    std::vector<Expression> args;
    args.push_back(lhs);
    args.push_back(rhs);
    return a.GetDeclContext(type->higher)->AccessMember(Expression(), "<", a).t->BuildCall(Expression(), std::move(args), a);
}
ConversionRank UserDefinedType::RankConversionFrom(Type* from, Analyzer& a) {
    if (from->IsReference(this)) {
        // This handles all cases except T to T&&.
        auto rank = ConversionRank::Zero;
        for(auto mem : llvmtypes) {
            if (auto lval = dynamic_cast<LvalueType*>(from)) {
                rank = std::max(rank, a.RankConversion(a.GetLvalueType(mem.t), mem.t));
            } else {
                rank = std::max(rank, a.RankConversion(a.GetRvalueType(mem.t), mem.t));
            }
        }
        return rank;
    }
    
    // No U to T/T& conversions permitted right now
    return ConversionRank::None;
}