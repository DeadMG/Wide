#include <Wide/Codegen/LLVMGenerator.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/Function.h>

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
using namespace LLVMCodegen;

void Generator::operator()() {        
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(triple, llvm::Triple(triple).getArchName(), "", targetopts));
    main.setDataLayout(targetmachine->getDataLayout()->getStringRepresentation());  

    EmitCode();
        
    llvm::PassManager pm;
    std::ofstream file(outputfile, std::ios::trunc | std::ios::binary);
    llvm::raw_os_ostream out(file);
    llvm::formatted_raw_ostream format_out(out);
    
    pm.add(new llvm::DataLayout(main.getDataLayout()));
    for(auto&& pass : llvmopts.Passes)        
        pm.add(pass->createPass(pass->getPassID()));

    if (llvmopts.Emit)
        targetmachine->addPassesToEmitFile(pm, format_out, llvm::TargetMachine::CodeGenFileType::CGFT_ObjectFile);
    pm.run(main);
}

Generator::Generator(const Options::LLVM& l, std::string ofile, std::string trip)
    : main("", context)
    , llvmopts(l)
    , outputfile(std::move(ofile))
    , triple(std::move(trip))
{
    main.setTargetTriple(triple);
}

Function* Generator::CreateFunction(std::function<llvm::Type*(llvm::Module*)> ty, std::string name, Semantic::Function* debug, bool tramp) {
    auto p = arena.Allocate<Function>(std::move(ty), std::move(name), debug, tramp);
    functions.push_back(p);
    return p;
}

Variable* Generator::CreateVariable(std::function<llvm::Type*(llvm::Module*)> t, unsigned align) {
    return arena.Allocate<Variable>(std::move(t), align);
}

LLVMCodegen::Expression* AssertExpression(Codegen::Expression* e) {
	auto p = dynamic_cast<LLVMCodegen::Expression*>(e);
	assert(p);
	return p;
}
LLVMCodegen::Statement* AssertStatement(Codegen::Statement* e) {
	auto p = dynamic_cast<LLVMCodegen::Statement*>(e);
	assert(p);
	return p;
}

FunctionCall* Generator::CreateFunctionCall(Codegen::Expression* obj, std::vector<Codegen::Expression*> args, std::function<llvm::Type*(llvm::Module*)> ty) {
	std::vector<LLVMCodegen::Expression*> exprs;
    for(auto ex : args)
		exprs.push_back(AssertExpression(ex));
    return arena.Allocate<FunctionCall>(AssertExpression(obj), std::move(exprs), std::move(ty));
}

StringExpression* Generator::CreateStringExpression(std::string str) {
    return arena.Allocate<StringExpression>(std::move(str));
}
NamedGlobalVariable* Generator::CreateGlobalVariable(std::string name) {
    return arena.Allocate<NamedGlobalVariable>(std::move(name));
}

StoreExpression* Generator::CreateStore(Codegen::Expression* obj, Codegen::Expression* val) {
    return arena.Allocate<StoreExpression>(AssertExpression(obj), AssertExpression(val));
}
LoadExpression* Generator::CreateLoad(Codegen::Expression* obj) {
    return arena.Allocate<LoadExpression>(AssertExpression(obj));
}
ReturnStatement* Generator::CreateReturn() {
    return arena.Allocate<ReturnStatement>();
}
ReturnStatement* Generator::CreateReturn(Codegen::Expression* obj) {
    return arena.Allocate<ReturnStatement>(AssertExpression(obj));
}
FunctionValue* Generator::CreateFunctionValue(std::string name) {
    return arena.Allocate<FunctionValue>(name);
}

void Generator::EmitCode() {    
	for(auto&& x : tus)
		x(&main);
    std::string err;
    if (llvm::verifyModule(main, llvm::VerifierFailureAction::PrintMessageAction, &err))
        throw std::runtime_error("Internal Compiler Error: An LLVM module failed verification.");
    std::string s;
    llvm::raw_string_ostream stream(s);
    main.print(stream, nullptr);
    for(auto&& x : functions) {
        x->EmitCode(&main, context, *this);
    }    
	s.clear();
    main.print(stream, nullptr);    
    if (llvm::verifyModule(main, llvm::VerifierFailureAction::PrintMessageAction, &err))
        throw std::runtime_error("Internal Compiler Error: An LLVM module failed verification.");
}

NegateExpression* Generator::CreateNegateExpression(Codegen::Expression* val) {
    return arena.Allocate<NegateExpression>(AssertExpression(val));
}

IntegralExpression* Generator::CreateIntegralExpression(uint64_t val, bool is_signed, std::function<llvm::Type*(llvm::Module*)> ty) {
    return arena.Allocate<IntegralExpression>(val, is_signed, std::move(ty));
}
ChainExpression* Generator::CreateChainExpression(Codegen::Statement* s, Codegen::Expression* e) {
    return arena.Allocate<ChainExpression>(AssertStatement(s), AssertExpression(e));
}

FieldExpression* Generator::CreateFieldExpression( Codegen::Expression* e, unsigned f) {
    return arena.Allocate<FieldExpression>([=] { return f; }, AssertExpression(e));
}        
FieldExpression* Generator::CreateFieldExpression(Codegen::Expression* e, std::function<unsigned()> f) {
    return arena.Allocate<FieldExpression>(f, AssertExpression(e));
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

IfStatement* Generator::CreateIfStatement(Codegen::Expression* cond, Codegen::Statement* tr, Codegen::Statement* fls) {
    return arena.Allocate<IfStatement>(AssertExpression(cond), AssertStatement(tr), fls ? AssertStatement(fls) : nullptr);
}

TruncateExpression* Generator::CreateTruncate(Codegen::Expression* expr, std::function<llvm::Type*(llvm::Module*)> ty) {
    return arena.Allocate<TruncateExpression>(AssertExpression(expr), std::move(ty));
}

ChainStatement* Generator::CreateChainStatement(Codegen::Statement* l, Codegen::Statement* r) {
    assert(l); assert(r);
    return arena.Allocate<ChainStatement>(AssertStatement(l), AssertStatement(r));
}

WhileStatement* Generator::CreateWhile(Codegen::Expression* c, Codegen::Statement* b) {
    return arena.Allocate<WhileStatement>(AssertExpression(c), AssertStatement(b));
}

NullExpression* Generator::CreateNull(std::function<llvm::Type*(llvm::Module*)> ty) {
    return arena.Allocate<NullExpression>(std::move(ty));
}

IntegralLeftShiftExpression* Generator::CreateLeftShift(Codegen::Expression* l, Codegen::Expression* r) {
    return arena.Allocate<IntegralLeftShiftExpression>(AssertExpression(l), AssertExpression(r));
}

IntegralRightShiftExpression* Generator::CreateRightShift(Codegen::Expression* l, Codegen::Expression* r, bool s) {
    return arena.Allocate<IntegralRightShiftExpression>(AssertExpression(l), AssertExpression(r), s);
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

OrExpression* Generator::CreateOrExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) {
    return arena.Allocate<OrExpression>(AssertExpression(lhs), AssertExpression(rhs));
}
EqualityExpression* Generator::CreateEqualityExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) {
    return arena.Allocate<EqualityExpression>(AssertExpression(lhs), AssertExpression(rhs));
}
PlusExpression* Generator::CreatePlusExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) {
    return arena.Allocate<PlusExpression>(AssertExpression(lhs), AssertExpression(rhs));
}
MultiplyExpression* Generator::CreateMultiplyExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) {
    return arena.Allocate<MultiplyExpression>(AssertExpression(lhs), AssertExpression(rhs));
}
AndExpression* Generator::CreateAndExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) {
    return arena.Allocate<AndExpression>(AssertExpression(lhs), AssertExpression(rhs));
}

ZExt* Generator::CreateZeroExtension(Codegen::Expression* e, std::function<llvm::Type*(llvm::Module*)> func) {
    return arena.Allocate<ZExt>(AssertExpression(e), std::move(func));
}
SExt* Generator::CreateSignedExtension(Codegen::Expression* e, std::function<llvm::Type*(llvm::Module*)> func) {
    return arena.Allocate<SExt>(AssertExpression(e), std::move(func));
}
IsNullExpression* Generator::CreateIsNullExpression(Codegen::Expression* e) {
    return arena.Allocate<IsNullExpression>(AssertExpression(e));
}
IntegralLessThan* Generator::CreateLT(Codegen::Expression* l, Codegen::Expression* r, bool is_signed) {
	return arena.Allocate<IntegralLessThan>(AssertExpression(l), AssertExpression(r), is_signed);
}
SubExpression* Generator::CreateSubExpression(Codegen::Expression* l, Codegen::Expression* r) {
	return arena.Allocate<SubExpression>(AssertExpression(l), AssertExpression(r));
}
XorExpression* Generator::CreateXorExpression(Codegen::Expression* l, Codegen::Expression* r) {
	return arena.Allocate<XorExpression>(AssertExpression(l), AssertExpression(r));
}
ModExpression* Generator::CreateModExpression(Codegen::Expression* l, Codegen::Expression* r, bool is_signed) {
	return arena.Allocate<ModExpression>(AssertExpression(l), AssertExpression(r), is_signed);
}
DivExpression* Generator::CreateDivExpression(Codegen::Expression* l, Codegen::Expression* r, bool is_signed) {
	return arena.Allocate<DivExpression>(AssertExpression(l), AssertExpression(r), is_signed);
}

llvm::DataLayout Generator::GetDataLayout() {
	return llvm::DataLayout(main.getDataLayout());
}

std::size_t Generator::GetInt8AllocSize() {
	return GetDataLayout().getTypeAllocSize(llvm::IntegerType::getInt8Ty(context));
}

llvm::LLVMContext& Generator::GetContext() {
	return context;
}

void Generator::AddClangTU(std::function<void(llvm::Module*)> tu) {
	tus.push_back(tu);
}
