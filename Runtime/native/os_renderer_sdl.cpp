// Thin SDL2 OS/window bridge. Rasterization and frame orchestration are
// authored in Lib/std/render2d.noqeri and examples/graphics_demo.noqeri.
// SDL2 is an external host dependency, not a rewritten Noqeri GPU driver.
#include "noqeri/os_renderer_sdl.h"
#include <SDL.h>
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {
SDL_Window*window=nullptr;
SDL_Renderer*renderer=nullptr;
SDL_Texture*texture=nullptr;
uint32_t first_pixel=0,last_pixel=0;
uint32_t width=0,height=0;
bool presented=false;
bool video_owned=false;
int scale_mode=0;
void applyTextureFiltering(){
#if SDL_VERSION_ATLEAST(2,0,12)
    if(texture)SDL_SetTextureScaleMode(texture,
        scale_mode==1 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
#endif
}
void cleanup(){
    if(texture){SDL_DestroyTexture(texture);texture=nullptr;}
    if(renderer){SDL_DestroyRenderer(renderer);renderer=nullptr;}
    if(window){SDL_DestroyWindow(window);window=nullptr;}
    if(video_owned){SDL_QuitSubSystem(SDL_INIT_VIDEO);video_owned=false;}
    width=0;height=0;
}
}
extern "C" {
int nq_os_renderer_fit_viewport(uint32_t sw,uint32_t sh,
    uint32_t ow,uint32_t oh,int mode,nq_os_renderer_viewport*result){
    if(!result || !sw || !sh || !ow || !oh ||
       sw>4096 || sh>4096 || ow>16384 || oh>16384 ||
       mode<0 || mode>2)return -EINVAL;
    uint64_t dw=0,dh=0;
    const uint64_t scale=std::min<uint64_t>(ow/sw,oh/sh);
    if(mode==0 && scale>=1){
        dw=uint64_t(sw)*scale;
        dh=uint64_t(sh)*scale;
    }else if(uint64_t(ow)*sh<=uint64_t(oh)*sw){
        dw=ow;
        dh=std::max<uint64_t>(1,uint64_t(ow)*sh/sw);
    }else{
        dh=oh;
        dw=std::max<uint64_t>(1,uint64_t(oh)*sw/sh);
    }
    result->width=static_cast<int>(dw);
    result->height=static_cast<int>(dh);
    result->x=static_cast<int>((ow-dw)/2);
    result->y=static_cast<int>((oh-dh)/2);
    return 0;
}
int nq_os_renderer_set_scale_mode(int mode){
    if(mode<0||mode>2)return -EINVAL;
    scale_mode=mode;
    applyTextureFiltering();
    return 0;
}
int nq_os_renderer_present(const uint32_t*argb,size_t pixel_count,uint32_t w,uint32_t h){
    if(!argb||!w||!h||w>4096||h>4096)return -EINVAL;
    if(static_cast<size_t>(w)*static_cast<size_t>(h)>pixel_count)return -ERANGE;
    if(w>static_cast<uint32_t>(std::numeric_limits<int>::max()/4))return -ERANGE;
    if(!window){
        if((SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO)==0){
            if(SDL_InitSubSystem(SDL_INIT_VIDEO)!=0)return -ENODEV;
            video_owned=true;
        }
        window=SDL_CreateWindow("Noqeri Render2D", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,static_cast<int>(w),static_cast<int>(h),
            SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
        if(!window){cleanup();return -ENODEV;}
        // Prefer GPU compositing. Keep nearest as the initial default to
        // avoid an unintended blurry upscale on new SDL textures.
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                    scale_mode==1 ? "linear" : "nearest");
        renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
        if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(!renderer){cleanup();return -ENODEV;}
        texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,static_cast<int>(w),static_cast<int>(h));
        if(!texture){cleanup();return -ENODEV;}
        applyTextureFiltering();
        width=w;height=h;
    }
    if(w!=width||h!=height)return -EINVAL;
    if(SDL_UpdateTexture(texture,nullptr,argb,static_cast<int>(w)*4)!=0)return -EIO;
    int outputWidth=0,outputHeight=0;
    if(SDL_GetRendererOutputSize(renderer,&outputWidth,&outputHeight)!=0 ||
       outputWidth<1 || outputHeight<1)return -EIO;
    nq_os_renderer_viewport fitted{};
    if(nq_os_renderer_fit_viewport(w,h,static_cast<uint32_t>(outputWidth),
          static_cast<uint32_t>(outputHeight),scale_mode,&fitted)!=0)return -ERANGE;
    const SDL_Rect destination{fitted.x,fitted.y,fitted.width,fitted.height};
    // Black letterboxing eliminates stretched aspect ratios on resize.
    if(SDL_SetRenderDrawColor(renderer,0,0,0,255)!=0 ||
       SDL_RenderClear(renderer)!=0 ||
       SDL_RenderCopy(renderer,texture,nullptr,&destination)!=0)return -EIO;
    SDL_RenderPresent(renderer);
    first_pixel=argb[0];last_pixel=argb[static_cast<size_t>(w)*h-1];
    presented=true;
    return 0;
}
int nq_os_renderer_pump(uint32_t duration_ms){
    if(!window)return -ENODEV;
    if(duration_ms>10000)return -EINVAL;
    const uint32_t start=SDL_GetTicks();
    SDL_Event event{};
    bool open=true;
    while(open && SDL_GetTicks()-start<duration_ms){
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_QUIT)open=false;
        }
        SDL_Delay(5);
    }
    return 0;
}
int nq_os_renderer_close(){cleanup();return 0;}
}
#ifdef NOQERI_OS_RENDERER_TEST_APP
extern "C" int noqeri_entry(const void* abi);
int main(){
    const int rc=noqeri_entry(nullptr);
    if(rc!=0||!presented||first_pixel!=0xff000040u||
       last_pixel!=0xffffff40u){
        std::fputs("Noqeri SDL2 OS renderer test failed\n",stderr);
        cleanup();return 1;
    }
    std::puts("Noqeri SDL2 OS renderer: native Noqeri pixels presented via SDL2: PASS");
    cleanup();
    return 0;
}

#endif // NOQERI_OS_RENDERER_TEST_APP
