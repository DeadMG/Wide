#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Util/Memory/MemoryArena.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Hashers.h>
#include <Wide/Semantic/ClangTU.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <typeindex>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

namespace llvm {
    class Type;
    class LLVMContext;
    class Value;
    class Module;
}

namespace clang {
    template<unsigned> class UnresolvedSet;
    class QualType;
    class DeclContext;
    class ClassTemplateDecl;
    class NamedDecl;
}

namespace Wide {
    namespace Parse {
        struct Module;
        struct FunctionBase;
        struct Expression;
        struct ModuleLevelDeclaration;
        struct Type;
        struct FunctionArgument;
        struct DeclContext;
        struct Identifier;
        struct TemplateType;
        struct Lambda;
    };
    namespace Semantic {
        class ClangTU;
        struct Callable;
        class Function;
        class Module;
        class ClangNamespace;
        class ClangType;
        struct Type;
        class ConstructorType;
        class LvalueType;
        class RvalueType;
        class FunctionType;
        class ClangTemplateClass;
        class OverloadSet;       
        class UserDefinedType;
        class IntegralType;
        class ClangIncludeEntity;
        class PointerType;
        struct OverloadResolvable;
        class FloatType;
        struct NullType;
        struct Result;
        class TupleType;
        class VoidType;
        class Bool;
        class StringType;
        class ArrayType;
        class TemplateType;
        class LambdaType;
        class Analyzer {
            std::unique_ptr<ClangTU> AggregateTU;
            Module* global;

            std::unordered_map<std::unordered_set<OverloadResolvable*>, std::unordered_map<Type*, std::unique_ptr<OverloadSet>>, SetTypeHasher> callable_overload_sets;
            std::unordered_map<std::string, ClangTU> headers;
            std::unordered_map<clang::QualType, Type*, ClangTypeHasher> GeneratedClangTypes;
            std::unordered_map<clang::QualType, std::unique_ptr<ClangType>, ClangTypeHasher> ClangTypes;
            std::unordered_map<clang::DeclContext*, std::unique_ptr<ClangNamespace>> ClangNamespaces;
            std::unordered_map<Type*, std::unordered_map<std::vector<Type*>, std::unordered_map<bool, std::unique_ptr<FunctionType>>, VectorTypeHasher>> FunctionTypes;
            std::unordered_map<const Parse::FunctionBase*, std::unordered_map<std::vector<Type*>, std::unique_ptr<Function>, VectorTypeHasher>> WideFunctions;
            std::unordered_map<const Parse::TemplateType*, std::unordered_map<std::vector<Type*>, std::unique_ptr<TemplateType>, VectorTypeHasher>> WideTemplateInstantiations;
            std::unordered_map<Type*, std::unique_ptr<LvalueType>> LvalueTypes;
            std::unordered_map<Type*, std::unique_ptr<RvalueType>> RvalueTypes;
            std::unordered_map<Type*, std::unique_ptr<ConstructorType>> ConstructorTypes;
            std::unordered_map<clang::ClassTemplateDecl*, std::unique_ptr<ClangTemplateClass>> ClangTemplateClasses;
            std::unordered_map<unsigned, std::unique_ptr<FloatType>> FloatTypes;
            std::unordered_map<std::unordered_set<clang::NamedDecl*>, std::unordered_map<Type*, std::unique_ptr<OverloadSet>>, SetTypeHasher> clang_overload_sets;
            std::unordered_map<const Parse::Type*, std::unordered_map<Type*, std::unique_ptr<UserDefinedType>>> UDTs;
            std::unordered_map<const Parse::Module*, std::unique_ptr<Module>> WideModules;
            std::unordered_map<unsigned, std::unordered_map<bool, std::unique_ptr<IntegralType>>> integers;
            std::unordered_map<Type*, std::unique_ptr<PointerType>> Pointers;
            std::unordered_map<OverloadSet*, std::unordered_map<OverloadSet*, std::unique_ptr<OverloadSet>>> CombinedOverloadSets;
            std::unordered_map<const Parse::FunctionBase*, std::unique_ptr<OverloadResolvable>> FunctionCallables;
            std::unordered_map<const Parse::TemplateType*, std::unique_ptr<OverloadResolvable>> TemplateTypeCallables;
            std::unordered_map<std::vector<Type*>, std::unique_ptr<TupleType>, VectorTypeHasher> tupletypes;
            std::unordered_map<std::string, std::unique_ptr<StringType>> LiteralStringTypes;
            std::unordered_map<const Parse::Lambda*, std::unordered_map<std::vector<std::pair<std::string, Type*>>, std::unique_ptr<LambdaType>, VectorTypeHasher>> LambdaTypes;
            std::unordered_map<Type*, std::unordered_map<unsigned, std::unique_ptr<ArrayType>>> ArrayTypes;

            const Options::Clang* clangopts;

            std::unique_ptr<ClangIncludeEntity> ClangInclude;
            std::unique_ptr<NullType> null;
            std::unique_ptr<VoidType> Void;
            std::unique_ptr<Bool> Boolean;
            std::unique_ptr<OverloadSet> EmptyOverloadSet;
            std::unique_ptr<OverloadResolvable> PointerCast;
            std::unique_ptr<OverloadResolvable> Move;

            llvm::DataLayout layout;
            std::unordered_map<const Parse::Expression*, std::unique_ptr<Expression>> ExpressionCache;
        public:
            auto GetFunctions() -> const decltype(WideFunctions)& { return WideFunctions; }

            std::function<void(Lexer::Range where, Type* t)> QuickInfo;
            std::function<void(Lexer::Range where)> ParameterHighlight;

            const llvm::DataLayout& GetDataLayout() { return layout; }
            void AddClangType(clang::QualType t, Type* match);
            
            Type* GetVoidType();
            Type* GetNullType();
            Type* GetBooleanType();
            //Type* GetNothingFunctorType();
            Type* GetTypeForString(std::string str);
            Type* GetTypeForInteger(llvm::APInt val);
            
            // The contract of this function is to return the Wide type that corresponds to that Clang type.
            // Not to return a ClangType instance.
            Type* GetClangType(ClangTU& from, clang::QualType t);
            ClangNamespace* GetClangNamespace(ClangTU& from, clang::DeclContext* dc);
            FunctionType* GetFunctionType(Type* ret, const std::vector<Type*>& t, bool variadic);
            Module* GetWideModule(const Parse::Module* m, Module* higher);
            Function* GetWideFunction(const Parse::FunctionBase* p, Type* context, const std::vector<Type*>&, std::string name);
            LvalueType* GetLvalueType(Type* t);
            Type* GetRvalueType(Type* t);
            ConstructorType* GetConstructorType(Type* t);
            ClangTemplateClass* GetClangTemplateClass(ClangTU& from, clang::ClassTemplateDecl*);
            OverloadSet* GetOverloadSet();
            OverloadSet* GetOverloadSet(std::unordered_set<OverloadResolvable*> c, Type* nonstatic = nullptr);
            OverloadSet* GetOverloadSet(OverloadResolvable* c);
            OverloadSet* GetOverloadSet(OverloadSet*, OverloadSet*, Type* context = nullptr);
            OverloadSet* GetOverloadSet(std::unordered_set<clang::NamedDecl*> decls, ClangTU* from, Type* context);
            UserDefinedType* GetUDT(const Parse::Type*, Type* context, std::string name);
            IntegralType* GetIntegralType(unsigned, bool);
            PointerType* GetPointerType(Type* to);
            FloatType* GetFloatType(unsigned);
            Module* GetGlobalModule();
            TupleType* GetTupleType(std::vector<Type*> types);
            OverloadResolvable* GetCallableForFunction(const Parse::FunctionBase* f, Type* context, std::string name);
            OverloadResolvable* GetCallableForTemplateType(const Parse::TemplateType* t, Type* context);
            TemplateType* GetTemplateType(const Parse::TemplateType* t, Type* context, std::vector<Type*> arguments, std::string name);
            LambdaType* GetLambdaType(const Parse::Lambda* funcbase, std::vector<std::pair<std::string, Type*>> types, Type* context);
            ArrayType* GetArrayType(Type* t, unsigned num);

            std::unordered_map<std::type_index, std::function<std::unique_ptr<Expression>(Analyzer& a, Type* lookup, const Parse::Expression* e)>> expression_handlers;
            template<typename T, typename F> void AddExpressionHandler(F f) {
                expression_handlers[typeid(const T)] = [f](Analyzer& a, Type* lookup, const Parse::Expression* e) {
                    return f(a, lookup, static_cast<const T*>(e));
                };
            }
            std::unique_ptr<Expression> AnalyzeCachedExpression(Type* lookup, const Parse::Expression* e);
            std::unique_ptr<Expression> AnalyzeExpression(Type* lookup, const Parse::Expression* e);

            Analyzer(const Options::Clang&, const Parse::Module*);

            ClangTU* LoadCPPHeader(std::string file, Lexer::Range where);
            ClangTU* AggregateCPPHeader(std::string file, Lexer::Range where);
            
            ~Analyzer();

            void GenerateCode(llvm::Module* module);
            ClangTU* GetAggregateTU();
        };
        bool IsRvalueType(Type* t);
        bool IsLvalueType(Type* t);
        Lexer::Access GetAccessSpecifier(Type* from, Type* to);
        void AnalyzeExportedFunctions(Analyzer& a);
        bool IsMultiTyped(const Parse::FunctionArgument& f);
        bool IsMultiTyped(const Parse::FunctionBase* f);
        Type* InferTypeFromExpression(Expression* e, bool local);
    }
}
