// Native Noqeri 3D CPU raster differential test. This C++ file is ONLY a
// reference oracle and driver. Geometry/raster implementation is in Noqeri.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

template<typename T> struct Slice { T* data; std::size_t length; };
extern "C" std::uint64_t nqProjectVertex3D(std::int64_t,std::int64_t,std::int64_t,
                                          std::size_t,std::size_t,Slice<std::int64_t>*);
extern "C" std::uint64_t nqDepthClear3D(Slice<std::uint32_t>*,std::size_t,std::size_t);
extern "C" std::uint64_t nqRasterTriangle3D(Slice<std::uint32_t>*,Slice<std::uint32_t>*,
                                          std::size_t,std::size_t,Slice<std::int64_t>*,std::uint32_t);

namespace {
using I=std::int64_t;
using U=std::uint32_t;
constexpr U guard=0x9ac0f0d1u, initial=0x12040506u;
void require(bool condition,const std::string& msg) {
    if(!condition)throw std::runtime_error(msg);
}
bool referenceProject(I x,I y,I z,std::size_t w,std::size_t h,std::vector<I>& out,std::size_t n) {
    if(w==0||h==0||w>4096||h>4096||n<3||z<1||z>65535||
        x< -262144||x>262144||y< -262144||y>262144)return false;
    I focal=static_cast<I>(std::min(w,h)/2);
    I sx=static_cast<I>(w/2)+x*focal/z;
    I sy=static_cast<I>(h/2)-y*focal/z;
    if(sx< -8192||sx>8192||sy< -8192||sy>8192)return false;
    out[0]=sx;out[1]=sy;out[2]=z;
    return true;
}
bool referenceDepth(std::vector<U>& data,std::size_t n,std::size_t w,std::size_t h) {
    if(!w||!h||w>4096||h>4096||h>n/w)return false;
    std::fill(data.begin(),data.begin()+static_cast<std::ptrdiff_t>(w*h),
              std::numeric_limits<U>::max());
    return true;
}
bool referenceTriangle(std::vector<U>& pixels,std::vector<U>& depth,
                       std::size_t plen,std::size_t dlen,std::size_t w,std::size_t h,
                       const std::array<I,9>& v,std::size_t vn,U color) {
    if(!w||!h||w>4096||h>4096||vn<9||h>plen/w||h>dlen/w)return false;
    for(int a=0;a<3;++a) {
        if(v[a*3]< -8192||v[a*3]>8192||
           v[a*3+1]< -8192||v[a*3+1]>8192||
           v[a*3+2]<1||v[a*3+2]>65535)return false;
    }
    const auto edge=[](I ax,I ay,I bx,I by,I px,I py) {
        return (px-ax)*(by-ay)-(py-ay)*(bx-ax);
    };
    const I area=edge(v[0],v[1],v[3],v[4],v[6],v[7]);
    if(!area)return true;
    // Deliberately independent per-pixel whole-surface coverage, no
    // incremental edge or depth arithmetic shared with optimized Noqeri.
    for(std::size_t y=0;y<h;++y)for(std::size_t x=0;x<w;++x) {
        const I px=static_cast<I>(x),py=static_cast<I>(y);
        I e0=edge(v[3],v[4],v[6],v[7],px,py);
        I e1=edge(v[6],v[7],v[0],v[1],px,py);
        I e2=edge(v[0],v[1],v[3],v[4],px,py);
        if(area>0 ? (e0<0||e1<0||e2<0) : (e0>0||e1>0||e2>0))continue;
        if(area<0){e0=-e0;e1=-e1;e2=-e2;}
        const I positiveArea=area<0?-area:area;
        U z=static_cast<U>((e0*v[2]+e1*v[5]+e2*v[8])/positiveArea);
        const std::size_t index=y*w+x;
        if(z<depth[index]){depth[index]=z;pixels[index]=color;}
    }
    return true;
}
void triangleCase(std::size_t w,std::size_t h,std::array<I,9> verts,
                  std::size_t plen,std::size_t dlen,std::size_t vn,U color) {
    const std::size_t size=std::max({w*h+4,plen+4,dlen+4});
    std::vector<U> pixels(size,initial),depth(size,32000u);
    std::vector<U> expectedPixels=pixels,expectedDepth=depth;
    std::array<I,12> vertexStorage{};
    std::copy(verts.begin(),verts.end(),vertexStorage.begin());
    Slice<U> p{pixels.data(),plen},d{depth.data(),dlen};
    Slice<I> v{vertexStorage.data(),vn};
    bool actual=nqRasterTriangle3D(&p,&d,w,h,&v,color)!=0;
    bool expected=referenceTriangle(expectedPixels,expectedDepth,plen,dlen,w,h,verts,vn,color);
    require(actual==expected,"triangle status mismatch");
    require(pixels==expectedPixels,"triangle color image differs from C reference");
    require(depth==expectedDepth,"triangle depth image differs from C reference");
}
void testProjection() {
    for(const auto& vertex:std::array<std::array<I,3>,12>{{
        {{0,0,256}},{{100,-32,256}},{{-100,120,300}},{{8192,0,2}},
        {{262144,0,65535}},{{-262144,30,256}},{{0,0,0}},
        {{0,0,65536}},{{262145,0,32}},{{0,-262145,64}},
        {{1,1,1}},{{1,1,65535}}
    }})for(const auto size:std::array<std::array<std::size_t,2>,6>{{
        {{0,16}},{{32,0}},{{1,1}},{{63,65}},{{256,256}},{{4096,4096}}
    }})for(std::size_t n: {0u,2u,3u,4u}){
        std::vector<I> actual(8,-100200300),expected=actual;
        Slice<I> target{actual.data(),n};
        const bool ok=nqProjectVertex3D(vertex[0],vertex[1],vertex[2],
                                          size[0],size[1],&target)!=0;
        const bool status=referenceProject(vertex[0],vertex[1],vertex[2],
                                           size[0],size[1],expected,n);
        require(ok==status,"projection status mismatch");
        require(actual==expected,"projection coordinates or output guard mismatch");
    }
}
void testDepth() {
    for(std::size_t w:{0u,1u,2u,3u,63u,64u,65u,255u,4097u})
        for(std::size_t h:{0u,1u,2u,3u,65u,4097u}){
            const std::size_t n=std::min<std::size_t>(w*h,400);
            for(std::size_t length : {n,n? n-1 : 0}){
                std::vector<U> actual(n+4,guard),expected=actual;
                Slice<U> slice{actual.data(),length};
                bool ok=nqDepthClear3D(&slice,w,h)!=0;
                bool status=referenceDepth(expected,length,w,h);
                require(ok==status,"depth clear status mismatch");
                require(actual==expected,"depth clear corrupted tail or wrong values");
            }
        }
}
void testTriangles(){
    const std::array<I,9> triangle{{2,2,400,24,3,400,8,25,400}};
    triangleCase(32,32,triangle,1024,1024,9,0xff00ff00u);
    auto reverse=triangle; std::swap_ranges(reverse.begin(),reverse.begin()+3,reverse.begin()+6);
    triangleCase(32,32,reverse,1024,1024,9,0xff443322u);
    for(const auto& sizes:std::array<std::array<std::size_t,2>,6>{{
       {{0,0}},{{1,1}},{{17,19}},{{63,65}},{{64,64}},{{257,257}}
    }}) {
        std::size_t len=sizes[0]*sizes[1];
        for(std::size_t shortened: {0u,1u}) {
            const auto actual=shortened&&len?len-1:len;
            triangleCase(sizes[0],sizes[1],triangle,actual,len,9,0xffaa7733u);
            triangleCase(sizes[0],sizes[1],triangle,len,actual,9,0xffbb8833u);
        }
    }
    triangleCase(32,32,triangle,1024,1024,8,0xff112233u);
    auto invalid=triangle; invalid[2]=0;
    triangleCase(32,32,invalid,1024,1024,9,0xffcc0011u);
    invalid=triangle; invalid[6]=-8193;
    triangleCase(32,32,invalid,1024,1024,9,0xffcc0011u);
    invalid=triangle; invalid[3]=8193;
    triangleCase(32,32,invalid,1024,1024,9,0xffcc0011u);
    invalid=triangle; invalid[8]=65536;
    triangleCase(32,32,invalid,1024,1024,9,0xffcc0011u);
    triangleCase(32,32,{{0,0,7,5,5,8,10,10,9}},1024,1024,9,0xffffff00u);
    triangleCase(32,32,{{-8,-8,10,5,5,15,9,100,2}},1024,1024,9,0xff123455u);
    triangleCase(32,32,{{-8192,-8192,1,8192,-8192,65535,0,8192,40}},
                 1024,1024,9,0xff112244u);
    triangleCase(32,32,{{-8192,8192,1,-8192,-8192,65535,8192,0,40}},
                 1024,1024,9,0xff112244u);
    std::mt19937 rng(0x5eedd3u);
    for(int trial=0;trial<250;++trial){
        std::array<I,9> v{};
        for(int k=0;k<3;k++){
            v[k*3]=static_cast<I>(rng()%100)-35;
            v[k*3+1]=static_cast<I>(rng()%100)-35;
            v[k*3+2]=static_cast<I>(rng()%65000)+1;
        }
        triangleCase(64,64,v,4096,4096,9,0xff00ff00u + static_cast<U>(trial));
    }
}
void testOcclusion(){
    constexpr std::size_t width=48,height=48,n=width*height;
    std::vector<U> pixels(n+3,guard),depth(n+3,guard);
    Slice<U> p{pixels.data(),n},d{depth.data(),n};
    require(nqDepthClear3D(&d,width,height)!=0,"depth init failed");
    for(std::size_t i=0;i<n;++i)require(depth[i]==UINT32_MAX,"incorrect initial depth");
    std::array<I,9> far{{4,4,500,43,4,500,23,44,500}};
    std::array<I,9> near{{10,10,200,37,10,200,23,37,200}};
    Slice<I> f{far.data(),9},q{near.data(),9};
    require(nqRasterTriangle3D(&p,&d,width,height,&f,0xff112233u)!=0,"draw far failed");
    require(nqRasterTriangle3D(&p,&d,width,height,&q,0xffaabbccu)!=0,"draw near failed");
    require(nqRasterTriangle3D(&p,&d,width,height,&f,0xffef0101u)!=0,"far redraw failed");
    require(pixels[20*width+23]==0xffaabbccu,"near triangle was occluded by farther triangle");
    require(depth[20*width+23]==200u,"incorrect nearer depth");
    require(pixels[n]==guard && depth[n]==guard,"depth/rgba tail overwritten");
}
} // namespace

int main(){
    try {
        testProjection();
        testDepth();
        testTriangles();
        testOcclusion();
        std::cout<<"Noqeri compiled 3D projection, depth and triangle differential: PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"Noqeri render3d differential failure: "<<e.what()<<'\n';
        return 1;
    }
}
