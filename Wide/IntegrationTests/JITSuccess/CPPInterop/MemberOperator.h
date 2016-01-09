struct test {
    bool operator==(const test& other) const;
};
bool test::operator==(const test& other) const { return true; }