#include "owned_value_list.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <new>
#include <thread>
#include <vector>
struct Item {int* ptr;};
struct Tracker {int live=0,created=0,dropped=0;bool fail=false;};
static int32_t clone(void* d,const void* s,void* v){
 auto& t=*static_cast<Tracker*>(v);if(t.fail)return -1;
 new(d) Item{new int(*static_cast<const Item*>(s)->ptr)};
 ++t.live;++t.created;return 0;
}
static void drop(void* d,void* v){
 auto& t=*static_cast<Tracker*>(v);delete static_cast<Item*>(d)->ptr;
 static_cast<Item*>(d)->~Item();--t.live;++t.dropped;
}
int main(){
 assert(nq_value_list_new_copy(0,8)==-5);
 assert(nq_value_list_new_copy(8,3)==-5);
 const auto h=nq_value_list_new_copy(sizeof(uint64_t),alignof(uint64_t));
 assert(h>0);
 for(uint64_t i=0;i<20000;i++)assert(nq_value_list_append(h,&i)==0);
 uint64_t value=0;
 assert(nq_value_list_get(h,19999,&value)==0&&value==19999);
 assert(nq_value_list_get(h,20000,&value)==-3);
 assert(nq_value_list_remove(h,0)==0);
 assert(nq_value_list_get(h,0,&value)==0&&value==1);
 assert(nq_value_list_clear(h)==0&&nq_value_list_size(h)==0);
 assert(nq_value_list_close(h)==0&&nq_value_list_append(h,&value)==-1);
 Tracker tracker;
 assert(nq_value_list_new_custom(sizeof(Item),alignof(Item),clone,nullptr,&tracker)==-5);
 const auto c=nq_value_list_new_custom(sizeof(Item),alignof(Item),clone,drop,&tracker);
 assert(c>0);
 int original=123;
 Item input{&original};
 for(int i=0;i<40;i++)assert(nq_value_list_append(c,&input)==0);
 assert(tracker.live==40);
 tracker.fail=true;
 assert(nq_value_list_append(c,&input)==-6);
 assert(nq_value_list_remove(c,1)==-6);
 assert(tracker.live==40&&nq_value_list_size(c)==40);
 tracker.fail=false;
 assert(nq_value_list_remove(c,1)==0&&tracker.live==39);
 alignas(Item) unsigned char output[sizeof(Item)]{};
 assert(nq_value_list_get(c,0,output)==0&&tracker.live==40);
 drop(output,&tracker);
 assert(nq_value_list_close(c)==0&&tracker.live==0);
 assert(tracker.created==tracker.dropped);
 std::vector<std::thread> ts;
 for(int j=0;j<4;j++)ts.emplace_back([](){
  const auto id=nq_value_list_new_copy(sizeof(uint32_t),alignof(uint32_t));
  assert(id>0);
  for(uint32_t k=0;k<1000;k++)assert(nq_value_list_append(id,&k)==0);
  assert(nq_value_list_size(id)==1000&&nq_value_list_close(id)==0);
 });
 for(auto& t:ts)t.join();
}
