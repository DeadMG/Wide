struct inner {
    bool b;
};
struct outer {
    inner i;
    outer() { i.b = true; }
    inner& operator*() { return i; }
};