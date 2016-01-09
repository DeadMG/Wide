struct test {
    virtual bool f() { return false; }
};
bool g(test& t) {
    return t.f();
}