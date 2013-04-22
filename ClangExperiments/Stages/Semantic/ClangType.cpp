#include "ClangType.h"
#include "Analyzer.h"
#include "FunctionType.h"
#include "../Codegen/Expression.h"
#include "../../Util/MakeUnique.h"
#include "ClangOverloadSet.h"
#include "ClangTU.h"
#include "../Codegen/Generator.h"
#include "LvalueType.h"
#include "RvalueType.h"
#include "ClangTemplateClass.h"
#include "ConstructorType.h"

#include <iostream>

#pragma warning(push, 0)

#include <clang/AST/ASTContext.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/Lookup.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/DerivedTypes.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

ClangType::ClangType(ClangUtil::ClangTU* src, clang::QualType t) 
    : from(src), type(t.getCanonicalType()) 
{
    // Declare any special members that need it.    
    // Also fix up their exception spec because for some reason Clang doesn't until you ask it.
    auto recdecl = type->getAsCXXRecordDecl();
    if (!recdecl) return;

    auto ProcessImplicitSpecialMember = [this](
        std::function<bool()> needs,
        std::function<clang::CXXMethodDecl*()> declare, 
        std::function<void(clang::CXXMethodDecl*)> define,
        std::function<clang::CXXMethodDecl*()> lookup
    ) {
        if (needs()) {
            auto decl = declare();
            from->GetSema().EvaluateImplicitExceptionSpec(clang::SourceLocation(), decl);
            define(decl);
        } else {
            auto decl = lookup();
            if (decl && decl->isDefaulted() && !decl->isDeleted()) {                
                if (decl->getType()->getAs<clang::FunctionProtoType>()->getExtProtoInfo().ExceptionSpecType == clang::ExceptionSpecificationType::EST_Unevaluated) {
                    from->GetSema().EvaluateImplicitExceptionSpec(clang::SourceLocation(), decl);
                }                
                if (!decl->doesThisDeclarationHaveABody()) {
                    define(decl);
                }
            }
        }
    };

    ProcessImplicitSpecialMember(
        [&]{ return recdecl->needsImplicitCopyAssignment(); },
        [&]{ return from->GetSema().DeclareImplicitCopyAssignment(recdecl); },
        [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitCopyAssignment(clang::SourceLocation(), decl); },
        [&]{ return from->GetSema().LookupCopyingAssignment(recdecl, 0, false, 0); }
    );
    ProcessImplicitSpecialMember(
        [&]{ return recdecl->needsImplicitCopyConstructor(); },
        [&]{ return from->GetSema().DeclareImplicitCopyConstructor(recdecl); },
        [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitCopyConstructor(clang::SourceLocation(), static_cast<clang::CXXConstructorDecl*>(decl)); },
        [&]{ return from->GetSema().LookupCopyingConstructor(recdecl, 0); }
    );
    ProcessImplicitSpecialMember(
        [&]{ return recdecl->needsImplicitMoveConstructor(); },
        [&]{ return from->GetSema().DeclareImplicitMoveConstructor(recdecl); },
        [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitMoveConstructor(clang::SourceLocation(), static_cast<clang::CXXConstructorDecl*>(decl)); },
        [&]{ return from->GetSema().LookupMovingConstructor(recdecl, 0); }
    );
    ProcessImplicitSpecialMember(
        [&]{ return recdecl->needsImplicitDestructor(); },
        [&]{ return from->GetSema().DeclareImplicitDestructor(recdecl); },
        [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitDestructor(clang::SourceLocation(), static_cast<clang::CXXDestructorDecl*>(decl)); },
        [&]{ return from->GetSema().LookupDestructor(recdecl); }
    );
    ProcessImplicitSpecialMember(
        [&]{ return recdecl->needsImplicitMoveAssignment(); },
        [&]{ return from->GetSema().DeclareImplicitMoveAssignment(recdecl); },
        [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitMoveAssignment(clang::SourceLocation(), decl); },
        [&]{ return from->GetSema().LookupMovingAssignment(recdecl, 0, false, 0); }
    );
    ProcessImplicitSpecialMember(
        [&]{ return recdecl->needsImplicitDefaultConstructor(); },
        [&]{ return from->GetSema().DeclareImplicitDefaultConstructor(recdecl); },
        [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitDefaultConstructor(clang::SourceLocation(), static_cast<clang::CXXConstructorDecl*>(decl)); },
        [&]{ return from->GetSema().LookupDefaultConstructor(recdecl); }
    );
}
clang::QualType ClangType::GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) {
    if (&tu != from)
        throw std::runtime_error("Attempted to use a C++ type outside the TU where it was declared.");
    return type;
}

Expression ClangType::AccessMember(Expression val, std::string name, Analyzer& a) {
    clang::LookupResult lr(
        from->GetSema(), 
        clang::DeclarationNameInfo(clang::DeclarationName(from->GetIdentifierInfo(name)), clang::SourceLocation()),
        clang::Sema::LookupNameKind::LookupOrdinaryName);

    if (!from->GetSema().LookupQualifiedName(lr, type.getCanonicalType()->getAs<clang::TagType>()->getDecl()))
        throw std::runtime_error("Attempted to access a member of a Clang type, but Clang could not find the name.");
    if (lr.isAmbiguous())
        throw std::runtime_error("Attempted to access a member of a Clang type, but Clang said the lookup was ambiguous.");
    if (lr.isSingleResult()) {
        if (val.Expr) {
            // Check for members first.
            if (auto field = llvm::dyn_cast<clang::FieldDecl>(lr.getFoundDecl())) {
                Expression out;
                if (auto lval = dynamic_cast<LvalueType*>(val.t)) {
                    // The result is an lvalue of that type.
                    out.t = a.GetLvalueType(a.GetClangType(*from, field->getType()));
                } else {
                    out.t = a.GetRvalueType(a.GetClangType(*from, field->getType()));
                }
                out.Expr = a.gen->CreateFieldExpression(val.Expr, from->GetFieldNumber(field));
                return out;
            }        
            if (auto fun = llvm::dyn_cast<clang::CXXMethodDecl>(lr.getFoundDecl())) {                
                Expression out;
                std::vector<Type*> args;
                for(auto decl = fun->param_begin(); decl != fun->param_end(); ++decl) {
                    args.push_back(a.GetClangType(*from, (*decl)->getType()));
                }
                out.t = a.GetFunctionType(a.GetClangType(*from, fun->getResultType()), args);
                if (a.gen)
                    out.Expr = a.gen->CreateFunctionValue(from->MangleName(fun));
                return out;
            }
        }
        if (auto ty = llvm::dyn_cast<clang::TypeDecl>(lr.getFoundDecl())) {
            Expression out;
            out.t = a.GetConstructorType(a.GetClangType(*from, from->GetASTContext().getTypeDeclType(ty)));
            out.Expr = nullptr;
            return out;
        }
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(lr.getFoundDecl())) {    
            Expression out;
            out.t = a.GetLvalueType(a.GetClangType(*from, vardecl->getType()));
            if (a.gen)
                out.Expr = a.gen->CreateGlobalVariable(from->MangleName(vardecl));
            return out;
        }
        if (auto tempdecl = llvm::dyn_cast<clang::ClassTemplateDecl>(lr.getFoundDecl())) {
            Expression out;
            out.Expr = nullptr;
            out.t = a.GetClangTemplateClass(*from, tempdecl);
            return out;
        }
        throw std::runtime_error("Found a decl but didn't know how to interpret it.");
    }    
    auto ptr = Wide::Memory::MakeUnique<clang::UnresolvedSet<8>>();
    clang::UnresolvedSet<8>& us = *ptr;
    for(auto it = lr.begin(); it != lr.end(); ++it) {
        us.addDecl(*it);
    }
    Expression out;
    out.t = a.arena.Allocate<ClangOverloadSet>(std::move(ptr), from, val.Expr ? this : nullptr);
    out.Expr = val.Expr;
    return out;
}

Expression ClangType::BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) {
    throw std::runtime_error("Attempted to call a Clang object. Will deal with this later.");
}

Expression ClangType::BuildOverloadedOperator(Expression lhs, Expression rhs, Analyzer& a, clang::OverloadedOperatorKind opkind, clang::BinaryOperator::Opcode opcode) {
    clang::ADLResult result;
    auto declname = from->GetASTContext().DeclarationNames.getCXXOperatorName(opkind);
    std::vector<clang::Expr*> exprs;
       
    // Decay the types, because for some reason expressions of type T& are bad. They need to be lvalues of type T.
    auto lhsty = type.getNonLValueExprType(from->GetASTContext());
    auto rhsty = rhs.t->GetClangType(*from, a).getNonLValueExprType(from->GetASTContext());
    clang::OpaqueValueExpr first(clang::SourceLocation(), lhsty, Semantic::GetKindOfType(lhs.t));
    exprs.push_back(&first);
    clang::OpaqueValueExpr second(clang::SourceLocation(), rhsty, Semantic::GetKindOfType(rhs.t));
    exprs.push_back(&second);
    from->GetSema().ArgumentDependentLookup(declname, true, clang::SourceLocation(), exprs, result);
    clang::UnresolvedSet<10> out;
    for(auto x : result)
        out.addDecl(x);
    auto opresult = from->GetSema().CreateOverloadedBinOp(clang::SourceLocation(), opcode, out, &first, &second);
    if (!opresult.get())
        throw std::runtime_error("Attempted to use a binary operator on a left-hand-type which was a Clang type, but Clang could not resolve the call.");
    auto callexpr = llvm::dyn_cast<clang::CallExpr>(opresult.get());
       
    auto fun = callexpr->getDirectCallee();
    if (!fun) {
        std::cout << "Fun was 0.\n";
        llvm::raw_os_ostream out(std::cout);
        callexpr->printPretty(out, 0, clang::PrintingPolicy(from->GetASTContext().getLangOpts()));
    }

    Type* ret = a.GetClangType(*from, fun->getResultType());
    std::vector<Type*> types;
    types.push_back(a.GetClangType(*from, fun->getParamDecl(0)->getType()));
    if (fun->getNumParams() == 1) {
        // Member method, and this is missing.
        types.insert(types.begin(), lhs.t);
    } else {
        types.push_back(a.GetClangType(*from, fun->getParamDecl(1)->getType()));
    }

    auto funty = a.GetFunctionType(ret, types);
    
    Expression clangfunc;
    clangfunc.t = funty;

    if (a.gen)
        clangfunc.Expr = a.gen->CreateFunctionValue(from->MangleName(fun));

    std::vector<Expression> expressions;
    expressions.push_back(lhs); expressions.push_back(rhs);
    return funty->BuildCall(clangfunc, std::move(expressions), a);
}
Expression ClangType::BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildOverloadedOperator(lhs, rhs, a, clang::OverloadedOperatorKind::OO_LessLess, clang::BinaryOperatorKind::BO_Shl);
}

Expression ClangType::BuildRightShift(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildOverloadedOperator(lhs, rhs, a, clang::OverloadedOperatorKind::OO_GreaterGreater, clang::BinaryOperatorKind::BO_Shr);
}

std::function<llvm::Type*(llvm::Module*)> ClangType::GetLLVMType(Analyzer& a) {
    return from->GetLLVMTypeFromClangType(type);
}

Expression ClangType::BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildOverloadedOperator(lhs, rhs, a, clang::OverloadedOperatorKind::OO_Equal, clang::BinaryOperatorKind::BO_Assign);
}
Expression ClangType::BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a) {    
    return BuildOverloadedOperator(lhs, rhs, a, clang::OverloadedOperatorKind::OO_Less, clang::BinaryOperatorKind::BO_LT);
}
Expression ClangType::BuildLTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildOverloadedOperator(lhs, rhs, a, clang::OverloadedOperatorKind::OO_LessEqual, clang::BinaryOperatorKind::BO_LE);
}
Expression ClangType::BuildGTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildOverloadedOperator(lhs, rhs, a, clang::OverloadedOperatorKind::OO_Greater, clang::BinaryOperatorKind::BO_GT);
}
Expression ClangType::BuildGTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildOverloadedOperator(lhs, rhs, a, clang::OverloadedOperatorKind::OO_GreaterEqual, clang::BinaryOperatorKind::BO_GE);
}
           
Expression ClangType::BuildValueConstruction(std::vector<Expression> args, Analyzer& a) {
    // Presumably, args[0].t != this
    if (args[0].t == this)
        return args[0];
    
    Expression out;
    out.t = this;
    auto mem = a.gen->CreateVariable(GetLLVMType(a));
    out.Expr = a.gen->CreateLoad(a.gen->CreateChainExpression(BuildInplaceConstruction(mem, args, a), mem));
    return out;
}

Codegen::Expression* ClangType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {    
    if (args.size() == 1 && args[0].t == this) {
        return a.gen->CreateStore(mem, args[0].Expr);
    }
    clang::UnresolvedSet<8> us;

    auto recdecl = type->getAsCXXRecordDecl();    
    // The first argument is pseudo-this.
    for(auto begin = recdecl->ctor_begin(); begin != recdecl->ctor_end(); ++begin) {
        us.addDecl(*begin);
    }
    std::vector<clang::OpaqueValueExpr> exprs;
    for(auto x : args) {
        exprs.push_back(clang::OpaqueValueExpr(clang::SourceLocation(), x.t->GetClangType(*from, a).getNonLValueExprType(from->GetASTContext()), GetKindOfType(x.t)));
    }
    clang::OverloadCandidateSet s((clang::SourceLocation()));
    std::vector<clang::Expr*> exprptrs;
    for(auto&& x : exprs) {
        exprptrs.push_back(&x);
    }
    for(auto&& x : us) {
        auto con = static_cast<clang::CXXConstructorDecl*>(x);
        clang::DeclAccessPair d;
        d.set(con, con->getAccess());
        from->GetSema().AddOverloadCandidate(con, d, exprptrs, s);
    }
    clang::OverloadCandidateSet::iterator best;
    auto result = s.BestViableFunction(from->GetSema(), clang::SourceLocation(), best);
    if (result == clang::OverloadingResult::OR_Ambiguous)
        throw std::runtime_error("Attempted to call an overloaded constructor, but Clang said the overloads were ambiguous.");
    if (result == clang::OverloadingResult::OR_Deleted)
        throw std::runtime_error("Attempted to call an overloaded constructor, but Clang said the best match was a C++ deleted function.");
    if (result == clang::OverloadingResult::OR_No_Viable_Function)
        throw std::runtime_error("Attempted to call an overloaded constructor, but Clang said that there were no viable functions.");
    auto fun = best->Function;
    

    std::vector<Type*> types;

    // Constructor signatures don't seem to include the need for "this", so just lie.
    // It all turns out alright.
    Expression self;
    self.t = a.GetRvalueType(this);
    self.Expr = mem;
    types.push_back(self.t);

    for(unsigned i = 0; i < fun->getNumParams(); ++i) {
        // Clang's overload resolution may ask us to create a default parameter.
        // Check if the function takes more params than we do and if so, create a default.
        auto paramdecl = fun->getParamDecl(i);
        if (i >= args.size()) {
            if (paramdecl->hasDefaultArg()) {
                // Assume that it is default-construct for now because fuck every other thing.
                std::vector<Expression> conargs;
                auto ty = a.GetClangType(*from, paramdecl->getType());
                // If ty is T&, construct lvalue construction.
                if (auto lval = dynamic_cast<LvalueType*>(ty)) {
                    args.push_back(lval->IsReference()->BuildLvalueConstruction(conargs, a));
                } else if (auto rval = dynamic_cast<RvalueType*>(ty)) {
                    args.push_back(rval->IsReference()->BuildRvalueConstruction(conargs, a));
                } else 
                    args.push_back(ty->BuildValueConstruction(conargs, a));
            }
        }
        types.push_back(a.GetClangType(*from, paramdecl->getType()));
        if (a.GetClangType(*from, paramdecl->getType()) == this)
            throw std::runtime_error("Fuck");
    }
    args.insert(args.begin(), self);

    auto rty = a.GetClangType(*from, fun->getResultType());
    auto funty = a.GetFunctionType(rty, types);
    Expression obj;
    obj.t = funty;
    obj.Expr = a.gen->CreateFunctionValue(from->MangleName(fun));
    return funty->BuildCall(obj, args, a).Expr;
}

bool ClangType::IsComplexType() {
    auto decl = type.getCanonicalType()->getAsCXXRecordDecl();
	return decl && from->IsComplexType(decl);
}

Expression ClangType::BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildOverloadedOperator(lhs, rhs, a, clang::OverloadedOperatorKind::OO_EqualEqual, clang::BinaryOperatorKind::BO_EQ);
}

Expression ClangType::BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildOverloadedOperator(lhs, rhs, a, clang::OverloadedOperatorKind::OO_ExclaimEqual, clang::BinaryOperatorKind::BO_NE);
}
Wide::Codegen::Expression* ClangType::BuildBooleanConversion(Expression self, Analyzer& a) {
    clang::OpaqueValueExpr ope(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(self.t));
    auto p = from->GetSema().PerformContextuallyConvertToBool(&ope);
    if (!p.get())
        throw std::runtime_error("Attempted to convert an object to bool contextually, but Clang said this was not possible.");
    auto callexpr = llvm::dyn_cast<clang::CallExpr>(p.get());
       
    auto fun = callexpr->getDirectCallee();
    if (!fun) {
        std::cout << "Fun was 0.\n";
        llvm::raw_os_ostream out(std::cout);
        callexpr->printPretty(out, 0, clang::PrintingPolicy(from->GetASTContext().getLangOpts()));
    }
    Type* ret = a.GetClangType(*from, fun->getResultType());
    // As a conversion operator, it has to be a member method only taking this as argument.
    std::vector<Type*> types;
    types.insert(types.begin(), self.t);
    auto funty = a.GetFunctionType(ret, types);    
    Expression clangfunc;
    clangfunc.t = funty;
    if (a.gen)
        clangfunc.Expr = a.gen->CreateFunctionValue(from->MangleName(fun));
    std::vector<Expression> expressions;
    expressions.push_back(self);
    auto e = funty->BuildCall(clangfunc, std::move(expressions), a);

    // The return type should be bool.
    if (e.t == a.Boolean)
        return e.Expr;
    throw std::runtime_error("Attempted to contextually convert to bool, but Clang gave back a function that did not return a bool. WTF.");
}