#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/SemanticExpression.h>
#include <sstream>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
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


UserDefinedType::UserDefinedType(const AST::Type* t, Analyzer& a, Type* context)
: a(a) {
    iscomplex = false;
    std::unordered_map<std::string, unsigned> mem;
    type = t;
    allocsize = 0;
    align = 0;
    std::stringstream stream;
    stream << "struct.__" << this;
    llvmname = stream.str();

    struct TypeMemberVariableLookupContext : public Type {
        Type* context;
        Type* udt;
        Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression self, std::string name, Analyzer& a) override {
            if (name == "this")
                return a.GetConstructorType(udt)->BuildValueConstruction(a);
            return context->AccessMember(self, name, a);
        }
    };
    TypeMemberVariableLookupContext lcontext;
    lcontext.context = context;
    lcontext.udt = this;
    for (auto&& var : t->variables) {
        auto expr = a.AnalyzeExpression(&lcontext, var->initializer).Resolve(nullptr);
        expr.t = expr.t->Decay();
        member m;
        if (auto con = dynamic_cast<ConstructorType*>(expr.t)) {
            expr.t = con->GetConstructedType();
            m.InClassInitializer = nullptr;
        }
        else {
            m.InClassInitializer = var->initializer;
        }
        m.t = expr.t;
        auto talign = m.t->alignment(a);
        if (allocsize % talign != 0) {
            auto adjustment = align - (allocsize % talign);
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
        allocsize = a.gen->GetInt8AllocSize();
        align = llvm::DataLayout(a.gen->GetDataLayout()).getABIIntegerTypeAlignment(8);
    }
    if (allocsize % align != 0) {
        auto adjustment = align - (allocsize % align);
        allocsize += adjustment;
        types.push_back([&a, adjustment](llvm::Module* mod) {
            return llvm::ArrayType::get(llvm::IntegerType::getInt8Ty(mod->getContext()), adjustment);
        });
    }

    ty = [&](llvm::Module* m) -> llvm::Type* {
        if (m->getTypeByName(llvmname)) {
            if (llvmtypes.empty())
                a.gen->AddEliminateType(m->getTypeByName(llvmname));
            return m->getTypeByName(llvmname);
        }
        std::vector<llvm::Type*> llvmtypes;
        for (auto&& x : types)
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

    AST::Function* des;
    // Generate destructors    
    if (type->Functions.find("~type") != type->Functions.end()) {
        des = *type->Functions.at("~type")->functions.begin();
    } else {
        static const std::shared_ptr<std::string> destructor_location = std::make_shared<std::string>("Automatically generated destructor");
        des = a.arena.Allocate<AST::Function>(
            "~type",
            std::vector<AST::Statement*>(),
            std::vector<AST::Statement*>(),
            Lexer::Range(destructor_location),
            std::vector<AST::FunctionArgument>(),
            GetDeclContext(),
            std::vector<AST::Variable*>()
        );
    }

    for(auto x = llvmtypes.rbegin(); x != llvmtypes.rend(); ++x) {
        if (x->t->IsReference()) continue;
        des->statements.push_back(a.arena.Allocate<SemanticExpression>(ConcreteExpression(nullptr, 
            ConcreteExpression(a.AsLvalueType(x->t), a.gen->CreateFieldExpression(a.gen->CreateParameterExpression(0), x->num))
            .AccessMember("~type", a)
            ->BuildCall(a, Lexer::Range(std::shared_ptr<std::string>())).Resolve(a.GetVoidType()).Expr)));
    }
    std::unordered_set<const AST::Function*> funcs;
    funcs.insert(des);
    destructor = a.arena.Allocate<OverloadSet>(std::move(funcs), a.AsLvalueType(this));

    iscomplex = iscomplex || type->Functions.find("~type") != type->Functions.end();
    if (type->Functions.find("type") != type->Functions.end()) {
        std::vector<Type*> self;
        self.push_back(a.AsLvalueType(this));
        iscomplex = iscomplex || a.GetOverloadSet(type->Functions.at("type"), a.AsLvalueType(this))->ResolveOverloadRank(std::move(self), a) != ConversionRank::None;
    }
    
    std::unordered_set<const AST::Function*> cons;
    if (type->Functions.find("type") == type->Functions.end() || type->name == "__lambda") {
        cons.insert(AddDefaultConstructor(a));
        cons.insert(AddCopyConstructor(a));
        cons.insert(AddMoveConstructor(a));
        cons.erase(nullptr);
    } else {
        for(auto x : type->Functions.at("type")->functions)
            cons.insert(x);
    }
    constructor = a.arena.Allocate<OverloadSet>(cons);
}

std::function<llvm::Type*(llvm::Module*)> UserDefinedType::GetLLVMType(Analyzer& a) {
    return ty;
}

Codegen::Expression* UserDefinedType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a) {
    if (!IsComplexType() && args.size() == 1 && args[0].t->Decay() == this) {
        return a.gen->CreateStore(mem, args[0].BuildValue(a).Expr);
    }
    return a.GetOverloadSet(type->Functions.at("type"), a.AsLvalueType(this))->BuildValueConstruction(ConcreteExpression(a.AsLvalueType(this), mem), a).BuildCall(std::move(args), a, Lexer::Range(std::shared_ptr<std::string>())).Resolve(nullptr).Expr;
}

Wide::Util::optional<ConcreteExpression> UserDefinedType::AccessMember(ConcreteExpression expr, std::string name, Analyzer& a) {
    if (name == "~type") {
        if (!expr.t->IsReference())
            expr = expr.t->BuildLvalueConstruction(expr, a);
        else if (a.IsRvalueType(expr.t))
            expr.t = a.AsLvalueType(expr.t->Decay());
        return destructor->BuildValueConstruction(expr, a);
    }
    if (members.find(name) != members.end()) {
        auto member = llvmtypes[members[name]];
        ConcreteExpression out;
        if (expr.t->IsReference()) {
            out.Expr = a.gen->CreateFieldExpression(expr.Expr, member.num);
            if (a.IsLvalueType(expr.t)) {
                out.t = a.AsLvalueType(member.t);
            } else {
                out.t = a.AsRvalueType(member.t);
            }
            // Need to collapse the pointers here- if member is a T* under the hood, then it'll be a T** when accessed
            // So load it to become a T* like the reference type expects.
            if (member.t->IsReference())
                out.Expr = a.gen->CreateLoad(out.Expr);
            return out;
        } else {
            return ConcreteExpression(member.t, a.gen->CreateFieldExpression(expr.Expr, member.num));
        }
    }
    if (type->Functions.find(name) != type->Functions.end()) {
        auto self = expr.t == this ? BuildRvalueConstruction(expr, a) : expr;
        return a.GetOverloadSet(type->Functions.at(name), self.t)->BuildValueConstruction(self, a);
    }
    return Wide::Util::none;
}

const AST::DeclContext* UserDefinedType::GetDeclContext() {
    return type;
}
ConcreteExpression UserDefinedType::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType t, Analyzer& a) {
    if (type->opcondecls.find(t) != type->opcondecls.end()) {
        lhs = lhs.t == this ? BuildRvalueConstruction(lhs, a) : lhs;
        return a.GetOverloadSet(type->opcondecls.at(t), lhs.t)->BuildValueConstruction(lhs, a).BuildCall(rhs, a, Lexer::Range(std::shared_ptr<std::string>())).Resolve(nullptr);
    }
    return Type::BuildBinaryExpression(lhs, rhs, t, a);
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
        for(auto&& x : type->Functions.at("()")->functions) {
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

ConversionRank UserDefinedType::RankConversionFrom(Type* from, Analyzer& a) {
    std::vector<Type*> arg;
    arg.push_back(from);
    return a.GetOverloadSet(type->Functions.at("type"), a.AsLvalueType(this))->ResolveOverloadRank(std::move(arg), a);
}

Expression UserDefinedType::BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    auto self = val.t == this ? BuildRvalueConstruction(val, a) : val;
    if (type->opcondecls.find(Lexer::TokenType::OpenBracket) != type->opcondecls.end())
        return a.GetOverloadSet(type->opcondecls.at(Lexer::TokenType::OpenBracket), self.t)->BuildValueConstruction(self, a).BuildCall(std::move(args), a, where);
    throw std::runtime_error("Attempt to call a user-defined type with no operator() defined.");
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

AST::Function* UserDefinedType::AddCopyConstructor(Analyzer& a) {
    // If non-complex, we just use a load/store, no need for explicit.
    if (!IsComplexType()) return nullptr;

    auto should = true;
    for(auto&& m : llvmtypes) {
        should = should && ((a.RankConversion(a.AsLvalueType(m.t), m.t) == ConversionRank::Zero) || !m.t->IsComplexType());
    }
    if (!should) return nullptr;
    std::vector<AST::FunctionArgument> args;
    AST::FunctionArgument self;
    self.name = "other";
    self.type = a.arena.Allocate<SemanticExpression>(ConcreteExpression(a.GetConstructorType(a.AsLvalueType(this)), nullptr));
    args.push_back(self);

    static const auto location = std::make_shared<std::string>("Analyzer copy constructor internal detail");

    std::vector<AST::Variable*> initializers;
    for(auto&& m : llvmtypes) {
        initializers.push_back(
            a.arena.Allocate<AST::Variable>(m.name, a.arena.Allocate<AST::MemberAccess>(m.name, a.arena.Allocate<AST::Identifier>("other", Lexer::Range(location)), Lexer::Range(location)), Lexer::Range(location))
        );
    }
    return a.arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), Lexer::Range(location), std::move(args), type, std::move(initializers));
}
AST::Function* UserDefinedType::AddMoveConstructor(Analyzer& a) {
    // If non-complex, we just use a load/store, no need for explicit.
    if (!IsComplexType()) return nullptr;

    auto should = true;
    for(auto&& m : llvmtypes) {
        should = should && ((a.RankConversion(a.GetRvalueType(m.t), m.t) == ConversionRank::Zero)|| !m.t->IsComplexType());
    }
    if (!should) return nullptr;
    std::vector<AST::FunctionArgument> args;
    AST::FunctionArgument self;
    self.name = "other";
    self.type = a.arena.Allocate<SemanticExpression>(ConcreteExpression(a.GetConstructorType(a.GetRvalueType(this)), nullptr));
    args.push_back(self);

    static const auto location = std::make_shared<std::string>("Analyzer move constructor internal detail");

    std::vector<AST::Variable*> initializers;
    auto other = ConcreteExpression(a.GetRvalueType(this), a.gen->CreateParameterExpression(1));
    for(auto&& m : llvmtypes) {
        initializers.push_back(a.arena.Allocate<AST::Variable>(m.name, a.arena.Allocate<SemanticExpression>(*other.t->AccessMember(other, m.name, a)), Lexer::Range(location)));
    }
    return a.arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), Lexer::Range(location), std::move(args), type, std::move(initializers));
}
AST::Function* UserDefinedType::AddDefaultConstructor(Analyzer& a) {
    std::vector<AST::FunctionArgument> args;
    std::vector<AST::Variable*> initializers;
    static const auto location = std::make_shared<std::string>("Analyzer default constructor internal detail");
    return a.arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), Lexer::Range(location), std::move(args), type, std::move(initializers));
}