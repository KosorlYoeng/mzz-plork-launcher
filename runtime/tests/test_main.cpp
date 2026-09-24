#include "TestFramework.h"

// Individual TEST_CASE definitions live in the other .cpp files in this
// directory; they self-register via static initializers, so this file
// only needs to run the registry.
int main() {
    return mzzplork::test::RunAll();
}
