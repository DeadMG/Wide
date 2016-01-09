struct allocator {
};
struct string {
    bool b;
    string(bool arg, allocator a = allocator())
        : b(arg) {}
};
bool f(string s = true) {
    return s.b;
}