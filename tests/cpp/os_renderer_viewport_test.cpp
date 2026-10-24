#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/os_renderer_sdl.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
namespace {
void fit(unsigned sw,unsigned sh,unsigned ow,unsigned oh,int mode,
         int x,int y,int w,int h){
    nq_os_renderer_viewport result{-1,-1,-1,-1};
    assert(nq_os_renderer_fit_viewport(sw,sh,ow,oh,mode,&result)==0);
    assert(result.x==x&&result.y==y&&result.width==w&&result.height==h);
}
}
int main(){
    // 16:9 must never stretch to 4:3.
    fit(640,360,1280,900,0,0,90,1280,720);
    fit(640,360,800,600,0,0,75,800,450);
    fit(640,360,800,600,1,0,75,800,450);
    fit(640,360,800,600,2,0,75,800,450);
    fit(320,200,960,640,0,0,20,960,600);
    fit(4,3,8,9,0,0,1,8,6);
    fit(4096,4096,1,1,0,0,0,1,1);
    fit(640,360,1280,900,2,0,90,1280,720);
    nq_os_renderer_viewport out{3,4,5,6};
    assert(nq_os_renderer_fit_viewport(0,360,640,360,0,&out)==-EINVAL);
    assert(nq_os_renderer_fit_viewport(640,0,640,360,0,&out)==-EINVAL);
    assert(nq_os_renderer_fit_viewport(640,360,0,360,0,&out)==-EINVAL);
    assert(nq_os_renderer_fit_viewport(640,360,640,0,0,&out)==-EINVAL);
    assert(nq_os_renderer_fit_viewport(4097,360,640,360,0,&out)==-EINVAL);
    assert(nq_os_renderer_fit_viewport(640,360,16385,360,0,&out)==-EINVAL);
    assert(nq_os_renderer_fit_viewport(640,360,640,360,3,&out)==-EINVAL);
    assert(nq_os_renderer_fit_viewport(640,360,640,360,0,nullptr)==-EINVAL);
    assert(out.x==3&&out.y==4&&out.width==5&&out.height==6);
    assert(nq_os_renderer_set_scale_mode(0)==0);
    assert(nq_os_renderer_set_scale_mode(1)==0);
    assert(nq_os_renderer_set_scale_mode(2)==0);
    assert(nq_os_renderer_set_scale_mode(3)==-EINVAL);
    assert(nq_os_renderer_close()==0);
    std::puts("Noqeri SDL viewport, crisp scaling and invalid-input guards: PASS");
}
