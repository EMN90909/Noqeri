#include <stdint.h>
#include <stdio.h>
extern int64_t direct_fixed_array(uint64_t index);
extern int64_t rebound_array(uint64_t index);
extern int64_t address_taken_array(uint64_t index);
int main(void) {
    static const uint64_t indices[]={0,1,127,255};
    for(size_t i=0;i<sizeof(indices)/sizeof(indices[0]);++i){
        const uint64_t n=indices[i];
        if(direct_fixed_array(n)!=42||
           rebound_array(n)!=42||
           address_taken_array(n)!=42){
            fprintf(stderr,"fixed stack index failed at %llu\n",
                    (unsigned long long)n);
            return 1;
        }
    }
    puts("indexed-array-ok");
    return 0;
}
