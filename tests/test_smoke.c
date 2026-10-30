#include "t.h"

static void test_harness_works(void) {
    T_NEAR(1.0f, 1.0f + 1e-7f, 1e-5f);
}

int main(void) {
    T_RUN(test_harness_works);
    return T_DONE();
}
