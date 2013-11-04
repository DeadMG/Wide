namespace ClangADL {
    struct test {};
    void operator==(decltype(nullptr) lhs, test rhs) {}
}