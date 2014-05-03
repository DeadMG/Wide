struct x {
    virtual bool f() { return false; }
};
bool f(x&& p) {
    return p.f();
}