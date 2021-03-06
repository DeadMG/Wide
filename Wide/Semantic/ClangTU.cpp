#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/FunctionType.h>
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
#include <llvm/Support/Path.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Basic/AllDiagnostics.h>
#include <llvm/Support/raw_ostream.h>
#include <CodeGen/TargetInfo.h>
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

clang::TargetInfo* CreateTargetInfoFromTriple(clang::DiagnosticsEngine& engine, clang::TargetOptions opts) {
    auto target = std::make_shared<clang::TargetOptions>(opts);
    auto targetinfo = clang::TargetInfo::CreateTargetInfo(engine, target);
    targetinfo->setCXXABI(clang::TargetCXXABI::GenericItanium);
    return targetinfo;
}

class ClangTUDiagnosticConsumer : public clang::DiagnosticConsumer {
private:
    Lexer::Position GetPosition(clang::SourceManager& sm, clang::SourceLocation l) {
        auto offset = sm.getFileOffset(l);
        auto startcol = sm.getColumnNumber(sm.getFileID(l), offset);
        auto startline = sm.getLineNumber(sm.getFileID(l), offset);
        auto filename = std::string(sm.getFilename(l));
        Lexer::Position p(std::make_shared<std::string>(filename));
        p.column = startcol;
        p.line = startline;
        p.offset = offset;
        return p;
    }
public:
    std::vector<ClangDiagnostic> diagnostics;
    ClangTUDiagnosticConsumer() {}

    void BeginSourceFile(const clang::LangOptions& langopts, const clang::Preprocessor* PP) override final {}
    void EndSourceFile() override final {}
    void finish() override final {}
    bool IncludeInDiagnosticCounts() const override final { return true; }
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& Info) {
        llvm::SmallVector<char, 5> diagnostic;
        Info.FormatDiagnostic(diagnostic);
        ClangDiagnostic diag;
        diag.severity = (ClangDiagnostic::Severity)level;
        diag.what = std::string(diagnostic.begin(), diagnostic.end());
        auto locations = Info.getRanges();
        for (auto&& loc : locations) {
            diag.where.push_back(Lexer::Range(GetPosition(Info.getSourceManager(), loc.getBegin()), GetPosition(Info.getSourceManager(), loc.getEnd())));
        }
        diagnostics.emplace_back(diag);
        clang::DiagnosticConsumer::HandleDiagnostic(level, Info);
    }
};

class ClangTU::Impl {
public:    
    std::set<std::pair<clang::CXXDestructorDecl*, clang::CodeGen::StructorType>> destructors;
    std::set<std::pair<clang::CXXConstructorDecl*, clang::CodeGen::StructorType>> constructors;
    std::set<clang::VarDecl*> globals;
    std::set<clang::FunctionDecl*> functions;

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
    clang::Parser p; 
    std::unordered_map<std::string, std::string> AbsoluteToRelativeFileMapping;
private:
    std::unique_ptr<clang::CodeGen::CodeGenModule> codegenmod;
public:
    void CreateCodegen(llvm::Module& mod, llvm::DataLayout& layout) {
        codegenmod = Wide::Memory::MakeUnique<clang::CodeGen::CodeGenModule>(astcon, Options->CodegenOptions, mod, layout, engine);
    }
    clang::CodeGen::CodeGenModule& GetCodegenModule(llvm::Module* module) {
        return *codegenmod;
    } 
    
    /*struct WideVFS : clang::vfs::FileSystem {
        const std::unordered_map<std::string, std::string>* imports;

        llvm::ErrorOr<clang::vfs::Status> status(const llvm::Twine& path) override final {
            if (imports->find(path.getSingleStringRef()) == imports->end()) {
                return clang::vfs::Status(path.getSingleStringRef(), path.getSingleStringRef(), llvm::sys::fs::UniqueID(), llvm::sys::TimeValue(), 0, 0, 0, llvm::sys::fs::file_type::regular_file, llvm::sys::fs::perms::all_read);
            }
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }
        std::error_code openFileForRead(const llvm::Twine& path, std::unique_ptr<clang::vfs::File>& result) override final {
            if (imports->find(path.getSingleStringRef()) == imports->end())
                return std::make_error_code(std::errc::no_such_file_or_directory);           
            struct WideFile : clang::vfs::File {

            };
        }
        /*std::error_code getBufferForFile(const llvm::Twine &path, std::unique_ptr<llvm::MemoryBuffer> &result, int64_t FileSize, bool RequiresNullTerminator, bool IsVolatile) override final {
            if (imports->find(path.getSingleStringRef()) == imports->end())
                return std::make_error_code(std::errc::no_such_file_or_directory);
            result = std::unique_ptr<llvm::MemoryBuffer>(llvm::MemoryBuffer::getMemBuffer(imports->at(path.getSingleStringRef())));
            return std::error_code();
        }
        clang::vfs::directory_iterator dir_begin(const llvm::Twine &Dir, std::error_code &EC) override final {

        }
    };*/

    struct WideASTSource : clang::ExternalASTSource, llvm::RefCountedBase<WideASTSource> {
        WideASTSource(Analyzer& a) : a(a) {}
        Analyzer& a;
        void CompleteType(clang::TagDecl* tag) override final {
            if (auto cxxrec = llvm::dyn_cast<clang::CXXRecordDecl>(tag))
                if (auto matchinfo = a.MaybeGetClangTypeInfo(cxxrec))
                    matchinfo->Complete();
        }
        bool layoutRecordType(
            const clang::RecordDecl* rec,
            uint64_t& size,
            uint64_t& alignment,
            llvm::DenseMap<const clang::FieldDecl*, uint64_t>& fields,
            llvm::DenseMap<const clang::CXXRecordDecl*, clang::CharUnits>& bases,
            llvm::DenseMap<const clang::CXXRecordDecl*, clang::CharUnits>& vbases
       ) override final {
            if (auto cxxrec = llvm::dyn_cast<clang::CXXRecordDecl>(rec)) {
                if (auto matchinfo = a.MaybeGetClangTypeInfo(cxxrec)) {
                    matchinfo->Layout(size, alignment, fields, bases, vbases);
                    return true;
                }                
            }
            return false;
        }
    };
    
    Impl(std::string file, const Wide::Options::Clang& opts, Lexer::Range where, Analyzer& a)        
        : Options(&opts)
        , FileManager(opts.FileSearchOptions)
        , engine(opts.DiagnosticIDs, opts.DiagnosticOptions.get(), &DiagnosticConsumer, false)
        , targetinfo(CreateTargetInfoFromTriple(engine, opts.TargetOptions))
        , sm(engine, FileManager)
        , hs(opts.HeaderSearchOptions, sm, engine, opts.LanguageOptions, targetinfo.get())
        , langopts(Options->LanguageOptions)
        , preproc(Options->PreprocessorOptions, engine, langopts, sm, hs, ci)
        , astcon(langopts, sm, preproc.getIdentifierTable(), preproc.getSelectorTable(), preproc.getBuiltinInfo())
        , consumer(stuff)
        , sema(preproc, astcon, consumer, clang::TranslationUnitKind::TU_Complete) 
        , p(preproc, sema, false)
    {
        // WHY THE FUCK AM I DOING THIS MYSELF CLANG
        // CAN'T YOU READ YOUR OWN CONSTRUCTOR PARAMETERS AND OPTIONS STRUCTS?
        std::vector<clang::DirectoryLookup> lookups;
        for (auto entry : opts.HeaderSearchOptions->UserEntries) {
            auto lookup = clang::DirectoryLookup(FileManager.getDirectory(entry.Path), clang::SrcMgr::CharacteristicKind::C_System, false);
            if (!lookup.getDir())
                throw SpecificError<ClangCouldNotInterpretPath>(a, where, "Clang could not interpret path " + entry.Path);
            lookups.push_back(lookup);
        }
        hs.SetSearchPaths(lookups, 0, 0, true);

        llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> wastsource(new WideASTSource(a));
        astcon.setExternalSource(wastsource);      
        astcon.InitBuiltinTypes(*targetinfo);
        preproc.enableIncrementalProcessing(true);
        preproc.Initialize(*targetinfo); // why would you do this
        clang::InitializePreprocessor(preproc, *Options->PreprocessorOptions, Options->FrontendOptions);
        preproc.getBuiltinInfo().InitializeBuiltins(preproc.getIdentifierTable(), Options->LanguageOptions);
        std::vector<std::string> paths;
        for (auto it = hs.search_dir_begin(); it != hs.search_dir_end(); ++it) {
            paths.push_back(it->getDir()->getName());
        }
        const clang::DirectoryLookup* directlookup = nullptr;
        auto entry = hs.LookupFile(file, clang::SourceLocation(), true, nullptr, directlookup, {}, nullptr, nullptr, nullptr);
        if (!entry)
            throw SpecificError<CouldNotFindCPPFile>(a, where, "Could not find file.");
        
        auto fileid = sm.createFileID(entry, clang::SourceLocation(), clang::SrcMgr::CharacteristicKind::C_User);
        if (fileid.isInvalid())
            throw SpecificError<CouldNotTranslateCPPFile>(a, where, "Could not translate file.");
        AbsoluteToRelativeFileMapping[entry->getName()] = file;


        sm.setMainFileID(fileid);
        engine.getClient()->BeginSourceFile(Options->LanguageOptions, &preproc);
        preproc.EnterMainSourceFile();
        p.Initialize();
        auto Consumer = &sema.getASTConsumer();
        clang::Parser::DeclGroupPtrTy ADecl;
        do {
            if (ADecl && !Consumer->HandleTopLevelDecl(ADecl.get()))
                return;
        } while (!p.ParseTopLevelDecl(ADecl));
        for (auto I = sema.WeakTopLevelDecls().begin(), E = sema.WeakTopLevelDecls().end(); I != E; ++I)
            Consumer->HandleTopLevelDecl(clang::DeclGroupRef(*I));

        Consumer->HandleTranslationUnit(sema.getASTContext());

        a.ClangDiagnostics.insert(a.ClangDiagnostics.end(), DiagnosticConsumer.diagnostics.begin(), DiagnosticConsumer.diagnostics.end());
        if (engine.hasFatalErrorOccurred())
            throw SpecificError<CPPFileContainedErrors>(a, where, "Clang reported errors in file.");
    }

    clang::DeclContext* GetDeclContext() {
        return astcon.getTranslationUnitDecl();
    }

    void AddFile(std::string filename, Lexer::Range where, Analyzer& a) {
        llvm::SmallVector<char, 10> smallvec(filename.begin(), filename.end());
        llvm::sys::fs::make_absolute(smallvec);
        auto abspath = std::string(smallvec.begin(), smallvec.end());
        if (AbsoluteToRelativeFileMapping.find(abspath) != AbsoluteToRelativeFileMapping.end())
            return;
        AbsoluteToRelativeFileMapping[abspath] = filename;
        std::vector<std::string> paths;
        for (auto it = hs.search_dir_begin(); it != hs.search_dir_end(); ++it)
            paths.push_back(it->getDir()->getName());
        const clang::DirectoryLookup* directlookup = nullptr;
        auto entry = hs.LookupFile(filename, clang::SourceLocation(), true, nullptr, directlookup, {}, nullptr, nullptr, nullptr);
        if (!entry)
            throw SpecificError<CouldNotFindCPPFile>(a, where, "Could not find C++ file " + filename);

        auto fileid = sm.createFileID(entry, sm.getLocForEndOfFile(sm.getMainFileID()), clang::SrcMgr::CharacteristicKind::C_User);
        if (fileid.isInvalid())
            throw SpecificError<CouldNotTranslateCPPFile>(a, where, "Could not translate C++ file " + filename);
        // Partially a re-working of clang::ParseAST's implementation

        preproc.EnterSourceFile(fileid, preproc.GetCurDirLookup(), sm.getLocForEndOfFile(sm.getMainFileID()));
       
        auto Consumer = &sema.getASTConsumer();
        clang::Parser::DeclGroupPtrTy ADecl;
        do {
            if (ADecl && !Consumer->HandleTopLevelDecl(ADecl.get()))
                return;
        } while (!p.ParseTopLevelDecl(ADecl));
        for (auto I = sema.WeakTopLevelDecls().begin(), E = sema.WeakTopLevelDecls().end(); I != E; ++I)
            Consumer->HandleTopLevelDecl(clang::DeclGroupRef(*I));

        Consumer->HandleTranslationUnit(sema.getASTContext());
        a.ClangDiagnostics.insert(a.ClangDiagnostics.end(), DiagnosticConsumer.diagnostics.begin(), DiagnosticConsumer.diagnostics.end());
        if (engine.hasFatalErrorOccurred())
            throw SpecificError<CPPFileContainedErrors>(a, where, "Clang reported errors in file.");
    }
};

ClangTU::~ClangTU() {}

ClangTU::ClangTU(std::string file, const Wide::Options::Clang& ccs, Lexer::Range where, Analyzer& an)
: impl(Wide::Memory::MakeUnique<Impl>(file, ccs, where, an)), a(an)
{
}
void ClangTU::AddFile(std::string filename, Lexer::Range where) {
    return impl->AddFile(filename, where, a);
}

ClangTU::ClangTU(ClangTU&& other)
    : impl(std::move(other.impl)), a(other.a), visited(std::move(other.visited)) {}

void ClangTU::GenerateCodeAndLinkModule(llvm::Module* module, llvm::DataLayout& layout, Analyzer& a) {
    impl->sema.ActOnEndOfTranslationUnit();
    impl->CreateCodegen(*module, layout);
    // Cause all the side effects. Fuck you, Clang.
    for (auto x : impl->destructors)
        impl->GetCodegenModule(module).getAddrOfCXXStructor(x.first, x.second);
    for (auto x : impl->constructors)
        impl->GetCodegenModule(module).getAddrOfCXXStructor(x.first, x.second);
    for (auto x : impl->functions)
        impl->GetCodegenModule(module).GetAddrOfFunction(x, GetFunctionType(x, *this, a)->GetLLVMType(module)->getElementType());
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
    auto arg = impl->GetCodegenModule(module).getCXXABI().getRecordArgABI(decl);
    return arg != clang::CodeGen::CGCXXABI::RecordArgABI::RAA_Default;
}

void ClangTU::MarkDecl(clang::NamedDecl* D) {
    if (auto funcdecl = llvm::dyn_cast<clang::CXXMethodDecl>(D)) {
        if (funcdecl->getType()->getAs<clang::FunctionProtoType>()->getExtProtoInfo().ExceptionSpec.Type == clang::ExceptionSpecificationType::EST_Unevaluated) {
            GetSema().EvaluateImplicitExceptionSpec(clang::SourceLocation(), funcdecl);
        }
    }/*
    if (D->hasAttrs()) {
        D->addAttr(new (impl->astcon) clang::UsedAttr(clang::SourceLocation(), impl->astcon));
    } else {
        clang::AttrVec v;
        v.push_back(new (impl->astcon) clang::UsedAttr(clang::SourceLocation(), impl->astcon));
        D->setAttrs(v);
    }*/
    impl->sema.MarkAnyDeclReferenced(clang::SourceLocation(), D, true);
}
std::function<llvm::Function*(llvm::Module*)> ClangTU::GetObject(Analyzer& a, clang::CXXDestructorDecl* D, clang::CodeGen::StructorType d) {
    MarkDecl(D);
    impl->destructors.insert(std::make_pair(D, d));
    return [this, D, d](llvm::Module* module) {
        auto val = impl->GetCodegenModule(module).getAddrOfCXXStructor(D, d);
        assert(val);
        auto func = llvm::dyn_cast<llvm::Function>(val);
        assert(func);
        return func;
    };
}
std::function<llvm::Function*(llvm::Module*)> ClangTU::GetObject(Analyzer& a, clang::CXXConstructorDecl* D, clang::CodeGen::StructorType d) {
    MarkDecl(D);
    impl->constructors.insert(std::make_pair(D, d));
    return [this, D, d](llvm::Module* module) {
        auto val = impl->GetCodegenModule(module).getAddrOfCXXStructor(D, d);
        assert(val);
        auto func = llvm::dyn_cast<llvm::Function>(val);
        assert(func);
        return func;
    };
}
std::function<llvm::GlobalVariable*(llvm::Module*)> ClangTU::GetObject(Analyzer& a, clang::VarDecl* D) {
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
std::function<llvm::Function*(llvm::Module*)> ClangTU::GetObject(Analyzer& a, clang::FunctionDecl *D) {
    assert(!llvm::dyn_cast<clang::CXXConstructorDecl>(D));
    assert(!llvm::dyn_cast<clang::CXXDestructorDecl>(D));

    MarkDecl(D);
    impl->functions.insert(D);
    return [this, D, &a](llvm::Module* module) -> llvm::Function* {
        auto val = impl->GetCodegenModule(module).GetAddrOfFunction(D, GetFunctionType(D, *this, a)->GetLLVMType(module)->getElementType());
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
std::unordered_map<std::string, std::string> ClangTU::GetFilename() {
    return impl->AbsoluteToRelativeFileMapping;
}
clang::SourceLocation ClangTU::GetLocationForRange(Lexer::Range r) {
    auto entry = impl->FileManager.getFile(*r.begin.name);
    return impl->sm.translateFileLineCol(entry, r.begin.line, r.begin.column);
}
clang::Expr* ClangTU::ParseMacro(std::string macro, Lexer::Range where) {
    auto&& pp = GetSema().getPreprocessor();
    auto info = pp.getMacroInfo(GetIdentifierInfo(macro));
    if (!info)
        throw SpecificError<CouldNotFindMacro>(a, where, "Could not find macro of name " + macro);
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
    throw SpecificError<MacroNotValidExpression>(a, where, "Macro was not a valid expression.");
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
llvm::PointerType* ClangTU::GetFunctionPointerType(const clang::CodeGen::CGFunctionInfo& info, llvm::Module* module) {
    return llvm::PointerType::get(impl->GetCodegenModule(module).getTypes().GetFunctionType(info), 0);
}
const clang::CodeGen::CGFunctionInfo& ClangTU::GetABIForFunction(const clang::FunctionProtoType* proto, clang::CXXRecordDecl* decl, llvm::Module* module) {
    if (decl) return impl->GetCodegenModule(module).getTypes().arrangeCXXMethodType(decl, proto);
    return impl->GetCodegenModule(module).getTypes().arrangeFreeFunctionType(GetASTContext().getCanonicalType(GetASTContext().getFunctionType(proto->getReturnType(), proto->getParamTypes(), proto->getExtProtoInfo())).getAs<clang::FunctionProtoType>());
}
clang::CodeGen::CodeGenModule& ClangTU::GetCodegenModule(llvm::Module* module) {
    return impl->GetCodegenModule(module);
}