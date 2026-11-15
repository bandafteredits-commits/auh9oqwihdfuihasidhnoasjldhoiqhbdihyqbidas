// BUILD (x64 Native Tools Command Prompt for VS):  cl /nologo /O2 /GL /MT /EHsc /LD cpbob.cpp /link /OUT:cpbob.dll
// Keep cpbob.dll in the SAME folder as CheesyProxxy.exe.
// cpbob.cpp - CheesyProxxy "Sword bob" DLL. Loaded into Minecraft Bedrock by an injector, on top
// of other clients/DLLs (Oderso etc). Cosmetic first-person animation only: when YOUR hit is a
// crit, the held sword dips down and comes back. No server, network or gameplay changes.
//
// STATUS: settings polling + animation are done (cpbob_core.h, unit-tested). The two hooks in
// hooks_enable()/hooks_disable() are NOT written yet: they need addresses/offsets from the exact
// Bedrock build you run (see the TODO blocks). Until then the DLL loads and does nothing visible.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>

// ======================= core: crit detection + bob animation (unit-tested) =======================
// cpbob_core.h - portable core for CheesyProxxy "Sword bob" (crit animation).
// Header-only, no allocations, no OS calls. Only decides WHEN a crit happened and WHAT
// vertical offset the first-person held item should have this frame. Cosmetic only:
// it never touches server data, networking or gameplay.

namespace cpbob {

struct Settings {
    bool  enabled    = false;   // off by default
    float depth      = 0.12f;   // how far the sword dips, in item-space units (~blocks)
    float durationMs = 220.0f;  // whole down+back motion
    float downFrac   = 0.35f;   // share of the motion spent going down (rest = coming back)
};

// What the hook reads from the local player each frame / on each landed hit.
struct PlayerState {
    bool  onGround   = true;
    bool  inLiquid   = false;
    bool  onLadder   = false;
    bool  riding     = false;
    float fallDistance = 0.0f;
};

// Standard crit rule: a hit landed while falling, off the ground, not swimming/climbing/riding.
inline bool isCrit(const PlayerState& p) {
    return !p.onGround && !p.inLiquid && !p.onLadder && !p.riding && p.fallDistance > 0.0f;
}

class BobAnim {
public:
    void trigger()            { t_ = 0.0f; active_ = true; }      // a new crit restarts the bob
    void reset()              { t_ = 0.0f; active_ = false; }
    bool active() const       { return active_; }
    // Advance by dt seconds and return the Y offset to ADD to the held item (<= 0 = down).
    float step(const Settings& s, float dt) {
        if (!s.enabled || !active_) { if (!s.enabled) active_ = false; return 0.0f; }
        float dur = s.durationMs * 0.001f;
        if (dur < 0.02f) dur = 0.02f;
        float df = s.downFrac < 0.05f ? 0.05f : (s.downFrac > 0.95f ? 0.95f : s.downFrac);
        t_ += dt;
        if (t_ >= dur) { active_ = false; return 0.0f; }
        float p = t_ / dur, k;
        const float HALF_PI = 1.57079632679f;
        if (p < df) k = std::sin((p / df) * HALF_PI);                     // fast ease-out down
        else        k = std::cos(((p - df) / (1.0f - df)) * HALF_PI);     // smooth ease back up
        return -s.depth * k;
    }
private:
    float t_ = 0.0f; bool active_ = false;
};

} // namespace cpbob
// ======================= DLL =======================

static volatile LONG g_run = 1;
static cpbob::Settings g_set;                 // written by the poll thread, read by the render hook
static cpbob::BobAnim  g_anim;
static const wchar_t* CFG = L"C:\\Users\\Public\\CheesyProxxy\\swordbob.cfg";  // 2 dwords: enabled, reserved

// ---- hooks -----------------------------------------------------------------------------------
// COEXISTING WITH OTHER CLIENTS: never overwrite bytes another client already patched. For each
// target, check the first bytes: if it starts with E9 (jmp rel32) or FF 25 (jmp [rip+x]), another
// hook is installed, so hook THAT jump's destination instead, and always call through to the
// original/trampoline so their hook keeps running. Both hooks below must follow this rule.
//
// TODO hook 1 - local attack landed (e.g. the player attack/hit function):
//     read cpbob::PlayerState from the local player (onGround, inLiquid, onLadder, riding,
//     fallDistance), call original, then: if (g_set.enabled && cpbob::isCrit(state)) g_anim.trigger();
// TODO hook 2 - first-person held-item render (the item/hand transform, once per frame):
//     float dy = g_anim.step(g_set, dtSeconds);   // dy <= 0 while dipping
//     add dy to the held item's Y translation, then call original.
static void hooks_enable()  { /* install hook 1 + hook 2 here */ }
static void hooks_disable() { /* remove hooks, g_anim.reset() */ g_anim.reset(); }

static void poll_settings() {
    DWORD v[2] = {0, 0}, got = 0;
    HANDLE f = CreateFileW(CFG, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) { ReadFile(f, v, 8, &got, nullptr); CloseHandle(f); }
    g_set.enabled = (got >= 4 && v[0] == 1);
}

static DWORD WINAPI worker(LPVOID) {
    bool hooked = false;
    while (InterlockedCompareExchange(&g_run, 1, 1)) {
        poll_settings();                                   // 4x/second, like the culling DLL
        if (g_set.enabled && !hooked) { hooks_enable();  hooked = true; }
        if (!g_set.enabled && hooked) { hooks_disable(); hooked = false; }
        Sleep(250);
    }
    if (hooked) hooks_disable();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)); }
    if (reason == DLL_PROCESS_DETACH) InterlockedExchange(&g_run, 0);
    return TRUE;
}
