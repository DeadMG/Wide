struct x {
    virtual bool f() { return false; }
};
struct y : public x {
    virtual bool f() { return true; }
};
bool f(x&& x);
bool g() {
    return f(y());
}