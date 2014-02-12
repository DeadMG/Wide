struct test {
    bool f();
    bool g() {
        return f();
    }
};