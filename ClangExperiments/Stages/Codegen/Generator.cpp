#include "Generator.h"
#include "Expression.h"
#include "Function.h"

#include <fstream>

#pragma warning(push, 0)

#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Support/raw_os_ostream.h>

#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/PassManager.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Transforms/IPO.h>

#pragma warning(pop)

using namespace Wide;
using namespace Codegen;

void Generator::operator()() {        
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(clangopts.TargetOptions.Triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(clangopts.TargetOptions.Triple, llvm::Triple(clangopts.TargetOptions.Triple).getArchName(), "", targetopts));
    main.setDataLayout(targetmachine->getDataLayout()->getStringRepresentation());  

    EmitCode();
        
    llvm::PassManager pm;
    std::ofstream file(clangopts.FrontendOptions.OutputFile, std::ios::trunc | std::ios::binary);
    llvm::raw_os_ostream out(file);
    llvm::formatted_raw_ostream format_out(out);
    
    pm.add(new llvm::DataLayout(main.getDataLayout()));
    for(auto&& pass : llvmopts.Passes)        
        pm.add(pass->createPass(pass->getPassID()));

    targetmachine->addPassesToEmitFile(pm, format_out, llvm::TargetMachine::CodeGenFileType::CGFT_ObjectFile);
    pm.run(main);
}

Generator::Generator(const Options::LLVM& l, const Options::Clang& c)
    : main("", context)
    , llvmopts(l)
    , clangopts(c)
{
    main.setTargetTriple(c.TargetOptions.Triple);
}

Function* Generator::CreateFunction(std::function<llvm::Type*(llvm::Module*)> ty, std::string name, bool tramp) {
    auto p = arena.Allocate<Function>(std::move(ty), std::move(name), tramp);
    functions.push_back(p);
    return p;
}

Variable* Generator::CreateVariable(std::function<llvm::Type*(llvm::Module*)> t) {
    return arena.Allocate<Variable>(std::move(t));
}

FunctionCall* Generator::CreateFunctionCall(Expression* obj, std::vector<Expression*> args, std::function<llvm::Type*(llvm::Module*)> ty) {
    return arena.Allocate<FunctionCall>(obj, std::move(args), std::move(ty));
}

StringExpression* Generator::CreateStringExpression(std::string str) {
    return arena.Allocate<StringExpression>(std::move(str));
}
NamedGlobalVariable* Generator::CreateGlobalVariable(std::string name) {
    return arena.Allocate<NamedGlobalVariable>(std::move(name));
}

StoreExpression* Generator::CreateStore(Expression* obj, Expression* val) {
    return arena.Allocate<StoreExpression>(obj, val);
}
LoadExpression* Generator::CreateLoad(Expression* obj) {
    return arena.Allocate<LoadExpression>(obj);
}
ReturnStatement* Generator::CreateReturn() {
    return arena.Allocate<ReturnStatement>();
}
ReturnStatement* Generator::CreateReturn(Expression* obj) {
    return arena.Allocate<ReturnStatement>(obj);
}
FunctionValue* Generator::CreateFunctionValue(std::string name) {
    return arena.Allocate<FunctionValue>(name);
}

void Generator::EmitCode() {    
    std::string err;
    if (llvm::verifyModule(main, llvm::VerifierFailureAction::PrintMessageAction, &err))
        __debugbreak();
    std::string s;
    llvm::raw_string_ostream stream(s);
    main.print(stream, nullptr);
    for(auto&& x : functions) {
        x->EmitCode(&main, context, *this);
    }    
	s.clear();
    main.print(stream, nullptr);    
    if (llvm::verifyModule(main, llvm::VerifierFailureAction::PrintMessageAction, &err))
        __debugbreak();
}

Int8Expression* Generator::CreateInt8Expression(char val) {
    return arena.Allocate<Int8Expression>(val);
}

ChainExpression* Generator::CreateChainExpression(Statement* s, Expression* e) {
    return arena.Allocate<ChainExpression>(s, e);
}

FieldExpression* Generator::CreateFieldExpression(Expression* e, unsigned f) {
    return arena.Allocate<FieldExpression>(f, e);
}           

bool Generator::IsEliminateType(llvm::Type* t) {
    return eliminate_types.find(t) != eliminate_types.end();
}

void Generator::AddEliminateType(llvm::Type* t) {
    eliminate_types.insert(t);
}

ParamExpression* Generator::CreateParameterExpression(unsigned num) {
    return arena.Allocate<ParamExpression>([=]{ return num; });
}

ParamExpression* Generator::CreateParameterExpression(std::function<unsigned()> num) {
    return arena.Allocate<ParamExpression>(std::move(num));
}

IfStatement* Generator::CreateIfStatement(Expression* cond, Statement* tr, Statement* fls) {
    return arena.Allocate<IfStatement>(cond, std::move(tr), std::move(fls));
}

TruncateExpression* Generator::CreateTruncate(Expression* expr, std::function<llvm::Type*(llvm::Module*)> ty) {
    return arena.Allocate<TruncateExpression>(expr, std::move(ty));
}

ChainStatement* Generator::CreateChainStatement(Statement* l, Statement* r) {
    return arena.Allocate<ChainStatement>(l, r);
}

WhileStatement* Generator::CreateWhile(Expression* c, Statement* b) {
    return arena.Allocate<WhileStatement>(c, b);
}

NullExpression* Generator::CreateNull(std::function<llvm::Type*(llvm::Module*)> ty) {
    return arena.Allocate<NullExpression>(std::move(ty));
}

IntegralLeftShiftExpression* Generator::CreateLeftShift(Expression* l, Expression* r) {
    return arena.Allocate<IntegralLeftShiftExpression>(l, r);
}

IntegralRightShiftExpression* Generator::CreateRightShift(Expression* l, Expression* r) {
    return arena.Allocate<IntegralRightShiftExpression>(l, r);
}

void Generator::TieFunction(llvm::Function* llvmf, Function* f) {
    funcs[llvmf] = f;
}

Function* Generator::FromLLVMFunc(llvm::Function* f) {
    if (funcs.find(f) == funcs.end())
        assert(false && "Tried to look up an llvm Function that did not correspond to a Wide function.");
    return funcs[f];
}
// Domagoj, you cockface.
