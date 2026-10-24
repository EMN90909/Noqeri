#include <stdint.h>
#include <stdio.h>
extern uint64_t phi_jump_true(void);
extern uint64_t phi_jump_false(void);
extern uint64_t phi_fall_true(void);
extern uint64_t phi_fall_false(void);
extern uint64_t phi_swap_one(void);
extern uint64_t phi_swap_two(void);
int main(void){
    uint64_t results[]={
        phi_jump_true(),phi_jump_false(),
        phi_fall_true(),phi_fall_false(),
        phi_swap_one(),phi_swap_two()
    };
    const uint64_t expected[]={12,7,7,12,210,120};
    for(unsigned i=0;i<6;++i){
        if(results[i]!=expected[i]){
            fprintf(stderr,"Phi edge mismatch %u: %llu instead of %llu\n",
                    i,(unsigned long long)results[i],
                    (unsigned long long)expected[i]);
            return 1;
        }
    }
    return 0;
}
