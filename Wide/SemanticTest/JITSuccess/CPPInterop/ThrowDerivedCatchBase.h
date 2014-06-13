struct base {
    virtual bool g() { return false; }
    virtual ~base() {}
};
void f(bool&);
bool g(bool& arg) {
    try {
        f(arg);
        return false;
    }
    catch (base& b) {
        return b.g();
    }
}
