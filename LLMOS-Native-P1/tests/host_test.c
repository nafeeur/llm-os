#include "llmos/core.h"
#include <stdio.h>

int main(void) {
    llmos_initialize(false);
    int status = llmos_run_selftests(true);
    if (status == 0) puts("host integration: PASS");
    else puts("host integration: FAIL");
    return status;
}
