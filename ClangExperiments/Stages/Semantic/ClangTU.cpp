#include "ClangTU.h"
#include "Util.h"
#include "../../Util/MakeUnique.h"

#include <functional>
#include <string>
#include <fstream>
#include <unordered_map>

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
        , codegenmod(astcon, ccs.Options->CodegenOptions, ccs.Options->TargetOptions, mod, ccs.layout, ccs.engine)
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

    void GenerateCodeAndLinkModule(llvm::Module* main) {
        for(auto x : stuff)
            codegenmod.EmitTopLevelDecl(x);
        codegenmod.Release();

        llvm::Linker link("", main);
        link.LinkInModule(&mod);
        link.releaseModule();
        HandleErrors();
    }

    // To be added: The relevant query APIs.
};

ClangTU::~ClangTU() {}

ClangTU::ClangTU(llvm::LLVMContext& c, std::string file, Semantic::ClangCommonState& ccs)
    : impl(Wide::Util::make_unique<Impl>(c, file, ccs)) 
{
}

ClangTU::ClangTU(ClangTU&& other)
    : impl(std::move(other.impl)) {}

void ClangTU::GenerateCodeAndLinkModule(llvm::Module* main) {
    return impl->GenerateCodeAndLinkModule(main);
}

clang::DeclContext* ClangTU::GetDeclContext() {
    return impl->GetDeclContext();
}

clang::QualType Simplify(clang::QualType inc) {
    inc = inc.getCanonicalType();
    inc.removeLocalConst();
    return inc;
}

std::function<llvm::Type*(llvm::Module*)> ClangTU::GetLLVMTypeFromClangType(clang::QualType t) {
    auto imp = impl.get();
    return [=](llvm::Module* mod) {
        return imp->DeferredTypeMap[Simplify(t)](mod);
    };
}

std::string ClangTU::MangleName(clang::NamedDecl* D) {
    
    auto MarkFunction = [&](clang::FunctionDecl* d) {
        struct GeneratingVisitor : public clang::RecursiveASTVisitor<GeneratingVisitor> {
            clang::ASTContext* astcon;
            clang::Sema* sema;

            bool VisitFunctionDecl(clang::FunctionDecl* d) {
                if (!d) return true;
                d->setInlineSpecified(false);
                if (d->hasAttrs()) {
                    d->addAttr(new (*astcon) clang::UsedAttr(clang::SourceLocation(), *astcon));
                } else {
                    clang::AttrVec v;
                    v.push_back(new (*astcon) clang::UsedAttr(clang::SourceLocation(), *astcon));
                    d->setAttrs(v);
                }                
                if (d->isTemplateInstantiation())
                    sema->InstantiateFunctionDefinition(clang::SourceLocation(), d, true, true);
                if (d->hasBody())
                    TraverseStmt(d->getBody());
                if (auto con = llvm::dyn_cast<clang::CXXConstructorDecl>(d)) {
                    for(auto begin = con->init_begin(); begin != con->init_end(); ++begin) {
                        TraverseStmt((*begin)->getInit());
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
        v.VisitFunctionDecl(d);
    };
    
    std::function<void(clang::CXXRecordDecl*)> RecursiveMarkTypes;
    RecursiveMarkTypes = [&](clang::CXXRecordDecl* dec) {
        if (!dec) return;
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
        if (impl->DeferredTypeMap.find(Simplify(vardecl->getType())) == impl->DeferredTypeMap.end()) {
            impl->DeferredTypeMap[Simplify(vardecl->getType())] = [=](llvm::Module* mod) {
                return static_cast<llvm::PointerType*>(mod->getGlobalVariable(name)->getType())->getElementType();
            };
        }
        impl->codegenmod.GetAddrOfGlobal(vardecl);
        return name;
    }
    if (auto desdecl = llvm::dyn_cast<clang::CXXDestructorDecl>(D)) {
        RecursiveMarkTypes(desdecl->getParent());

        auto gd = clang::GlobalDecl(desdecl, clang::CXXDtorType::Dtor_Complete);   
        impl->codegenmod.GetAddrOfGlobal(gd);
        auto name = impl->codegenmod.getMangledName(gd);   
        auto t = impl->astcon.getRecordType(desdecl->getParent());
        if (impl->DeferredTypeMap.find(Simplify(t)) == impl->DeferredTypeMap.end()) {
            impl->DeferredTypeMap[Simplify(t)] = [=](llvm::Module* mod) {
                // The first param of the constructor is a pointer to this, so find it and get the element type.
                return static_cast<llvm::PointerType*>(mod->getFunction(name)->getFunctionType()->getParamType(0))->getElementType();
            };
        }
        return name;
    }
    if (auto condecl = llvm::dyn_cast<clang::CXXConstructorDecl>(D)) {

        RecursiveMarkTypes(condecl->getParent());

        auto gd = clang::GlobalDecl(condecl, clang::CXXCtorType::Ctor_Complete);   
        impl->codegenmod.GetAddrOfGlobal(gd);
        auto name = impl->codegenmod.getMangledName(gd);   
        auto t = impl->astcon.getRecordType(condecl->getParent());
        if (impl->DeferredTypeMap.find(Simplify(t)) == impl->DeferredTypeMap.end()) {
            impl->DeferredTypeMap[Simplify(t)] = [=](llvm::Module* mod) {
                // The first param of the constructor is a pointer to this, so find it and get the element type.
                return static_cast<llvm::PointerType*>(mod->getFunction(name)->getFunctionType()->getParamType(0))->getElementType();
            };
        }
        return name;
    }
    if (auto funcdecl = llvm::dyn_cast<clang::FunctionDecl>(D)) {
        funcdecl->setInlineSpecified(false);

        if (funcdecl->hasAttrs()) {
            funcdecl->addAttr(new (impl->astcon) clang::UsedAttr(clang::SourceLocation(), impl->astcon));
        } else {
            clang::AttrVec v;
            v.push_back(new (impl->astcon) clang::UsedAttr(clang::SourceLocation(), impl->astcon));
            funcdecl->setAttrs(v);
        }

        if (funcdecl->isTemplateInstantiation())
            impl->sema.InstantiateFunctionDefinition(clang::SourceLocation(), funcdecl, true, true);
        auto fun = impl->codegenmod.GetAddrOfGlobal(funcdecl);
        auto name = impl->codegenmod.getMangledName(funcdecl);

        // These counts mismatch for two reasons: this, and return type. Take care of "this" first.
        // Then check the return type.
        // Then do all the normal parameters.
        auto count = static_cast<llvm::Function*>(fun)->getFunctionType()->getNumParams();
        auto clangcount = funcdecl->getNumParams();
        bool is_method = false;
        if (auto methdecl = llvm::dyn_cast<clang::CXXMethodDecl>(D)) {
            // If the LLVM function takes two more arguments, then complex return *and* this. Return goes first, so look at the second for this.
            // Else, find it as the first parameter.
            auto qt = impl->astcon.getRecordType(methdecl->getParent());
            if (impl->DeferredTypeMap.find(Simplify(qt)) == impl->DeferredTypeMap.end()) {
                impl->DeferredTypeMap[Simplify(qt)] = [=](llvm::Module* mod) -> llvm::Type* {
                    return static_cast<llvm::PointerType*>(mod->getFunction(name)->getFunctionType()->getParamType((count - clangcount) - 1))->getElementType();
                };
            }
            is_method = true;
        }
        // If we were a method, then this is taken care of, but if the count still disagrees by two, then find a ReturnType* as the first parameter.
        // Else, find ReturnType as the return type.
        if (is_method && (count - clangcount == 2) || !is_method && (count - clangcount == 1)) {
            if (impl->DeferredTypeMap.find(Simplify(funcdecl->getResultType())) == impl->DeferredTypeMap.end()) {
                impl->DeferredTypeMap[Simplify(funcdecl->getResultType())] = [=](llvm::Module* mod) -> llvm::Type* {
                    return static_cast<llvm::PointerType*>(mod->getFunction(name)->getFunctionType()->getParamType(0))->getElementType();
                };
            }
        } else {
            // ReturnType is the real return type.
            if (impl->DeferredTypeMap.find(Simplify(funcdecl->getResultType())) == impl->DeferredTypeMap.end()) {
                impl->DeferredTypeMap[Simplify(funcdecl->getResultType())] = [=](llvm::Module* mod) -> llvm::Type* {
                    return mod->getFunction(name)->getFunctionType()->getReturnType();
                };
            }
        }
        // Now proceed through the others. Start at count - clangcount and keep going till count.
        for(std::size_t i = (count - clangcount) - 1; i < clangcount; ++i) {
            // If it is a complex type by value, we will be passing a pointer to it.
            if (impl->DeferredTypeMap.find(Simplify(funcdecl->getParamDecl(i)->getType())) == impl->DeferredTypeMap.end()) {
                auto t = funcdecl->getParamDecl(i)->getType();
                if (auto decl = t->getAsCXXRecordDecl()) {
                    if(!(decl->hasTrivialCopyConstructor() && decl->hasTrivialMoveConstructor() && decl->hasTrivialDestructor())) {
                        impl->DeferredTypeMap[Simplify(funcdecl->getParamDecl(i)->getType())] = [=](llvm::Module* mod) -> llvm::Type* {
                            return static_cast<llvm::PointerType*>(mod->getFunction(name)->getFunctionType()->getParamType(i))->getElementType();
                        };
                    }
                } else {
                    impl->DeferredTypeMap[Simplify(funcdecl->getParamDecl(i)->getType())] = [=](llvm::Module* mod) -> llvm::Type* {
                        return mod->getFunction(name)->getFunctionType()->getParamType(i);
                    };
                }
            }
        }
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

llvm::Constant* ClangTU::GetAddrOfGlobal(clang::GlobalDecl GD) {
    return impl->codegenmod.GetAddrOfGlobal(GD);
}

unsigned ClangTU::GetFieldNumber(clang::FieldDecl* f) {
    return impl->codegenmod.getTypes().getCGRecordLayout(f->getParent()).getLLVMFieldNo(f);
}

clang::SourceLocation ClangTU::GetFileEnd() {
    return impl->sm.getLocForEndOfFile(impl->sm.getMainFileID());
}