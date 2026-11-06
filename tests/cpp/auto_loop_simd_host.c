#include <stdint.h>
#include <stdio.h>
extern uint64_t simd_loop(void);
int main(void){
    const uint64_t sum=simd_loop();
    if(sum!=6400000ULL){
        fprintf(stderr,"loop SIMD wrong output: %llu\n",
                (unsigned long long)sum);
        return 1;
    }
    printf("%llu\n",(unsigned long long)sum);
    return 0;
}
