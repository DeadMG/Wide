#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Codegen/Function.h>
#include <Wide/Semantic/ConstructorType.h>

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

struct SemanticExpression : public AST::Expression {
    Semantic::Expression e;
    SemanticExpression(Semantic::Expression expr)
        : e(expr), AST::Expression(Lexer::Range()) {}
};

bool UserDefinedType::IsComplexType() {
    if (!processedconstructors) {                
        iscomplex = iscomplex || type->Functions.find("~type") != type->Functions.end();
        if (type->Functions.find("type") != type->Functions.end()) {
            std::vector<Type*> self;
            self.push_back(a.GetLvalueType(this));
            iscomplex = iscomplex || a.GetOverloadSet(type->Functions["type"], a.GetLvalueType(this))->ResolveOverloadRank(std::move(self), a) != ConversionRank::None;
            //self.push_back(a.GetRvalueType(this));
            //iscomplex = iscomplex || a.GetOverloadSet(type->Functions["type"], this)->ResolveOverloadRank(std::move(self), a) != ConversionRank::None;
        }
        
        if (type->Functions.find("type") == type->Functions.end()) {
            a.AddDefaultConstructor(type, this);
            a.AddCopyConstructor(type, this);
            a.AddMoveConstructor(type, this);
        }

        processedconstructors = true;
        
    }
    return iscomplex;
}


UserDefinedType::UserDefinedType(AST::Type* t, Analyzer& a, Type* context)
: a(a) {
    iscomplex = false;
    std::unordered_map<std::string, unsigned> mem;
    type = t;
    allocsize = 0;
    align = 0;
    std::stringstream stream;
    stream << "struct.__" << this;
    llvmname = stream.str();
    processedconstructors = false;
    
    struct TypeMemberVariableLookupContext : public Type {
        Type* context;
        Type* udt;
        Expression AccessMember(Expression self, std::string name, Analyzer& a) {
            if (name == "this")
                return a.GetConstructorType(udt)->BuildValueConstruction(a);
            return context->AccessMember(self, name, a);
        }
    };
    TypeMemberVariableLookupContext lcontext;
    lcontext.context = context;
    lcontext.udt = this;
    for(auto&& var : t->variables) {
        auto expr = a.AnalyzeExpression(&lcontext, var->initializer);
        expr.t = expr.t->Decay();
        member m;
        if (auto con = dynamic_cast<ConstructorType*>(expr.t)) {
            expr.t = con->GetConstructedType();
            m.InClassInitializer = nullptr;
        } else {
            m.InClassInitializer = var->initializer;
        }
        m.t = expr.t;
        auto talign = m.t->alignment(a);
        if (allocsize % talign != 0) {
            auto adjustment = (allocsize % talign);
            allocsize += adjustment;
            types.push_back([&a, adjustment](llvm::Module* mod) {
                return llvm::ArrayType::get(llvm::IntegerType::getInt8Ty(mod->getContext()), adjustment);
            });
        }
        align = std::max(align, talign);
        allocsize += m.t->size(a);

        m.num = types.size();
        m.name = var->name;
        mem[var->name] = llvmtypes.size();
        types.push_back(m.t->GetLLVMType(a));
        llvmtypes.push_back(m);
        iscomplex = iscomplex || expr.t->IsComplexType();
    }
    if (t->variables.empty()) {
        allocsize = llvm::DataLayout(a.gen->main.getDataLayout()).getTypeAllocSize(llvm::IntegerType::getInt8Ty(a.gen->context));
        align = llvm::DataLayout(a.gen->main.getDataLayout()).getABIIntegerTypeAlignment(8);
    }
    
    ty = [&](llvm::Module* m) -> llvm::Type* {
        if (m->getTypeByName(llvmname)) {
            if (llvmtypes.empty())
                a.gen->AddEliminateType(m->getTypeByName(llvmname));
            return m->getTypeByName(llvmname);
        }
        std::vector<llvm::Type*> llvmtypes;
        for(auto&& x : types)
            llvmtypes.push_back(x(m));
        if (llvmtypes.empty()) {
            llvmtypes.push_back(llvm::IntegerType::getInt8Ty(m->getContext()));
        }
        auto ty = llvm::StructType::create(llvmtypes, llvmname);
        if (llvmtypes.empty())
            a.gen->AddEliminateType(ty);
        return ty;
    };
    // Enable member lookup
    members = std::move(mem);
    
    // Generate destructors
    std::vector<AST::Statement*> body;
    for(auto x = llvmtypes.rbegin(); x != llvmtypes.rend(); ++x) {
        if (x->t->IsReference()) continue;
        body.push_back(a.arena.Allocate<SemanticExpression>(Expression(nullptr, 
            Expression(a.GetLvalueType(x->t), a.gen->CreateFieldExpression(a.gen->CreateParameterExpression(0), x->num))
            .AccessMember("~type", a)
            .BuildCall(a).Expr)));
    }
    
    if (type->Functions.find("~type") != type->Functions.end()) {
        body.insert(
            body.begin(), 
            a.arena.Allocate<SemanticExpression>(
                a.GetOverloadSet(type->Functions["~type"], a.GetLvalueType(this))
                ->BuildValueConstruction(Expression(a.GetLvalueType(this), a.gen->CreateParameterExpression(0)), a)
                .BuildCall(a)));
    }
    
    auto des = a.arena.Allocate<AST::Function>(
        "~type", 
        std::move(body), 
        std::vector<AST::Statement*>(), 
        Lexer::Range(), 
        std::vector<AST::FunctionArgument>(), 
        GetDeclContext(), 
        std::vector<AST::VariableStatement*>()
    );
    if (type->Functions.find("~type") != type->Functions.end()) {
        type->Functions["~type"]->functions = Wide::Concurrency::Vector<AST::Function*>();
    } else {
        type->Functions["~type"] = a.arena.Allocate<AST::FunctionOverloadSet>("~type", GetDeclContext());
    }
    type->Functions["~type"]->functions.push_back(des);    
}

std::function<llvm::Type*(llvm::Module*)> UserDefinedType::GetLLVMType(Analyzer& a) {
    return ty;
}

Codegen::Expression* UserDefinedType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (!IsComplexType() && args.size() == 1 && args[0].t->Decay() == this) {
        return a.gen->CreateStore(mem, args[0].BuildValue(a).Expr);
    }
    return a.GetOverloadSet(type->Functions["type"], a.GetLvalueType(this))->BuildValueConstruction(Expression(a.GetLvalueType(this), mem), a).BuildCall(std::move(args), a).Expr;
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
            return Expression(member.t, a.gen->CreateFieldExpression(expr.Expr, member.num));
        }
    }
    if (type->Functions.find(name) != type->Functions.end()) {
        auto self = expr.t == this ? BuildRvalueConstruction(expr, a) : expr;
        return a.GetOverloadSet(type->Functions[name], self.t)->BuildValueConstruction(self, a);
    }
    throw std::runtime_error("Attempted to access a name of an object, but no such member existed.");
}

AST::DeclContext* UserDefinedType::GetDeclContext() {
    return type;
}

Expression UserDefinedType::BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) {
    if (dynamic_cast<LvalueType*>(lhs.t)) {
        // If we have an overloaded operator, call that.
        if (type->Functions.find("=") != type->Functions.end()) {
            return a.GetOverloadSet(type->Functions["="], lhs.t)->BuildValueConstruction(lhs, a).BuildCall(rhs, a);
        }
        if (!IsComplexType()) {
            return Expression(lhs.t, a.gen->CreateStore(lhs.Expr, rhs.BuildValue(a).Expr));
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
            auto num = x.num;
            Expression rhs(t, a.gen->CreateFieldExpression(rhs.Expr, [this, num] { return AdjustFieldOffset(num);}));
            Expression lhs(a.GetLvalueType(x.t), a.gen->CreateFieldExpression(lhs.Expr, [this, num] { return AdjustFieldOffset(num);}));

            auto construct = x.t->BuildAssignment(lhs, rhs, a);
            e = e ? a.gen->CreateChainExpression(e, construct.Expr) : construct.Expr;
        }
        // The original expr referred to the memory we were in- return that.
        e = a.gen->CreateChainExpression(e, lhs.Expr);
        out.t = lhs.t;
        return out;
    }
    throw std::runtime_error("Attempt to assign to an rvalue of user-defined type.");
}
clang::QualType UserDefinedType::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    if (clangtypes.find(&TU) != clangtypes.end())
        return clangtypes[&TU];
    
    std::stringstream stream;
    stream << "__" << this;

    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(stream.str()));
    recdecl->startDefinition();
    clangtypes[&TU] = TU.GetASTContext().getTypeDeclType(recdecl);

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
                }, TU.MangleName(meth), nullptr, true);// If an i8/i1 mismatch, fix it up for us amongst other things.
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
    auto size = TU.GetASTContext().getTypeSizeInChars(TU.GetASTContext().getTypeDeclType(recdecl).getTypePtr());
    TU.GetDeclContext()->addDecl(recdecl);
    a.AddClangType(TU.GetASTContext().getTypeDeclType(recdecl), this);
    return clangtypes[&TU];
}

bool UserDefinedType::HasMember(std::string name) {
    return type->Functions.find(name) != type->Functions.end() || members.find(name) != members.end();
}

Expression UserDefinedType::BuildBinaryOperator(std::string opname, Expression lhs, Expression rhs, Analyzer& a) {
    if (type->Functions.find(opname) != type->Functions.end()) {
        return a.GetOverloadSet(type->Functions[opname], lhs.t)->BuildValueConstruction(lhs, a).BuildCall(rhs, a);
    }
    // May also match a global operator
    std::vector<Expression> args;
    args.push_back(lhs);
    args.push_back(rhs);
    try {
        return a.GetDeclContext(type->higher)->AccessMember(Expression(), opname, a).BuildCall(args, a);
    } catch(...) {
        return a.GetDeclContext(rhs.t->GetDeclContext()->higher)->AccessMember(Expression(), opname, a).BuildCall(std::move(args), a);
    }
}

Expression UserDefinedType::BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildBinaryOperator("<", lhs, rhs, a);
}
ConversionRank UserDefinedType::RankConversionFrom(Type* from, Analyzer& a) {
    IsComplexType();
    std::vector<Type*> arg;
    arg.push_back(from);
    return a.GetOverloadSet(type->Functions["type"], a.GetLvalueType(this))->ResolveOverloadRank(std::move(arg), a);
}

Expression UserDefinedType::BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) {
    return AccessMember(val, "()", a).BuildCall(std::move(args), a);
}

Expression UserDefinedType::BuildOr(Expression lhs, Expression rhs, Analyzer& a) {
    return BuildBinaryOperator("|", lhs, rhs, a);
}

std::size_t UserDefinedType::size(Analyzer& a) {
    return allocsize;
}
std::size_t UserDefinedType::alignment(Analyzer& a) {
    return align;
}
unsigned UserDefinedType::AdjustFieldOffset(unsigned index) {
    return index;
    /*if (clangtypes.empty()) return index;
    return clangtypes.begin()->first->GetFieldNumber(*std::next(clangtypes.begin()->second->getAsCXXRecordDecl()->field_begin(), index));*/
}