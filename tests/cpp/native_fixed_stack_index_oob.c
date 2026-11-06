#include <stdint.h>
extern int64_t direct_fixed_array(uint64_t index);
int main(void) {
    volatile uint64_t outside=256;
    (void)direct_fixed_array(outside);
    return 0;
}
