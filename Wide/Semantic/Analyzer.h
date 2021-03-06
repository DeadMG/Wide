#pragma once

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
#include <boost/uuid/random_generator.hpp>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/CharUnits.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
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
    class FunctionProtoType;
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
        namespace Functions {
            class Function;
            class FunctionSkeleton;
        }
        class ClangTU;
        struct Callable;
        class Module;
        class ClangNamespace;
        class ClangType;
        struct Type;
        class ConstructorType;
        class LvalueType;
        class RvalueType;
        class WideFunctionType;
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
        class ClangFunctionType;
        class Bool;
        class StringType;
        class ArrayType;
        class LambdaType;
        class MemberDataPointer;
        class MemberFunctionPointer;
        struct Return;
        struct ClangTypeInfo {
            Type* ty;
            std::function<void()> Complete;
            std::function<void(
                uint64_t&, 
                uint64_t&, 
                llvm::DenseMap<const clang::FieldDecl*, uint64_t>&,
                llvm::DenseMap<const clang::CXXRecordDecl*, clang::CharUnits>&, 
                llvm::DenseMap<const clang::CXXRecordDecl*, clang::CharUnits>&
            )> Layout;
        };
        class Analyzer {
        public:
            // Other people remove things from here on destruction, so it needs to be destructed last.
            std::unordered_set<AnalyzerError*> errors;

        private:
            std::unique_ptr<ClangTU> AggregateTU;
            Module* global;
            boost::uuids::random_generator uuid_generator;
            std::unordered_map<std::unordered_set<OverloadResolvable*>, std::unordered_map<Type*, std::unique_ptr<OverloadSet>>, SetTypeHasher> callable_overload_sets;
            std::unordered_map<std::string, ClangTU> headers;
            std::unordered_map<clang::QualType, std::unique_ptr<ClangType>, ClangTypeHasher> ClangTypes;
            std::unordered_map<clang::DeclContext*, std::unique_ptr<ClangNamespace>> ClangNamespaces;
            std::unordered_map<Type*, std::unordered_map<std::vector<Type*>, std::map<llvm::CallingConv::ID, std::unordered_map<bool, std::unique_ptr<WideFunctionType>>>, VectorTypeHasher>> FunctionTypes;
            std::unordered_map<Functions::FunctionSkeleton*, std::unordered_map<std::vector<Type*>, std::unique_ptr<Functions::Function>, VectorTypeHasher>> WideFunctions;
            std::unordered_map<Type*, std::unique_ptr<LvalueType>> LvalueTypes;
            std::unordered_map<Type*, std::unique_ptr<RvalueType>> RvalueTypes;
            std::unordered_map<Type*, std::unique_ptr<ConstructorType>> ConstructorTypes;
            std::unordered_map<clang::ClassTemplateDecl*, std::unique_ptr<ClangTemplateClass>> ClangTemplateClasses;
            std::unordered_map<unsigned, std::unique_ptr<FloatType>> FloatTypes;
            std::unordered_map<std::unordered_set<clang::NamedDecl*>, std::unordered_map<Type*, std::unique_ptr<OverloadSet>>, SetTypeHasher> clang_overload_sets;
            std::unordered_map<const Parse::Type*, std::unordered_map<Location, std::unique_ptr<UserDefinedType>>> UDTs;
            std::unordered_map<const Parse::Module*, std::unique_ptr<Module>> WideModules;
            std::unordered_map<unsigned, std::unordered_map<bool, std::unique_ptr<IntegralType>>> integers;
            std::unordered_map<Type*, std::unique_ptr<PointerType>> Pointers;
            std::unordered_map<std::pair<OverloadSet*, OverloadSet*>, std::unordered_map<Type*, std::unique_ptr<OverloadSet>>, PairTypeHasher, PairTypeEquality> CombinedOverloadSets;
            std::unordered_map<const Parse::FunctionBase*, std::unordered_map<Location, std::unique_ptr<OverloadResolvable>>> FunctionCallables;
            std::unordered_map<std::vector<Type*>, std::unique_ptr<TupleType>, VectorTypeHasher> tupletypes;
            std::unordered_map<const Parse::Lambda*, std::unordered_map<std::vector<std::pair<Parse::Name, Type*>>, std::unique_ptr<LambdaType>, VectorTypeHasher>> LambdaTypes;
            std::unordered_map<Type*, std::unordered_map<unsigned, std::unique_ptr<ArrayType>>> ArrayTypes;
            std::unordered_map<Type*, std::unordered_map<Type*, std::unique_ptr<MemberDataPointer>>> MemberDataPointers;
            std::unordered_map<Type*, std::unordered_map<FunctionType*, std::unique_ptr<MemberFunctionPointer>>> MemberFunctionPointers;
            std::unordered_map<const clang::FunctionProtoType*, std::unordered_map<clang::QualType, std::unordered_map<ClangTU*, std::unique_ptr<ClangFunctionType>>, ClangTypeHasher>> ClangMemberFunctionTypes;
            std::unordered_map<const clang::FunctionProtoType*, std::unordered_map<ClangTU*, std::unique_ptr<ClangFunctionType>>> ClangFunctionTypes;
            std::unordered_map<const clang::CXXRecordDecl*, ClangTypeInfo> GeneratedClangTypes;
            std::unordered_map<const Parse::FunctionBase*, std::unordered_map<Location, std::unique_ptr<Functions::FunctionSkeleton>>> FunctionSkeletons;

            const Options::Clang* clangopts;

            std::unique_ptr<ClangIncludeEntity> ClangInclude;
            std::unique_ptr<NullType> null;
            std::unique_ptr<VoidType> Void;
            std::unique_ptr<Bool> Boolean;
            std::unique_ptr<OverloadSet> EmptyOverloadSet;
            std::unique_ptr<OverloadResolvable> PointerCast;
            std::unique_ptr<OverloadResolvable> Move;
            std::unique_ptr<StringType> LiteralStringType;

            llvm::DataLayout layout;
            std::unordered_map<const Parse::Expression*, std::unordered_map<Location, std::shared_ptr<Expression>>> ExpressionCache;
            std::unique_ptr<llvm::Module> ConstantModule;
        public:
            auto GetFunctions() -> const decltype(WideFunctions)& { return WideFunctions; }
            
            const llvm::DataLayout& GetDataLayout() { return layout; }
            void AddClangType(const clang::CXXRecordDecl* t, ClangTypeInfo match);
            ClangTypeInfo* MaybeGetClangTypeInfo(const clang::CXXRecordDecl* decl);
            
            Type* GetVoidType();
            Type* GetNullType();
            Type* GetBooleanType();
            //Type* GetNothingFunctorType();
            Type* GetLiteralStringType();
            Type* GetTypeForInteger(llvm::APInt val);
            
            // The contract of this function is to return the Wide type that corresponds to that Clang type.
            // Not to return a ClangType instance.
            Type* GetClangType(ClangTU& from, clang::QualType t);
            ClangNamespace* GetClangNamespace(ClangTU& from, Location l, clang::DeclContext* dc);
            WideFunctionType* GetFunctionType(Type* ret, const std::vector<Type*>& t, bool variadic);
            WideFunctionType* GetFunctionType(Type* ret, const std::vector<Type*>& t, bool variadic, llvm::CallingConv::ID);
            //WideFunctionType* GetFunctionType(Type* ret, const std::vector<Type*>& t, bool variadic, clang::CallingConv);
            ClangFunctionType* GetFunctionType(const clang::FunctionProtoType*, clang::QualType, ClangTU&);
            ClangFunctionType* GetFunctionType(const clang::FunctionProtoType*, ClangTU&);
            ClangFunctionType* GetFunctionType(const clang::FunctionProtoType*, Wide::Util::optional<clang::QualType>, ClangTU&);
            Module* GetWideModule(const Parse::Module* m, Location higher, std::string name);
            Functions::FunctionSkeleton* GetFunctionSkeleton(const Parse::FunctionBase* p, Location context);
            Functions::FunctionSkeleton* GetFunctionSkeleton(const Parse::Constructor* p, Location context) = delete;
            Functions::FunctionSkeleton* GetFunctionSkeleton(const Parse::Destructor* p, Location context) = delete;
            Functions::Function* GetWideFunction(Functions::FunctionSkeleton* skeleton);
            LvalueType* GetLvalueType(Type* t);
            Type* GetRvalueType(Type* t);
            ConstructorType* GetConstructorType(Type* t);
            ClangTemplateClass* GetClangTemplateClass(ClangTU& from, Location l, clang::ClassTemplateDecl*);
            OverloadSet* GetOverloadSet();
            OverloadSet* GetOverloadSet(std::unordered_set<OverloadResolvable*> c, Type* nonstatic = nullptr);
            OverloadSet* GetOverloadSet(OverloadResolvable* c);
            OverloadSet* GetOverloadSet(OverloadSet*, OverloadSet*, Type* nonstatic_override = nullptr);
            OverloadSet* GetOverloadSet(std::unordered_set<clang::NamedDecl*> decls, ClangTU* from, Type* nonstatic_override = nullptr);
            UserDefinedType* GetUDT(const Parse::Type*, Location context, std::string name);
            IntegralType* GetIntegralType(unsigned, bool);
            PointerType* GetPointerType(Type* to);
            FloatType* GetFloatType(unsigned);
            Module* GetGlobalModule();
            TupleType* GetTupleType(std::vector<Type*> types);
            OverloadResolvable* GetCallableForFunction(const Parse::FunctionBase*, Location context);
            LambdaType* GetLambdaType(const Parse::Lambda* lam, Location l, std::vector<std::pair<Parse::Name, Type*>> types);
            ArrayType* GetArrayType(Type* t, unsigned num);
            MemberDataPointer* GetMemberDataPointer(Type* source, Type* dest);
            MemberFunctionPointer* GetMemberFunctionPointer(Type* source, FunctionType* dest);

            std::unordered_map<
                std::type_index, 
                std::function<std::shared_ptr<Expression>(const Parse::Expression*, Analyzer& a, Location location, std::shared_ptr<Expression>) >>
            ExpressionHandlers;
            std::unordered_map<
                std::type_index, 
                std::function<std::shared_ptr<Expression>(const Parse::SharedObject* , Analyzer& a, Location l, std::string name)>>
            SharedObjectHandlers;
            std::unordered_map<
                std::type_index, 
                std::function<std::shared_ptr<Expression>(const Parse::UniqueAccessContainer*, Analyzer& a, Location l, std::string name)>>
            UniqueObjectHandlers;
            std::unordered_map<
                std::type_index,
                std::function<std::shared_ptr<Expression>(const Parse::MultipleAccessContainer*, Analyzer& a, Location l, Parse::Access access, std::string name, Lexer::Range where)>>
            MultiObjectHandlers;
            std::unordered_map<
                std::type_index,
                std::function<std::shared_ptr<Statement>(const Parse::Statement*, Functions::FunctionSkeleton* skel, Analyzer& a, Location l, std::shared_ptr<Expression>) >>
            StatementHandlers;
            std::unordered_map<
                std::type_index,
                std::function<std::unordered_set<Parse::Name>(const Parse::Statement*, Analyzer& a, std::unordered_set<Parse::Name>&)>>
            LambdaCaptureAnalyzers;

            std::unordered_map<const Parse::Statement*, std::vector<std::unique_ptr<Semantic::Error>>> StatementErrors;
            std::vector<ClangDiagnostic> ClangDiagnostics;

            std::shared_ptr<Expression> AnalyzeExpression(Location l, const Parse::Expression* e, std::shared_ptr<Expression> _this);

            Analyzer(const Options::Clang&, const Parse::Module*, llvm::LLVMContext& con);

            ClangTU* LoadCPPHeader(std::string file, Lexer::Range where);
            ClangTU* AggregateCPPHeader(std::string file, Lexer::Range where);
            ~Analyzer();

            void GenerateCode(llvm::Module* module);
            ClangTU* GetAggregateTU();

            std::vector<Type*> GetFunctionParameters(const Parse::FunctionBase* p, Location context);
            bool HasImplicitThis(const Parse::FunctionBase* p, Location context);

            std::string GetUniqueFunctionName();

            llvm::APInt EvaluateConstantIntegerExpression(std::shared_ptr<Expression> e);

        };
        template<typename T> struct base_pointer; template<typename T> struct base_pointer<const T*> { typedef T type; };
        template<typename T> struct base_pointer; template<typename T> struct base_pointer<T*> { typedef T type; };
        template<typename T, typename Ret, typename First, typename... Args, typename F> void AddHandler(std::unordered_map<std::type_index, std::function<Ret(First, Args...)>>& map, F f) {
            static_assert(std::is_base_of<typename base_pointer<First>::type, T>::value, "T must derive from the first argument.");
            map[typeid(T)] = [f](First farg, Args... args) {
                return f(static_cast<T*>(farg), std::forward<Args>(args)...);
            };
        }
        template<typename T>  void AddError(Analyzer& a, const Parse::Statement* stmt, std::string msg) {
            AddError<T>(a, stmt, stmt->location, msg);
        }
        template<typename T> void AddError(Analyzer& a, const Parse::Statement* stmt, Lexer::Range where, std::string msg) {
            a.StatementErrors[stmt].push_back(Wide::Memory::MakeUnique<Semantic::SpecificError<T>>(a, where, msg));
        }
        std::shared_ptr<Expression> LookupName(Location l, Parse::Name name, Lexer::Range where, std::shared_ptr<Expression> _this, const Parse::Import* import);
        std::shared_ptr<Statement> AnalyzeStatement(Analyzer& a, Functions::FunctionSkeleton* skel, const Parse::Statement* stmt, Location l, std::shared_ptr<Expression> _this);
        bool IsRvalueType(Type* t);
        bool IsLvalueType(Type* t);
        //Parse::Access GetAccessSpecifier(Type* from, Type* to);
        void AnalyzeExportedFunctions(Analyzer& a, std::function<void(const Parse::AttributeFunctionBase*, std::string, Location)> func);
        void AnalyzeExportedFunctions(Analyzer& a);
        std::string GetOperatorName(Parse::OperatorName name);
        std::string GetNameAsString(Parse::Name name);
        Type* CollapseType(Type* source, Type* member);
        llvm::Value* CollapseMember(Type* source, std::pair<llvm::Value*, Type*> member, CodegenContext& con);
        std::function<void(CodegenContext&)> ThrowObject(std::shared_ptr<Expression> expr, Context c);
        Type* GetNonstaticContext(Location context);
        std::unordered_set<Parse::Name> GetLambdaCaptures(const Parse::Statement* s, Analyzer& a, std::unordered_set<Parse::Name>& local_names);
    }
}
