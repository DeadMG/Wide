#include <Wide/Semantic/Module.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)

#include <clang/AST/Type.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Module::Module(AST::Module* p) 
    : m(p) {}

void Module::AddSpecialMember(std::string name, Expression t){
    SpecialMembers[std::move(name)] = t;
}
Wide::Util::optional<Expression> Module::AccessMember(Expression val, std::string name, Analyzer& a) {
    if (m->decls.find(name) != m->decls.end()) {
        auto decl = m->decls[name];
        if (auto moddecl = dynamic_cast<AST::Module*>(decl)) {
            return a.GetWideModule(moddecl)->BuildValueConstruction(a);
        }
        if (auto usedecl = dynamic_cast<AST::Using*>(decl)) {
            auto expr = a.AnalyzeExpression(this, usedecl->expr);
            if (auto conty = dynamic_cast<ConstructorType*>(expr.t->Decay())) {
                return conty->BuildValueConstruction(a);
            }
            if (auto fun = dynamic_cast<OverloadSet*>(expr.t->Decay())) {
				if (m->higher) {
					auto decl = a.GetDeclContext(m->higher)->AccessMember(val, name, a);
					if (!decl) return expr;
					if (auto overset = dynamic_cast<OverloadSet*>(decl->t->Decay())) {
						return a.GetOverloadSet(fun, overset)->BuildValueConstruction(a);
					}
				}
                return expr;
			}
			if (auto temp = dynamic_cast<ClangTemplateClass*>(expr.t->Decay()))
                return expr;
			if (a.IsLvalueType(expr.t))
                return expr;
			if (auto nam = dynamic_cast<ClangNamespace*>(expr.t->Decay()))
                return expr;
			if (auto mod = dynamic_cast<Module*>(expr.t->Decay()))
                return expr;
            throw std::runtime_error("Attempted to using something that was not a type, template, module, or function");
        }
        if (auto overdecl = dynamic_cast<AST::FunctionOverloadSet*>(decl)) {
			if (m->higher)
				if (auto decl = a.GetDeclContext(m->higher)->AccessMember(val, name, a))
					if (auto overset = dynamic_cast<OverloadSet*>(decl->t->Decay()))
						return a.GetOverloadSet(a.GetOverloadSet(overdecl), overset)->BuildValueConstruction(a);
            return a.GetOverloadSet(overdecl)->BuildValueConstruction(a);          
        }
        if (auto tydecl = dynamic_cast<AST::Type*>(decl)) {
            return a.GetConstructorType(a.GetUDT(tydecl, this))->BuildValueConstruction(a);
        }
        throw std::runtime_error("Attempted to access a member of a Wide module, but did not recognize it as a using, a type, or a function.");
    }
    if (SpecialMembers.find(name) != SpecialMembers.end()) {
        return SpecialMembers[name];
    }
    if (m->higher)
        return a.GetDeclContext(m->higher)->AccessMember(val, name, a);
	return Wide::Util::none;
}

AST::DeclContext* Module::GetDeclContext() {
    return m;
}