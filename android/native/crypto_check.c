#include "mbedtls/aes.h"

int lab_aes_selftest(void) {
    return mbedtls_aes_self_test(1);
}
