#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include "MinHook.h"
#include "../src/mastery_policy.h"

struct Registers { uint64_t total, rax, rcx, rdx, r9, r10, r11, r13, flags; uint8_t xmm[96]; };
extern "C" {
void* g_mastery_trampoline = nullptr;
void* g_mastery_after_trampoline = nullptr;
void MasteryDetour();
void MasteryAfterDetour();
void ExerciseBefore(void*, void*, uint32_t, uint32_t, Registers*);
void ExerciseAfter(void*, void*, float*);
extern uint8_t FixtureHookSite[];
extern uint8_t FixtureAfterSite[];
void TrashVolatiles();
}
static unsigned calls = 0, observed = 0, failures = 0, checks = 0;
static void* expectedProfile;
static void* expectedReport;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while(0)

extern "C" uint32_t AdjustMasteryGain(uint8_t* profile, uint8_t* settings, uint32_t current, uint32_t gained) noexcept {
    ++calls;
    auto result = mastery::adjust(*reinterpret_cast<float*>(profile+0x44), current, gained,
        reinterpret_cast<uint16_t*>(settings+0x138), *reinterpret_cast<uint16_t*>(settings+0x14a));
    TrashVolatiles();
    return result.gain;
}
extern "C" void ObserveMasteryCommit(void* profile, void* report) noexcept {
    ++observed;
    CHECK(profile == expectedProfile);
    CHECK(report == expectedReport);
    TrashVolatiles();
}

int main() {
    const uint16_t curve[9] = {150,450,1000,1900,3250,5150,7700,11000,15150};
    auto checkPolicy = [&](float p, uint32_t old, uint32_t gain, uint32_t expected, mastery::Reason reason, uint16_t cap=9) {
        auto d=mastery::adjust(p,old,gain,curve,cap); CHECK(d.gain==expected); CHECK(d.reason==reason);
    };
    checkPolicy(0,0,1,11000,mastery::Reason::Boost);
    checkPolicy(5.5f,4200,2,6800,mastery::Reason::Boost);
    checkPolicy(7.5f,9350,1,1650,mastery::Reason::Boost);
    checkPolicy(7.5f,9350,65000,1650,mastery::Reason::Boost); // 16-bit overflow avoidance
    checkPolicy(0,0,0,0,mastery::Reason::NoAward);
    checkPolicy(8,11000,10,10,mastery::Reason::AlreadyMax);
    checkPolicy(-1,0,10,10,mastery::Reason::InvalidProgress);
    checkPolicy(std::numeric_limits<float>::quiet_NaN(),0,10,10,mastery::Reason::InvalidProgress);
    checkPolicy(std::numeric_limits<float>::infinity(),0,10,10,mastery::Reason::InvalidProgress);
    checkPolicy(9,0,10,10,mastery::Reason::InvalidProgress);
    checkPolicy(0,0,10,10,mastery::Reason::WrongCap,10);
    checkPolicy(0,100,10,10,mastery::Reason::InconsistentTotal);
    checkPolicy(0,0,65536,65536,mastery::Reason::InconsistentTotal);
    uint16_t broken[9]; memcpy(broken,curve,sizeof(curve)); broken[3]=broken[2];
    CHECK(mastery::adjust(0,0,1,broken,9).reason==mastery::Reason::InvalidCurve);
    alignas(16) uint8_t profile[0x100]{}, settings[0x200]{}, report[0xb00]{};
    memcpy(settings+0x138,curve,sizeof(curve));
    *reinterpret_cast<uint16_t*>(settings+0x14a)=9;
    Registers before{};
    ExerciseBefore(profile,settings,0,1,&before);
    CHECK(before.total==1); CHECK(calls==0);
    CHECK(MH_Initialize()==MH_OK);
    CHECK(MH_CreateHook(FixtureHookSite, MasteryDetour, &g_mastery_trampoline)==MH_OK);
    CHECK(MH_CreateHook(FixtureAfterSite, MasteryAfterDetour, &g_mastery_after_trampoline)==MH_OK);
    CHECK(MH_EnableHook(MH_ALL_HOOKS)==MH_OK);
    for (unsigned i=0; i<1000; ++i) {
        float raw=(i%3==0)?0.0f:((i%3==1)?5.5f:8.0f);
        uint32_t old=(i%3==0)?0:((i%3==1)?4200:11000);
        uint32_t gain=(i%5==0)?0:1;
        *reinterpret_cast<float*>(profile+0x44)=raw;
        Registers out{}; ExerciseBefore(profile,settings,old,gain,&out);
        CHECK(out.total==((gain && raw<8)?11000:old+gain));
        CHECK(out.rax==old);
        CHECK(out.rcx==reinterpret_cast<uintptr_t>(profile));
        CHECK(out.rdx==reinterpret_cast<uintptr_t>(settings));
        CHECK(out.r9==gain); CHECK(out.r10==0x0123456789abcdefULL);
        CHECK(out.r11==old); CHECK(out.r13==0);
        CHECK((out.flags & 0x8c5)==0x44);
        bool vectors=true; for(auto b:out.xmm) vectors &= b==0x5a; CHECK(vectors);
        CHECK(*reinterpret_cast<float*>(profile+0x44)==raw); // before hook never edits saved rank
    }
    CHECK(calls==1000);
    expectedProfile=profile; expectedReport=report;
    float after=-1; *reinterpret_cast<float*>(profile+0x44)=8.0f;
    ExerciseAfter(profile,report,&after);
    CHECK(observed==1); CHECK(after==8.0f);
    CHECK(MH_DisableHook(MH_ALL_HOOKS)==MH_OK);
    ExerciseBefore(profile,settings,0,1,&before);
    CHECK(before.total==1); CHECK(calls==1000);
    CHECK(MH_Uninitialize()==MH_OK);
    std::printf("%u checks; %u failures; 1000 real x64 before-detour/trampoline calls and observer tested\n",checks,failures);
    return failures?1:0;
}
