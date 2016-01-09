void f();
bool g() {
    try {
        f();
        return false;
    } catch (int) {
        return true;
    }
}