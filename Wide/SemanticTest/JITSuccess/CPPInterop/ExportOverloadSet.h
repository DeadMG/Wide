bool f();
bool f(int x) {
    if (x == 2)
        return f();
    return false;
}