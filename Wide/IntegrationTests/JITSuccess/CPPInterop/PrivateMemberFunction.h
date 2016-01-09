struct test {
private:
    bool f(decltype(nullptr) null) { return false; }
public:
    bool f(int* p) { return true; }
};