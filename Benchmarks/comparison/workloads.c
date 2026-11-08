#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static double get_time_ms(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart * 1000.0;
}
#else
#include <time.h>
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
#endif

static int compare_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

// Wrapping unsigned arithmetic is defined in C, Go and Noqeri.
static uint64_t run_arithmetic_checksum(void) {
    uint64_t acc=0;
    for(uint64_t i=0;i<5000000u;i++)
        acc=acc*UINT64_C(1664525)+i*UINT64_C(1013904223)+UINT64_C(17);
    return acc;
}
static void arithmetic_checksum(void) {
    printf("%" PRIu64 "\n",run_arithmetic_checksum());
}

static uint64_t run_modulo(void) {
    uint64_t acc=0;
    for(uint64_t i=0;i<5000000u;i++)
        acc=(acc+((i*17u+13u)%97u))%1000000007u;
    return acc;
}
static void modulo(void) {
    printf("%" PRIu64 "\n", run_modulo());
}

static void run_matrix(int64_t* out0, int64_t* out255) {
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
    *out0 = c[0];
    *out255 = c[255];
}
static void matrix(void) {
    int64_t o0 = 0, o255 = 0;
    run_matrix(&o0, &o255);
    printf("%" PRId64 "\n%" PRId64 "\n", o0, o255);
}

static void run_sorting_stable(int64_t* out0, int64_t* out255) {
    int64_t values[256];
    for(size_t i=0;i<256;i++)
        values[i]=(int64_t)((i*7919u+104729u)%65521u);
    for(size_t round=0;round<200;round++){
        for(size_t a=1;a<256;a++){
            int64_t key=values[a];
            size_t b=a;
            while(b>0&&values[b-1]>key){
                values[b]=values[b-1];
                b--;
            }
            values[b]=key;
        }
    }
    *out0=values[0];
    *out255=values[255];
}
static void sorting_stable(void) {
    int64_t o0=0,o255=0;
    run_sorting_stable(&o0,&o255);
    printf("%" PRId64 "\n%" PRId64 "\n",o0,o255);
}

static uint64_t run_reshuffled(void) {
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
    return checksum;
}
static void reshuffled(void) {
    printf("%" PRIu64 "\n", run_reshuffled());
}

int main(int argc,char**argv) {
    if(argc<2)return 2;
    if(strcmp(argv[1],"--bench")==0){
        if(argc<3)return 2;
        const char* workload = argv[2];
        int samples = 25;
        if(argc>3) samples = atoi(argv[3]);
        if(samples<=0) samples = 25;

        double* timings = (double*)malloc(sizeof(double)*samples);
        char output_buf[256] = {0};

        if(strcmp(workload,"arithmetic_checksum")==0){
            for(int w=0;w<3;w++) run_arithmetic_checksum();
            for(int sample=0;sample<samples;sample++){
                double t0=get_time_ms();
                uint64_t res=run_arithmetic_checksum();
                double t1=get_time_ms();
                timings[sample]=t1-t0;
                snprintf(output_buf,sizeof(output_buf),"%" PRIu64,res);
            }
        }else if(strcmp(workload,"modulo")==0){
            for(int w=0;w<3;w++) run_modulo();
            for(int s=0;s<samples;s++){
                double t0 = get_time_ms();
                uint64_t res = run_modulo();
                double t1 = get_time_ms();
                timings[s] = t1 - t0;
                snprintf(output_buf, sizeof(output_buf), "%" PRIu64, res);
            }
        }else if(strcmp(workload,"matrix")==0){
            int64_t o0=0, o255=0;
            for(int w=0;w<3;w++) run_matrix(&o0, &o255);
            for(int s=0;s<samples;s++){
                double t0 = get_time_ms();
                run_matrix(&o0, &o255);
                double t1 = get_time_ms();
                timings[s] = t1 - t0;
                snprintf(output_buf, sizeof(output_buf), "%" PRId64 " %" PRId64, o0, o255);
            }
        }else if(strcmp(workload,"sorting_stable")==0){
            int64_t o0=0,o255=0;
            for(int w=0;w<3;w++) run_sorting_stable(&o0,&o255);
            for(int sample=0;sample<samples;sample++){
                double t0=get_time_ms();
                run_sorting_stable(&o0,&o255);
                double t1=get_time_ms();
                timings[sample]=t1-t0;
                snprintf(output_buf,sizeof(output_buf),"%" PRId64 " %" PRId64,o0,o255);
            }
        }else if(strcmp(workload,"sorting_reshuffled")==0){
            for(int w=0;w<3;w++) run_reshuffled();
            for(int s=0;s<samples;s++){
                double t0 = get_time_ms();
                uint64_t res = run_reshuffled();
                double t1 = get_time_ms();
                timings[s] = t1 - t0;
                snprintf(output_buf, sizeof(output_buf), "%" PRIu64, res);
            }
        }else{
            free(timings);
            return 2;
        }

        qsort(timings, samples, sizeof(double), compare_double);
        double median = timings[samples / 2];
        double p95 = timings[(size_t)(samples * 0.95)];
        double min_t = timings[0];
        double max_t = timings[samples - 1];

        printf("samples=%d median=%.2f ms p95=%.2f ms min=%.2f ms max=%.2f ms\n",
               samples, median, p95, min_t, max_t);
        printf("output=%s\n", output_buf);
        free(timings);
        return 0;
    }
    if(strcmp(argv[1],"arithmetic_checksum")==0){arithmetic_checksum();return 0;}
    if(strcmp(argv[1],"modulo")==0){modulo();return 0;}
    if(strcmp(argv[1],"matrix")==0){matrix();return 0;}
    if(strcmp(argv[1],"sorting_stable")==0){sorting_stable();return 0;}
    if(strcmp(argv[1],"sorting_reshuffled")==0){reshuffled();return 0;}
    return 2;
}
