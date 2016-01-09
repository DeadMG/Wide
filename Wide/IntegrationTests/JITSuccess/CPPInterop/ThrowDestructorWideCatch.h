#pragma once

struct t {
    bool val;
    t() : val(true) {}
    ~t() { val = false; }
    bool f() { return val; }
};

f() {
    throw t();
}