#include <stdint.h>
#include <stdio.h>
extern uint64_t noqeri_entry(void*);
extern uint64_t simd_sub(void);
int main(void){
    const uint64_t sum=noqeri_entry(0);
    const uint64_t difference=simd_sub();
    if(sum!=37u||difference!=39u){
        fprintf(stderr,"SIMD result mismatch: %llu %llu\n",
                (unsigned long long)sum,(unsigned long long)difference);
        return 1;
    }
    return 0;
}
