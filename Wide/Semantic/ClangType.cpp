#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Lexer/Token.h>
#include <iostream>
#include <array>

#pragma warning(push, 0)
#include <clang/AST/RecordLayout.h>
#include <clang/AST/ASTContext.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/Lookup.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/DerivedTypes.h>
#include <clang/Sema/Overload.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

namespace std {
    template<> struct hash<clang::AccessSpecifier> {
        std::size_t operator()(clang::AccessSpecifier a) const {
            return std::hash<int>()((int)a);
        }
    };
}
const std::unordered_map<clang::AccessSpecifier, Lexer::Access> AccessMapping = {
    { clang::AccessSpecifier::AS_public, Lexer::Access::Public },
    { clang::AccessSpecifier::AS_private, Lexer::Access::Private },
    { clang::AccessSpecifier::AS_protected, Lexer::Access::Protected },
};

std::unique_ptr<Expression> GetOverloadSet(clang::NamedDecl* d, ClangTU* from, std::unique_ptr<Expression> self, Context c, Analyzer& a) {
    std::unordered_set<clang::NamedDecl*> decls;
    if (d)
        decls.insert(d);
    return a.GetOverloadSet(std::move(decls), from, self->GetType())->BuildValueConstruction(Expressions(std::move(self)), c);
}
OverloadSet* GetOverloadSet(clang::LookupResult& lr, ClangTU* from, Type* self, Lexer::Access access, Analyzer& a) {
    std::unordered_set<clang::NamedDecl*> decls;
    for (auto decl : lr) {
        if (access < AccessMapping.at(decl->getAccess()))
            continue;
        decls.insert(decl);
    }
    if (decls.empty())
        return nullptr;
    return a.GetOverloadSet(std::move(decls), from, self);
}

void ClangType::ProcessImplicitSpecialMember(std::function<bool()> needs, std::function<clang::CXXMethodDecl*()> declare, std::function<void(clang::CXXMethodDecl*)> define, std::function<clang::CXXMethodDecl*()> lookup) {
    if (needs()) {
        auto decl = declare();
        from->GetSema().EvaluateImplicitExceptionSpec(clang::SourceLocation(), decl);
        if (!decl->isDeleted())
            define(decl);
    }
    else {
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


ClangType::ClangType(ClangTU* src, clang::QualType t, Analyzer& a) 
: from(src), type(t.getCanonicalType()), Type(a)
{
    // Declare any special members that need it.    
    // Also fix up their exception spec because for some reason Clang doesn't until you ask it.
    auto recdecl = type->getAsCXXRecordDecl();
    if (!recdecl) return;
    if (!recdecl->hasDefinition()) {
        auto spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(recdecl);
        auto loc = from->GetFileEnd();
        auto tsk = clang::TemplateSpecializationKind::TSK_ExplicitInstantiationDefinition;
        from->GetSema().InstantiateClassTemplateSpecialization(loc, spec, tsk);
        //from->GetSema().InstantiateClassTemplateSpecializationMembers(loc, llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(spec->getDefinition()), tsk);
    }
}
Wide::Util::optional<clang::QualType> ClangType::GetClangType(ClangTU& tu) {
    if (&tu != from) return Wide::Util::none;
    return type;
}

std::unique_ptr<Expression> ClangType::AccessMember(std::unique_ptr<Expression> val, std::string name, Context c) {
    auto access = GetAccessSpecifier(c.from, this);

    if (!val->GetType()->IsReference())
        val = BuildRvalueConstruction(Expressions(std::move(val)), c);

    clang::LookupResult lr(
        from->GetSema(), 
        clang::DeclarationNameInfo(clang::DeclarationName(from->GetIdentifierInfo(name)), clang::SourceLocation()),
        clang::Sema::LookupNameKind::LookupOrdinaryName);
    lr.suppressDiagnostics();

    if (!from->GetSema().LookupQualifiedName(lr, type.getCanonicalType()->getAs<clang::TagType>()->getDecl()))
        return nullptr;
    if (lr.isAmbiguous())
        throw ClangLookupAmbiguous(name, this, c.where);
    if (lr.isSingleResult()) {
        auto declaccess = AccessMapping.at(lr.getFoundDecl()->getAccess());
        if (access < declaccess)
            return Type::AccessMember(std::move(val), name, c);

        if (auto field = llvm::dyn_cast<clang::FieldDecl>(lr.getFoundDecl())) {
            return PrimitiveAccessMember(std::move(val), field->getFieldIndex() + type->getAsCXXRecordDecl()->bases_end() - type->getAsCXXRecordDecl()->bases_begin());
        }        
        if (auto fun = llvm::dyn_cast<clang::CXXMethodDecl>(lr.getFoundDecl())) {
            return GetOverloadSet(fun, from, std::move(val), { this, c.where }, analyzer);
        }
        if (auto ty = llvm::dyn_cast<clang::TypeDecl>(lr.getFoundDecl()))
            return analyzer.GetConstructorType(analyzer.GetClangType(*from, from->GetASTContext().getTypeDeclType(ty)))->BuildValueConstruction(Expressions(), c);
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(lr.getFoundDecl())) {    
            auto mangle = from->MangleName(vardecl);
            return CreatePrimUnOp(std::move(val), analyzer.GetLvalueType(analyzer.GetClangType(*from, vardecl->getType())), [mangle](llvm::Value* self, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                return g.module->getGlobalVariable(mangle(g));
            });
        }
        if (auto tempdecl = llvm::dyn_cast<clang::ClassTemplateDecl>(lr.getFoundDecl()))
            return analyzer.GetClangTemplateClass(*from, tempdecl)->BuildValueConstruction(Expressions(), c);
        throw ClangUnknownDecl(name, this, c.where);
    }    
    auto set = GetOverloadSet(lr, from, val->GetType(), access, analyzer);
    return set ? set->BuildValueConstruction(Expressions(std::move(val)), c) : Type::AccessMember(std::move(val), name, c);
}

llvm::Type* ClangType::GetLLVMType(Codegen::Generator& g) {
    return from->GetLLVMTypeFromClangType(type, g);
}
           
bool ClangType::IsComplexType(Codegen::Generator& g) {
    auto decl = type.getCanonicalType()->getAsCXXRecordDecl();
    return decl && from->IsComplexType(decl, g);
}

std::unique_ptr<Expression> ClangType::BuildBooleanConversion(std::unique_ptr<Expression> ex, Context c) {
    /*if (self.t->Decay() == self.t)
        self = BuildRvalueConstruction({ self }, c);*/
    if (!ex->GetType()->IsReference())
        ex = ex->GetType()->Decay()->BuildRvalueConstruction(Expressions(std::move(ex)), c);

    clang::OpaqueValueExpr ope(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(ex->GetType()));
    auto p = from->GetSema().PerformContextuallyConvertToBool(&ope);
    if (!p.get())
        throw NoBooleanConversion(ex->GetType(), c.where);
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
        assert(false && "Attempted to contextually convert to bool, but Clang gave back an expression that did not involve a function call.");
    }

    return GetOverloadSet(fun, from, std::move(ex), c, analyzer);
}

#pragma warning(disable : 4244)
std::size_t ClangType::size() {
    return from->GetASTContext().getTypeSizeInChars(type).getQuantity();
}
std::size_t ClangType::alignment() {
    return from->GetASTContext().getTypeAlignInChars(type).getQuantity();
}
#pragma warning(default : 4244)
Type* ClangType::GetContext() {
    return analyzer .GetClangNamespace(*from, type->getAsCXXRecordDecl()->getDeclContext());
}

namespace std {
    template<> struct iterator_traits<clang::ADLResult::iterator> {
        typedef std::input_iterator_tag iterator_category;
        typedef std::size_t difference_type;
    };
}

OverloadSet* ClangType::CreateADLOverloadSet(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access) {
    if (access != Lexer::Access::Public) return CreateADLOverloadSet(what, lhs, rhs, access);
    clang::ADLResult res;
    clang::OpaqueValueExpr lhsexpr(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(lhs));
    clang::OpaqueValueExpr rhsexpr(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(rhs));
    std::vector<clang::Expr*> exprs;
    exprs.push_back(&lhsexpr);
    exprs.push_back(&rhsexpr);
    from->GetSema().ArgumentDependentLookup(from->GetASTContext().DeclarationNames.getCXXOperatorName(GetTokenMappings().at(what).first), true, clang::SourceLocation(), exprs, res);
    std::unordered_set<clang::NamedDecl*> decls;
    decls.insert(res.begin(), res.end());
    return analyzer.GetOverloadSet(std::move(decls), from, GetContext());
}

OverloadSet* ClangType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType name, Lexer::Access access) {
    if (!ProcessedAssignmentOperators && name == Lexer::TokenType::Assignment) {
        ProcessedAssignmentOperators = true;
        auto recdecl = type->getAsCXXRecordDecl();
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitCopyAssignment(); },
            [&]{ return from->GetSema().DeclareImplicitCopyAssignment(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitCopyAssignment(clang::SourceLocation(), decl); },
            [&]{ return from->GetSema().LookupCopyingAssignment(recdecl, 0, false, 0); }
        );
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitMoveAssignment(); },
            [&]{ return from->GetSema().DeclareImplicitMoveAssignment(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitMoveAssignment(clang::SourceLocation(), decl); },
            [&]{ return from->GetSema().LookupMovingAssignment(recdecl, 0, false, 0); }
        );        
    }
    auto opkind = GetTokenMappings().at(name).first;
    clang::LookupResult lr(
        from->GetSema(), 
        from->GetASTContext().DeclarationNames.getCXXOperatorName(opkind), 
        clang::SourceLocation(),
        clang::Sema::LookupNameKind::LookupOrdinaryName
    );
    lr.suppressDiagnostics();
    if (!from->GetSema().LookupQualifiedName(lr, type.getCanonicalType()->getAs<clang::TagType>()->getDecl()))
        return analyzer.GetOverloadSet();

    std::unordered_set<clang::NamedDecl*> decls;
    for (auto decl : lr)
        if (AccessMapping.at(decl->getAccess()) <= access)
            decls.insert(decl);
    return analyzer.GetOverloadSet(std::move(decls), from, self);
}

OverloadSet* ClangType::CreateConstructorOverloadSet(Lexer::Access access) {
    if (!ProcessedConstructors) {
        auto recdecl = type->getAsCXXRecordDecl();
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitDefaultConstructor(); },
            [&]{ return from->GetSema().DeclareImplicitDefaultConstructor(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitDefaultConstructor(clang::SourceLocation(), static_cast<clang::CXXConstructorDecl*>(decl)); },
            [&]{ return from->GetSema().LookupDefaultConstructor(recdecl); }
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
        ProcessedConstructors = true;
    }
    auto cons = from->GetSema().LookupConstructors(type->getAsCXXRecordDecl());
    std::unordered_set<clang::NamedDecl*> decls;
    for (auto con : cons)
        if (AccessMapping.at(con->getAccess()) <= access)
            decls.insert(con);
    auto tupcon = GetTypesForTuple() ? TupleInitializable::CreateConstructorOverloadSet(Lexer::Access::Public) : analyzer.GetOverloadSet();
    return analyzer.GetOverloadSet(analyzer.GetOverloadSet(decls, from, analyzer.GetLvalueType(this)), tupcon);
}

std::unique_ptr<Expression> ClangType::BuildDestructorCall(std::unique_ptr<Expression> self, Context c) {
    if (!ProcessedDestructors) {
        auto recdecl = type->getAsCXXRecordDecl();
        ProcessImplicitSpecialMember(
            [&]{ return recdecl->needsImplicitDestructor(); },
            [&]{ return from->GetSema().DeclareImplicitDestructor(recdecl); },
            [&](clang::CXXMethodDecl* decl) { return from->GetSema().DefineImplicitDestructor(clang::SourceLocation(), static_cast<clang::CXXDestructorDecl*>(decl)); },
            [&]{ return from->GetSema().LookupDestructor(recdecl); }
        );
        ProcessedDestructors = true;
    }
    auto des = from->GetSema().LookupDestructor(type->getAsCXXRecordDecl());
    if (des->isTrivial())
        return Type::BuildDestructorCall(std::move(self), c);
    std::unordered_set<clang::NamedDecl*> decls;
    decls.insert(des);
    auto set = analyzer.GetOverloadSet(decls, from, analyzer.GetLvalueType(this))->BuildValueConstruction(Expressions(std::move(self)), { this, c.where });
    return set->GetType()->BuildCall(std::move(set), Expressions(), c);
}

Wide::Util::optional<std::vector<Type*>> ClangType::GetTypesForTuple() {
    auto recdecl = type->getAsCXXRecordDecl();
    if (!recdecl) return Wide::Util::none;
    if (recdecl->hasUserDeclaredCopyAssignment()
     || recdecl->hasUserDeclaredCopyConstructor()
     || recdecl->hasUserDeclaredDestructor()
     || recdecl->hasUserDeclaredMoveAssignment()
     || recdecl->hasUserDeclaredMoveConstructor())
        return Wide::Util::none;
    std::vector<Type*> types;
    for (auto it = recdecl->bases_begin(); it != recdecl->bases_end(); ++it) {
        types.push_back(analyzer.GetClangType(*from, it->getType()));
    }
    for (auto it = recdecl->field_begin(); it != recdecl->field_end(); ++it) {
        types.push_back(analyzer.GetClangType(*from, it->getType()));
    }
    return types;
}

std::unique_ptr<Expression> ClangType::PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num) {
    auto type_convert = [this](Type* source_ty, Type* root_ty) -> Type* {
        // If it's not a reference, just return the root type.
        if (!source_ty->IsReference())
            return root_ty;

        // If the source is an lvalue, the result is an lvalue.
        if (IsLvalueType(source_ty))
            return analyzer.GetLvalueType(root_ty->Decay());

        // It's not a value or an lvalue so must be rvalue.
        return analyzer.GetRvalueType(root_ty);
    };

    clang::QualType resty;
    std::size_t numbases = type->getAsCXXRecordDecl()->bases_end() - type->getAsCXXRecordDecl()->bases_begin();
    if (num >= numbases) {
        resty = std::next(type->getAsCXXRecordDecl()->field_begin(), num - numbases)->getType();
    } else {
        resty = std::next(type->getAsCXXRecordDecl()->bases_begin(), num)->getType();
    }
    auto source_type = self->GetType();
    auto root_type = analyzer.GetClangType(*from, resty);
    auto result_type = type_convert(source_type, root_type);
    auto fieldnum = num >= numbases 
        ? from->GetFieldNumber(*std::next(type->getAsCXXRecordDecl()->field_begin(), num - numbases))
        : from->GetBaseNumber(type->getAsCXXRecordDecl(), (type->getAsCXXRecordDecl()->bases_begin() + num)->getType()->getAsCXXRecordDecl());
    return CreatePrimUnOp(std::move(self), result_type, [this, num, result_type, resty, source_type, root_type, fieldnum](llvm::Value* self, Codegen::Generator& g, llvm::IRBuilder<>& bb) -> llvm::Value* {
        std::size_t numbases = type->getAsCXXRecordDecl()->bases_end() - type->getAsCXXRecordDecl()->bases_begin();
        if (num < numbases && resty->getAsCXXRecordDecl()->isEmpty())
            return bb.CreatePointerCast(self, result_type->GetLLVMType(g));
        if (source_type->IsReference())
            if (root_type->IsReference())
                return bb.CreateLoad(bb.CreateStructGEP(self, fieldnum(g)));
            return bb.CreateStructGEP(self, fieldnum(g));
        return bb.CreateExtractValue(self, fieldnum(g));
    });
}

InheritanceRelationship ClangType::IsDerivedFrom(Type* other) {
    auto otherbase = dynamic_cast<BaseType*>(other);
    if (!otherbase) return InheritanceRelationship::NotDerived;
    auto otherclangty = other->GetClangType(*from);
    if (!otherclangty) return InheritanceRelationship::NotDerived;
    auto otherdecl = (*otherclangty)->getAsCXXRecordDecl();
    auto recdecl = type->getAsCXXRecordDecl();
    InheritanceRelationship result = InheritanceRelationship::NotDerived;
    for (auto base = recdecl->bases_begin(); base != recdecl->bases_end(); ++base) {
        auto basedecl = base->getType()->getAsCXXRecordDecl();
        if (basedecl == otherdecl) {
            if (result == InheritanceRelationship::NotDerived)
                result = InheritanceRelationship::UnambiguouslyDerived;
            else
                result = InheritanceRelationship::AmbiguouslyDerived;
            continue;
        }
        auto Base = dynamic_cast<BaseType*>(analyzer.GetClangType(*from, base->getType()));
        auto subresult = Base->IsDerivedFrom(other);
        if (subresult == InheritanceRelationship::AmbiguouslyDerived)
            result = InheritanceRelationship::AmbiguouslyDerived;
        if (subresult == InheritanceRelationship::UnambiguouslyDerived) {
            if (result == InheritanceRelationship::NotDerived)
                result = subresult;
            if (result == InheritanceRelationship::UnambiguouslyDerived)
                result = InheritanceRelationship::AmbiguouslyDerived;
        }
    }
    return result;
}
std::unique_ptr<Expression> ClangType::AccessBase(std::unique_ptr<Expression> self, Type* other) {
    auto recdecl = type->getAsCXXRecordDecl();
    other = other->Decay();
    assert(IsDerivedFrom(other) == InheritanceRelationship::UnambiguouslyDerived);
    for (auto baseit = recdecl->bases_begin(); baseit != recdecl->bases_end(); ++baseit) {
        auto basety = analyzer.GetClangType(*from, baseit->getType());
        auto base = dynamic_cast<BaseType*>(basety);
        Type* result;
        if (auto ptr = dynamic_cast<PointerType*>(self->GetType()->Decay())) {
            result = analyzer.GetPointerType(other);
        }
        if (IsLvalueType(self->GetType()))
            result = analyzer.GetLvalueType(other);
        else
            result = analyzer.GetRvalueType(other);
        if (basety == other) {
            // Gotta account for EBO
            // We have the same pointer/value category as the argument.
            if (baseit->getType()->getAsCXXRecordDecl()->isEmpty()) {
                return PrimitiveAccessMember(std::move(self), baseit - recdecl->bases_begin());
            }
            return PrimitiveAccessMember(std::move(self), baseit - recdecl->bases_begin());
        }
        if (base->IsDerivedFrom(other) == InheritanceRelationship::UnambiguouslyDerived)
            return base->AccessBase(AccessBase(std::move(self), basety), other);
    }
    assert(false);
    return nullptr;
}
std::string ClangType::explain() {
    auto basename = GetContext()->explain() + "." + type->getAsCXXRecordDecl()->getName().str();
    if (auto tempspec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(type->getAsCXXRecordDecl())) {
        basename += "(";
        for (auto&& arg : tempspec->getTemplateArgs().asArray()) {
            if (arg.getKind() == clang::TemplateArgument::ArgKind::Type) {
                basename += analyzer.GetClangType(*from, arg.getAsType())->explain();
            }
            if (&arg != &tempspec->getTemplateArgs().asArray().back())
                basename += ", ";
        }
        basename += ")";
    }
    return basename;
}
std::vector<std::pair<BaseType*, unsigned>> ClangType::GetBases() {
    auto&& layout = from->GetASTContext().getASTRecordLayout(type->getAsCXXRecordDecl());
    std::vector<std::pair<BaseType*, unsigned>> out;
    for (auto basespec = type->getAsCXXRecordDecl()->bases_begin(); basespec != type->getAsCXXRecordDecl()->bases_end(); basespec++) {
        out.push_back(std::make_pair(dynamic_cast<BaseType*>(analyzer.GetClangType(*from, basespec->getType())), layout.getBaseClassOffset(basespec->getType()->getAsCXXRecordDecl()).getQuantity()));
    }
    return out;
}
std::unique_ptr<Expression> GetVTablePointer(std::unique_ptr<Expression> self, const clang::CXXRecordDecl* current, Analyzer& a, ClangTU* from, Type* vptrty) {
    auto&& layout = from->GetASTContext().getASTRecordLayout(current);
    if (layout.hasOwnVFPtr())
        return CreatePrimUnOp(std::move(self), a.GetLvalueType(a.GetPointerType(vptrty)), [](llvm::Value* val, Codegen::Generator& g, llvm::IRBuilder<>& b) {
            return b.CreateStructGEP(val, 0);
        });
    auto basenum = from->GetBaseNumber(current, layout.getPrimaryBase());
    self = CreatePrimUnOp(std::move(self), a.GetPointerType(a.GetClangType(*from, from->GetASTContext().getTypeDeclType(layout.getPrimaryBase()))), [from, basenum](llvm::Value* val, Codegen::Generator& g, llvm::IRBuilder<>& b) {
        return b.CreateStructGEP(val, basenum(g));
    });
    return GetVTablePointer(std::move(self), layout.getPrimaryBase(), a, from, vptrty);
}

std::unique_ptr<Expression> ClangType::GetVirtualPointer(std::unique_ptr<Expression> self) {
    assert(self->GetType()->IsReference());
    return ::GetVTablePointer(std::move(self), type->getAsCXXRecordDecl(), analyzer, from, GetVirtualPointerType());
}

Type* ClangType::GetVirtualPointerType() {
    return analyzer.GetFunctionType(analyzer.GetIntegralType(32, true), {}, true);
}
std::vector<BaseType::VirtualFunction> ClangType::ComputeVTableLayout() {
    return std::vector<VirtualFunction>();
}
std::unique_ptr<Expression> ClangType::FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset) {
    return nullptr;
}
bool ClangType::IsEliminateType() {
    return type->getAsCXXRecordDecl()->isEmpty();
}