struct x {
    virtual void immaterial() {}
};
struct z {
    virtual bool f() { return false; }
    virtual bool g() { return true; }
};
struct y : public x, public z {
    virtual bool f() { return true; }
};