void f() {
    throw "hi";
}

void g() {
    try {
        f();
    } catch(const char*) {
    }
}