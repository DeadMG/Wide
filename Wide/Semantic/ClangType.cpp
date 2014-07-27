#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/ClangTU.h>
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
#include <sstream>
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
const std::unordered_map<clang::AccessSpecifier, Parse::Access> AccessMapping = {
    { clang::AccessSpecifier::AS_public, Parse::Access::Public },
    { clang::AccessSpecifier::AS_private, Parse::Access::Private },
    { clang::AccessSpecifier::AS_protected, Parse::Access::Protected },
};

std::shared_ptr<Expression> GetOverloadSet(clang::NamedDecl* d, ClangTU* from, std::shared_ptr<Expression> self, Context c, Analyzer& a) {
    std::unordered_set<clang::NamedDecl*> decls;
    if (d)
        decls.insert(d);
    auto ty = self->GetType();
    return a.GetOverloadSet(std::move(decls), from, self->GetType())->BuildValueConstruction({ self }, c);
}
OverloadSet* GetOverloadSet(clang::LookupResult& lr, ClangTU* from, Type* self, Parse::Access access, Analyzer& a) {
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

std::shared_ptr<Expression> ClangType::AccessMember(std::shared_ptr<Expression> val, std::string name, Context c) {
    auto access = GetAccessSpecifier(c.from, this);

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
            return analyzer.GetConstructorType(analyzer.GetClangType(*from, from->GetASTContext().getTypeDeclType(ty)))->BuildValueConstruction({}, c);
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(lr.getFoundDecl())) {    
            auto var = from->GetObject(vardecl);
            return CreatePrimUnOp(std::move(val), analyzer.GetLvalueType(analyzer.GetClangType(*from, vardecl->getType())), [var](llvm::Value* self, CodegenContext& con) {
                return var(con);
            });
        }
        if (auto tempdecl = llvm::dyn_cast<clang::ClassTemplateDecl>(lr.getFoundDecl()))
            return BuildChain(val, analyzer.GetClangTemplateClass(*from, tempdecl)->BuildValueConstruction({}, c));
        throw ClangUnknownDecl(name, this, c.where);
    }    
    auto set = GetOverloadSet(lr, from, val->GetType(), access, analyzer);
    return set ? set->BuildValueConstruction({ val }, c) : Type::AccessMember(std::move(val), name, c);
}

llvm::Type* ClangType::GetLLVMType(llvm::Module* module) {
    return from->GetLLVMTypeFromClangType(type, module);
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

OverloadSet* ClangType::CreateADLOverloadSet(Lexer::TokenType what, Parse::Access access) {
    if (access != Parse::Access::Public) return CreateADLOverloadSet(what, access);
    clang::ADLResult res;
    clang::OpaqueValueExpr lhsexpr(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(this));
    std::vector<clang::Expr*> exprs;
    exprs.push_back(&lhsexpr);
    from->GetSema().ArgumentDependentLookup(from->GetASTContext().DeclarationNames.getCXXOperatorName(GetTokenMappings().at(what).first), true, clang::SourceLocation(), exprs, res);
    std::unordered_set<clang::NamedDecl*> decls;
    decls.insert(res.begin(), res.end());
    return analyzer.GetOverloadSet(std::move(decls), from, GetContext());
}

OverloadSet* ClangType::CreateOperatorOverloadSet(Lexer::TokenType name, Parse::Access access) {
    if (!ProcessedAssignmentOperators && name == &Lexer::TokenTypes::Assignment) {
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
    if (name == &Lexer::TokenTypes::QuestionMark) {
        auto get_bool_convert = [&, this](Type* t)-> std::unique_ptr<OverloadResolvable> {
            auto ope = std::make_shared<clang::OpaqueValueExpr>(clang::SourceLocation(), type.getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(t));
            auto p = from->GetSema().PerformContextuallyConvertToBool(ope.get());
            if (!p.get())
                return nullptr;
            // We have some expression. Try to interpret it.
            
            auto base = p.get();
            return MakeResolvable([this, ope, base](std::vector<std::shared_ptr<Expression>> exprs, Context c) {
                return InterpretExpression(base, *from, c, analyzer, { { ope.get(), exprs[0] } });
            }, { t });
        };
        if (!boollvalue) boollvalue = get_bool_convert(analyzer.GetLvalueType(this));
        if (!boolrvalue) boolrvalue = get_bool_convert(analyzer.GetRvalueType(this));
       
        return analyzer.GetOverloadSet(
            *boollvalue ? analyzer.GetOverloadSet(boollvalue->get()) : analyzer.GetOverloadSet(), 
            *boollvalue ? analyzer.GetOverloadSet(boolrvalue->get()) : analyzer.GetOverloadSet()
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
    return analyzer.GetOverloadSet(std::move(decls), from, this);
}

OverloadSet* ClangType::CreateConstructorOverloadSet(Parse::Access access) {
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
    auto tupcon = GetTypesForTuple() ? TupleInitializable::CreateConstructorOverloadSet(Parse::Access::Public) : analyzer.GetOverloadSet();
    return analyzer.GetOverloadSet(analyzer.GetOverloadSet(decls, from, analyzer.GetLvalueType(this)), tupcon);
}

std::function<void(CodegenContext&)> ClangType::BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize) {
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
        return Type::BuildDestructorCall(std::move(self), c, true);
    if (des->isVirtual() && !devirtualize) {
        std::unordered_set<clang::NamedDecl*> decls;
        decls.insert(des);
        auto set = analyzer.GetOverloadSet(decls, from, analyzer.GetLvalueType(this))->BuildValueConstruction({ self }, { this, c.where });
        auto call = set->GetType()->BuildCall(std::move(set), {}, c);
        return [=](CodegenContext& con) {
            call->GetValue(con);
        };
    }
    auto obj = from->GetObject(des, clang::CXXDtorType::Dtor_Complete);
    return [=](CodegenContext& con) {
        auto val = self->GetValue(con);
        con->CreateCall(obj(con), { val });
    };
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

std::shared_ptr<Expression> ClangType::PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) {
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
    return CreatePrimUnOp(std::move(self), result_type, [this, num, result_type, resty, source_type, root_type, fieldnum](llvm::Value* self, CodegenContext& con) -> llvm::Value* {
        std::size_t numbases = type->getAsCXXRecordDecl()->bases_end() - type->getAsCXXRecordDecl()->bases_begin();
        if (num < numbases && resty->getAsCXXRecordDecl()->isEmpty())
            return con->CreatePointerCast(self, result_type->GetLLVMType(con));
        if (source_type->IsReference()) {
            if (root_type->IsReference())
                return con->CreateLoad(con.CreateStructGEP(self, fieldnum(con)));
            return con.CreateStructGEP(self, fieldnum(con));
        }
        return con->CreateExtractValue(self, fieldnum(con));
    });
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
std::vector<std::pair<Type*, unsigned>> ClangType::GetBasesAndOffsets() {
    auto&& layout = from->GetASTContext().getASTRecordLayout(type->getAsCXXRecordDecl());
    std::vector<std::pair<Type*, unsigned>> out;
    // Skip virtual bases for now, we don't support.
    // Skip them silently because we need a base list to compute stuff like access specifiers.
    for (auto basespec = type->getAsCXXRecordDecl()->bases_begin(); basespec != type->getAsCXXRecordDecl()->bases_end(); basespec++) {
        if (!basespec->isVirtual())
            out.push_back(std::make_pair(analyzer.GetClangType(*from, basespec->getType()), layout.getBaseClassOffset(basespec->getType()->getAsCXXRecordDecl()).getQuantity()));
    }
    return out;
}
std::shared_ptr<Expression> GetVTablePointer(std::shared_ptr<Expression> self, const clang::CXXRecordDecl* current, Analyzer& a, ClangTU* from, Type* vptrty) {
    auto&& layout = from->GetASTContext().getASTRecordLayout(current);
    if (layout.hasOwnVFPtr())
        return CreatePrimUnOp(std::move(self), a.GetLvalueType(a.GetPointerType(vptrty)), [](llvm::Value* val, CodegenContext& con) {
            return con.CreateStructGEP(val, 0);
        });
    auto basenum = from->GetBaseNumber(current, layout.getPrimaryBase());
    self = CreatePrimUnOp(std::move(self), a.GetPointerType(a.GetClangType(*from, from->GetASTContext().getTypeDeclType(layout.getPrimaryBase()))), [from, basenum](llvm::Value* val, CodegenContext& con) {
        return con.CreateStructGEP(val, basenum(con));
    });
    return GetVTablePointer(std::move(self), layout.getPrimaryBase(), a, from, vptrty);
}

std::shared_ptr<Expression> ClangType::GetVirtualPointer(std::shared_ptr<Expression> self) {
    if (!type->getAsCXXRecordDecl()->isPolymorphic()) return nullptr;
    assert(self->GetType()->IsReference());
    return ::GetVTablePointer(std::move(self), type->getAsCXXRecordDecl(), analyzer, from, GetVirtualPointerType());
}

Type* ClangType::GetVirtualPointerType() {
    // The calling convention here is kinda suspect, but we always cast, so...
    return analyzer.GetFunctionType(analyzer.GetIntegralType(32, true), {}, true, llvm::CallingConv::C);
}

Type::VTableLayout ClangType::ComputePrimaryVTableLayout() {
    // ITANIUM ABI SPECIFIC
    auto CreateVFuncFromMethod = [&](clang::CXXMethodDecl* methit) {
        VTableLayout::VirtualFunction vfunc;
        vfunc.name = methit->getName();
        vfunc.ret = analyzer.GetClangType(*from, methit->getResultType());
        if (methit->getRefQualifier() == clang::RefQualifierKind::RQ_RValue)
            vfunc.args.push_back(analyzer.GetRvalueType(this));
        else
            vfunc.args.push_back(analyzer.GetLvalueType(this));
        for (auto paramit = methit->param_begin(); paramit != methit->param_end(); ++paramit) {
            vfunc.args.push_back(analyzer.GetClangType(*from, (*paramit)->getType()));
        }
        return vfunc;
    };
    auto&& layout = from->GetASTContext().getASTRecordLayout(type->getAsCXXRecordDecl());
    // If we have a primary base, then the vtable is the base vtable followed by the derived vtable.
    if (!layout.hasOwnVFPtr()) {
        auto pbase = analyzer.GetClangType(*from, from->GetASTContext().getRecordType(layout.getPrimaryBase()));
        auto pbaselayout = pbase->GetVtableLayout();
        for (auto methit = type->getAsCXXRecordDecl()->method_begin(); methit != type->getAsCXXRecordDecl()->method_end(); ++methit) {
            if (!methit->isVirtual()) continue;
            // No additional slot if it overrides a method from primary base AND the return type does not require offsetting
            auto add_extra_slot = [&] {
                if (methit->size_overridden_methods() == 0)
                    return true;
                for (auto overriddenit = methit->begin_overridden_methods(); overriddenit != methit->end_overridden_methods(); ++overriddenit) {
                    auto basemeth = *overriddenit;
                    if (basemeth->getParent() == layout.getPrimaryBase()) {
                        if (basemeth->getResultType() == methit->getResultType())
                            return false;
                        // If they have an adjustment of zero.
                        auto basety = analyzer.GetClangType(*from, basemeth->getResultType());
                        auto derty = analyzer.GetClangType(*from, methit->getResultType());
                        if (derty->InheritsFromAtOffsetZero(basety)) return false;
                        continue;
                    }
                }
                return true;
            };
            if (!add_extra_slot()) continue;
            VTableLayout::VirtualFunctionEntry entry;
            entry.abstract = methit->isPure();
            entry.function = CreateVFuncFromMethod(*methit);
            pbaselayout.layout.push_back(entry);
        }
        return pbaselayout;
    }
    VTableLayout out;
    out.offset = 2;
    VTableLayout::VirtualFunctionEntry offset;
    offset.abstract = false;
    offset.function = VTableLayout::SpecialMember::OffsetToTop;
    out.layout.push_back(offset);
    VTableLayout::VirtualFunctionEntry RTTI;
    RTTI.abstract = false;
    RTTI.function = VTableLayout::SpecialMember::RTTIPointer;
    out.layout.push_back(RTTI);
    for (auto methit = type->getAsCXXRecordDecl()->method_begin(); methit != type->getAsCXXRecordDecl()->method_end(); ++methit) {
        if (methit->isVirtual()) {
            VTableLayout::VirtualFunctionEntry entry;
            entry.abstract = methit->isPure();
            if (auto des = llvm::dyn_cast<clang::CXXDestructorDecl>(*methit)) {
                entry.function = VTableLayout::SpecialMember::Destructor;
                out.layout.push_back(entry);
                entry.function = VTableLayout::SpecialMember::ItaniumABIDeletingDestructor;
            }
            else
                entry.function = CreateVFuncFromMethod(*methit);
            out.layout.push_back(entry);
        }
    }
    return out;
}

std::shared_ptr<Expression> ClangType::VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry, unsigned offset) {
    // ITANIUM ABI SPECIFIC
    struct VTableThunk : Expression {
        VTableThunk(std::function<llvm::Function*(llvm::Module*)> f, unsigned off, FunctionType* sig)
        : func(f), offset(off), signature(sig)
        {}
        FunctionType* signature;
        unsigned offset;
        std::function<llvm::Function*(llvm::Module*)> func;
        Type* GetType() override final {
            return signature;
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            if (offset == 0)
                return func(con);
            auto this_index = (std::size_t)signature->GetReturnType()->IsComplexType();
            std::stringstream strstr;
            strstr << "__" << this << offset;
            auto thunk = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(signature->GetLLVMType(con)->getElementType()), llvm::GlobalValue::LinkageTypes::InternalLinkage, strstr.str(), con);
            llvm::BasicBlock* bb = llvm::BasicBlock::Create(con, "entry", thunk);
            llvm::IRBuilder<> irbuilder(bb);
            auto self = std::next(thunk->arg_begin(), signature->GetReturnType()->IsComplexType());
            auto offset_self = irbuilder.CreateConstGEP1_32(irbuilder.CreatePointerCast(self, llvm::IntegerType::getInt8PtrTy(con->getContext())), -offset);
            auto cast_self = irbuilder.CreatePointerCast(offset_self, std::next(func(con)->arg_begin(), this_index)->getType());
            std::vector<llvm::Value*> args;
            for (std::size_t i = 0; i < thunk->arg_size(); ++i) {
                if (i == this_index)
                    args.push_back(cast_self);
                else
                    args.push_back(std::next(thunk->arg_begin(), i));
            }
            auto call = irbuilder.CreateCall(func(con), args);
            if (call->getType() == llvm::Type::getVoidTy(con))
                irbuilder.CreateRetVoid();
            else
                irbuilder.CreateRet(call);
            return thunk;
        }
    };
    if (auto mem = boost::get<VTableLayout::SpecialMember>(&entry.function)) {
        auto conv = GetCallingConvention(type->getAsCXXRecordDecl()->getDestructor());
        if (*mem == VTableLayout::SpecialMember::Destructor) {
            return Wide::Memory::MakeUnique<VTableThunk>(from->GetObject(type->getAsCXXRecordDecl()->getDestructor(), clang::CXXDtorType::Dtor_Complete), offset, analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this) }, false, conv));
        }
        if (*mem == VTableLayout::SpecialMember::ItaniumABIDeletingDestructor) {
            return Wide::Memory::MakeUnique<VTableThunk>(from->GetObject(type->getAsCXXRecordDecl()->getDestructor(), clang::CXXDtorType::Dtor_Deleting), offset, analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this) }, false, conv));
        }
        return nullptr;
    }
    // args includes this, which will have a different type here.
    // Pretend that it really has our type.
    auto func = boost::get<VTableLayout::VirtualFunction>(entry.function);
    auto args = func.args;
    auto name = func.name;
    auto ret = func.ret;
    if (IsLvalueType(args[0]))
        args[0] = analyzer.GetLvalueType(this);
    else
        args[0] = analyzer.GetRvalueType(this);
    for (auto methit = type->getAsCXXRecordDecl()->method_begin(); methit != type->getAsCXXRecordDecl()->method_end(); ++methit) {
        auto func = *methit;
        if (func->getName() != name)
            continue;
        std::vector<Type*> f_args;
        if (methit->getRefQualifier() == clang::RefQualifierKind::RQ_RValue)
            f_args.push_back(analyzer.GetRvalueType(this));
        else
            f_args.push_back(analyzer.GetLvalueType(this));
        for (auto arg_it = func->param_begin(); arg_it != func->param_end(); ++arg_it)
            f_args.push_back(analyzer.GetClangType(*from, (*arg_it)->getType()));
        if (args != f_args)
            continue;
        auto fty = analyzer.GetFunctionType(analyzer.GetClangType(*from, func->getResultType()), f_args, func->isVariadic(), GetCallingConvention(func));
        return Wide::Memory::MakeUnique<VTableThunk>(from->GetObject(func), offset, fty);
    }
    return nullptr;
}
bool ClangType::IsEmpty() {
    return type->getAsCXXRecordDecl()->isEmpty();
}
Type* ClangType::GetConstantContext() {
    if (type->getAsCXXRecordDecl()->isEmpty())
        return this;
    return nullptr;
}
bool ClangType::IsSourceATarget(Type* first, Type* second, Type* context) {
    auto firstclangty = first->GetClangType(*from);
    if (!firstclangty) return false;
    // Must succeed when we're a ClangType.
    auto secondclangty = second->GetClangType(*from);
    if (!secondclangty) return false;

    clang::OpaqueValueExpr ope(clang::SourceLocation(), firstclangty->getNonLValueExprType(from->GetASTContext()), Semantic::GetKindOfType(first));
    auto sequence = from->GetSema().TryImplicitConversion(&ope, *secondclangty, false, false, false, false, false);
    if (sequence.getKind() == clang::ImplicitConversionSequence::Kind::UserDefinedConversion)
        return true;
    return false;
}
namespace {
    Lexer::Position PositionFromSourceLocation(clang::SourceLocation loc, clang::SourceManager& src) {
        Lexer::Position begin(std::make_shared<std::string>(src.getFilename(loc)));
        begin.column = src.getSpellingColumnNumber(loc);
        begin.line = src.getSpellingLineNumber(loc);
        begin.offset = src.getDecomposedSpellingLoc(loc).second;
        return begin;
    }
    Lexer::Range RangeFromSourceRange(clang::SourceRange range, clang::SourceManager& src) {
        return Lexer::Range(PositionFromSourceLocation(range.getBegin(), src), PositionFromSourceLocation(range.getEnd(), src));
    }
}
std::vector<ConstructorContext::member> ClangType::GetConstructionMembers() {
    std::vector<ConstructorContext::member> out;
    auto&& layout = from->GetASTContext().getASTRecordLayout(type->getAsCXXRecordDecl());
    for (auto baseit = type->getAsCXXRecordDecl()->bases_begin(); baseit != type->getAsCXXRecordDecl()->bases_end(); ++baseit) {
        ConstructorContext::member mem(RangeFromSourceRange(baseit->getSourceRange(), from->GetASTContext().getSourceManager()));
        mem.t = analyzer.GetClangType(*from, baseit->getType());
        mem.num = EmptyBaseOffset{ layout.getBaseClassOffset(baseit->getType()->getAsCXXRecordDecl()).getQuantity() };
        out.push_back(std::move(mem));
    }
    unsigned i = 0;
    for (auto fieldit = type->getAsCXXRecordDecl()->field_begin(); fieldit != type->getAsCXXRecordDecl()->field_end(); ++fieldit) {
        ConstructorContext::member mem(RangeFromSourceRange(fieldit->getSourceRange(), from->GetASTContext().getSourceManager()));
        mem.t = analyzer.GetClangType(*from, fieldit->getType());
        mem.num = { layout.getFieldOffset(i++) };
        mem.name = fieldit->getName();
        if (auto expr = fieldit->getInClassInitializer()) {
            auto style = fieldit->getInClassInitStyle();
            mem.InClassInitializer = [this, expr, fieldit](std::shared_ptr<Expression> field) {
                return InterpretExpression(expr, *from, { this, RangeFromSourceRange(fieldit->getSourceRange(), from->GetASTContext().getSourceManager()) }, analyzer);
            };
        }
        out.push_back(std::move(mem));
    }
    return std::move(out);
}
OverloadSet* ClangType::GetDestructorOverloadSet() {
    auto des = type->getAsCXXRecordDecl()->getDestructor();
    std::unordered_set<clang::NamedDecl*> decls = { des };
    return analyzer.GetOverloadSet(decls, from, nullptr);
}
llvm::Constant* ClangType::GetRTTI(llvm::Module* module) {
    return from->GetItaniumRTTI(type, module);
}
bool ClangType::IsTriviallyDestructible() {
    return !type->getAsCXXRecordDecl() || type->getAsCXXRecordDecl()->hasTrivialDestructor();
}
bool ClangType::IsTriviallyCopyConstructible() {
    return !type->getAsCXXRecordDecl() || type->getAsCXXRecordDecl()->hasTrivialCopyConstructor();
}
std::shared_ptr<Expression> ClangType::AccessStaticMember(std::string name, Context c) {
    auto access = GetAccessSpecifier(c.from, this);
    
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
            return Type::AccessStaticMember(name, c);

        if (auto field = llvm::dyn_cast<clang::FieldDecl>(lr.getFoundDecl())) {
            // TODO: Make PTM here.
            return nullptr;
        }
        if (auto fun = llvm::dyn_cast<clang::CXXMethodDecl>(lr.getFoundDecl())) {
            return analyzer.GetOverloadSet({ fun }, from, nullptr)->BuildValueConstruction({}, c);
        }
        if (auto ty = llvm::dyn_cast<clang::TypeDecl>(lr.getFoundDecl()))
            return analyzer.GetConstructorType(analyzer.GetClangType(*from, from->GetASTContext().getTypeDeclType(ty)))->BuildValueConstruction({}, c);
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(lr.getFoundDecl())) {
            auto var = from->GetObject(vardecl);
            return CreatePrimGlobal(analyzer.GetLvalueType(analyzer.GetClangType(*from, vardecl->getType())), [var](CodegenContext& con) {
                return var(con);
            });
        }
        if (auto tempdecl = llvm::dyn_cast<clang::ClassTemplateDecl>(lr.getFoundDecl()))
            return analyzer.GetClangTemplateClass(*from, tempdecl)->BuildValueConstruction({}, c);
        throw ClangUnknownDecl(name, this, c.where);
    }
    std::unordered_set<clang::NamedDecl*> decls(lr.begin(), lr.end());
    return analyzer.GetOverloadSet(decls, from, nullptr)->BuildValueConstruction({}, c);
}
