#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Util/Codegen/InitializeLLVM.h>
#include <functional>
#include <string>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <CodeGen/CodeGenModule.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
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
#include <clang/Basic/AllDiagnostics.h>
#include <llvm/Support/raw_ostream.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;   

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

clang::TargetInfo* CreateTargetInfoFromTriple(clang::DiagnosticsEngine& engine, std::string triple) {
    clang::TargetOptions& target = *new clang::TargetOptions();
    target.Triple = triple;
    auto targetinfo = clang::TargetInfo::CreateTargetInfo(engine, &target);
    targetinfo->setCXXABI(clang::TargetCXXABI::GenericItanium);
    return targetinfo;
}

class ClangTUDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
    std::vector<std::string> diagnostics;
    ClangTUDiagnosticConsumer() {}

    void BeginSourceFile(const clang::LangOptions& langopts, const clang::Preprocessor* PP) override final {}
    void EndSourceFile() override final {}
    void finish() override final {}
    bool IncludeInDiagnosticCounts() const override final { return true; }
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& Info) {
        llvm::SmallVector<char, 5> diagnostic;
        Info.FormatDiagnostic(diagnostic);
        diagnostics.emplace_back(diagnostic.begin(), diagnostic.end());
        clang::DiagnosticConsumer::HandleDiagnostic(level, Info);
    }
};

class ClangTU::Impl {
public:    
    const Options::Clang* Options;
    clang::FileManager FileManager;
    ClangTUDiagnosticConsumer DiagnosticConsumer;
    clang::DiagnosticsEngine engine;
    clang::SourceManager sm;
    std::unique_ptr<clang::TargetInfo> targetinfo;
    clang::CompilerInstance ci;
    clang::HeaderSearch hs;
    llvm::DataLayout layout;

    clang::LangOptions langopts;
    std::vector<clang::Decl*> stuff;
    clang::Preprocessor preproc;
    clang::ASTContext astcon;
    CodeGenConsumer consumer;
    clang::Sema sema;
    std::string filename;
    std::unordered_map<clang::QualType, std::function<llvm::Type*(llvm::Module*)>, ClangTypeHasher> DeferredTypeMap;
    llvm::Module mod;
    clang::CodeGen::CodeGenModule codegenmod;
    
    // Clang has a really annoying habit of changing it's mind about this
    // producing multiple distinct llvm::Type*s for one QualType, so perform
    // our own caching on top.    
    
    Impl(llvm::LLVMContext& con, std::string file, const Wide::Options::Clang& opts, Lexer::Range where)        
        : Options(&opts)
        , FileManager(opts.FileSearchOptions)
        , engine(opts.DiagnosticIDs, opts.DiagnosticOptions.getPtr(), &DiagnosticConsumer, false)
        , targetinfo(CreateTargetInfoFromTriple(engine, opts.TargetOptions.Triple))
        , sm(engine, FileManager)
        , hs(opts.HeaderSearchOptions, sm, engine, opts.LanguageOptions, targetinfo.get())
        , layout(targetinfo->getTargetDescription())
        , langopts(Options->LanguageOptions)
        , preproc(Options->PreprocessorOptions, engine, langopts, targetinfo.get(), sm, hs, ci)
        , astcon(langopts, sm, targetinfo.get(), preproc.getIdentifierTable(), preproc.getSelectorTable(), preproc.getBuiltinInfo(), 1000)
        , consumer(stuff)
        , sema(preproc, astcon, consumer, clang::TranslationUnitKind::TU_Complete) 
        , mod("", con)
        , codegenmod(astcon, Options->CodegenOptions, mod, layout, engine)
        , filename(std::move(file))   
    {
        Codegen::InitializeLLVM();
        std::string err;
        const llvm::Target& llvmtarget = *llvm::TargetRegistry::lookupTarget(opts.TargetOptions.Triple, err);
        llvm::TargetOptions llvmtargetopts;
        auto targetmachine = std::unique_ptr<llvm::TargetMachine>(llvmtarget.createTargetMachine(opts.TargetOptions.Triple, llvm::Triple(opts.TargetOptions.Triple).getArchName(), "", llvmtargetopts));
        mod.setDataLayout(targetmachine->getDataLayout()->getStringRepresentation());  
        mod.setTargetTriple(Options->TargetOptions.Triple);
        clang::InitializePreprocessor(preproc, *Options->PreprocessorOptions, *Options->HeaderSearchOptions, Options->FrontendOptions);
        preproc.getBuiltinInfo().InitializeBuiltins(preproc.getIdentifierTable(), Options->LanguageOptions);
        std::vector<std::string> paths;
        for (auto it = hs.search_dir_begin(); it != hs.search_dir_end(); ++it)
            paths.push_back(it->getDir()->getName());
        const clang::DirectoryLookup* directlookup = nullptr;
        auto entry = hs.LookupFile(filename, true, nullptr, directlookup, nullptr, nullptr, nullptr, nullptr);
        if (!entry)
            entry = FileManager.getFile(filename);
        if (!entry)
            throw CantFindHeader(filename, paths, where);
        
        auto fileid = sm.createFileID(entry, clang::SourceLocation(), clang::SrcMgr::CharacteristicKind::C_User);
        if (fileid.isInvalid())
            throw CannotTranslateFile(filename, where);
        sm.setMainFileID(fileid);
        engine.getClient()->BeginSourceFile(Options->LanguageOptions, &preproc);
        ParseAST(sema);

        std::string errors;
        for (auto diag : DiagnosticConsumer.diagnostics)
            errors += diag;
        if (engine.hasFatalErrorOccurred())
            throw ClangFileParseError(filename, errors, where);
        if (!errors.empty()) Options->OnDiagnostic(errors);
        errors.clear();
    }

    clang::DeclContext* GetDeclContext() {
        return astcon.getTranslationUnitDecl();
    }
};

ClangTU::~ClangTU() {}

std::string ClangTU::PopLastDiagnostic() {
    auto lastdiag = std::move(impl->DiagnosticConsumer.diagnostics.back());
    impl->DiagnosticConsumer.diagnostics.pop_back();
    return lastdiag;
}

ClangTU::ClangTU(llvm::LLVMContext& c, std::string file, const Wide::Options::Clang& ccs, Lexer::Range where)
    : impl(Wide::Memory::MakeUnique<Impl>(c, file, ccs, where)) 
{
}

ClangTU::ClangTU(ClangTU&& other)
    : impl(std::move(other.impl)) {}

void ClangTU::GenerateCodeAndLinkModule(llvm::Module* main) {
    for(auto x : impl->stuff)
        impl->codegenmod.EmitTopLevelDecl(x);
    impl->codegenmod.Release();

    std::string mod;
    llvm::raw_string_ostream stream(mod);
    impl->mod.print(stream, nullptr);

    llvm::Linker link(main);
    std::string fuck;
    link.linkInModule(&impl->mod, &fuck);
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
    // The Clang functions that should expose this functionality do not in fact work at all, so hack it horribly.
    auto imp = impl.get();

    return [this, imp, t, &a](llvm::Module* mod) -> llvm::Type* {
        // If we were converted from some Wide type, go to them instead of going through LLVM named type.
        if (!dynamic_cast<Semantic::ClangType*>(a.GetClangType(*this, t))) {
            return a.GetClangType(*this, t)->GetLLVMType(a)(mod);
        }
        
        auto RD = t.getCanonicalType()->getAsCXXRecordDecl();
        if (prebaked_types.find(t) != prebaked_types.end())
            return prebaked_types[t](mod);

        // Below logic copy pastad from CodeGenModule::addRecordTypeName
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
    if (!decl) return false;
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
    if (auto funcdecl = llvm::dyn_cast<clang::CXXMethodDecl>(D)) {
        if (funcdecl->getType()->getAs<clang::FunctionProtoType>()->getExtProtoInfo().ExceptionSpecType == clang::ExceptionSpecificationType::EST_Unevaluated) {
            GetSema().EvaluateImplicitExceptionSpec(clang::SourceLocation(), funcdecl);
        }        
    }
    if (D->hasAttrs()) {
        D->addAttr(new (impl->astcon) clang::UsedAttr(clang::SourceLocation(), impl->astcon));
    } else {
        clang::AttrVec v;
        v.push_back(new (impl->astcon) clang::UsedAttr(clang::SourceLocation(), impl->astcon));
        D->setAttrs(v);
    }
    impl->sema.MarkAnyDeclReferenced(clang::SourceLocation(), D, true);

    if (auto desdecl = llvm::dyn_cast<clang::CXXDestructorDecl>(D)) {
        auto gd = clang::GlobalDecl(desdecl, clang::CXXDtorType::Dtor_Complete);
        impl->codegenmod.GetAddrOfGlobal(gd);
        return impl->codegenmod.getMangledName(gd);
    }
    if (auto condecl = llvm::dyn_cast<clang::CXXConstructorDecl>(D)) {
        auto gd = clang::GlobalDecl(condecl, clang::CXXCtorType::Ctor_Complete);
        impl->codegenmod.GetAddrOfGlobal(gd);
        return impl->codegenmod.getMangledName(gd);
    }
    if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(D)) {
        impl->codegenmod.GetAddrOfGlobal(vardecl);
        return impl->codegenmod.getMangledName(vardecl);
    }
    if (auto funcdecl = llvm::dyn_cast<clang::FunctionDecl>(D)) {
        //funcdecl->setInlineSpecified(false);
        //funcdecl->setLateTemplateParsed(false);
        impl->codegenmod.GetAddrOfGlobal(funcdecl);
        return impl->codegenmod.getMangledName(funcdecl);
    }
    assert(false);
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

unsigned ClangTU::GetBaseNumber(clang::CXXRecordDecl* self, clang::CXXRecordDecl* f) {
    return impl->codegenmod.getTypes().getCGRecordLayout(self).getNonVirtualBaseLLVMFieldNo(f);
}

clang::SourceLocation ClangTU::GetFileEnd() {
    return impl->sm.getLocForEndOfFile(impl->sm.getMainFileID());
}
std::string ClangTU::GetFilename() {
    return impl->filename;
}