#include "Windows.h"

HANDLE GetStdout() {
    return GetStdHandle(STD_OUTPUT_HANDLE);
}