void f(bool&);
bool g(bool& arg) {
    try {
        f(arg);
        return false;
    } catch (int i) {
        return true;
    }
}