struct allocator {
};
struct string {
    const char* p;
    string(const char* x, allocator a = allocator())
        : p(x) {}
};
bool f(const string& s = "lollerskates") {
    return true;
}