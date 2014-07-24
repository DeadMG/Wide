#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangType.h>
#include <functional>
#include <string>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>

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
#include <clang/Parse/Parser.h>
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
    std::set<std::pair<clang::CXXDestructorDecl*, clang::CXXDtorType>> destructors;
    std::set<std::pair<clang::CXXConstructorDecl*, clang::CXXCtorType>> constructors;
    std::set<clang::VarDecl*> globals;
    std::set<clang::FunctionDecl*> functions;

    std::unordered_set<const clang::FileEntry*> files;
    const Options::Clang* Options;
    clang::FileManager FileManager;
    ClangTUDiagnosticConsumer DiagnosticConsumer;
    clang::DiagnosticsEngine engine;
    clang::SourceManager sm;
    std::unique_ptr<clang::TargetInfo> targetinfo;
    clang::CompilerInstance ci;
    clang::HeaderSearch hs;
    std::unordered_set<clang::FieldDecl*> FieldNumbers;
    std::unordered_map<const clang::CXXRecordDecl*, std::unordered_set<const clang::CXXRecordDecl*>> BaseNumbers;

    clang::LangOptions langopts;
    std::vector<clang::Decl*> stuff;
    clang::Preprocessor preproc;
    clang::ASTContext astcon;
    CodeGenConsumer consumer;
    clang::Sema sema;
    std::string filename;
    clang::Parser p;
private:
    std::unique_ptr<clang::CodeGen::CodeGenModule> codegenmod;
public:
    void CreateCodegen(llvm::Module& mod, llvm::DataLayout& layout) {
        codegenmod = Wide::Memory::MakeUnique<clang::CodeGen::CodeGenModule>(astcon, Options->CodegenOptions, mod, layout, engine);
    }
    clang::CodeGen::CodeGenModule& GetCodegenModule(llvm::Module* module) {
        return *codegenmod;
    }
    // Clang has a really annoying habit of changing it's mind about this
    // producing multiple distinct llvm::Type*s for one QualType, so perform
    // our own caching on top.    
    
    Impl(std::string file, const Wide::Options::Clang& opts, Lexer::Range where)        
        : Options(&opts)
        , FileManager(opts.FileSearchOptions)
        , engine(opts.DiagnosticIDs, opts.DiagnosticOptions.getPtr(), &DiagnosticConsumer, false)
        , targetinfo(CreateTargetInfoFromTriple(engine, opts.TargetOptions.Triple))
        , sm(engine, FileManager)
        , hs(opts.HeaderSearchOptions, sm, engine, opts.LanguageOptions, targetinfo.get())
        , langopts(Options->LanguageOptions)
        , preproc(Options->PreprocessorOptions, engine, langopts, targetinfo.get(), sm, hs, ci)
        , astcon(langopts, sm, targetinfo.get(), preproc.getIdentifierTable(), preproc.getSelectorTable(), preproc.getBuiltinInfo(), 1000)
        , consumer(stuff)
        , sema(preproc, astcon, consumer, clang::TranslationUnitKind::TU_Complete) 
        , filename(std::move(file))   
        , p(preproc, sema, false)
    {
        preproc.enableIncrementalProcessing(true);
        std::string err;
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
        files.insert(entry);
        sm.setMainFileID(fileid);
        engine.getClient()->BeginSourceFile(Options->LanguageOptions, &preproc);
        preproc.EnterMainSourceFile();
        p.Initialize();
        auto Consumer = &sema.getASTConsumer();
        auto External = sema.getASTContext().getExternalSource();
        if (External)
            External->StartTranslationUnit(Consumer);
        clang::Parser::DeclGroupPtrTy ADecl;
        do {
            if (ADecl && !Consumer->HandleTopLevelDecl(ADecl.get()))
                return;
        } while (!p.ParseTopLevelDecl(ADecl));
        for (auto I = sema.WeakTopLevelDecls().begin(),
            E = sema.WeakTopLevelDecls().end(); I != E; ++I)
            Consumer->HandleTopLevelDecl(clang::DeclGroupRef(*I));

        Consumer->HandleTranslationUnit(sema.getASTContext());

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

    void AddFile(std::string filename, Lexer::Range where) {
        std::vector<std::string> paths;
        for (auto it = hs.search_dir_begin(); it != hs.search_dir_end(); ++it)
            paths.push_back(it->getDir()->getName());
        const clang::DirectoryLookup* directlookup = nullptr;
        auto entry = hs.LookupFile(filename, true, nullptr, directlookup, nullptr, nullptr, nullptr, nullptr);
        if (!entry)
            entry = FileManager.getFile(filename);
        if (!entry)
            throw CantFindHeader(filename, paths, where);

        auto fileid = sm.createFileID(entry, sm.getLocForEndOfFile(sm.getMainFileID()), clang::SrcMgr::CharacteristicKind::C_User);
        if (fileid.isInvalid())
            throw CannotTranslateFile(filename, where);
        if (files.find(entry) != files.end())
            return;
        files.insert(entry);
        // Partially a re-working of clang::ParseAST's implementation

        preproc.EnterSourceFile(fileid, preproc.GetCurDirLookup(), sm.getLocForEndOfFile(sm.getMainFileID()));
       
        auto Consumer = &sema.getASTConsumer();
        auto External = sema.getASTContext().getExternalSource();
        if (External)
            External->StartTranslationUnit(Consumer);
        clang::Parser::DeclGroupPtrTy ADecl;
        do {
            if (ADecl && !Consumer->HandleTopLevelDecl(ADecl.get()))
                return;
        } while (!p.ParseTopLevelDecl(ADecl));
        for (auto I = sema.WeakTopLevelDecls().begin(),
            E = sema.WeakTopLevelDecls().end(); I != E; ++I)
            Consumer->HandleTopLevelDecl(clang::DeclGroupRef(*I));

        Consumer->HandleTranslationUnit(sema.getASTContext());
        std::string errors;
        for (auto diag : DiagnosticConsumer.diagnostics)
            errors += diag;
        if (engine.hasFatalErrorOccurred())
            throw ClangFileParseError(filename, errors, where);
        if (!errors.empty()) Options->OnDiagnostic(errors);
        errors.clear();
    }
};

ClangTU::~ClangTU() {}

std::string ClangTU::PopLastDiagnostic() {
    auto lastdiag = std::move(impl->DiagnosticConsumer.diagnostics.back());
    impl->DiagnosticConsumer.diagnostics.pop_back();
    return lastdiag;
}

ClangTU::ClangTU(std::string file, const Wide::Options::Clang& ccs, Lexer::Range where, Analyzer& an)
: impl(Wide::Memory::MakeUnique<Impl>(file, ccs, where)), a(an)
{
}
void ClangTU::AddFile(std::string filename, Lexer::Range where) {
    return impl->AddFile(filename, where);
}

ClangTU::ClangTU(ClangTU&& other)
    : impl(std::move(other.impl)), a(other.a), visited(std::move(other.visited)) {}

void ClangTU::GenerateCodeAndLinkModule(llvm::Module* module, llvm::DataLayout& layout) {
    impl->sema.ActOnEndOfTranslationUnit();
    impl->CreateCodegen(*module, layout);
    // Cause all the side effects. Fuck you, Clang.
    for (auto x : impl->destructors)
        impl->GetCodegenModule(module).GetAddrOfCXXDestructor(x.first, x.second);
    for (auto x : impl->constructors)
        impl->GetCodegenModule(module).GetAddrOfCXXConstructor(x.first, x.second);
    for (auto x : impl->functions)
        impl->GetCodegenModule(module).GetAddrOfFunction(x);
    for (auto x : impl->globals)
        impl->GetCodegenModule(module).GetAddrOfGlobal(x);
    for (auto field : impl->FieldNumbers)
        impl->GetCodegenModule(module).getTypes().getCGRecordLayout(field->getParent()).getLLVMFieldNo(field);
    for (auto&& pair : impl->BaseNumbers) {
        if (pair.first->isEmpty()) {
            impl->GetCodegenModule(module).getTypes().getCGRecordLayout(pair.first);
            continue;
        }
        for (auto base : pair.second)
            impl->GetCodegenModule(module).getTypes().getCGRecordLayout(pair.first).getNonVirtualBaseLLVMFieldNo(base);
    }
    for (auto x : impl->stuff)
        impl->GetCodegenModule(module).EmitTopLevelDecl(x);
    impl->GetCodegenModule(module).Release();
}

clang::DeclContext* ClangTU::GetDeclContext() {
    return impl->GetDeclContext();
}

llvm::Type* ClangTU::GetLLVMTypeFromClangType(clang::QualType t, llvm::Module* module) {
    // The Clang functions that should expose this functionality do not in fact work at all, so hack it horribly.
    // If we were converted from some Wide type, go to them instead of going through LLVM named type.
    if (!dynamic_cast<Semantic::ClangType*>(a.GetClangType(*this, t))) {
        return a.GetClangType(*this, t)->GetLLVMType(module);
    }
    
    auto RD = t.getCanonicalType()->getAsCXXRecordDecl();
    
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
    auto ty = module->getTypeByName(TypeName);
    if (ty) return ty;
        
    std::string s;
    llvm::raw_string_ostream stream(s);
    module->print(stream, nullptr);
    assert(false && "Attempted to look up a Clang type, but it did not exist in the module. You need to find out where this type came from- is it some unconverted primitive type?");
    return nullptr; // Shut up control path warning
}

bool ClangTU::IsComplexType(clang::CXXRecordDecl* decl, llvm::Module* module) {
    if (!decl) return false;
    auto indirect = impl->GetCodegenModule(module).getCXXABI().isReturnTypeIndirect(decl);
    auto arg = impl->GetCodegenModule(module).getCXXABI().getRecordArgABI(decl);
    auto t = impl->astcon.getTypeDeclType(decl);
    if (!indirect && arg != clang::CodeGen::CGCXXABI::RecordArgABI::RAA_Default)
        assert(false);
    if (indirect && arg != clang::CodeGen::CGCXXABI::RecordArgABI::RAA_Indirect)
        assert(false);
    return indirect;
}

void ClangTU::MarkDecl(clang::NamedDecl* D) {
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
}
std::function<llvm::Function*(llvm::Module*)> ClangTU::GetObject(clang::CXXDestructorDecl* D, clang::CXXDtorType d) {
    MarkDecl(D);
    impl->destructors.insert(std::make_pair(D, d));
    return [this, D, d](llvm::Module* module) {
        auto val = impl->GetCodegenModule(module).GetAddrOfCXXDestructor(D, d);
        assert(val);
        auto func = llvm::dyn_cast<llvm::Function>(val);
        assert(func);
        return func;
    };
}
std::function<llvm::Function*(llvm::Module*)> ClangTU::GetObject(clang::CXXConstructorDecl* D, clang::CXXCtorType d) {
    MarkDecl(D);
    impl->constructors.insert(std::make_pair(D, d));
    return [this, D, d](llvm::Module* module) {
        auto val = impl->GetCodegenModule(module).GetAddrOfCXXConstructor(D, d);
        assert(val);
        auto func = llvm::dyn_cast<llvm::Function>(val);
        assert(func);
        return func;
    };
}
std::function<llvm::GlobalVariable*(llvm::Module*)> ClangTU::GetObject(clang::VarDecl* D) {
    MarkDecl(D);
    impl->globals.insert(D);
    return [this, D](llvm::Module* module) {
        auto val = impl->GetCodegenModule(module).GetAddrOfGlobalVar(D);
        assert(val);
        auto func = llvm::dyn_cast<llvm::GlobalVariable>(val);
        assert(func);
        return func;
    };
}
std::function<llvm::Function*(llvm::Module*)> ClangTU::GetObject(clang::FunctionDecl *D) {
    assert(!llvm::dyn_cast<clang::CXXConstructorDecl>(D));
    assert(!llvm::dyn_cast<clang::CXXDestructorDecl>(D));

    MarkDecl(D);
    impl->functions.insert(D);
    return [this, D](llvm::Module* module) -> llvm::Function* {
        auto val = impl->GetCodegenModule(module).GetAddrOfFunction(D);
        assert(val);
        auto func = llvm::dyn_cast<llvm::Function>(val);
        if (func) return func;
        if (func = llvm::dyn_cast<llvm::Function>(val->getOperand(0)))
            return func;
        assert(false && "Failed to get LLVM function back out from Clang.");
        return nullptr;// Shut up compiler.
    };
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

std::function<unsigned(llvm::Module*)> ClangTU::GetFieldNumber(clang::FieldDecl* f) {
    impl->FieldNumbers.insert(f);
    return [this, f](llvm::Module* module) {
        return impl->GetCodegenModule(module).getTypes().getCGRecordLayout(f->getParent()).getLLVMFieldNo(f);
    };
}

std::function<unsigned(llvm::Module*)> ClangTU::GetBaseNumber(const clang::CXXRecordDecl* self, const clang::CXXRecordDecl* f) {
    impl->BaseNumbers[self].insert(f);
    return [this, self, f](llvm::Module* module) {
        return impl->GetCodegenModule(module).getTypes().getCGRecordLayout(self).getNonVirtualBaseLLVMFieldNo(f);
    };
}

clang::SourceLocation ClangTU::GetFileEnd() {
    return impl->sm.getLocForEndOfFile(impl->sm.getMainFileID());
}
std::string ClangTU::GetFilename() {
    return impl->filename;
}
clang::Expr* ClangTU::ParseMacro(std::string macro, Lexer::Range where) {
    auto&& pp = GetSema().getPreprocessor();
    auto info = pp.getMacroInfo(GetIdentifierInfo(macro));
    if (!info)
        throw std::runtime_error("No macro found with the name " + macro);

    auto&& s = impl->sema;
    auto&& p = impl->p;
    std::vector<clang::Token> tokens;
    for (std::size_t num = 0; num < info->getNumTokens(); ++num)
        tokens.push_back(info->getReplacementToken(num));
    tokens.emplace_back();
    clang::Token& eof = tokens.back();
    eof.setKind(clang::tok::TokenKind::eof);
    eof.setLocation(GetFileEnd());
    eof.setIdentifierInfo(nullptr);
    pp.EnterTokenStream(tokens.data(), tokens.size(), false, false);
    p.ConsumeToken();// Eat the eof we will have been left with.
    auto expr = p.ParseExpression();
    if (expr.isUsable() || p.getCurToken().getKind() != clang::tok::TokenKind::eof) {
        return expr.get();
    }
    // Skip forward until eof so that we can keep using this parser/preprocessor in future.
    p.SkipUntil(clang::tok::TokenKind::eof);
    auto begin = GetSema().getSourceManager().getCharacterData(info->getDefinitionLoc()) + macro.size() + 1;
    auto end = begin + info->getDefinitionLength(GetSema().getSourceManager());
    auto macrodata = std::string(begin, end);
    throw MacroNotValidExpression(macrodata, macro, where);
}
unsigned int ClangTU::GetVirtualFunctionOffset(clang::CXXMethodDecl* meth, llvm::Module* module) {
    if (auto des = llvm::dyn_cast<clang::CXXDestructorDecl>(meth)) {
        return impl->GetCodegenModule(module).getItaniumVTableContext().getMethodVTableIndex(clang::GlobalDecl(des, clang::CXXDtorType::Dtor_Complete));
    }
    return impl->GetCodegenModule(module).getItaniumVTableContext().getMethodVTableIndex(clang::GlobalDecl(meth));
}
llvm::Constant* ClangTU::GetItaniumRTTI(clang::QualType rec, llvm::Module* m) {
    return impl->GetCodegenModule(m).GetAddrOfRTTIDescriptor(rec);
}