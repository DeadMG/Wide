#include <Wide/Util/Driver/Warnings.h>
#include <Wide/Semantic/Warnings/UnusedFunction.h>
#include <Wide/Semantic/Warnings/NonvoidFalloff.h>
#include <Wide/Parser/AST.h>
#include <iostream>

void Wide::Driver::PrintUnusedFunctionsWarning(const std::unordered_set<std::string>& files, Semantic::Analyzer& a) {
    auto unused = Wide::Semantic::GetUnusedFunctions(a);
    for (auto func : unused) {
        if (files.find(*std::get<0>(func).begin.name) != files.end())
            std::cout << "Warning: Unused function " << std::get<1>(func) << " at " << to_string(std::get<0>(func)) << "\n";
    }
}
void Wide::Driver::PrintNonvoidFalloffWarning(const std::unordered_set<std::string>& files, Semantic::Analyzer& a) {
    auto unused = Wide::Semantic::GetNonvoidFalloffFunctions(a);
    for (auto func : unused) {
        if (files.find(*std::get<0>(func).begin.name) != files.end())
            std::cout << "Warning: Control flow may fall off end of non-void function " << std::get<1>(func) << "\n";
    }
}

/*
def warn_variables_not_in_loop_body : Warning<
"variable%select{s| %1|s %1 and %2|s %1, %2, and %3|s %1, %2, %3, and %4}0 "
"used in loop condition not modified in loop body">,
InGroup<LoopAnalysis>, DefaultIgnore;
def warn_redundant_loop_iteration : Warning<
"variable %0 is %select{decremented|incremented}1 both in the loop header "
"and in the loop body">,
InGroup<LoopAnalysis>, DefaultIgnore;
def note_loop_iteration_here : Note<"%select{decremented|incremented}0 here">;

def warn_duplicate_enum_values : Warning<
"element %0 has been implicitly assigned %1 which another element has "
"been assigned">, InGroup<DiagGroup<"duplicate-enum">>, DefaultIgnore;
def note_duplicate_element : Note<"element %0 also has value %1">;

def warn_float_overflow : Warning<
"magnitude of floating-point constant too large for type %0; maximum is %1">,
InGroup<LiteralRange>;
def warn_float_underflow : Warning<
"magnitude of floating-point constant too small for type %0; minimum is %1">,
InGroup<LiteralRange>;
def warn_double_const_requires_fp64 : Warning<
"double precision constant requires cl_khr_fp64, casting to single precision">;

def warn_unused_parameter : Warning<"unused parameter %0">,
InGroup<UnusedParameter>, DefaultIgnore;
def warn_unused_variable : Warning<"unused variable %0">,
InGroup<UnusedVariable>, DefaultIgnore;
def warn_unused_const_variable : Warning<"unused variable %0">,
InGroup<UnusedConstVariable>, DefaultIgnore;
def warn_unused_exception_param : Warning<"unused exception parameter %0">,
InGroup<UnusedExceptionParameter>, DefaultIgnore;
def warn_redefinition_in_param_list : Warning<
"redefinition of %0 will not be visible outside of this function">,
InGroup<Visibility>;
def warn_unused_function : Warning<"unused function %0">,
InGroup<UnusedFunction>, DefaultIgnore;
def warn_unused_member_function : Warning<"unused member function %0">,
InGroup<UnusedMemberFunction>, DefaultIgnore;
def warn_unneeded_member_function : Warning<
"member function %0 is not needed and will not be emitted">,
InGroup<UnneededMemberFunction>, DefaultIgnore;
def warn_unused_private_field: Warning<"private field %0 is not used">,
InGroup<UnusedPrivateField>, DefaultIgnore;

def warn_parameter_size: Warning< 32
"%0 is a large (%1 bytes) pass-by-value argument; "
"pass it by reference instead ?">, InGroup<LargeByValueCopy>;
def warn_return_value_size: Warning<
"return value of %0 is a large (%1 bytes) pass-by-value object; "
"pass it by reference instead ?">, InGroup<LargeByValueCopy>;
def warn_return_value_udt: Warning<
"%0 has C-linkage specified, but returns user-defined type %1 which is "
"incompatible with C">, InGroup<ReturnTypeCLinkage>;
def warn_return_value_udt_incomplete: Warning<
"%0 has C-linkage specified, but returns incomplete type %1 which could be "
"incompatible with C">, InGroup<ReturnTypeCLinkage>;

def warn_decl_shadow :
Warning<"declaration shadows a %select{"
"local variable|"
"variable in %2|"
"static data member of %2|"
"field of %2}1">,
InGroup<Shadow>, DefaultIgnore;

def err_maybe_falloff_nonvoid_block : Error<
"control may reach end of non-void block">;
def warn_suggest_noreturn_function : Warning<
"%select{function|method}0 %1 could be declared with attribute 'noreturn'">,
InGroup<MissingNoreturn>, DefaultIgnore;
def warn_unreachable : Warning<"will never be executed">,
InGroup<DiagGroup<"unreachable-code">>, DefaultIgnore;

/// Built-in functions.
def ext_implicit_lib_function_decl : ExtWarn<
"implicitly declaring library function '%0' with type %1">;
def note_please_include_header : Note<
"please include the header <%0> or explicitly provide a "
"declaration for '%1'">;
def note_previous_builtin_declaration : Note<"%0 is a builtin with type %1">;
def warn_implicit_decl_requires_stdio : Warning<
"declaration of built-in function '%0' requires inclusion of the header "
"<stdio.h>">,
InGroup<BuiltinRequiresHeader>;
def warn_implicit_decl_requires_setjmp : Warning<
"declaration of built-in function '%0' requires inclusion of the header "
"<setjmp.h>">,
InGroup<BuiltinRequiresHeader>;
def warn_implicit_decl_requires_ucontext : Warning<
"declaration of built-in function '%0' requires inclusion of the header "
"<ucontext.h>">,
InGroup<BuiltinRequiresHeader>;
def warn_redecl_library_builtin : Warning<
"incompatible redeclaration of library function %0">,
InGroup<DiagGroup<"incompatible-library-redeclaration">>;
def err_builtin_definition : Error<"definition of builtin function %0">;
def err_types_compatible_p_in_cplusplus : Error<
"__builtin_types_compatible_p is not valid in C++">;
def warn_builtin_unknown : Warning<"use of unknown builtin %0">,
InGroup<ImplicitFunctionDeclare>, DefaultError;
def warn_dyn_class_memaccess : Warning<
"%select{destination for|source of|first operand of|second operand of}0 this "
"%1 call is a pointer to dynamic class %2; vtable pointer will be "
"%select{overwritten|copied|moved|compared}3">,
InGroup<DiagGroup<"dynamic-class-memaccess">>;
def note_bad_memaccess_silence : Note<
"explicitly cast the pointer to silence this warning">;
def warn_sizeof_pointer_expr_memaccess : Warning<
"'%0' call operates on objects of type %1 while the size is based on a "
"different type %2">,
InGroup<SizeofPointerMemaccess>;
def warn_sizeof_pointer_expr_memaccess_note : Note<
"did you mean to %select{dereference the argument to 'sizeof' (and multiply "
"it by the number of elements)|remove the addressof in the argument to "
"'sizeof' (and multiply it by the number of elements)|provide an explicit "
"length}0?">;
def warn_sizeof_pointer_type_memaccess : Warning<
"argument to 'sizeof' in %0 call is the same pointer type %1 as the "
"%select{destination|source}2; expected %3 or an explicit length">,
InGroup<SizeofPointerMemaccess>;
def warn_strlcpycat_wrong_size : Warning<
"size argument in %0 call appears to be size of the source; expected the size of "
"the destination">,
InGroup<DiagGroup<"strlcpy-strlcat-size">>;
def note_strlcpycat_wrong_size : Note<
"change size argument to be the size of the destination">;

def warn_strncat_large_size : Warning<
"the value of the size argument in 'strncat' is too large, might lead to a "
"buffer overflow">, InGroup<StrncatSize>;
def warn_strncat_src_size : Warning<"size argument in 'strncat' call appears "
"to be size of the source">, InGroup<StrncatSize>;
def warn_strncat_wrong_size : Warning<
"the value of the size argument to 'strncat' is wrong">, InGroup<StrncatSize>;
def note_strncat_wrong_size : Note<
"change the argument to be the free space in the destination buffer minus "
"the terminating null byte">;

/// main()
// static main() is not an error in C, just in C++.
def warn_static_main : Warning<"'main' should not be declared static">,
InGroup<Main>;
def err_static_main : Error<"'main' is not allowed to be declared static">;
def err_inline_main : Error<"'main' is not allowed to be declared inline">;
def ext_noreturn_main : ExtWarn<
"'main' is not allowed to be declared _Noreturn">, InGroup<Main>;
def note_main_remove_noreturn : Note<"remove '_Noreturn'">;
def err_constexpr_main : Error<
"'main' is not allowed to be declared constexpr">;
def err_mainlike_template_decl : Error<"'%0' cannot be a template">;
def err_main_returns_nonint : Error<"'main' must return 'int'">;
def ext_main_returns_nonint : ExtWarn<"return type of 'main' is not 'int'">,
InGroup<MainReturnType>;
def note_main_change_return_type : Note<"change return type to 'int'">;
def err_main_surplus_args : Error<"too many parameters (%0) for 'main': "
"must be 0, 2, or 3">;
def warn_main_one_arg : Warning<"only one parameter on 'main' declaration">,
InGroup<Main>;
def err_main_arg_wrong : Error<"%select{first|second|third|fourth}0 "
"parameter of 'main' (%select{argument count|argument array|environment|"
"platform-specific data}0) must be of type %1">;

def warn_conflicting_overriding_ret_types : Warning<
"conflicting return type in "
"declaration of %0%diff{: $ vs $|}1,2">,
InGroup<OverridingMethodMismatch>, DefaultIgnore;

def warn_conflicting_ret_types : Warning<
"conflicting return type in "
"implementation of %0%diff{: $ vs $|}1,2">,
InGroup<MismatchedReturnTypes>;

def warn_non_covariant_overriding_ret_types : Warning<
"conflicting return type in "
"declaration of %0: %1 vs %2">,
InGroup<OverridingMethodMismatch>, DefaultIgnore;

def warn_non_covariant_ret_types : Warning<
"conflicting return type in "
"implementation of %0: %1 vs %2">,
InGroup<MethodSignatures>, DefaultIgnore;

def warn_conflicting_overriding_param_types : Warning<
"conflicting parameter types in "
"declaration of %0%diff{: $ vs $|}1,2">,
InGroup<OverridingMethodMismatch>, DefaultIgnore;

def warn_conflicting_param_types : Warning<
"conflicting parameter types in "
"implementation of %0%diff{: $ vs $|}1,2">,
InGroup<MismatchedParameterTypes>;

def warn_conflicting_param_modifiers : Warning<
"conflicting distributed object modifiers on parameter type "
"in implementation of %0">,
InGroup<DistributedObjectModifiers>;

def warn_conflicting_overriding_param_modifiers : Warning<
"conflicting distributed object modifiers on parameter type "
"in declaration of %0">,
InGroup<OverridingMethodMismatch>, DefaultIgnore;

def warn_non_contravariant_overriding_param_types : Warning<
"conflicting parameter types in "
"declaration of %0: %1 vs %2">,
InGroup<OverridingMethodMismatch>, DefaultIgnore;

def warn_non_contravariant_param_types : Warning<
"conflicting parameter types in "
"implementation of %0: %1 vs %2">,
InGroup<MethodSignatures>, DefaultIgnore;

def warn_conflicting_overriding_variadic :Warning<
"conflicting variadic declaration of method and its "
"implementation">,
InGroup<OverridingMethodMismatch>, DefaultIgnore;

def err_abstract_type_in_decl : Error<
"%select{return|parameter|variable|field|instance variable|"
"synthesized instance variable}0 type %1 is an abstract class">;
def err_allocation_of_abstract_type : Error<
"allocating an object of abstract class type %0">;
def err_throw_abstract_type : Error<
"cannot throw an object of abstract type %0">;
def err_array_of_abstract_type : Error<"array of abstract class type %0">;
def err_capture_of_abstract_type : Error<
"by-copy capture of value of abstract type %0">;

// C++ class members
def warn_call_to_pure_virtual_member_function_from_ctor_dtor : Warning<
"call to pure virtual member function %0; overrides of %0 in subclasses are "
"not available in the %select{constructor|destructor}1 of %2">;

def err_illegal_union_or_anon_struct_member : Error<
"%select{anonymous struct|union}0 member %1 has a non-trivial "
"%select{constructor|copy constructor|move constructor|copy assignment "
"operator|move assignment operator|destructor}2">;
def warn_cxx98_compat_nontrivial_union_or_anon_struct_member : Warning<
"%select{anonymous struct|union}0 member %1 with a non-trivial "
"%select{constructor|copy constructor|move constructor|copy assignment "
"operator|move assignment operator|destructor}2 is incompatible with C++98">,
InGroup<CXX98Compat>, DefaultIgnore;

// C++ constructors
def err_constructor_cannot_be : Error<"constructor cannot be declared '%0'">;
def err_invalid_qualified_constructor : Error<
"'%0' qualifier is not allowed on a constructor">;
def err_ref_qualifier_constructor : Error<
"ref-qualifier '%select{&&|&}0' is not allowed on a constructor">;

def err_constructor_return_type : Error<
"constructor cannot have a return type">;
def err_constructor_redeclared : Error<"constructor cannot be redeclared">;
def err_constructor_byvalue_arg : Error<
"copy constructor must pass its first argument by reference">;
def warn_no_constructor_for_refconst : Warning<
"%select{struct|interface|union|class|enum}0 %1 does not declare any "
"constructor to initialize its non-modifiable members">;
def note_refconst_member_not_initialized : Note<
"%select{const|reference}0 member %1 will never be initialized">;
def ext_ms_explicit_constructor_call : ExtWarn<
"explicit constructor calls are a Microsoft extension">, InGroup<Microsoft>;

// C++ destructors
def err_invalid_qualified_destructor : Error<
"'%0' qualifier is not allowed on a destructor">;
def err_ref_qualifier_destructor : Error<
"ref-qualifier '%select{&&|&}0' is not allowed on a destructor">;
def err_destructor_return_type : Error<"destructor cannot have a return type">;
def err_destructor_redeclared : Error<"destructor cannot be redeclared">;
def err_destructor_with_params : Error<"destructor cannot have any parameters">;

def err_destructor_template : Error<
"destructor cannot be declared as a template">;
*/