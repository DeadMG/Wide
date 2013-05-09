#include "UserDefinedType.h"
#include "Analyzer.h"
#include "../Codegen/Generator.h"
#include "LvalueType.h"
#include "RvalueType.h"
#include "Module.h"
#include "ClangTU.h"
#include "OverloadSet.h"
#include "Function.h"
#include "FunctionType.h"
#include "../Codegen/Function.h"
#include "ConstructorType.h"

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
            if (auto con = dynamic_cast<ConstructorType*>(expr.t))
                expr.t = con->GetConstructedType();
            else
                throw std::runtime_error("Expected the expression giving the type of a member variable to be a type.");
            member m;
            m.t = expr.t;
            m.num = llvmtypes.size();
            m.name = decl->name;
            mem[var->name] = llvmtypes.size();
            llvmtypes.push_back(m);
            iscomplex = iscomplex || expr.t->IsComplexType();
        }
    }
    
    std::stringstream stream;
    stream << "struct.__" << this;
    llvmname = stream.str();

    ty = [&](llvm::Module* m) -> llvm::Type* {
        if (m->getTypeByName(llvmname)) {
            if (llvmtypes.empty())
                a.gen->AddEliminateType(m->getTypeByName(llvmname));
            return m->getTypeByName(llvmname);
        }
        std::vector<llvm::Type*> types;
        for(auto&& x : llvmtypes)
            types.push_back(x.t->GetLLVMType(a)(m));
        if (types.empty()) {
            types.push_back(llvm::IntegerType::getInt8Ty(m->getContext()));
        }
        auto ty = llvm::StructType::create(types, llvmname);
        if (llvmtypes.empty())
            a.gen->AddEliminateType(ty);
        return ty;
    };

    members = std::move(mem);
}

std::function<llvm::Type*(llvm::Module*)> UserDefinedType::GetLLVMType(Analyzer& a) {
    return ty;
}

Codegen::Expression* UserDefinedType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (type->Functions.find("type") != type->Functions.end()) {
        std::vector<Expression> setargs;
        setargs.push_back(Expression(a.GetLvalueType(this), mem));
        auto set = a.GetOverloadSet(type->Functions["type"], this)->BuildValueConstruction(std::move(setargs), a);
        return set.t->BuildCall(set, std::move(args), a).Expr;
    }
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
            if (dynamic_cast<LvalueType*>(args[0].t)) {
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
        Expression out;
        if (expr.t->IsReference()) {
            out.Expr = a.gen->CreateFieldExpression(expr.Expr, member.num);
            if (dynamic_cast<LvalueType*>(expr.t)) {
                out.t = a.GetLvalueType(member.t);
            } else {
                out.t = a.GetRvalueType(member.t);
            }
            // Need to collapse the pointers here- if member is a T* under the hood, then it'll be a T** when accessed
            // So load it to become a T* like the reference type expects.
            if (member.t->IsReference())
                out.Expr = a.gen->CreateLoad(out.Expr);
            return out;
        } else {
            out.Expr = a.gen->CreateFieldExpression(expr.Expr, member.num);
            out.t = member.t;
            return out;
        }
    }
    if (type->Functions.find(name) != type->Functions.end()) {
        std::vector<Expression> args;
        args.push_back(expr);
        if (expr.t == this)
            args.push_back(BuildRvalueConstruction(std::move(args), a));
        return a.GetOverloadSet(type->Functions[name], this)->BuildValueConstruction(args, a);
    }
    throw std::runtime_error("Attempted to access a name of an object, but no such member existed.");
}

AST::DeclContext* UserDefinedType::GetDeclContext() {
    return type->higher;
}

Expression UserDefinedType::BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) {
    if (dynamic_cast<LvalueType*>(lhs.t)) {
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
            if (x.t->IsReference())
                throw std::runtime_error("Attempted to assign to a user-defined type which had a member reference but no user-defined assignment operator.");
            Type* t;
            if (dynamic_cast<LvalueType*>(rhs.t)) {
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
    // Todo: Expose member functions
    // Only those which are not generic right now
    if (type->Functions.find("()") != type->Functions.end()) {
        for(auto&& x : type->Functions["()"]->functions) {
            bool skip = false;
            for(auto&& arg : x->args)
                if (!arg.type)
                    skip = true;
            if (skip) continue;
            auto f = a.GetWideFunction(x, this);
            auto sig = f->GetSignature(a);
            auto ret = sig->GetReturnType();
            auto args = sig->GetArguments();
            args.erase(args.begin());
            sig = a.GetFunctionType(ret, args);
            auto meth = clang::CXXMethodDecl::Create(
                TU.GetASTContext(), 
                recdecl, 
                clang::SourceLocation(), 
                clang::DeclarationNameInfo(TU.GetASTContext().DeclarationNames.getCXXOperatorName(clang::OverloadedOperatorKind::OO_Call), clang::SourceLocation()),
                sig->GetClangType(TU, a),
                0,
                clang::FunctionDecl::StorageClass::SC_Extern,
                false,
                false,
                clang::SourceLocation()
            );      
            assert(!meth->isStatic());
            meth->setAccess(clang::AccessSpecifier::AS_public);
            std::vector<clang::ParmVarDecl*> decls;
            for(auto&& arg : sig->GetArguments()) {
                decls.push_back(clang::ParmVarDecl::Create(TU.GetASTContext(),
                    meth,
                    clang::SourceLocation(),
                    clang::SourceLocation(),
                    nullptr,
                    arg->GetClangType(TU, a),
                    nullptr,
                    clang::VarDecl::StorageClass::SC_Auto,
                    nullptr
                ));
            }
            meth->setParams(decls);
            recdecl->addDecl(meth);
            if (clangtypes.empty()) {
                auto trampoline = a.gen->CreateFunction([=, &a, &TU](llvm::Module* m) -> llvm::Type* {
                    auto fty = llvm::dyn_cast<llvm::FunctionType>(sig->GetLLVMType(a)(m)->getPointerElementType());
                    std::vector<llvm::Type*> args;
                    for(auto it = fty->param_begin(); it != fty->param_end(); ++it) {
                        args.push_back(*it);
                    }
		    		// If T is complex, then "this" is the second argument. Else it is the first.
                    auto self = TU.GetLLVMTypeFromClangType(TU.GetASTContext().getTypeDeclType(recdecl), a)(m)->getPointerTo();
		    		if (sig->GetReturnType()->IsComplexType()) {
		    			args.insert(args.begin() + 1, self);
		    		} else {
		    			args.insert(args.begin(), self);
		    		}
                    return llvm::FunctionType::get(fty->getReturnType(), args, false)->getPointerTo();
                }, TU.MangleName(meth), true);// If an i8/i1 mismatch, fix it up for us amongst other things.
                // The only statement is return f().
                std::vector<Codegen::Expression*> exprs;
                // Unlike OverloadSet, we wish to simply forward all parameters after ABI adjustment performed by FunctionCall in Codegen.
                if (sig->GetReturnType()->IsComplexType()) {
                    // Two hidden arguments: ret, this, skip this and do the rest.
                    for(std::size_t i = 0; i < sig->GetArguments().size() + 2; ++i) {
                        exprs.push_back(a.gen->CreateParameterExpression(i));
                    }
                } else {
                    // One hidden argument: this, pos 0.
                    for(std::size_t i = 0; i < sig->GetArguments().size() + 1; ++i) {
                        exprs.push_back(a.gen->CreateParameterExpression(i));
                    }
                }
                trampoline->AddStatement(a.gen->CreateReturn(a.gen->CreateFunctionCall(a.gen->CreateFunctionValue(f->GetName()), exprs, f->GetLLVMType(a))));
            }
        }
    }

    recdecl->completeDefinition();
    TU.GetDeclContext()->addDecl(recdecl);
    a.AddClangType(TU.GetASTContext().getTypeDeclType(recdecl), this);
    return clangtypes[&TU] = TU.GetASTContext().getTypeDeclType(recdecl);
}

bool UserDefinedType::HasMember(std::string name) {
    return type->Functions.find(name) != type->Functions.end() || members.find(name) != members.end();
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
            if (dynamic_cast<LvalueType*>(from)) {
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

Expression UserDefinedType::BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) {
    auto set = AccessMember(val, "()", a);
    return set.t->BuildCall(set, std::move(args), a);
}