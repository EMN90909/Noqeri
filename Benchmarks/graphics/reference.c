// Equivalent C loops to Lib/std/render2d.noqeri, matched input and host.
#include <stddef.h>
#include <stdint.h>
extern uint32_t* nq_bench_framebuffer(void);
extern size_t nq_bench_width(void);
extern size_t nq_bench_height(void);
extern size_t nq_bench_frames(void);
extern int nq_bench_mode(void);
extern void nq_bench_clock_start(void);
extern void nq_bench_clock_stop(void);
extern void nq_bench_frame_marker(uint32_t* pixels, size_t count);

static uint32_t fractal_pixel(size_t x,size_t y,size_t width,size_t height) {
    int64_t cx=((int64_t)x*3072)/(int64_t)width-2048;
    int64_t cy=((int64_t)y*2458)/(int64_t)height-1229;
    int64_t zx=0,zy=0,step=0;
    while(step<32 && zx*zx+zy*zy<=4194304) {
        int64_t next_x=(zx*zx-zy*zy)/1024+cx;
        int64_t next_y=(2*zx*zy)/1024+cy;
        zx=next_x;zy=next_y;step++;
    }
    if(step==32)return 0xff000000u;
    uint32_t red=(uint32_t)((step*9)%256);
    uint32_t green=(uint32_t)((step*5)%256);
    uint32_t blue=(uint32_t)((step*13)%256);
    return 0xff000000u+red*65536u+green*256u+blue;
}

int c_render_entry(const void* ignored) {
    (void)ignored;
    size_t width=nq_bench_width(), height=nq_bench_height();
    size_t count=width*height, frames=nq_bench_frames();
    int mode=nq_bench_mode();
    uint32_t* pixels=nq_bench_framebuffer();
    nq_bench_clock_start();
    for(size_t f=0;f<frames;f++){
        for(size_t i=0;i<count;i++){
            size_t x=i%width,y=i/width;
            if(mode==2){
                pixels[i]=fractal_pixel(x,y,width,height);
            }else if(mode==0){
                uint32_t red=width>1?(uint32_t)((x*255)/(width-1)):0;
                uint32_t green=height>1?(uint32_t)((y*255)/(height-1)):0;
                pixels[i]=0xff000000u+
                    (red%256u)*65536u+(green%256u)*256u+64u;
            }else{
                size_t pattern=((x/16)+(y/16))%2;
                pixels[i]=pattern==0?0xff123456u:0xfffedcbau;
            }
        }
        nq_bench_frame_marker(pixels,count);
    }
    nq_bench_clock_stop();
    return 0;
}
