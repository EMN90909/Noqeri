#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void modulo(void) {
    uint64_t acc=0;
    for(uint64_t i=0;i<5000000u;i++)
        acc=(acc+((i*17u+13u)%97u))%1000000007u;
    printf("%" PRIu64 "\n",acc);
}
static void matrix(void) {
    int64_t a[256],b[256],c[256];
    for(size_t i=0;i<256;i++){
        a[i]=(int64_t)((i*17u+3u)%97u);
        b[i]=(int64_t)((i*29u+11u)%89u);
        c[i]=0;
    }
    for(size_t pass=0;pass<200;pass++)
        for(size_t row=0;row<16;row++)
            for(size_t col=0;col<16;col++){
                int64_t sum=0;
                for(size_t k=0;k<16;k++)
                    sum+=a[row*16+k]*b[k*16+col];
                c[row*16+col]=sum;
            }
    printf("%" PRId64 "\n%" PRId64 "\n",c[0],c[255]);
}
static void reshuffled(void) {
    int64_t values[256];
    uint64_t checksum=0;
    for(size_t round=0;round<200;round++){
        for(size_t i=0;i<256;i++)
            values[i]=(int64_t)((i*7919u+(round+1u)*104729u)%65521u);
        for(size_t a=1;a<256;a++){
            int64_t key=values[a];
            size_t b=a;
            while(b>0&&values[b-1]>key){
                values[b]=values[b-1];b--;
            }
            values[b]=key;
        }
        checksum+=(uint64_t)(values[0]+values[255]);
    }
    printf("%" PRIu64 "\n",checksum);
}
int main(int argc,char**argv) {
    if(argc!=2)return 2;
    if(strcmp(argv[1],"modulo")==0){modulo();return 0;}
    if(strcmp(argv[1],"matrix")==0){matrix();return 0;}
    if(strcmp(argv[1],"sorting_reshuffled")==0){reshuffled();return 0;}
    return 2;
}
