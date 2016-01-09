struct test {
    bool operator==(int arg) const;
};
bool operator==(test lhs, const char* rhs) {
    return false;
}
bool test::operator==(int arg) const {
    return true;
}