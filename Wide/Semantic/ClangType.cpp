#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Util/MakeUnique.h>
#include <Wide/Semantic/ClangOverloadSet.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Lexer/Token.h>
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

    if (!recdecl->hasDefinition()) {
        auto spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(recdecl);
        if (!spec)
            throw std::runtime_error("Attempt to use a Clang type which was incomplete.");
        auto loc = from->GetFileEnd();
        auto tsk = clang::TemplateSpecializationKind::TSK_ExplicitInstantiationDefinition;
        from->GetSema().InstantiateClassTemplateSpecialization(loc, spec, tsk);
        //from->GetSema().InstantiateClassTemplateSpecializationMembers(loc, llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(spec->getDefinition()), tsk);
    }

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

Expression ClangType::BuildOverloadSet(Expression self, std::string name, clang::LookupResult& lr, Analyzer& a) {
    auto ptr = Wide::Memory::MakeUnique<clang::UnresolvedSet<8>>();
    clang::UnresolvedSet<8>& us = *ptr;
    for(auto it = lr.begin(); it != lr.end(); ++it) {
        us.addDecl(*it);
    }
    Expression out;
    if (self.t && !self.t->IsReference()) {
        std::vector<Expression> args;
        args.push_back(self);
        self = self.t->BuildRvalueConstruction(std::move(args), a);
    }
    LookupResultCache[name] = a.arena.Allocate<ClangOverloadSet>(std::move(ptr), from, self.t);
    std::vector<Expression> args;
    if (self.t)
        args.push_back(self);
    return LookupResultCache[name]->BuildValueConstruction(self, a);
}

Wide::Util::optional<Expression> ClangType::AccessMember(Expression val, std::string name, Analyzer& a) {
    if (LookupResultCache.find(name) != LookupResultCache.end()) {
        std::vector<Expression> args;
        if (val.Expr) args.push_back(val);
        return LookupResultCache[name]->BuildValueConstruction(std::move(args), a);
    }
    if (name == "~type") {
        auto des = from->GetSema().LookupDestructor(type->getAsCXXRecordDecl());
        auto ptr = Wide::Memory::MakeUnique<clang::UnresolvedSet<8>>();
        clang::UnresolvedSet<8>& us = *ptr;
        us.addDecl(des);
        
        if (val.t && !val.t->IsReference()) {
            std::vector<Expression> args;
            args.push_back(val);
            val = val.t->BuildRvalueConstruction(std::move(args), a);
        }
        LookupResultCache[name] = a.arena.Allocate<ClangOverloadSet>(std::move(ptr), from, val.t);
        std::vector<Expression> args;
        if (val.t)
            args.push_back(val);
        return LookupResultCache[name]->BuildValueConstruction(val, a);
    }
    clang::LookupResult lr(
        from->GetSema(), 
        clang::DeclarationNameInfo(clang::DeclarationName(from->GetIdentifierInfo(name)), clang::SourceLocation()),
        clang::Sema::LookupNameKind::LookupOrdinaryName);

    if (!from->GetSema().LookupQualifiedName(lr, type.getCanonicalType()->getAs<clang::TagType>()->getDecl()))
        return Wide::Util::none;
    if (lr.isAmbiguous())
        throw std::runtime_error("Attempted to access a member of a Clang type, but Clang said the lookup was ambiguous.");
    if (lr.isSingleResult()) {
        if (val.Expr) {
            // Check for members first.
            if (auto field = llvm::dyn_cast<clang::FieldDecl>(lr.getFoundDecl())) {
                Expression out;
                if (a.IsLvalueType(val.t)) {
                    // The result is an lvalue of that type.
                    out.t = a.AsLvalueType(a.GetClangType(*from, field->getType()));
                } else {
                    out.t = a.AsRvalueType(a.GetClangType(*from, field->getType()));
                }
                out.Expr = a.gen->CreateFieldExpression(val.Expr, from->GetFieldNumber(field));
                return out;
            }        
            if (auto fun = llvm::dyn_cast<clang::CXXMethodDecl>(lr.getFoundDecl())) {     
                return BuildOverloadSet(val, std::move(name), lr, a);
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
            out.t = a.AsLvalueType(a.GetClangType(*from, vardecl->getType()));
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
    return BuildOverloadSet(val, std::move(name), lr, a);
}

Expression ClangType::BuildCall(Expression self, std::vector<Expression> args, Analyzer& a) {
    clang::OpaqueValueExpr ope(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(self.t));
    auto declname = from->GetASTContext().DeclarationNames.getCXXOperatorName(clang::OverloadedOperatorKind::OO_Call);
    clang::LookupResult lr(from->GetSema(), clang::DeclarationNameInfo(declname, clang::SourceLocation()), clang::Sema::LookupNameKind::LookupOrdinaryName);
    auto result = from->GetSema().LookupQualifiedName(lr, type->getAsCXXRecordDecl(), false);
    if (!result) {
        throw std::runtime_error("Attempted to call a Clang type, but Clang said that it could not find the member.");
    }
    return BuildOverloadSet(self, "()", lr, a).BuildCall(std::move(args), a);
}


std::function<llvm::Type*(llvm::Module*)> ClangType::GetLLVMType(Analyzer& a) {
    return from->GetLLVMTypeFromClangType(type, a);
}
           
Codegen::Expression* ClangType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (args.size() == 1 && args[0].t->Decay() == this && !IsComplexType()) {
        return a.gen->CreateStore(mem, args[0].t->BuildValue(args[0], a).Expr);
    }
    clang::UnresolvedSet<8> us;

    auto recdecl = type->getAsCXXRecordDecl();    
    // The first argument is pseudo-this.
    if (!recdecl) {
        type->dump();
        // Just store.
        throw std::runtime_error("Attempted to in-place construct a type that was not a CXXRecordDecl. Maybe a union or something. This is not supported.");
    }

    for(auto begin = recdecl->ctor_begin(); begin != recdecl->ctor_end(); ++begin) {
        us.addDecl(*begin);
    }
    std::vector<clang::OpaqueValueExpr> exprs;
    for(auto x : args) {
        exprs.push_back(clang::OpaqueValueExpr(clang::SourceLocation(), x.t->GetClangType(*from, a).getNonLValueExprType(from->GetASTContext()), GetKindOfType(x.t)));
    }
    std::vector<clang::Expr*> exprptrs;
    for(auto&& x : exprs) {
        exprptrs.push_back(&x);
    }
    clang::OverloadCandidateSet s((clang::SourceLocation()));
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
    if (args.size() == 0 && fun->isTrivial()) {
        return mem;
    }
    if (args.size() == 1 && fun->isTrivial() && args[0].t->Decay() == this) {
        return a.gen->CreateStore(mem, args[0].t->BuildValue(args[0], a).Expr);
    }

    std::vector<Type*> types;

    // Constructor signatures don't seem to include the need for "this", so just lie.
    // It all turns out alright.
    Expression self;
    self.t = a.AsLvalueType(this);
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
                if (a.IsLvalueType(ty)) {
                    args.push_back(ty->Decay()->BuildLvalueConstruction(conargs, a));
                } else if (a.IsRvalueType(ty)) {
                    args.push_back(ty->Decay()->BuildRvalueConstruction(conargs, a));
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

static const std::unordered_map<Lexer::TokenType, std::pair<clang::OverloadedOperatorKind, clang::BinaryOperatorKind>> BinaryTokenMapping = []() 
    -> std::unordered_map<Lexer::TokenType, std::pair<clang::OverloadedOperatorKind, clang::BinaryOperatorKind>> 
{
    std::unordered_map<Lexer::TokenType, std::pair<clang::OverloadedOperatorKind, clang::BinaryOperatorKind>> ret;
    ret[Lexer::TokenType::NotEqCmp] = std::make_pair(clang::OverloadedOperatorKind::OO_ExclaimEqual, clang::BinaryOperatorKind::BO_NE);
    ret[Lexer::TokenType::EqCmp] = std::make_pair(clang::OverloadedOperatorKind::OO_EqualEqual, clang::BinaryOperatorKind::BO_EQ);
    ret[Lexer::TokenType::LT] = std::make_pair(clang::OverloadedOperatorKind::OO_Less, clang::BinaryOperatorKind::BO_LT);
    ret[Lexer::TokenType::GT] = std::make_pair(clang::OverloadedOperatorKind::OO_Greater, clang::BinaryOperatorKind::BO_GT);
    ret[Lexer::TokenType::LTE] = std::make_pair(clang::OverloadedOperatorKind::OO_LessEqual, clang::BinaryOperatorKind::BO_LE);
    ret[Lexer::TokenType::GTE] = std::make_pair(clang::OverloadedOperatorKind::OO_GreaterEqual, clang::BinaryOperatorKind::BO_GE);
    
    ret[Lexer::TokenType::Assignment] = std::make_pair(clang::OverloadedOperatorKind::OO_Equal, clang::BinaryOperatorKind::BO_Assign);

    ret[Lexer::TokenType::LeftShift] = std::make_pair(clang::OverloadedOperatorKind::OO_LessLess, clang::BinaryOperatorKind::BO_Shl);
    ret[Lexer::TokenType::LeftShiftAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_LessLessEqual, clang::BinaryOperatorKind::BO_ShlAssign);
    ret[Lexer::TokenType::RightShift] = std::make_pair(clang::OverloadedOperatorKind::OO_GreaterGreater, clang::BinaryOperatorKind::BO_Shr);
    ret[Lexer::TokenType::RightShiftAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_GreaterGreaterEqual, clang::BinaryOperatorKind::BO_ShrAssign);
    ret[Lexer::TokenType::Plus] = std::make_pair(clang::OverloadedOperatorKind::OO_Plus, clang::BinaryOperatorKind::BO_Add);
    ret[Lexer::TokenType::PlusAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_PlusEqual, clang::BinaryOperatorKind::BO_AddAssign);
    ret[Lexer::TokenType::Minus] = std::make_pair(clang::OverloadedOperatorKind::OO_Minus, clang::BinaryOperatorKind::BO_Sub);
    ret[Lexer::TokenType::MinusAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_MinusEqual, clang::BinaryOperatorKind::BO_SubAssign);
    ret[Lexer::TokenType::Divide] = std::make_pair(clang::OverloadedOperatorKind::OO_Slash, clang::BinaryOperatorKind::BO_Div);
    ret[Lexer::TokenType::DivAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_SlashEqual, clang::BinaryOperatorKind::BO_DivAssign);
    ret[Lexer::TokenType::Modulo] = std::make_pair(clang::OverloadedOperatorKind::OO_Percent, clang::BinaryOperatorKind::BO_Rem);
    ret[Lexer::TokenType::ModAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_PercentEqual, clang::BinaryOperatorKind::BO_RemAssign);
    ret[Lexer::TokenType::Dereference] = std::make_pair(clang::OverloadedOperatorKind::OO_Star, clang::BinaryOperatorKind::BO_Mul);
    ret[Lexer::TokenType::MulAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_StarEqual, clang::BinaryOperatorKind::BO_MulAssign);
    ret[Lexer::TokenType::Xor] = std::make_pair(clang::OverloadedOperatorKind::OO_Caret, clang::BinaryOperatorKind::BO_Xor);
    ret[Lexer::TokenType::XorAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_CaretEqual, clang::BinaryOperatorKind::BO_XorAssign);
    ret[Lexer::TokenType::Or] = std::make_pair(clang::OverloadedOperatorKind::OO_Pipe, clang::BinaryOperatorKind::BO_Or);
    ret[Lexer::TokenType::OrAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_PipeEqual, clang::BinaryOperatorKind::BO_OrAssign);
    ret[Lexer::TokenType::And] = std::make_pair(clang::OverloadedOperatorKind::OO_Amp, clang::BinaryOperatorKind::BO_And);
    ret[Lexer::TokenType::AndAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_AmpEqual, clang::BinaryOperatorKind::BO_AndAssign);
    return ret;
}();
Expression ClangType::BuildBinaryExpression(Expression lhs, Expression rhs, Lexer::TokenType tok, Analyzer& a) {
    auto opkind = BinaryTokenMapping.at(tok).first;
    auto opcode = BinaryTokenMapping.at(tok).second;
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
        return Type::BuildBinaryExpression(lhs, rhs, tok, a);
    auto callexpr = llvm::dyn_cast<clang::CallExpr>(opresult.get());
       
    auto fun = callexpr->getDirectCallee();
    if (!fun) {
        std::cout << "Fun was 0.\n";
        llvm::raw_os_ostream out(std::cout);
        callexpr->printPretty(out, 0, clang::PrintingPolicy(from->GetASTContext().getLangOpts()));
        throw std::runtime_error("Internal compiler error: Attempted to build an overloaded binary operator, and Clang resolved the operator, but not to a function?");
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
Wide::Codegen::Expression* ClangType::BuildBooleanConversion(Expression self, Analyzer& a) {
    clang::OpaqueValueExpr ope(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(self.t));
    auto p = from->GetSema().PerformContextuallyConvertToBool(&ope);
    if (!p.get())
        throw std::runtime_error("Attempted to convert an object to bool contextually, but Clang said this was not possible.");
    clang::CallExpr* callexpr;
    if (auto ice = llvm::dyn_cast<clang::ImplicitCastExpr>(p.get())) {
        callexpr = llvm::dyn_cast<clang::CallExpr>(ice->getSubExpr());
    } else {
        callexpr = llvm::dyn_cast<clang::CallExpr>(p.get());
    }
       
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
    // If the function really returns an i1, the code generator will implicitly patch it up for us.
    if (e.t == a.GetBooleanType())
        return e.Expr;
    throw std::runtime_error("Attempted to contextually convert to bool, but Clang gave back a function that did not return a bool. WTF.");
}

ConversionRank ClangType::RankConversionFrom(Type* src, Analyzer& a) {
    // Now in the realms of: implicit conversions, is the type movable, etc.
    clang::UnresolvedSet<8> us;

    auto recdecl = type->getAsCXXRecordDecl();    
    // The first argument is pseudo-this.
    for(auto begin = recdecl->ctor_begin(); begin != recdecl->ctor_end(); ++begin) {
        us.addDecl(*begin);
    }

    clang::OpaqueValueExpr ope(clang::SourceLocation(), src->GetClangType(*from, a).getNonLValueExprType(from->GetASTContext()), GetKindOfType(src));
    std::vector<clang::Expr*> exprptrs;
    exprptrs.push_back(&ope);
    clang::OverloadCandidateSet s((clang::SourceLocation()));
    for(auto&& x : us) {
        auto con = static_cast<clang::CXXConstructorDecl*>(x);
        clang::DeclAccessPair d;
        d.set(con, con->getAccess());
        from->GetSema().AddOverloadCandidate(con, d, exprptrs, s);
    }
    clang::OverloadCandidateSet::iterator best;
    auto result = s.BestViableFunction(from->GetSema(), clang::SourceLocation(), best);
    if (result != clang::OverloadingResult::OR_Success)
        return ConversionRank::None;
    // Assign ranks differently if it's a copy/move to if it's an implicit conversion
    if (src->IsReference(this))
        return ConversionRank::Zero;
    return ConversionRank::Two;
}

Expression ClangType::BuildDereference(Expression self, Analyzer& a) {
    clang::OpaqueValueExpr ope(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(self.t));
    auto declname = from->GetASTContext().DeclarationNames.getCXXOperatorName(clang::OverloadedOperatorKind::OO_Star);
    clang::LookupResult lr(from->GetSema(), clang::DeclarationNameInfo(declname, clang::SourceLocation()), clang::Sema::LookupNameKind::LookupOrdinaryName);
    auto result = from->GetSema().LookupQualifiedName(lr, type->getAsCXXRecordDecl(), false);
    if (!result) {
        throw std::runtime_error("Attempted to de-reference a Clang type, but Clang said that it could not find the member.");
    }
    return BuildOverloadSet(self, "*", lr, a).BuildCall(a);
}

Expression ClangType::BuildIncrement(Expression self, bool postfix, Analyzer& a) {
    if (postfix) {
        std::vector<Expression> args;
        args.push_back(self);
        auto result = BuildRvalueConstruction(args, a);
        self = self.t->BuildIncrement(self, false, a);
        result.Expr = a.gen->CreateChainExpression(a.gen->CreateChainExpression(result.Expr, self.Expr), result.Expr);
        return result;
    }
    clang::OpaqueValueExpr ope(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(self.t));
    auto declname = from->GetASTContext().DeclarationNames.getCXXOperatorName(clang::OverloadedOperatorKind::OO_PlusPlus);
    clang::LookupResult lr(from->GetSema(), clang::DeclarationNameInfo(declname, clang::SourceLocation()), clang::Sema::LookupNameKind::LookupOrdinaryName);
    auto result = from->GetSema().LookupQualifiedName(lr, type->getAsCXXRecordDecl(), false);
    if (!result) {
        throw std::runtime_error("Attempted to de-reference a Clang type, but Clang said that it could not find the member.");
    }
    return BuildOverloadSet(self, "p++", lr, a).BuildCall(a);
}

std::size_t ClangType::size(Analyzer& a) {
    return from->GetASTContext().getTypeSizeInChars(type).getQuantity();
}
std::size_t ClangType::alignment(Analyzer& a) {
    return from->GetASTContext().getTypeAlignInChars(type).getQuantity();
}