struct test {
    bool* p;
    test(bool& var) : p(&var) {}
    ~test();
};