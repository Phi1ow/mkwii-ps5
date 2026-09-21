// SPDX-License-Identifier: GPL-3.0-only
#include "aurora_input.h"
#include "input.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>
namespace aurora { AuroraConfig g_config{}; }
namespace {
using namespace mkw::input;
int checks;
void check(bool value, const char* what) { ++checks; if(!value) {
    std::fprintf(stderr,"FAIL input loop check=%d %s SDL=%s\n",checks,what,SDL_GetError());
    throw std::runtime_error(what);
} }
std::array<int,4> users{10,-1,-1,-1};
std::array<std::array<uint8_t,1024>,4> raw{};
std::vector<int> closed;
struct Vibration { int handle; uint8_t low,high; };
std::vector<Vibration> vibrations;
int listError=0,readError=0,closeError=0,opens=0;
void button(unsigned slot, uint32_t bits) { std::memcpy(raw[slot].data(),&bits,4); }
unsigned count(const AuroraEvent* events,AuroraEventType type) {
    unsigned found=0,limit=0;
    for(;events->type!=AURORA_NONE;++events) { check(++limit<4096,"events terminated"); if(events->type==type)++found; }
    return found;
}
}
extern "C" int sceUserServiceInitialize(void*) { return 0; }
extern "C" int sceUserServiceGetInitialUser(int* p) { *p=10;return 0; }
extern "C" int sceUserServiceGetLoginUserIdList(int* p) { std::memcpy(p,users.data(),sizeof(users));return listError; }
extern "C" int scePadInit() { return 0; }
extern "C" int scePadOpen(int user,int type,int index,void*) { check(!type&&!index,"native device type");++opens;return user+100; }
extern "C" int scePadGetHandle(int user,int,int) { return user+100; }
extern "C" int scePadClose(int handle) { closed.push_back(handle);return closeError; }
extern "C" int scePadReadState(int handle,void* p) { std::memcpy(p,raw[(handle-110)/10].data(),1024);return readError; }
extern "C" int scePadSetVibration(int handle,const void* p) {
    const auto* b=static_cast<const uint8_t*>(p);vibrations.push_back({handle,b[0],b[1]});return 0;
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        check(std::filesystem::create_directory(argv[1]),"fresh config directory");
        aurora::g_config.userPath=argv[1];
        for(auto& bytes:raw){bytes[76]=1;for(unsigned i=4;i<8;++i)bytes[i]=128;}
        AuroraInput input;
        bool refused=false;try{input.poll();}catch(const std::logic_error&){refused=true;}
        check(refused,"poll requires initialization");
        check(input.initialize(true)&&input.initialize(true)&&opens==1,"single initialization");
        AuroraInput second;check(!second.initialize(true),"second owner refused");
        button(0,0x4000);
        check(count(input.poll(),AURORA_CONTROLLER_ADDED)==1,"one added event");
        PADStatus status[PAD_CHANMAX]{};
        check(PADRead(status)==0x80000000u&&status[0].button==PAD_BUTTON_A,"native sample through PADRead");
        check(count(input.poll(),AURORA_CONTROLLER_ADDED)==0,"stable poll has no duplicate added event");
        const auto* ctrl=aurora::input::get_controller_for_player(0);check(ctrl!=nullptr,"player zero controller");
        check(SDL_RumbleGamepad(ctrl->m_controller,0xffff,0x8000,10000),"rumble forwarded");
        check(vibrations.back().handle==110&&vibrations.back().low==255&&vibrations.back().high==128,"native rumble magnitude");
        button(0,0x80004000u);input.poll();PADRead(status);
        check(status[0].button==0,"system interception neutralizes buttons");
        check(vibrations.back().handle==110&&!vibrations.back().low&&!vibrations.back().high,"interception stops ongoing rumble");
        check(!SDL_RumbleGamepad(ctrl->m_controller,0xeeee,0x7000,10000),"intercepted rumble refused");
        button(0,0x4000);readError=-9;
        check(count(input.poll(),AURORA_CONTROLLER_REMOVED)==1&&input.native_error()==-9,"read failure removes device and reports error");
        PADRead(status);check(status[0].err==PAD_ERR_NO_CONTROLLER,"failed read cannot retain held A");
        readError=0;check(count(input.poll(),AURORA_CONTROLLER_ADDED)==1,"read recovery reconnects");
        PADSetPortForIndex(PADGetIndexForPort(0),2);input.poll();
        check(PADGetIndexForPort(2)>=0&&PADGetIndexForPort(0)==-1,"poll preserves user port remap");
        ctrl=aurora::input::get_controller_for_player(2);
        check(SDL_RumbleGamepad(ctrl->m_controller,0xffff,0xffff,10000),"start rumble before user switch");
        users={20,-1,-1,-1};button(1,0x2000);
        const auto* changes=input.poll();
        check(count(changes,AURORA_CONTROLLER_REMOVED)==1&&count(changes,AURORA_CONTROLLER_ADDED)==1,"user replacement emits remove and add");
        check(closed.back()==110,"old native handle closed");
        check(vibrations.back().handle==110&&!vibrations.back().low&&!vibrations.back().high,"outgoing native motor stopped");
        PADRead(status);check(status[2].button==PAD_BUTTON_B&&status[2].err==PAD_ERR_NONE,"replacement device actual PAD sample");
        ctrl=aurora::input::get_controller_for_player(2);
        check(SDL_RumbleGamepad(ctrl->m_controller,0xffff,0x8000,10000),"start rumble before user-list error");
        listError=-12;input.poll();PADRead(status);
        check(input.native_error()==-12&&PADCount()==0&&status[2].err==PAD_ERR_NO_CONTROLLER,"user-list failure removes stale devices");
        check(vibrations.back().handle==120&&!vibrations.back().low&&!vibrations.back().high,"user-list failure stops ongoing rumble");
        listError=0;input.poll();check(PADCount()==1,"user-list recovery");
        SDL_Event quit{};quit.type=SDL_EVENT_QUIT;check(SDL_PushEvent(&quit),"queue exit event");
        check(count(input.poll(),AURORA_EXIT)==1,"exit translated to Aurora");
        closeError=-15;check(input.close()==-15&&!input.initialized()&&PADCount()==0,"native close failure retained after SDL cleanup");
        check(!input.initialize(true),"cannot replace unclosed native handles");
        check(!second.initialize(true),"failed close retains exclusive native ownership");
        closeError=0;check(input.close()==0,"retry native close");
        check(input.initialize(true),"reinitialize after close");input.poll();
        check(PADCount()==1&&input.close()==0&&PADCount()==0,"second lifecycle releases PAD");
        check(std::filesystem::remove_all(argv[1])==2,"own configuration cleaned");
        std::printf("PASS Aurora input loop: %d checks through NativePads, SDL events and original PAD\n",checks);
        return 0;
    }catch(...){return 1;}
}
