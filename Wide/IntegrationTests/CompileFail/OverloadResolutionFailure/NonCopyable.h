struct test {
    test(test&&) = default;
    test(const test&) = delete;
    test() {}
};