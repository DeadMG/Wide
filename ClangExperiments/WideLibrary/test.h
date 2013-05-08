#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <tuple>
#include <set>
#include <deque>

template<typename T> struct add_rvalue_reference {
    typedef T&& result;
};