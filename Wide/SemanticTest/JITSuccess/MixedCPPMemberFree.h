struct test {
    bool operator==(int arg) const {
        return true;
    }
};
bool operator==(test lhs, const char* rhs) {
    return false;
}