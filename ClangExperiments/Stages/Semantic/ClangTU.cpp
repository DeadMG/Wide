#include "ClangTU.h"
#include "Util.h"
#include "../../Util/MakeUnique.h"

#include <functional>
#include <string>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include "Analyzer.h"
#include "../Codegen/Generator.h"

#pragma warning(push, 0)

#include <llvm/IR/Module.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <CodeGen/CodeGenModule.h>
#include <clang/Sema/Sema.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Frontend/Utils.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Parse/ParseAST.h>
#include <CodeGen/CGCXXABI.h>
#include <CodeGen/CGRecordLayout.h>
#include <llvm/Linker.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/ASTConsumer.h>
#include <llvm/Support/raw_ostream.h>

#pragma warning(pop)

using namespace Wide;
using namespace ClangUtil;

namespace Wide {
    namespace ClangUtil {        
        class CodeGenConsumer : public clang::ASTConsumer {
        public:
            CodeGenConsumer(std::vector<clang::Decl*>& arg)
                : vec(arg) {}
            std::vector<clang::Decl*>& vec;
            bool HandleTopLevelDecl(clang::DeclGroupRef arg) {
                for(auto&& x : arg) {
                    vec.push_back(x);
                }
                return true;
            }
        }; 
    }
}

class ClangTU::Impl {
public:
    Semantic::ClangCommonState& ccs;
    clang::LangOptions langopts;
    std::vector<clang::Decl*> stuff;
    clang::SourceManager sm;
    clang::Preprocessor preproc;
    clang::ASTContext astcon;
    Wide::ClangUtil::CodeGenConsumer consumer;
    clang::Sema sema;
    std::string filename;
    std::unordered_map<clang::QualType, std::function<llvm::Type*(llvm::Module*)>, ClangTypeHasher> DeferredTypeMap;
    llvm::Module mod;
    clang::CodeGen::CodeGenModule codegenmod;
    
    // Clang has a really annoying habit of changing it's mind about this
    // producing multiple distinct llvm::Type*s for one QualType, so perform
    // our own caching on top.
    std::unordered_map<clang::QualType, llvm::Type*, ClangTypeHasher> typemap;
    
    void HandleErrors() {
        if (ccs.engine.hasFatalErrorOccurred())
            throw std::runtime_error(ccs.errors);
        if (!ccs.errors.empty()) ccs.Options->OnDiagnostic(ccs.errors);
        ccs.errors.clear();
    }
    
    Impl(llvm::LLVMContext& con, std::string file, Semantic::ClangCommonState& state)
        : ccs(state)
        , sm(ccs.engine, ccs.FileManager)
        , langopts(ccs.Options->LanguageOptions)
        , preproc(ccs.Options->PreprocessorOptions, ccs.engine, langopts, ccs.targetinfo.get(), sm, ccs.hs, ccs.ci)
        , astcon(langopts, sm, ccs.targetinfo.get(), preproc.getIdentifierTable(), preproc.getSelectorTable(), preproc.getBuiltinInfo(), 1000)
        , consumer(stuff)
        , sema(preproc, astcon, consumer, clang::TranslationUnitKind::TU_Complete) 
        , mod("", con)
        , codegenmod(astcon, ccs.Options->CodegenOptions, mod, ccs.layout, ccs.engine)
        , filename(std::move(file))   
    {
        mod.setDataLayout(ccs.targetinfo->getTargetDescription());
        mod.setTargetTriple(ccs.Options->TargetOptions.Triple);
        clang::InitializePreprocessor(preproc, *ccs.Options->PreprocessorOptions, *ccs.Options->HeaderSearchOptions, ccs.Options->FrontendOptions);
        preproc.getBuiltinInfo().InitializeBuiltins(preproc.getIdentifierTable(), ccs.Options->LanguageOptions);

        const clang::DirectoryLookup* directlookup = nullptr;
        auto entry = ccs.hs.LookupFile(filename, true, nullptr, directlookup, nullptr, nullptr, nullptr, nullptr);
        if (!entry)
            entry = ccs.FileManager.getFile(filename);
        if (!entry)
            throw std::runtime_error("Could not find file " + filename);
        
        auto fileid = sm.createFileID(entry, clang::SourceLocation(), clang::SrcMgr::CharacteristicKind::C_User);
        if (fileid.isInvalid())
            throw std::runtime_error("Error translating file " + filename);
        sm.setMainFileID(fileid);
        ccs.engine.getClient()->BeginSourceFile(ccs.Options->LanguageOptions, &preproc);
        ParseAST(sema);
        ccs.engine.getClient()->EndSourceFile();
        HandleErrors();
    }

    clang::DeclContext* GetDeclContext() {
        return astcon.getTranslationUnitDecl();
    }
};

ClangTU::~ClangTU() {}

ClangTU::ClangTU(llvm::LLVMContext& c, std::string file, Semantic::ClangCommonState& ccs)
    : impl(Wide::Memory::MakeUnique<Impl>(c, file, ccs)) 
{
}

ClangTU::ClangTU(ClangTU&& other)
    : impl(std::move(other.impl)) {}

void ClangTU::GenerateCodeAndLinkModule(llvm::Module* main) {
    for(auto x : impl->stuff)
        impl->codegenmod.EmitTopLevelDecl(x);
    impl->codegenmod.Release();

    llvm::Linker link("", main);
    link.LinkInModule(&impl->mod);
    link.releaseModule();
    impl->HandleErrors();
}

clang::DeclContext* ClangTU::GetDeclContext() {
    return impl->GetDeclContext();
}

clang::QualType Simplify(clang::QualType inc) {
    inc = inc.getCanonicalType();
    inc.removeLocalConst();
    return inc;
}

std::function<llvm::Type*(llvm::Module*)> ClangTU::GetLLVMTypeFromClangType(clang::QualType t, Semantic::Analyzer& a) {
    auto imp = impl.get();

    return [=, &a](llvm::Module* mod) -> llvm::Type* {
		// Below logic copy pastad from CodeGenModule::addRecordTypeName
		auto RD = t.getCanonicalType()->getAsCXXRecordDecl();
		std::string TypeName;
		llvm::raw_string_ostream OS(TypeName);
        OS << RD->getKindName() << '.';
        if (RD->getIdentifier()) {
          if (RD->getDeclContext())
            RD->printQualifiedName(OS);
          else
            RD->printName(OS);
        } else if (const clang::TypedefNameDecl *TDD = RD->getTypedefNameForAnonDecl()) {
          if (TDD->getDeclContext())
            TDD->printQualifiedName(OS);
          else
            TDD->printName(OS);
        } else
          OS << "anon";
        
		OS.flush();
		auto ty = mod->getTypeByName(TypeName);      
        if (ty) {
            if (t->getAsCXXRecordDecl()->field_empty())
                a.gen->AddEliminateType(ty);
            return ty;
        }
		
        std::string s;
        llvm::raw_string_ostream stream(s);
        mod->print(stream, nullptr);
		assert(false && "Attempted to look up a Clang type, but it did not exist in the module. You need to find out where this type came from- is it some unconverted primitive type?");
        return nullptr; // Shut up control path warning
    };
}

bool ClangTU::IsComplexType(clang::CXXRecordDecl* decl) {
	auto indirect = impl->codegenmod.getCXXABI().isReturnTypeIndirect(decl);
	auto arg = impl->codegenmod.getCXXABI().getRecordArgABI(decl);
	auto t = impl->astcon.getTypeDeclType(decl);
	if (!indirect && arg != clang::CodeGen::CGCXXABI::RecordArgABI::RAA_Default)
		assert(false);
	if (indirect && arg != clang::CodeGen::CGCXXABI::RecordArgABI::RAA_Indirect)
		assert(false);
	return indirect;
}

std::string ClangTU::MangleName(clang::NamedDecl* D) {    
    auto MarkFunction = [&](clang::FunctionDecl* d) {
        struct GeneratingVisitor : public clang::RecursiveASTVisitor<GeneratingVisitor> {
            clang::ASTContext* astcon;
            clang::Sema* sema;
            std::unordered_set<clang::FunctionDecl*>* visited;

            bool VisitFunctionDecl(clang::FunctionDecl* d) {
                if (!d) return true;
                if (visited->find(d) != visited->end())
                    return true;
                visited->insert(d);
                if (d->isTemplateInstantiation()) {
                    if (d->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration)
                        d->setTemplateSpecializationKind(clang::TSK_ExplicitInstantiationDefinition);
                    sema->InstantiateFunctionDefinition(clang::SourceLocation(), d, true, true);
                }
                d->setInlineSpecified(false);
                d->setUsed(true);
                d->setReferenced(true);
                if (d->hasAttrs()) {
                    d->addAttr(new (*astcon) clang::UsedAttr(clang::SourceLocation(), *astcon));
                } else {
                    clang::AttrVec v;
                    v.push_back(new (*astcon) clang::UsedAttr(clang::SourceLocation(), *astcon));
                    d->setAttrs(v);
                }                
                if (d->hasBody())
                    TraverseStmt(d->getBody());
                if (auto con = llvm::dyn_cast<clang::CXXConstructorDecl>(d)) {
                    for(auto begin = con->decls_begin(); begin != con->decls_end(); ++begin) {
                        TraverseDecl(*begin);
                    }
                    std::unordered_set<clang::FieldDecl*> fields;
                    for(auto f = con->getParent()->field_begin(); f != con->getParent()->field_end(); ++f)
                        fields.insert(*f);
                    std::unordered_set<const clang::Type*> bases;
                    for(auto f = con->getParent()->bases_begin(); f != con->getParent()->bases_end(); ++f)
                        bases.insert(f->getType().getTypePtr());
                    for(auto begin = con->init_begin(); begin != con->init_end(); ++begin) {
                        TraverseStmt((*begin)->getInit());
                        fields.erase((*begin)->getMember());
                        bases.erase((*begin)->getBaseClass());
                    }
                    for(auto&& def : fields) {
                        if (auto rec = def->getType()->getAsCXXRecordDecl())
                            VisitFunctionDecl(sema->LookupDefaultConstructor(rec));
                    }
                    for(auto&& def : bases) {
                        if (auto rec = def->getAsCXXRecordDecl())
                            VisitFunctionDecl(sema->LookupDefaultConstructor(rec));
                    }
                }
                return true;
            }
            bool VisitCallExpr(clang::CallExpr* s) {
                if (s->getDirectCallee()) {
                    VisitFunctionDecl(s->getDirectCallee());
                }
                return true;
            }
        };
        
        GeneratingVisitor v;
        v.astcon = &impl->astcon;
        v.sema = &impl->sema;
        v.visited = &visited;
        v.VisitFunctionDecl(d);
    };
    
    std::function<void(clang::CXXRecordDecl*)> RecursiveMarkTypes;
    RecursiveMarkTypes = [&](clang::CXXRecordDecl* dec) {
        if (!dec) return;
        if (auto spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(dec)) {
            if (!spec->getDefinition()) {
                auto tsk = clang::TemplateSpecializationKind::TSK_ExplicitInstantiationDefinition;
                if (GetSema().InstantiateClassTemplateSpecialization(GetFileEnd(), spec, tsk))
                    throw std::runtime_error("Could not instantiate resulting class template specialization.");
                
                GetSema().InstantiateClassTemplateSpecializationMembers(GetFileEnd(), llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(spec->getDefinition()), tsk);
            }
        }
        MarkFunction(dec->getDestructor());
        for(auto ctor = dec->ctor_begin(); ctor != dec->ctor_end(); ++ctor)
            MarkFunction(*ctor);
        for(auto mem = dec->method_begin(); mem != dec->method_end(); ++mem)
            MarkFunction(*mem);
        if (!dec->hasDefinition()) return;
        for(auto base = dec->bases_begin(); base != dec->bases_end(); ++base)
            RecursiveMarkTypes(base->getType()->getAsCXXRecordDecl());
        for(auto field = dec->field_begin(); field != dec->field_end(); ++field)
            RecursiveMarkTypes(field->getType()->getAsCXXRecordDecl());
        for(auto decl = dec->decls_begin(); decl != dec->decls_end(); ++decl) {
            if (auto fun = llvm::dyn_cast<clang::FunctionDecl>(*decl))
                MarkFunction(fun);
            if (auto record = llvm::dyn_cast<clang::CXXRecordDecl>(*decl))
                RecursiveMarkTypes(record);
            if (auto type = llvm::dyn_cast<clang::TypedefDecl>(*decl))
                RecursiveMarkTypes(impl->astcon.getTypeDeclType(type)->getAsCXXRecordDecl());
        }
    };

    if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(D)) {
        auto name = impl->codegenmod.getMangledName(vardecl);
        if (vardecl->hasAttrs()) {
            vardecl->addAttr(new (impl->astcon) clang::UsedAttr(clang::SourceLocation(), impl->astcon));
        } else {
            clang::AttrVec v;
            v.push_back(new (impl->astcon) clang::UsedAttr(clang::SourceLocation(), impl->astcon));
            vardecl->setAttrs(v);
        }
        impl->codegenmod.GetAddrOfGlobal(vardecl);
        return name;
    }
    if (auto desdecl = llvm::dyn_cast<clang::CXXDestructorDecl>(D)) {
        MarkFunction(desdecl);
        //RecursiveMarkTypes(desdecl->getParent());
        auto gd = clang::GlobalDecl(desdecl, clang::CXXDtorType::Dtor_Complete); 
        auto name = impl->codegenmod.getMangledName(gd); 
        return name;
    }
    if (auto condecl = llvm::dyn_cast<clang::CXXConstructorDecl>(D)) {
        MarkFunction(condecl);
        //RecursiveMarkTypes(condecl->getParent());
        auto gd = clang::GlobalDecl(condecl, clang::CXXCtorType::Ctor_Complete);
        auto name = impl->codegenmod.getMangledName(gd);   
        return name;
    }
    if (auto funcdecl = llvm::dyn_cast<clang::FunctionDecl>(D)) {
        MarkFunction(funcdecl);
        auto name = impl->codegenmod.getMangledName(funcdecl);
        return name;
    }
    throw std::runtime_error("Attempted to mangle a name that could not be mangled.");
}

clang::ASTContext& ClangTU::GetASTContext() {
    return impl->astcon;
}

clang::Sema& ClangTU::GetSema() {
    return impl->sema;
}

clang::IdentifierInfo* ClangTU::GetIdentifierInfo(std::string name) {
    return impl->preproc.getIdentifierInfo(name);
}

unsigned ClangTU::GetFieldNumber(clang::FieldDecl* f) {
    return impl->codegenmod.getTypes().getCGRecordLayout(f->getParent()).getLLVMFieldNo(f);
}

clang::SourceLocation ClangTU::GetFileEnd() {
    return impl->sm.getLocForEndOfFile(impl->sm.getMainFileID());
}