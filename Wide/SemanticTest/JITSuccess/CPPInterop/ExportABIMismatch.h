struct test {};
bool f(bool b, test);
bool g() {
    return f(true, test());
}