#pragma once

#include <Wide/Parser/AST.h>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Hashers.h>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <string>
#include <cassert>
#include <list>
#include <memory>
#include <boost/variant.hpp>

#pragma warning(push, 0)
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/ADT/APInt.h>
#pragma warning(pop)


#pragma warning(disable : 4250)
// Decorated name length exceeded- name truncated
#pragma warning(disable : 4503)
// Performance warning for implicitly integers or pointers to bool
#pragma warning(disable : 4800)

namespace llvm {
    class Type;
    class LLVMContext;
    class Module;
    class Value;
}
namespace clang {
    class QualType;
}
namespace Wide {
    namespace Semantic {
        class ClangTU;
        class Analyzer;
        class OverloadSet;
        class Error;
        struct Type;
        
        class FunctionType;
        enum class OperatorAccess {
            Implicit,
            Explicit
        };
    }
}
namespace std {
    template<> struct hash<Wide::Semantic::OperatorAccess> {
        std::size_t operator()(Wide::Semantic::OperatorAccess kind) const {
            return std::hash<int>()((int)kind);
        }
    };
}
namespace Wide {
    namespace Semantic {
        class Module;
        class UserDefinedType;
        class ClangNamespace;
        class ClangType;
        class ClangTU;
        class LambdaType;
        struct Scope;
        struct Location {
        private:
            Location();
        public:
            static const Location Empty;
            Location(Analyzer& a, ClangTU* TU);
            Location(Analyzer& a);
            Location(Location previous, Module* next);
            Location(Location previous, UserDefinedType* next);
            Location(Location previous, LambdaType* next);
            Location(Location previous, ClangNamespace* next);
            Location(Location previous, ClangType* next);
            Location(Location previous, Scope* next);

            Location(Location&&) = default;
            Location(const Location&) = default;
            Location& operator=(Location&&) = default;
            Location& operator=(const Location&) = default;
            Analyzer& GetAnalyzer() const;

            struct WideLocation {
                std::vector<Module*> modules;
                std::vector<boost::variant<LambdaType*, UserDefinedType*>> types;
            };
            struct CppLocation {
                std::vector<ClangNamespace*> namespaces;
                std::vector<ClangType*> types;
            };
            boost::variant<WideLocation, CppLocation> location;
            Scope* localscope = nullptr;
            Parse::Access PublicOrWide(std::function<Parse::Access(WideLocation loc)>);
            Parse::Access PublicOrCpp(std::function<Parse::Access(CppLocation loc)>);
        };
        inline bool operator==(Location lhs, Location rhs) {
            return true;
        }
    }
}
namespace std {
    template<> struct hash<Wide::Semantic::Location> {
        std::size_t operator()(Wide::Semantic::Location l) const {
            return 0;
        }
    };
}
namespace Wide {
    namespace Semantic {
        struct Context {
            Context(Location l, Lexer::Range r) : from(std::move(l)), where(r) {}
            Location from;
            Lexer::Range where;
        };
        struct Type {
        public:
            enum class InheritanceRelationship {
                NotDerived,
                AmbiguouslyDerived,
                UnambiguouslyDerived
            };
            struct VTableLayout {
                VTableLayout() : offset(0) {}
                enum class SpecialMember {
                    Destructor,
                    ItaniumABIDeletingDestructor,
                    OffsetToTop,
                    RTTIPointer
                };
                struct VirtualFunction {
                    Parse::Name name;
                    bool final;
                    bool abstract;
                };
                struct VirtualFunctionEntry {
                    boost::variant<SpecialMember, VirtualFunction> func;
                    FunctionType* type;
                };
                unsigned offset;
                std::vector<VirtualFunctionEntry> layout;
            };

        private:
            std::unordered_map<Parse::Access, OverloadSet*> ConstructorOverloadSets;
            std::unordered_map<Parse::Access, std::unordered_map<Parse::OperatorName, std::unordered_map<OperatorAccess, OverloadSet*>>> OperatorOverloadSets;
            std::unordered_map<Location, std::unordered_map<Parse::OperatorName, OverloadSet*>> ADLResults;
            std::unordered_map<std::vector<std::pair<Type*, unsigned>>, std::shared_ptr<Expression>, VectorTypeHasher> ComputedVTables;
            Wide::Util::optional<VTableLayout> VtableLayout;
            Wide::Util::optional<VTableLayout> PrimaryVtableLayout;
            Wide::Util::optional<std::function<llvm::Function*(llvm::Module*)>> GetDestructorFunctionCache;
            llvm::Function* DestructorFunction;
            virtual std::function<llvm::Function*(llvm::Module*)> CreateDestructorFunction(Location from);

            VTableLayout ComputeVTableLayout();
            std::shared_ptr<Expression> CreateVTable(std::vector<std::pair<Type*, unsigned>> path, Location from);
            std::shared_ptr<Expression> GetVTablePointer(std::vector<std::pair<Type*, unsigned>> path, Location from);
            static std::shared_ptr<Expression> SetVirtualPointers(std::shared_ptr<Expression> self, std::vector<std::pair<Type*, unsigned>> path, Location from);
            virtual VTableLayout ComputePrimaryVTableLayout();
            virtual std::shared_ptr<Expression> ConstructCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c);
        protected:
            virtual std::function<void(CodegenContext&)> BuildDestruction(std::shared_ptr<Expression> self, Context c, bool devirtualize);
            virtual std::shared_ptr<Expression> AccessNamedMember(std::shared_ptr<Expression> t, std::string name, Context c);
            virtual OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access, OperatorAccess implicit);
            virtual OverloadSet* CreateADLOverloadSet(Parse::OperatorName name, Location from);
            virtual OverloadSet* CreateConstructorOverloadSet(Parse::Access access) = 0;
        public:
            virtual std::shared_ptr<Expression> AccessVirtualPointer(std::shared_ptr<Expression> self);
            virtual std::pair<FunctionType*, std::function<llvm::Function*(llvm::Module*)>> VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry) {
                assert(false); return{ {}, {} };
            }
            Type(Analyzer& a) : analyzer(a), DestructorFunction(nullptr) {}

            Analyzer& analyzer;

            virtual std::size_t size() = 0;
            virtual std::size_t alignment() = 0;
            virtual std::string explain() = 0;
            virtual llvm::Type* GetLLVMType(llvm::Module* module) = 0;

            virtual bool IsFinal();
            virtual bool IsAbstract();
            virtual bool IsReference(Type* to);
            virtual bool IsReference();
            virtual Type* Decay();
            virtual bool AlwaysKeepInMemory(llvm::Module* mod);
            virtual bool IsTriviallyDestructible();
            virtual bool IsTriviallyCopyConstructible();
            virtual Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU);
            virtual bool IsMoveConstructible(Location context);
            virtual bool IsCopyConstructible(Location context);
            virtual bool IsMoveAssignable(Location context);
            virtual bool IsCopyAssignable(Location context);
            virtual bool IsConstant();
            virtual bool IsEmpty();
            virtual Type* GetVirtualPointerType();
            virtual std::vector<Type*> GetBases();
            virtual Type* GetPrimaryBase();
            virtual std::unordered_map<unsigned, std::unordered_set<Type*>> GetEmptyLayout();
            virtual bool HasVirtualDestructor();
            virtual std::vector<std::pair<Type*, unsigned>> GetBasesAndOffsets();
            virtual bool IsNonstaticMemberContext();
            virtual std::function<llvm::Constant*(llvm::Module*)> GetRTTI();
            virtual Parse::Access GetAccess(Location location);
            virtual bool IsLookupContext();

            // Do not ever call from public API, it is for derived types and implementation details only.
            virtual bool IsSourceATarget(Type* source, Type* target, Location location) { return false; }
            virtual std::shared_ptr<Expression> AccessStaticMember(std::string name, Context c);

            std::shared_ptr<Expression> BuildValueConstruction(std::vector<std::shared_ptr<Expression>> args, Context c);
            std::shared_ptr<Expression> BuildRvalueConstruction(std::vector<std::shared_ptr<Expression>> exprs, Context c);

            virtual ~Type() {}

            virtual std::function<llvm::Function*(llvm::Module*)> GetDestructorFunction(Location from);
            std::function<llvm::Function*(llvm::Module*)> GetDestructorFunctionForEH(Location from);
            InheritanceRelationship IsDerivedFrom(Type* other);
            VTableLayout GetVtableLayout();
            VTableLayout GetPrimaryVTable();
            OverloadSet* GetConstructorOverloadSet(Parse::Access access);
            OverloadSet* PerformADL(Parse::OperatorName what, Location from);
            
            OverloadSet* AccessMember(Parse::OperatorName type, Parse::Access access, OperatorAccess implicit);
            std::shared_ptr<Expression> AccessStaticMember(Parse::Name name, Context c);
            bool InheritsFromAtOffsetZero(Type* other);
            unsigned GetOffsetToBase(Type* base);

            std::function<void(CodegenContext&)> BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize);
            static std::shared_ptr<Expression> GetVirtualPointer(std::shared_ptr<Expression> self);
            static std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c);
            static std::shared_ptr<Expression> AccessMember(std::shared_ptr<Expression> t, Parse::Name name, Context c);
            static std::shared_ptr<Expression> BuildBooleanConversion(std::shared_ptr<Expression>, Context);
            static std::shared_ptr<Expression> AccessBase(std::shared_ptr<Expression> self, Type* other);
            static std::shared_ptr<Expression> BuildInplaceConstruction(std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> exprs, Context c);
            static std::shared_ptr<Expression> BuildUnaryExpression(std::shared_ptr<Expression> self, Lexer::TokenType type, Context c);
            static std::shared_ptr<Expression> BuildBinaryExpression(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Lexer::TokenType type, Context c);
            static std::shared_ptr<Expression> SetVirtualPointers(std::shared_ptr<Expression>, Location from);
            static std::shared_ptr<Expression> BuildIndex(std::shared_ptr<Expression> obj, std::shared_ptr<Expression> arg, Context c);
            static bool IsFirstASecond(Type* first, Type* second, Location location);
        };

        struct Callable {
        public:
            std::shared_ptr<Expression> Call(std::vector<std::shared_ptr<Expression>> args, Context c);
            virtual std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) = 0;
            virtual std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) = 0;
        };
        struct OverloadResolvable {
            virtual Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*>, Analyzer& a, Location location) = 0;
            virtual Callable* GetCallableForResolution(std::vector<Type*>, Location, Analyzer& a) = 0;
        };

        std::vector<std::shared_ptr<Expression>> AdjustArgumentsForTypes(std::vector<std::shared_ptr<Expression>> args, std::vector<Type*> types, Context c);
        std::unique_ptr<OverloadResolvable> MakeResolvable(std::function<std::shared_ptr<Expression>(std::vector<std::shared_ptr<Expression>>, Context)> f, std::vector<Type*> types);

        struct TupleInitializable {
        private:
            std::unique_ptr<OverloadResolvable> TupleConstructor;
        public:
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access);
            virtual Type* GetSelfAsType() = 0;
            virtual Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() = 0;
            virtual std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) = 0;
        };        

        struct LLVMFieldIndex { unsigned index; };
        struct EmptyBaseOffset { unsigned offset; };
        typedef boost::variant<LLVMFieldIndex, EmptyBaseOffset> MemberLocation;

        struct ConstructorContext {
            struct member {
                member(Lexer::Range where)
                : location(where) {}
                member(const member&) = delete;
                member(member&& other) = default;
                Wide::Util::optional<std::string> name;
                Type* t;
                // Not necessarily actually an empty base but ClangType will only give us offsets pre-codegen.
                // and it could be an empty base so use offsets
                std::function<unsigned()> num;
                std::function<std::shared_ptr<Expression>(std::shared_ptr<Expression>)> InClassInitializer;
                Lexer::Range location;
            };
            virtual std::vector<member> GetConstructionMembers() = 0;
            virtual std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) = 0;
        };

        class PrimitiveType : public Type {
            std::unique_ptr<OverloadResolvable> CopyConstructor;
            std::unique_ptr<OverloadResolvable> ValueConstructor;
            std::unique_ptr<OverloadResolvable> MoveConstructor;
            std::unique_ptr<OverloadResolvable> AssignmentOperator;
            
        protected:
            PrimitiveType(Analyzer& a) : Type(a) {}
        public:
            bool IsMoveConstructible(Location) override final { return true; }
            bool IsCopyConstructible(Location) override final { return true; }
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access, OperatorAccess kind) override;
        };
        class MetaType : public PrimitiveType {
            std::unique_ptr<OverloadResolvable> DefaultConstructor;
        public:
            MetaType(Analyzer& a) : PrimitiveType(a) {}

            // NullType is an annoying special case needing to override these members for Reasons
            // nobody else should.
            llvm::Type* GetLLVMType(llvm::Module* module) override;
            std::size_t size() override;
            std::size_t alignment() override;
            bool IsConstant() override;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override final; 
            using Type::AccessMember;
        };
        
        llvm::Constant* GetGlobalString(std::string string, llvm::Module* m);
    }
}
