namespace ClangADL {
    struct test {};
    bool operator==(decltype(nullptr) lhs, test rhs) { return false; }
}