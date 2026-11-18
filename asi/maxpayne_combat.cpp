// Max Payne combat animations for GTA SA 1.0 US (retail + Project Eagle).
// Upper-body clips are partial overlays; the game keeps handling movement/tasks.
#include <plugin.h>
#include <CAnimManager.h>
#include <CAnimBlock.h>
#include <CAnimBlendHierarchy.h>
#include <CAnimBlendClumpData.h>
#include <CAnimBlendAssociation.h>
#include <CAnimBlendAssocGroup.h>
#include <CAnimBlendStaticAssociation.h>
#include <cstring>
#include <CPlayerPed.h>
#include <CPools.h>
#include <CWeapon.h>
#include <CPad.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <map>
#include <string>
#include <CGeneral.h>
#include <CCamera.h>
#include <CTimer.h>

using namespace plugin;

static CAnimBlock* g_full = nullptr;
static CAnimBlock* g_upper = nullptr;
static CAnimBlock* g_dodge = nullptr;
static CAnimBlock* g_loco = nullptr;
static CAnimBlock* g_body = nullptr;  // MP_BODY.IFP: SA player clip replacements
static bool g_mpLoco = true;       // MP idle/walk/run clips replace SA's player clips
static int g_moveDir = 0;          // free walk: 0 = native/forward, 1..7 = clockwise 45 deg sectors (MOVE<k>)
static bool g_turning = false;  // standing turn-in-place active (StandTurn)
static bool g_moveActive = false;  // free walk this frame: CJ keeps facing the camera
static bool g_mpFire = true;       // MAX PAYNE gunfire: armed = always ready (forced aim stance, fire any time, no aim button)
static bool g_nativeAim = false;   // with g_mpFire: physical R1 = SA's own aim again (not forced)
static bool g_mpCross = true;      // MAX PAYNE dot crosshair instead of SA's
static bool g_mpHipAcc = true;     // PINPOINT: no SA distance spread on hipfire shots
static bool g_mpAimAcc = true;     // PINPOINT: no SA distance spread while aiming
static bool g_mpControls = true;   // armed on foot (pad): always face the crosshair and strafe, like MP
static bool g_headLook = false;    // SA's head/look-at IK on CJ (looks at peds, follows the camera)
static bool g_idleLook = false;    // Max's full idle: looks right/left/right after stopping (1:1 MP)
static bool g_stillSide = false;   // Sway clips = Max's real torso/arm swing (non-Sway have frozen arms: 0 deg range)
static bool g_stillBack = false;
static bool g_inertia = true;      // pose inertialization: pops glide out over ~0.2 s (Max-smooth transitions)
static bool g_loaded = false;
static bool g_autoPlayer = false;  // old aim/shoot/reload overlays (layered on top)
static bool g_enemyAnims = true;
static bool g_inputDone = false;  // menu input already read this frame (Tick vs HUD hook)
static bool g_stickShape = true;   // Xbox right-stick curve on the pad
static int g_camMode = 0;          // pad camera: 0 CURRENT (console follow cam + pitch fix), 1 MAX PAYNE, 2 GTA SA (PC cam)
static float g_enemyDist = 40.0f;
static bool g_key[16] = {};
static bool g_fkey[13] = {};
static bool g_dodgeCombo = false;
static unsigned g_lastEnemyScan = 0;
static unsigned g_enemyEnableAt = 0;
static unsigned g_dodgeUntil = 0;
static bool g_wasInAir = false;
static int g_shells = 8;              // shotgun magazine, Max Payne style
static int g_shellWeapon = -1;
static unsigned g_reloadUntil = 0;
static bool g_reloadKey = false;
static bool g_showShells = false;

static CPlayerPed* FindPlayer(int id) {
    // 0x56E210 is FindPlayerPed; 0x56E0D0 is FindPlayerVehicle.
    return ((CPlayerPed*(__cdecl*)(int))0x56E210)(id);
}

static double g_modUs = 0; static int g_modN = 0;  // mod CPU time per frame (perf line, once a second)
struct ModTimer {
    LARGE_INTEGER t0; ModTimer() { QueryPerformanceCounter(&t0); }
    ~ModTimer() { LARGE_INTEGER t1, f; QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f); g_modUs += double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart); }
};
static void Log(const char* fmt, ...) {
    // Kept open + flushed every 100 ms: fopen/fclose per line (3+ lines per frame, on an external drive
    // under Wine) produced frame hitches of 100-300 ms = the "jolts" with a 170 ms free-walk grace expiring.
    static FILE* f = nullptr; static DWORD lastFlush = 0;
    if (!f) { f = fopen("maxpayne_combat.log", "a"); if (!f) return; }
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    DWORD now = GetTickCount();
    if (now - lastFlush > 100) { lastFlush = now; fflush(f); }
}

static CAnimBlock* GetBlock(const char* name) {
    return ((CAnimBlock*(__cdecl*)(const char*))0x4D3940)(name);
}

static CAnimBlock* LoadBlock(const char* file, const char* blockName, bool compressed = true) {
    RwStream* s = ((RwStream*(__cdecl*)(RwStreamType, RwStreamAccessType, const void*))0x7ECEF0)(
        rwSTREAMFILENAME, rwSTREAMREAD, file);
    if (!s) { Log("missing %s", file); return nullptr; }
    ((void(__cdecl*)(RwStream*, bool, const char (*)[32]))0x4D47F0)(s, compressed, nullptr);
    ((RwBool(__cdecl*)(RwStream*, void*))0x7ECE20)(s, nullptr);
    CAnimBlock* b = GetBlock(blockName);
    Log("load %s block=%s ptr=%p anims=%d", file, blockName, b, b ? b->animationCount : 0);
    return b;
}

static void LoadAll() {
    if (g_loaded) return;
    g_loaded = true;
    g_full = LoadBlock("MP_COMBAT.IFP", "MPC");
    g_upper = LoadBlock("MP_COMBAT_UPPER.IFP", "MPU");
    g_dodge = LoadBlock("MP_DODGE.IFP", "MPD");
    g_loco = LoadBlock("MP_LOCO.IFP", "MPL");
    g_body = LoadBlock("MP_BODY.IFP", "MPS", false);
    Log("version=%s full=%p upper=%p dodge=%p loco=%p", GetGameVersionName(), g_full, g_upper, g_dodge, g_loco);
    g_enemyEnableAt = GetTickCount() + 3000;
}

// SA blend deltas are amount per 50 fps tick-second: 0.25 took ~4 s to blend in, so the native
// pose (rifle-hold arms, walk legs) stayed visible for seconds. 8 is SA's usual fast blend.
static const float kBlendIn = 8.0f;

static CAnimBlendAssociation* Play(RpClump* clump, CAnimBlock* block, const char* name, int flags) {
    if (!clump || !block) return nullptr;
    CAnimBlendHierarchy* h = ((CAnimBlendHierarchy*(__cdecl*)(const char*, const CAnimBlock*))0x4D42F0)(name, block);
    if (!h) { Log("anim missing: %s", name); return nullptr; }
    // Indestructible keeps our association pointer valid across weapon/task
    // changes; otherwise SetBlend on a freed association corrupts the ped.
    return ((CAnimBlendAssociation*(__cdecl*)(RpClump*, CAnimBlendHierarchy*, int, float))0x4D4410)(
        clump, h, flags | ANIMATION_INDESTRUCTIBLE, kBlendIn);
}

struct Slot {
    CAnimBlendAssociation* a = nullptr;
    const char* clip = nullptr;
    CAnimBlock* block = nullptr;
};

static void SetBlend(CAnimBlendAssociation* a, float amount, float delta) {
    if (a) ((void(__thiscall*)(CAnimBlendAssociation*, float, float))0x4CEBA0)(a, amount, delta);
}

// Fade out and let SA delete it once invisible (we drop our pointer here).
static void FadeOut(CAnimBlendAssociation* a) {
    if (!a) return;
    a->m_nFlags |= 0x4; // BLEND_AUTO_REMOVE (gta-reversed name; plugin-sdk mislabels 0x4)
    SetBlend(a, a->m_fBlendAmount, -kBlendIn);
}

static void Stop(Slot& s) {
    FadeOut(s.a);
    s.a = nullptr;
    s.clip = nullptr;
    s.block = nullptr;
}

static void PlaySlot(Slot& s, RpClump* clump, CAnimBlock* block, const char* clip, int flags) {
    if (!clump || !block) return;
    if (s.a && s.clip == clip && s.block == block) return; // same clip already running/frozen
    FadeOut(s.a);
    s.a = Play(clump, block, clip, flags);
    s.clip = s.a ? clip : nullptr;
    s.block = block;
    if (s.a) Log("play %s flags=%d assoc=%p", clip, flags, s.a);
}

static bool Armed(CPed* ped) {
    CWeapon* w = &ped->m_aWeapons[ped->m_nSelectedWepSlot];
    return w->m_eWeaponType >= WEAPONTYPE_PISTOL && w->m_eWeaponType <= WEAPONTYPE_SNIPERRIFLE;
}

// Clip names follow Max's own setup in MP's default_skeleton.txt / max_payne.txt.
static const char* ShootClip(eWeaponType t) {
    switch (t) {
    case WEAPONTYPE_PISTOL:
    case WEAPONTYPE_PISTOL_SILENCED: return "ShootBeretta";
    case WEAPONTYPE_DESERT_EAGLE:    return "ShootDesertEagle";
    case WEAPONTYPE_SHOTGUN:         return "ShootPumpShotgun";
    case WEAPONTYPE_SAWNOFF:         return "ShootSawedShotgun";
    case WEAPONTYPE_SPAS12:          return "ShootJackhammer";
    case WEAPONTYPE_MICRO_UZI:
    case WEAPONTYPE_TEC9:            return "ShootIngram";
    case WEAPONTYPE_SNIPERRIFLE:     return "ShootSniper";
    default:                         return "ShootColt"; // MP5/AK/M4/rifle: Max's SHOOTMP5 is shootColt
    }
}

static const char* ReloadClip(eWeaponType t) {
    switch (t) {
    case WEAPONTYPE_PISTOL:
    case WEAPONTYPE_PISTOL_SILENCED: return "ReloadBeretta";
    case WEAPONTYPE_DESERT_EAGLE:    return "ReloadDesertEagle";
    case WEAPONTYPE_SHOTGUN:         return "ReloadPumpShotgun";
    case WEAPONTYPE_SAWNOFF:         return "ReloadSawedShotgun";
    case WEAPONTYPE_SPAS12:          return "ReloadJackhammer";
    case WEAPONTYPE_MICRO_UZI:
    case WEAPONTYPE_TEC9:            return "ReloadIngram";
    default:                         return "ReloadMP5";
    }
}

static bool Hostile(CPed* p) {
    int t = p->m_nPedType;
    return t == PED_TYPE_COP || t == PED_TYPE_CRIMINAL ||
           (t >= PED_TYPE_GANG1 && t <= PED_TYPE_GANG10 && t != PED_TYPE_GANG2);
}

static bool IsShotgun(eWeaponType t) {
    return t == WEAPONTYPE_SHOTGUN || t == WEAPONTYPE_SAWNOFF || t == WEAPONTYPE_SPAS12;
}

static bool OneHand(eWeaponType t) {
    return t == WEAPONTYPE_PISTOL || t == WEAPONTYPE_PISTOL_SILENCED ||
           t == WEAPONTYPE_DESERT_EAGLE || t == WEAPONTYPE_MICRO_UZI ||
           t == WEAPONTYPE_TEC9;
}

static bool IsMoving(CPed* p) {
    CVector v = p->m_vecMoveSpeed;
    return (v.x*v.x + v.y*v.y) > 0.0001f; // m_vecMoveSpeed is per 1/50 s: walking is ~0.03
}

// ---- MP body: SA's own player clips re-pointed at MP clips (MP_BODY.IFP, block MPS) ----
// SA's state machine keeps choosing idle/walk/run/gunmove exactly as before, it just plays
// Max's clip. Nothing is layered on top, so there are no two idles fighting. The MP clips
// carry baked root travel (tools: --root-motion stride), which SA uses to move CJ, so the
// planted foot stays put. Clip names in MPS = the SA clip they replace.
static const char* const kSwap[] = {
    "IDLE_stance", "WALK_player", "run_player", "run_1armed",
    "IDLE_armed", "WALK_armed", "run_armed",
    "GUNMOVE_FWD", "GUNMOVE_L", "GUNMOVE_R", "GUNMOVE_BWD", "WALK_start",
    // rocket launcher group: Max's legs under SA's own rocket carry (ROCKET* = MP legs + IDLE_ROCKET torso)
    "IDLE_ROCKET", "WALK_rocket", "run_rocket", "WALK_start_rocket",
};
static CAnimBlendHierarchy* Anim(const char* name, CAnimBlock* b) {
    return ((CAnimBlendHierarchy*(__cdecl*)(const char*, const CAnimBlock*))0x4D42F0)(name, b);
}

static bool InBody(CAnimBlendHierarchy* h) {
    int i = int(h - (CAnimBlendHierarchy*)0xB4EA40);
    return g_body && i >= g_body->startAnimation && i < g_body->startAnimation + g_body->animationCount;
}

// Re-point every static association using `from` at `to` (in player groups only if onlyPlayer).
static int Repoint(RpClump* clump, CAnimBlendHierarchy* from, CAnimBlendHierarchy* to, bool onlyPlayer, int id = -1) {
    CAnimBlendAssocGroup* groups = *(CAnimBlendAssocGroup**)0xB4EA34;
    int ngroups = *(int*)0xB4EA28, n = 0;
    CAnimBlock* ped = GetBlock("ped");
    CAnimBlendHierarchy* walkP = Anim("WALK_player", ped);
    for (int g = 0; groups && g < ngroups; ++g) {
        CAnimBlendStaticAssociation* as = groups[g].m_pAssociations;
        if (!as) continue;
        bool player = false;
        for (unsigned k = 0; k < groups[g].m_nNumAnimations; ++k)
            player |= as[k].m_pHeirarchy == walkP || InBody(as[k].m_pHeirarchy);  // walk slot may hold MOVE<k>
        if (onlyPlayer && !player) continue;
        for (unsigned k = 0; k < groups[g].m_nNumAnimations; ++k) {
            if (as[k].m_pHeirarchy != from || (id >= 0 && as[k].m_nAnimId != id)) continue;
            ((void(__thiscall*)(CAnimBlendStaticAssociation*))0x4CE9A0)(&as[k]);              // FreeSequenceArray
            ((void(__thiscall*)(CAnimBlendStaticAssociation*, RpClump*, CAnimBlendHierarchy*))0x4CEC20)(
                &as[k], clump, to);                                                            // Init
            ++n;
        }
    }
    return n;
}

// Which MP_BODY clip (if any) each SA clip in kSwap should play now. Variants: *Look = Max's
// idle with look-around, *Sway = sidestep/backpedal with the clip's own torso swing.
static const char* WantClip(const char* name, char* buf) {
    if (!g_mpLoco) return nullptr;
    if (!strcmp(name, "IDLE_ROCKET")) return "ROCKET_IDLE";
    bool rWalk = !strcmp(name, "WALK_rocket"), rRun = !strcmp(name, "run_rocket"), rStart = !strcmp(name, "WALK_start_rocket");
    if (rWalk || rRun || rStart) {
        if (g_moveDir) { snprintf(buf, 32, "ROCKET%d", g_moveDir); return buf; }
        return rWalk ? "ROCKET_WALK" : rRun ? "ROCKET_RUN" : nullptr;
    }
    bool isBack = !strcmp(name, "GUNMOVE_BWD");
    if (g_moveDir) {  // free walk: Max's clip for the stick's direction (still torso unless Sway)
        bool walk1 = !strcmp(name, "WALK_player") || !strcmp(name, "run_player") || !strcmp(name, "run_1armed") || !strcmp(name, "WALK_start");
        bool walk2 = !strcmp(name, "WALK_armed") || !strcmp(name, "run_armed");
        bool back = g_moveDir >= 3 && g_moveDir <= 5;
        const char* sway = (back ? g_stillBack : g_stillSide) ? "" : "Sway";
        if (walk1) { snprintf(buf, 32, "MOVE%d%s", g_moveDir, sway); return buf; }
        if (walk2) { snprintf(buf, 32, "MOVE2_%d%s", g_moveDir, sway); return buf; }
    }
    if (!strcmp(name, "WALK_start")) return nullptr;  // SA's own start step
    const char* suffix = "";
    if (!strncmp(name, "IDLE_", 5) && g_idleLook) suffix = "Look";
    if ((!strcmp(name, "GUNMOVE_L") || !strcmp(name, "GUNMOVE_R")) && !g_stillSide) suffix = "Sway";
    if (isBack && !g_stillBack) suffix = "Sway";
    snprintf(buf, 32, "%s%s", name, suffix);
    return buf;
}

static const float kPhase[] = {0.0, 0.986, 0.923, 0.882, 0.0, -0.013, 0.921, 0.173, 0.795, 0.963, 0.236, 0.0, 0.0, 0.795, 0.963, 0.236, 0.0, 0.0, 0.0, 0.0, 0.0, 0.907, 0.923, 0.925, 0.925, 0.925, 0.921, 0.921, 0.755, 0.755, 0.755, 0.752, 0.752, 0.395, 0.395, 0.395, 0.397, 0.397, 0.396, 0.396, 0.396, 0.397, 0.397, 0.399, 0.399, 0.399, 0.396, 0.396, 0.837, 0.837, 0.837, 0.048, 0.048, 0.925, 0.925, 0.925, 0.921, 0.921};  // 58 clips
// Foot-strike phase of a body clip (tools/phase_table.py), -1 if it has none.
static float PhaseOf(CAnimBlendHierarchy* h) {
    if (!InBody(h)) return -1;
    int i = int(h - (CAnimBlendHierarchy*)0xB4EA40) - g_body->startAnimation;
    return i >= 0 && i < int(sizeof(kPhase) / sizeof(*kPhase)) && kPhase[i] != 0 ? kPhase[i] : -1;
}

static CAnimBlendAssociation* g_fadeIn = nullptr; static float g_fadeT = 0;
// Re-create running instances of `from` (anim id `id`) from the re-pointed static association,
// keeping blend and normalized time, like CPlayerPed::ReApplyMoveAnims (0x609650).
static void RefreshRunning(RpClump* clump, CAnimBlendHierarchy* from, CAnimBlendHierarchy* to, int id) {
    for (auto* a = ((CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0)(clump); a;
         a = ((CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0)(a)) {
        // a clip SA already started fading (delta -4, still ~1.0) is the one being left: carry it over too, else SA's
        // new walk anim appears at blend 1.0 and the old one is gone in one frame (hard cut). Our own kills are < -50.
        if (a->m_pHierarchy != from || a->m_nAnimId != id || a->m_fBlendDelta < -50.0f || (a->m_fBlendDelta < 0 && a->m_fBlendAmount < 0.5f)) continue;
        auto* n = ((CAnimBlendAssociation*(__cdecl*)(RpClump*, int, int))0x4D3AA0)(clump, a->m_nAnimGroup, id);  // AddAnimation
        if (!n || n->m_pHierarchy != to) continue;
        // keep the new slot's own X-velocity bit (0x80): don't carry MOVE's onto SA's walk
        n->m_nFlags = (a->m_nFlags & ~0x80u) | (n->m_nFlags & 0x80u);
        // SA weights assocs in list order: the front one takes min(its blend, what is left). The new
        // one is in front, so copying the old blend (1.0) put it at full weight on frame 1 = a pop
        // on every sector change. Start it at 0 and ramp up while the old one ramps down.
        n->m_fBlendAmount = 0.0f;
        n->m_fBlendDelta = 6.0f;
        if (a->m_fBlendAmount > 0.5f) { g_fadeIn = n; g_fadeT = 0; }
        n->m_fSpeed = a->m_fSpeed;
        float tn = from->m_fTotalTime > 0 ? a->m_fCurrentTime / from->m_fTotalTime : 0;
        float pf = PhaseOf(from), pt = PhaseOf(to);
        if (pf >= 0 && pt >= 0) { tn += pt - pf; tn -= floorf(tn); }
        float t = tn * to->m_fTotalTime;
        ((void(__thiscall*)(CAnimBlendAssociation*, float))0x4CEA80)(n, t);  // SetCurrentTime
        // cross-fade the old direction out over ~0.15 s instead of cutting it (cut = visible pop
        // on every stick-sector change); the new one is in front of the list, so SA finds it
        a->m_nFlags |= 0x4;            // BLEND_AUTO_REMOVE
        a->m_fBlendDelta = -6.0f;
    }
}

// Anim id of the static associations that use `h` (first found), -1 if none.
static int SlotId(CAnimBlendHierarchy* h) {
    CAnimBlendAssocGroup* groups = *(CAnimBlendAssocGroup**)0xB4EA34;
    for (int g = 0, ng = *(int*)0xB4EA28; groups && g < ng; ++g)
        for (unsigned k = 0; groups[g].m_pAssociations && k < groups[g].m_nNumAnimations; ++k)
            if (groups[g].m_pAssociations[k].m_pHeirarchy == h) return groups[g].m_pAssociations[k].m_nAnimId;
    return -1;
}

static CAnimBlendHierarchy* g_cur[sizeof(kSwap) / sizeof(*kSwap)];  // what SA's slots point at now
static void ApplyBody(RpClump* clump) {
    auto& cur = g_cur;
    static int ids[sizeof(kSwap) / sizeof(*kSwap)];
    static bool idsInit = false;
    if (!idsInit) { idsInit = true; for (int& v : ids) v = -1; }
    if (!g_body) return;
    CAnimBlock* ped = GetBlock("ped");
    for (unsigned i = 0; i < sizeof(kSwap) / sizeof(*kSwap); ++i) {
        const char* name = kSwap[i];
        CAnimBlendHierarchy* sa = Anim(name, ped);
        if (!cur[i]) cur[i] = sa;
        // Several slots share one MOVE clip while free-walking, so the anim id is what tells
        // them apart. A slot whose group isn't loaded yet (run_1armed until a pistol) has no id;
        // re-pointing it unfiltered stole WALK_start and left it on MP's forward Run (M3): every
        // walk start then ran forward for a whole cycle. Leave such a slot alone until it loads.
        if (ids[i] < 0 || cur[i] == sa) ids[i] = SlotId(sa) >= 0 ? SlotId(sa) : ids[i];
        if (ids[i] < 0) { ids[i] = -1; continue; }
        char buf[32];
        const char* w = WantClip(name, buf);
        CAnimBlendHierarchy* want = w ? Anim(w, g_body) : sa;
        if (!sa || !want) { Log("swap: missing %s/%s", name, w ? w : "SA"); want = sa; }
        if (!sa || want == cur[i]) continue;
        // IDLE_stance / WALK_start are shared by every ped group: only re-point the player's.
        bool shared = !strcmp(name, "IDLE_stance") || !strcmp(name, "WALK_start");
        int n = Repoint(clump, cur[i], want, cur[i] == sa && shared, ids[i]);
        RefreshRunning(clump, cur[i], want, ids[i]);
        if (!g_moveActive) Log("swap %s -> %s: %d assocs", name, w ? w : "SA", n);
        cur[i] = want;
    }
}

// SA drops a clip's sideways root travel unless the running anim has
// ANIMATION_CAN_EXTRACT_X_VELOCITY (0x80, RpAnimBlend.cpp). Its walk/run slots don't (only
// GUNMOVE_L/R do), so MOVE2/MOVE6 played in them sidestepped on the spot (log: 0.000 m/frame
// sideways). Every MP_BODY clip has its real travel baked, so give them all the bit.
static void ExtractSideways(RpClump* c) {
    if (!g_body) return;
    for (auto* a = ((CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0)(c); a;
         a = ((CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0)(a)) {
        if (InBody(a->m_pHierarchy)) {
            if (!(a->m_nFlags & 0x1) && a->m_fBlendAmount > 0.01f) Log("unstarted MP assoc flags=%x t=%.3f bd=%.2f -> start", a->m_nFlags, a->m_fCurrentTime, a->m_fBlendDelta);
            a->m_nFlags |= 0x80 | 0x1;  // 0x1 = ANIMATION_STARTED: without it the clip's time never advances (frozen pose)
        }
    }
}

// Trace: log the player's dominant full-body clip whenever it changes (ped.ifp index, or MP).
// The clip that shows most: what TraceClip reports and what the mv line times.
static CAnimBlendAssociation* Dominant(RpClump* c) {
    CAnimBlendAssociation* best = nullptr;
    for (auto* a = ((CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0)(c); a;
         a = ((CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0)(a))
        if (!(a->m_nFlags & ANIMATION_PARTIAL) && (!best || a->m_fBlendAmount > best->m_fBlendAmount))
            best = a;
    return best;
}

static void TraceClip(RpClump* c) {
    static CAnimBlendHierarchy* last = nullptr;
    CAnimBlendAssociation* best = Dominant(c);
    if (!best || best->m_pHierarchy == last) return;
    last = best->m_pHierarchy;
    int idx = int(last - (CAnimBlendHierarchy*)0xB4EA40);
    CAnimBlock* ped = GetBlock("ped");
    bool mp = g_body && idx >= g_body->startAnimation && idx < g_body->startAnimation + g_body->animationCount;
    Log("clip %s#%d id=%d grp=%d speed=%.2f", mp ? "MPS" : "ped",
        idx - (mp ? g_body->startAnimation : ped ? ped->startAnimation : 0),
        best->m_nAnimId, best->m_nAnimGroup, best->m_fSpeed);
}

static void UpdatePlayer(CPlayerPed* p) {
    static Slot slot, legs;
    static eWeaponState lastState = WEAPONSTATE_READY;
    static unsigned lastShot = 0;
    if (!p || !p->m_pRwClump) return;
    if (GetTickCount() < g_dodgeUntil) return;
    if (!g_autoPlayer || p->m_pVehicle || !Armed(p)) { Stop(slot); Stop(legs); lastState = WEAPONSTATE_READY; return; }

    CWeapon* w = p->GetWeapon();
    static int logState = -1, logAim = -1, lastAmmo = -1, lastTotal = -1, lastType = -1;
    if ((int)w->m_nState != logState || (int)p->bIsAimingGun != logAim) {
        logState = (int)w->m_nState; logAim = (int)p->bIsAimingGun;
        Log("player state=%d aim=%d type=%d", (int)w->m_nState, (int)p->bIsAimingGun, (int)w->m_eWeaponType);
    }
    if ((int)w->m_eWeaponType != lastType) {
        lastType = (int)w->m_eWeaponType;
        lastAmmo = w->m_nAmmoInClip;
        lastTotal = w->m_nAmmoTotal;
        g_showShells = IsShotgun(w->m_eWeaponType);
        if (g_showShells) { g_shells = 8; g_shellWeapon = lastType; Log("shotgun shells=8"); }
        // No gap on switch: crossfade straight to the new weapon's idle, otherwise the
        // native rifle/shotgun hold pose shows (arms lifting) until our clip blends in.
        lastState = WEAPONSTATE_READY;
        Log("weapon switch type=%d", lastType);
    }
    // Native shotgun reload refills the 1-shell clip before gameProcessEvent,
    // so clip ammo is unreliable. Total ammo drops by one per shot.
    bool fired = (w->m_nState == WEAPONSTATE_FIRING)
              || (lastAmmo >= 0 && w->m_nAmmoInClip < lastAmmo)
              || (lastTotal >= 0 && w->m_nAmmoTotal < lastTotal);
    if (lastTotal >= 0 && w->m_nAmmoTotal < lastTotal) Log("total ammo %d -> %d", lastTotal, w->m_nAmmoTotal);
    lastAmmo = w->m_nAmmoInClip;
    lastTotal = w->m_nAmmoTotal;

    CPad* pad = CPad::GetPad(0);
    bool reloading = false;
    if (IsShotgun(w->m_eWeaponType)) {
        if (fired && g_shells > 0) { g_shells--; Log("shells=%d", g_shells); }
        bool rDown = (GetAsyncKeyState('R') & 0x8000) != 0;
        bool now = GetTickCount() >= g_reloadUntil;
        if (rDown && !g_reloadKey && g_shells < 8 && now && w->m_nAmmoTotal > 0) {
            g_reloadUntil = GetTickCount() + 1400;
            reloading = true;
            Log("manual reload shells=%d", g_shells);
        }
        g_reloadKey = rDown;
        if (g_reloadUntil && GetTickCount() >= g_reloadUntil) {
            g_shells = 8;
            w->m_nAmmoInClip = 1; // native shotgun clip is one shell
            g_reloadUntil = 0;
            Log("reload done shells=8");
        }
        if (pad) pad->bDisablePlayerFireWeapon = (g_shells <= 0 || GetTickCount() < g_reloadUntil);
        if (GetTickCount() < g_reloadUntil) reloading = true;
    }

    // Upper body (MP_COMBAT_UPPER.IFP, partial): stand pose, aim = first frame of the
    // weapon's shoot clip held, fire restarts the shoot clip, reload. Partial anims at full
    // blend override SA's own anims on their bones; full-body clips would only average.
    bool two = !OneHand(w->m_eWeaponType);
    unsigned flags = ANIMATION_PARTIAL;
    if (reloading || (w->m_nState == WEAPONSTATE_RELOADING && !IsShotgun(w->m_eWeaponType))) {
        if (lastState != WEAPONSTATE_RELOADING || reloading)
            PlaySlot(slot, p->m_pRwClump, g_upper, ReloadClip(w->m_eWeaponType), flags);
    } else if (fired) {
        lastShot = GetTickCount();
        if (lastState != WEAPONSTATE_FIRING) {
            Stop(slot); // restart the shot even if the same clip is held for aiming
            PlaySlot(slot, p->m_pRwClump, g_upper, ShootClip(w->m_eWeaponType), flags);
        }
    } else if (p->bIsAimingGun || GetTickCount() < lastShot + 1500) {
        // Aiming (or just fired): hold the shoot clip's first frame = MP's aim pose (Aim* in MPU).
        static std::map<std::string, std::string> aims;
        std::string n = std::string("Aim") + (ShootClip(w->m_eWeaponType) + 5);
        PlaySlot(slot, p->m_pRwClump, g_upper, aims.emplace(n, n).first->second.c_str(),
                 flags | ANIMATION_LOOPED);
    } else if (g_moveActive || g_turning) {
        // Free-walking or turning in place, not aiming: Max's MOVE/TURN clips are complete
        // full-body animations (arms, spine, head). Holding the partial Stand pose over them
        // froze the arms in the idle pose while the legs stepped - the user's "hands in an odd
        // manner" - so the partial layer steps aside and the turn's own arms play.
        Stop(slot);
    } else {
        auto* before = slot.a;
        PlaySlot(slot, p->m_pRwClump, g_upper, two ? "2_Stand" : "Stand", flags | ANIMATION_LOOPED);
        // arms go from Max's moving swing to the held Stand pose when you stop: ease it in
        // (~0.25 s) instead of SA's 0.125 s pop.
        if (slot.a && slot.a != before) SetBlend(slot.a, slot.a->m_fBlendAmount, 4.0f);
    }
    lastState = w->m_nState;
}

static void UpdateEnemies(CPlayerPed* player) {
    static std::map<CPed*, Slot> slots;
    if (!player || !g_enemyAnims || GetTickCount() < g_enemyEnableAt) return;
    CPad* pad = CPad::GetPad(0);
    if (!pad || pad->DisablePlayerControls) return;
    unsigned now = GetTickCount();
    if (now - g_lastEnemyScan < 100) return;
    g_lastEnemyScan = now;

    auto* pool = *(CPool<CPed, CCopPed>**)0xB74490;
    if (!pool) return;
    CVector pp = player->GetPosition();
    const float maxd2 = g_enemyDist * g_enemyDist;

    for (int i = 0; i < pool->m_nSize; ++i) {
        CPed* p = pool->GetAt(i);
        auto it = slots.find(p);
        if (!p || p == player || p->m_fHealth <= 0.0f || p->m_pVehicle || !p->m_pRwClump || !Hostile(p)) {
            if (it != slots.end()) slots.erase(it);
            continue;
        }
        CVector d = p->GetPosition() - pp;
        if (d.x*d.x + d.y*d.y + d.z*d.z > maxd2 || !Armed(p)) {
            if (it != slots.end()) { Stop(it->second); slots.erase(it); }
            continue;
        }
        CWeapon* w = &p->m_aWeapons[p->m_nSelectedWepSlot];
        Slot& s = slots[p];
        // Full-body previews replace the legs; only use them while the ped is
        // standing still so we don't freeze a walking/running cycle mid-stride.
        CVector v = p->m_vecMoveSpeed;
        bool still = (v.x*v.x + v.y*v.y) < 0.0001f;
        if (!still) {
            Stop(s);
        } else if (w->m_nState == WEAPONSTATE_RELOADING) {
            PlaySlot(s, p->m_pRwClump, g_full, ReloadClip(w->m_eWeaponType), 0);
        } else if (w->m_nState == WEAPONSTATE_FIRING) {
            PlaySlot(s, p->m_pRwClump, g_full, ShootClip(w->m_eWeaponType), 0);
        } else if (p->bIsAimingGun) {
            PlaySlot(s, p->m_pRwClump, g_full, ShootClip(w->m_eWeaponType), 0);
        } else {
            Stop(s);
        }
    }
}

static bool Edge(int i, int vk) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool e = down && !g_key[i];
    g_key[i] = down;
    return e;
}

#include "maxpayne_menu.h"

// ---- MP controls: Max always faces the crosshair; the left stick strafes/backpedals. ----
// SA's own aim mode is exactly that (CJ faces the camera, CTaskSimpleUseGun picks
// GUNMOVE_FWD/L/R/BWD from the stick, and those are Max's clips in MP_BODY), so while armed on
// foot we hold SA's "target" input. Runs right after CPad::UpdatePads (call at 0x53BEE6 in
// CGame::Process) so the player task sees it this frame; gameProcessEvent is too late.
static unsigned g_nativeUntil = 0;

// Last-used device. Forcing the target input with mouse+keyboard locks CJ into SA's mouse aim
// (user report), so MP controls only act while the Xbox pad was used last.
#include <xinput.h>
static short g_padRX = 0;  // right stick X (XInput), free walk uses it to tell look from drift
static bool PadInUse() {
    static bool pad = true;
    static DWORD (WINAPI* get)(DWORD, XINPUT_STATE*) = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"})
            if (HMODULE h = LoadLibraryA(dll)) { get = (decltype(get))GetProcAddress(h, "XInputGetState"); if (get) break; }
        Log("xinput %s", get ? "ok" : "missing");
    }
    XINPUT_STATE st{};
    if (get && get(0, &st) == ERROR_SUCCESS) {
        const XINPUT_GAMEPAD& g = st.Gamepad;
        g_padRX = g.sThumbRX;
        { static DWORD lx = 0; if ((g.sThumbLX || g.sThumbRX) && GetTickCount() - lx > 1000) { lx = GetTickCount(); Log("xi LX=%d RX=%d pad=%d,%d", g.sThumbLX, g.sThumbRX, CPad::GetPad(0)->NewState.LeftStickX, CPad::GetPad(0)->NewState.RightStickX); } }
        if (g.wButtons || g.bLeftTrigger > 40 || g.bRightTrigger > 40 || abs(g.sThumbLX) > 9000 ||
            abs(g.sThumbLY) > 9000 || abs(g.sThumbRX) > 9000 || abs(g.sThumbRY) > 9000) {
            if (!pad) Log("input: pad");
            pad = true;
        }
    }
    const CMouseControllerState& m = CPad::NewMouseControllerState;
    bool kbm = m.lmb || m.rmb || fabsf(m.x) > 2.0f || fabsf(m.y) > 2.0f;
    for (int k : {int('W'), int('A'), int('S'), int('D'), VK_SPACE, VK_LSHIFT}) kbm |= (GetAsyncKeyState(k) & 0x8000) != 0;
    if (kbm && pad) { Log("input: mouse/keyboard"); pad = false; }
    return pad;
}
// Xbox Max Payne right-stick shaping (traced from payne-recomp sub_002954B0): per-axis deadzone
// X 0.45 / Y 0.60, rescale, then cubic (0.8x)^3, full stick = 1.024 -> normalised to 1.
// ponytail: output stays in SA's own camera rate (no 184 deg/s match); tune kLookGain in game.
static const float kLookGain = 1.0f;
static short ShapeStick(short v, float dz) {
    float s = fabsf(v) / 127.0f; if (s <= dz) return 0;
    float x = fminf((s - dz) / (1 - dz), 1.0f), r = powf(0.8f * x, 3) / 0.512f * kLookGain;
    return (short)(copysignf(fminf(r, 1.0f), (float)v) * 127);
}

// ---- Pad camera modes. With the pad, CCam::Process (0x527A09) runs the console follow cam
// Process_FollowPed_SA 0x522D40 (the "locked to the body" feel; GInput sets 0x8CCF00 so the PC
// mouse cam 0x50F970 is skipped). Its pitch springs back to ~-0.15 and barely moves (traced).
//  CURRENT: keep it, and hand it the shaped right stick for its call (see FollowPedImpl).
//  MAX PAYNE: PC mouse cam, stick fed as Xbox rate (shaped * 1.024 * pi rad/s, both axes).
//  GTA SA: PC mouse cam with SA's own stick handling.
static float WrapPi(float a) { if (!isfinite(a)) return 0; return remainderf(a, 6.2831853f); }
static short g_camStickX = 0, g_camStickY = 0;  // shaped right stick, cached for the camera
static float g_pitchOfs = 0;  // our stick pitch, applied to the finished CCam vectors (see VectorsHook)
// SA reads m_vecFront as its own state next frame, so the render-time rotation has to be undone before its
// Process runs; otherwise its collision reacts to the pitched view (log: dist 3.64 -> 0.715 at po=1.300).
static CVector g_baseFront, g_baseSource; static bool g_haveBase = false;
static CVector g_camPiv; static float g_camDist = 0;  // follow-cam target and eye distance, for the orbit
static CVector RotPitch(CVector v, float a) {  // tilt v about the horizontal axis perpendicular to it
    float h = sqrtf(v.x * v.x + v.y * v.y); if (h < 1e-4f) return v;
    float nh = h * cosf(a) - v.z * sinf(a), nz = h * sinf(a) + v.z * cosf(a);
    return CVector(v.x / h * nh, v.y / h * nh, nz);
}
static bool g_camYawLock = false;  // hysteresis state for the strafe yaw-lock below
static CVector g_camT, g_camPed;  // diag: follow-cam target vs ped position at camera time
// Max Payne camera (mode 1): eye on a traced pitch curve (kMpCurve below), view along the pitch.
// Input: Xbox stick curve (1.5*sens+0.5)*(0.8x)^3 up to 1.024*pi rad/s (traced in the recomp).
// SA's own follow cam is NOT run here: it reads m_vecFront/m_vecSource back as its state, so every override
// fed itself and pinned at the clamp (log: fz=0.985 with the stick neutral, dist 0.89).
static int g_mpCamFrames = 0, g_mpHit = 0;
static CCam* g_liveCam = nullptr;  // last cam CCam processed (source/front = where the crosshair points)
static float g_mpYaw = 0, g_mpPitch = 0; static bool g_mpInit = false;
// Geometry traced from the recomp (MP_CAMLOG view matrices + MP_WLOG world matrices, tutorial alley, open
// ground, 2026-11-17): eye position relative to Max's root object (1.56 above the ground) as a function of
// the look pitch, in his heading frame. Units match SA metres (Max's hip bone 0.8, root 1.56 above the
// ground). The camera is NOT aimed at Max: the view direction is the pitch itself and the eye slides on this
// curve, so looking down carries the eye forward over his head (-88 deg = 0.6 above his root, straight down,
// he drops off the bottom of the screen) and looking up drops it behind his back to hip height.
// Columns: pitch (deg), back (behind root, along heading), up (above root). Constant 0.155 to the right.
static const float kMpCurve[][3] = {
    {-88, -0.15f, 0.60f}, {-86, -0.19f, 0.65f}, {-80.2f, -0.15f, 0.76f}, {-77.3f, -0.11f, 0.83f},
    {-71.4f, 0.01f, 0.99f}, {-65.5f, 0.18f, 1.17f}, {-56.6f, 0.57f, 1.47f}, {-53.6f, 0.74f, 1.57f},
    {-44.4f, 1.36f, 1.81f}, {-38.2f, 1.86f, 1.89f}, {-35.1f, 2.12f, 1.90f}, {-32.0f, 2.39f, 1.88f},
    {-28.9f, 2.66f, 1.84f}, {-25.9f, 2.92f, 1.77f}, {-22.8f, 3.18f, 1.68f}, {-19.7f, 3.41f, 1.56f},
    {-16.7f, 3.62f, 1.42f}, {-13.6f, 3.80f, 1.25f}, {-10.6f, 3.95f, 1.07f}, {-7.6f, 4.06f, 0.87f},
    {-4.6f, 4.13f, 0.67f}, {-1.7f, 4.14f, 0.47f}, {1.3f, 4.10f, 0.27f}, {4.1f, 4.02f, 0.09f},
    {7.0f, 3.90f, -0.08f}, {9.9f, 3.74f, -0.23f}, {12.7f, 3.57f, -0.36f}, {15.5f, 3.37f, -0.46f},
    {18.3f, 3.17f, -0.53f}, {21.1f, 2.96f, -0.59f}, {23.8f, 2.76f, -0.63f}, {26.6f, 2.56f, -0.65f},
    {29.4f, 2.38f, -0.65f}, {32.1f, 2.20f, -0.65f}, {34.9f, 2.04f, -0.64f}, {37.7f, 1.90f, -0.62f},
    {40.6f, 1.78f, -0.60f}, {43.4f, 1.67f, -0.59f}, {46.3f, 1.57f, -0.57f}, {49.3f, 1.48f, -0.57f},
    {52.2f, 1.41f, -0.57f}, {55.2f, 1.35f, -0.57f}, {64.3f, 1.19f, -0.63f}, {67.4f, 1.14f, -0.66f},
    {76.9f, 1.00f, -0.77f}, {80.0f, 0.95f, -0.81f}};
static const int kMpCurveN = sizeof(kMpCurve) / sizeof(kMpCurve[0]);
static const float kMpRootZ = 1.56f, kMpRight = 0.155f;
static const float kMpPitchMin = -88.0f * 3.14159265f / 180, kMpPitchMaxUp = 80.0f * 3.14159265f / 180;
static void MpCurve(float pitch, float& back, float& up) {
    float d = pitch * 180.0f / 3.14159265f; int i = 0;
    while (i < kMpCurveN - 2 && d > kMpCurve[i + 1][0]) i++;
    float t = fmaxf(0.0f, fminf(1.0f, (d - kMpCurve[i][0]) / (kMpCurve[i + 1][0] - kMpCurve[i][0])));
    back = kMpCurve[i][1] + t * (kMpCurve[i + 1][1] - kMpCurve[i][1]);
    up = kMpCurve[i][2] + t * (kMpCurve[i + 1][2] - kMpCurve[i][2]);
}
static const float kSaOriginZ = 1.0f;  // SA ped origin above its feet
static void __fastcall VectorsHook(CCam* c, void*);
static bool g_aimHeld = false;    // native aim: the player's own aim button is down this frame (hipfire = false)
static bool g_zoomWant = false;  // set by the cam hooks: SA's aim cam is running for the PLAYER's aim (native aim / SA gunfire)
static float g_zoom = 0;         // 0..1 smoothed
static void MPCam(CCam* c, CPlayerPed* p, CPad* pad, float dt) {
    static DWORD lastCall = 0; DWORD now = GetTickCount();
    if (now - lastCall > 700) g_mpInit = false;  // away (car, cutscene, other cam): come back behind the ped
    lastCall = now;
    if (g_camMode == 1 && !g_mpInit) {  // the SA cam's vectors are not trustworthy here (log: yaw 0 / pitch -1.309 at load)
        float h = p->m_fHeadingCurrent;
        g_mpYaw = atan2f(-cosf(h), sinf(h));
        g_mpPitch = -0.08f;  // MP rest pitch -4.6 deg
    }
    if (g_camMode != 1) { g_mpInit = false; return; }
    // g_camStickX/Y are captured in UpdatePadsHook after ForceAim has shaped them: reading pad->NewState here
    // gives zeros, because GInput's free aim consumes the right stick before the camera runs.
    float sx = g_camStickX / 127.0f, sy = g_camStickY / 127.0f;
    g_mpYaw -= sx * 1.024f * 3.14159265f * dt;
    g_mpPitch -= sy * 1.024f * 3.14159265f * dt;
    g_mpPitch = fmaxf(kMpPitchMin, fminf(kMpPitchMaxUp, g_mpPitch));
    g_mpYaw = WrapPi(g_mpYaw);
    // root = MP's root object height above the feet; SA's ped origin is ~1.0 above the feet.
    CVector root = p->GetPosition() + CVector(0, 0, kMpRootZ - kSaOriginZ);
    float cp = cosf(g_mpPitch), sp = sinf(g_mpPitch);
    CVector fh(-cosf(g_mpYaw), -sinf(g_mpYaw), 0), right(fh.y, -fh.x, 0);
    CVector front(fh.x * cp, fh.y * cp, sp);
    g_zoom += ((g_zoomWant ? 1.0f : 0.0f) - g_zoom) * fminf(1.0f, dt * 6.0f);  // ~0.25 s focus-in, SA's feel
    float back, up; MpCurve(g_mpPitch, back, up);
    CVector eye = root - fh * back + CVector(0, 0, up) + right * kMpRight;
    // SA's native aim zoom is a dolly-in, not FOV: source distance 3.515 idle -> 2.702 aiming (mode 0 trace).
    // Scale the whole eye offset so the focus reads the same in mode 1.
    eye = root + (eye - root) * (1.0f - 0.231f * g_zoom);
    {   // camera collision: pull the eye toward the root on a hit, ease back out, so walls never show their
        // back side and nothing pops.
        static float s_dist = 5.0f;
        CVector off = eye - root; float len = off.Magnitude();
        alignas(4) unsigned char cpt[0x2C] = {}; CEntity* hitEnt = nullptr;
        auto los = (bool(__cdecl*)(const CVector&, const CVector&, void*, CEntity*&, bool, bool, bool, bool, bool, bool, bool, bool))0x56BA00;
        float want = len;
        if (len > 0.05f && los(root, eye, cpt, hitEnt, true, false, false, true, false, false, true, false)) {
            CVector hit = *(CVector*)cpt;  // CColPoint::m_vecPoint
            want = fmaxf(0.0f, (hit - root).Magnitude() - 0.30f); g_mpHit++;
        }
        if (!g_mpInit || want < s_dist) s_dist = want; else s_dist = fminf(want, s_dist + 2.5f * dt);
        if (len > 0.05f && s_dist < len) eye = root + off * (s_dist / len);
    }
    c->m_vecSource = eye;
    c->m_vecFront = front;  // MP looks along the pitch, not at Max (he leaves the bottom of the screen looking down)
    c->m_fHorizontalAngle = g_mpYaw;
    c->m_fVerticalAngle = g_mpPitch;
    c->m_fDistance = (root - eye).Magnitude();
    c->m_fFOV = 70.0f;  // SA's cams set this every frame; a fresh CCam after a load has 0 = black screen
    // SA's cam functions end with GetVectorsReadyForRW (0x509CE0), which rebuilds m_vecUp; MPCam replaces them,
    // so up stayed (0,0,1) and the render matrix was skewed: the view kept looking level whatever the pitch
    // (log: F=0,0.26,-0.97 U=0,0,1; screens at full down showed the ceiling). This was the "no vertical rotation".
    VectorsHook(c, nullptr);
    // SA blends 350 ms between cams when the mode changes (FollowPed <-> Aim on LT): the render matrix then slides
    // from the old cam's pose, which showed as a displaced/zoomed view at the start of native aim. MP has one cam.
    ((CCamera*)0xB6F028)->m_bTransitionState = false;
    g_mpInit = true;
}
static void __fastcall FollowPedImpl(CCam* c, void*, const CVector& t, float o, float sv, float svw, bool scripted) {
    CPad* pad = CPad::GetPad(0);
    float dt = *(float*)0xB7CB5C / 50.0f;  // CTimer::ms_fTimeStep
    float sx = pad ? pad->NewState.RightStickX / 127.0f : 0, sy = pad ? pad->NewState.RightStickY / 127.0f : 0;
    auto pcCam = (void(__thiscall*)(CCam*, const CVector&, float, float, float))0x50F970;
    g_zoomWant = false;
    if (g_camMode == 1 && pad) {
        if (CPlayerPed* pp = FindPlayer(-1)) { MPCam(c, pp, pad, dt); g_mpCamFrames++; }
        return;
    }
    if (g_camMode == 2 && pad) {  // the PC cam reads stick Y inverted vs modes 0/1 (traced: stick down = look up)
        short y = pad->NewState.RightStickY; pad->NewState.RightStickY = -y;
        pcCam(c, t, o, sv, svw);
        pad->NewState.RightStickY = y; return;
    }
    // Pad-only strafe jitter (user m00726: "control issue, not keyboard"): with the stick idle the
    // console cam swings itself behind a ped moving sideways (log: ~0.6 rad/s yaw while strafing,
    // h fixed), in uneven per-frame steps -> CJ shakes on screen. While free-walking, undo that
    // swing: restore the yaw state and rotate this frame's view back about CJ.
    bool lockYaw = g_moveActive && fabsf(sx) < 0.1f;  // hysteresis: a raw stick hovering on the threshold used
    if (lockYaw) g_camYawLock = true;                  // to flip the lock on/off every frame, a jitter of its own
    else if (fabsf(sx) > 0.22f) g_camYawLock = false;
    if (!g_moveActive) g_camYawLock = false;
    lockYaw = g_moveActive && g_camYawLock;
    if (CPlayerPed* pp = FindPlayer(-1)) { g_camT = t; g_camPed = pp->GetPosition(); }
    float h0 = c->m_fHorizontalAngle;
    if (g_haveBase) { c->m_vecFront = g_baseFront; c->m_vecSource = g_baseSource; }  // undo last frame's render-time changes, or SA's collision reacts to them (dist 3.6 -> 0.69)
    ((void(__thiscall*)(CCam*, const CVector&, float, float, float, bool))0x522D40)(c, t, o, sv, svw, scripted);
    g_camPiv = t; g_camDist = (c->m_vecSource - t).Magnitude(); g_baseFront = c->m_vecFront; g_baseSource = c->m_vecSource; g_haveBase = true;
    if (lockYaw) {
        float a = -WrapPi(c->m_fHorizontalAngle - h0), cs = cosf(a), sn = sinf(a);
        auto rz = [&](CVector v) { return CVector(v.x * cs - v.y * sn, v.x * sn + v.y * cs, v.z); };
        c->m_fHorizontalAngle = h0;
        c->m_vecSource = t + rz(c->m_vecSource - t);
        c->m_vecFront = rz(c->m_vecFront);
        c->m_vecUp = rz(c->m_vecUp);
    }
    // SA's own follow cam takes the stick and answers ~4 deg/s at full deflection (log: fz -0.149 ->
    // -0.030 over 3 s), so Max Payne's pitch has to come from us. Post-rotating the front here fought
    // SA's internal vertical angle: its own pitch flipped +0.785 <-> -0.785 and the distance 1.42 <->
    // 3.49 every few frames while our offset sat still = the flicker. So integrate the offset here and
    // apply it in VectorsHook, on the finished vectors, where nothing downstream can undo it.
    if (pad) g_pitchOfs = 0;  // mode 0 keeps SA's own cam: pitching it from outside fought SA's state
    if (g_moveActive) {  // pad trace (run3): on long frames the cam sat one ped step behind sideways
        // (CJ 0.00/0.03/0.05 m off-centre, alternating with the uneven frame times) = the strafe shake;
        // keyboard runs were always 0. Re-centre CJ sideways in the view.
        if (CPlayerPed* pp = FindPlayer(-1)) {
            CVector d = pp->GetPosition() - c->m_vecSource;
            float fx = c->m_vecFront.x, fy = c->m_vecFront.y, m = sqrtf(fx * fx + fy * fy);
            if (m > 1e-3f) {
                float rx = fy / m, ry = -fx / m, lat = d.x * rx + d.y * ry;
                if (fabsf(lat) < 0.3f) { c->m_vecSource.x += lat * rx; c->m_vecSource.y += lat * ry; }
            }
        }
    }
}
// CCam::GetVectorsReadyForRW (0x509CE0) runs last, right before the vectors are handed to the renderer,
// so the offset applied here cannot be overwritten by SA later in the frame. RedirectJump overwrites the
// function's first bytes, so this REPLACES it: calling 0x509CE0 from here would recurse into the hook.
static void __fastcall VectorsHook(CCam* c, void*) {
    CVector& f = c->m_vecFront;
    float l = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
    if (l > 1e-9f) { f.x /= l; f.y /= l; f.z /= l; }
    if (f.x == 0.0f && f.y == 0.0f) f.x = f.y = 0.0001f;
    float ax = f.y, ay = -f.x, al = sqrtf(ax * ax + ay * ay);  // cross(front, world up)
    if (al > 1e-9f) { ax /= al; ay /= al; }
    c->m_vecUp = CVector(ay * f.z, -ax * f.z, ax * f.y - ay * f.x);  // cross(a, front)
}
// Armed + MAX PAYNE CONTROLS forces R1 (ForceAim), so SA runs its AIM cam (0x521500) instead of the follow cam:
// mode 1 has to own that one too, or the view never comes from MPCam (user: "no vertical rotation").
static void TraceCam(CCam* c);
static void __fastcall AimCamHook(CCam* c, void*, const CVector& t, float o, float sv, float svw) {
    CPad* pad = CPad::GetPad(0); CPlayerPed* pp = FindPlayer(-1);
    g_zoomWant = !(g_mpFire && (!g_nativeAim || !g_aimHeld));  // MP gunfire: focus only for the player's own native aim, never hipfire
    if (g_camMode == 1 && pad && pp) { MPCam(c, pp, pad, *(float*)0xB7CB5C / 50.0f); g_mpCamFrames++; }
    else ((void(__thiscall*)(CCam*, const CVector&, float, float, float))0x521500)(c, t, o, sv, svw);
    TraceCam(c);
}
static void __fastcall FollowPedHook(CCam* c, void* e, const CVector& t, float o, float sv, float svw, bool scripted) {
    FollowPedImpl(c, e, t, o, sv, svw, scripted);
    TraceCam(c);
}
static void TraceCam(CCam* c) {
    g_liveCam = c;
    static DWORD chk = 0; static bool on = false;
    if (GetTickCount() - chk > 500) { chk = GetTickCount(); on = GetFileAttributesA("mp_trace.txt") != INVALID_FILE_ATTRIBUTES; }
    CPlayerPed* p = FindPlayer(-1); CPad* pad = CPad::GetPad(0);
    if (!on || !p || !pad) return;
    CVector d = p->GetPosition() - c->m_vecSource;
    float fx = c->m_vecFront.x, fy = c->m_vecFront.y, m = sqrtf(fx * fx + fy * fy) + 1e-6f;
    Log("cf %u m=%d dt=%.4f rs=%d,%d ls=%d,%d yaw=%.4f fz=%.4f h=%.4f lat=%.4f dist=%.3f mv=%d dir=%d turn=%d cs=%d,%d po=%.3f nm=%d mpf=%d fov=%.1f src=%.2f,%.2f,%.2f ped=%.2f,%.2f,%.2f F=%.2f,%.2f,%.2f M=%.2f,%.2f,%.2f Mat=%.2f,%.2f,%.2f nc=%d hit=%d U=%.2f,%.2f,%.2f MR=%.2f,%.2f,%.2f MA=%.2f,%.2f,%.2f", GetTickCount(), g_camMode,
        *(float*)0xB7CB5C / 50.0f, pad->NewState.RightStickX, pad->NewState.RightStickY, pad->NewState.LeftStickX, pad->NewState.LeftStickY,
        atan2f(fy, fx), c->m_vecFront.z, p->m_fHeadingCurrent, (d.x * fy - d.y * fx) / m, d.Magnitude(), g_moveActive, g_moveDir, g_turning, g_camStickX, g_camStickY, g_mpPitch, (int)c->m_nMode, g_mpCamFrames, c->m_fFOV, c->m_vecSource.x, c->m_vecSource.y, c->m_vecSource.z, p->GetPosition().x, p->GetPosition().y, p->GetPosition().z, c->m_vecFront.x, c->m_vecFront.y, c->m_vecFront.z, ((CCamera*)0xB6F028)->m_mCameraMatrix.pos.x, ((CCamera*)0xB6F028)->m_mCameraMatrix.pos.y, ((CCamera*)0xB6F028)->m_mCameraMatrix.pos.z, ((CCamera*)0xB6F028)->m_mCameraMatrix.up.x, ((CCamera*)0xB6F028)->m_mCameraMatrix.up.y, ((CCamera*)0xB6F028)->m_mCameraMatrix.up.z, (int)(c - ((CCamera*)0xB6F028)->m_aCams) * 10 + ((CCamera*)0xB6F028)->m_nActiveCam, g_mpHit, c->m_vecUp.x, c->m_vecUp.y, c->m_vecUp.z, ((CCamera*)0xB6F028)->m_mCameraMatrix.right.x, ((CCamera*)0xB6F028)->m_mCameraMatrix.right.y, ((CCamera*)0xB6F028)->m_mCameraMatrix.right.z, ((CCamera*)0xB6F028)->m_mCameraMatrix.at.x, ((CCamera*)0xB6F028)->m_mCameraMatrix.at.y, ((CCamera*)0xB6F028)->m_mCameraMatrix.at.z);
}
// Crosshair: SA draws its own only while aiming (CHud::Draw calls DrawCrossHairs 0x58E020 at 0x58FBBF). MP shows a
// small dot whenever the player can shoot. Placed where SA's shots land (TheCamera 3rd-person CHair mult, which
// MPCam sets to the screen centre).
static float& CHairX() { return *(float*)0xB6EC14; }
static float& CHairY() { return *(float*)0xB6EC10; }
static void CrossHook() {
    CPlayerPed* p = FindPlayer(-1);
    if (!g_mpCross) { ((void(__cdecl*)())0x58E020)(); return; }
    if (g_menuOpen || !p || p->m_pVehicle || p->bInVehicle || !Armed(p) || p->GetWeapon()->m_eWeaponType == WEAPONTYPE_SNIPERRIFLE) {
        if (p && p->GetWeapon()->m_eWeaponType == WEAPONTYPE_SNIPERRIFLE) ((void(__cdecl*)())0x58E020)();
        return;
    }
    if (g_camMode == 1 && g_mpFire) { CHairX() = 0.5f; CHairY() = 0.5f; }  // MP: shots go through the screen centre
    float x = CHairX() * RsGlobal.maximumWidth, y = CHairY() * RsGlobal.maximumHeight, r = RsGlobal.maximumHeight / 360.0f;
    CSprite2d::DrawRect(CRect(x - 2 * r, y - 2 * r, x + 2 * r, y + 2 * r), CRGBA(0, 0, 0, 200));
    CSprite2d::DrawRect(CRect(x - 1 * r, y - 1 * r, x + 1 * r, y + 1 * r), CRGBA(255, 255, 255, 255));
}
static void ForceAim(CPlayerPed* p, CPad* pad) {
    // camera self-test (temporary): mp_camtest.txt in the game dir -> scripted right stick, R1 forced or not
    static DWORD testT0 = 0; static bool noForce = false;
    if (pad && !testT0 && GetFileAttributesA("mp_camtest.txt") != INVALID_FILE_ATTRIBUTES) { if (FILE* f = fopen("mp_camtest.txt", "r")) { int m; if (fscanf(f, "%d", &m) == 1) g_camMode = m; fclose(f); }
        DeleteFileA("mp_camtest.txt"); testT0 = GetTickCount(); Log("camtest start mode=%d", g_camMode); }
    {   // test hook: mp_cammode.txt (int) sets the pad camera mode, then is deleted
        static DWORD lastChk = 0;
        if (GetTickCount() - lastChk > 500) { lastChk = GetTickCount();
            if (FILE* f = fopen("mp_cammode.txt", "r")) { int m; if (fscanf(f, "%d", &m) == 1) { g_camMode = m; Log("cammode set %d", m); } fclose(f); DeleteFileA("mp_cammode.txt"); }
 }
    }
    noForce = false;
    if (pad && testT0) {
        DWORD t = GetTickCount() - testT0;
        static const struct { DWORD end; short y; bool force; } ph[] = {{2500,-127,1},{4000,0,1},{6500,127,1},{8000,0,1},{10500,-127,0},{12000,0,0},{14500,127,0},{16000,0,0}};
        int i = 0; while (i < 8 && t >= ph[i].end) i++;
        if (i == 8) { testT0 = 0; Log("camtest end"); }
        else { static int lastI = -1; if (i != lastI) { lastI = i; Log("camtest phase %d y=%d force=%d", i, ph[i].y, ph[i].force); }
            pad->NewState.RightStickX = 0; pad->NewState.RightStickY = ph[i].y ? ph[i].y : 1; noForce = !ph[i].force; }
    }
    if (pad) {  // camera diagnostics: which SA cam mode runs and whether pitch moves
        static DWORD last = 0;
        CCamera& cam = *(CCamera*)0xB6F028; CCam& c = cam.m_aCams[cam.m_nActiveCam];
        if (GetTickCount() - last > (testT0 ? 100u : 150u) && (testT0 || abs(pad->NewState.RightStickX) > 20 || abs(pad->NewState.RightStickY) > 20)) {
            last = GetTickCount();
            Log("cam cm=%d fz=%.3f mode=%d vert=%.3f horiz=%.3f alpha=%.3f beta=%.3f rs=%d,%d r1=%d mp=%d area=%d ped=%.1f,%.1f,%.1f src=%.1f,%.1f,%.1f ms=%d walk=%d armed=%d",
                g_camMode, c.m_vecFront.z, (int)c.m_nMode, c.m_fVerticalAngle, c.m_fHorizontalAngle, c.m_fTrueAlpha, c.m_fTrueBeta, pad->NewState.RightStickX,
                pad->NewState.RightStickY, pad->NewState.RightShoulder1, (int)g_mpControls, *(int*)0xB72914,
                p ? p->GetPosition().x : 0, p ? p->GetPosition().y : 0, p ? p->GetPosition().z : 0,
                c.m_vecSource.x, c.m_vecSource.y, c.m_vecSource.z, p ? (int)p->m_nMoveState : -1, (int)g_moveActive, p ? (int)Armed(p) : 0);
        }
    }
    if (g_stickShape && g_mpControls && !g_menuOpen && pad && !pad->DisablePlayerControls && PadInUse()) {
        pad->NewState.RightStickX = ShapeStick(pad->NewState.RightStickX, 0.45f);
        pad->NewState.RightStickY = ShapeStick(pad->NewState.RightStickY, 0.60f);
    }
    // dev helper (like mp_trace.txt): mp_gun.txt hands CJ a pistol so the gunfire modes can be tested from a
    // cold save; NATIVE AIM / GUNFIRE themselves are menu options.
    if (p && GetFileAttributesA("mp_gun.txt") != INVALID_FILE_ATTRIBUTES) {
        ((void(__thiscall*)(CPed*, int, unsigned, bool))0x5E6080)(p, 22, 200, true);
        ((void(__thiscall*)(CPed*, int))0x5E61F0)(p, 2);
    }
    if (noForce || !g_mpControls || g_menuOpen || !p || !pad || p->m_pVehicle || pad->DisablePlayerControls || !Armed(p) || !PadInUse()) return;
    if (p->GetWeapon()->m_eWeaponType == WEAPONTYPE_SNIPERRIFLE) return;  // its aim is the scope
    CControllerState& s = pad->NewState;
    if (s.ButtonTriangle) g_nativeUntil = GetTickCount() + 2000;  // let SA enter the car
    if (s.ButtonCross || GetTickCount() < g_nativeUntil) return;   // sprint: SA native run
    g_aimHeld = s.LeftShoulder2 != 0 || s.RightShoulder1 != 0;  // the player's own button, before we touch anything
    if (!g_mpFire || g_nativeAim) return;  // SA gunfire / native aim: the aim buttons are the player's
    // MP gunfire has no aim at all (user m01546: hipfire is not aiming): swallow the aim buttons so SA stays in
    // its hip state and never enters its own aim. The bullet still leaves along the dot (GunFireHook).
    s.LeftShoulder2 = 0; s.RightShoulder1 = 0;
}

// MP hipfire: FireGun (CTaskSimpleUseGun) passes targetPosn = nullptr while not aiming, and CWeapon::Fire then
// shoots along CJ's heading. We redirect only FireGun's call to Fire (0x61ECCD; the function itself stays
// intact, so it can still be called) and hand it the camera's crosshair point instead -> the bullet lands where
// the dot is, with SA's hip animation untouched. Native aim keeps SA's own target while its aim button is held.
static bool __fastcall GunFireHook(void* w, void*, CEntity* firedBy, CVector* startPosn, CVector* barrelPosn,
                                   CEntity* targetEnt, CVector* targetPosn, CVector* altPosn) {
    auto fire = (bool(__thiscall*)(void*, CEntity*, CVector*, CVector*, CEntity*, CVector*, CVector*))0x742300;
    CPlayerPed* pl = FindPlayer(-1); CPad* pad = CPad::GetPad(0);
    bool aiming = pad && (pad->NewState.LeftShoulder2 || pad->NewState.RightShoulder1);
    // PINPOINT toggles. SA makes player shots drift by (skillMax - weaponSkill) / CWeaponInfo::m_fAccuracy
    // (FireInstantHit 0x73FC04) = an angular error that grows with distance - the "reduced accuracy" of vanilla.
    // For the duration of this one shot give every weapon entry a huge accuracy (error ~ 0), then put the table
    // back: no permanent change, NPCs unaffected. 0xB7CD64 is written by the shotgun branch there, save it too.
    const bool pinpoint = pl && firedBy == (CEntity*)pl && (aiming ? g_mpAimAcc : g_mpHipAcc);
    static float savedAcc[80]; static float savedRange = 0.0f;
    unsigned char* wi = (unsigned char*)0xC8AAB8;
    if (pinpoint) {
        for (int i = 0; i < 80; ++i) { float* a = (float*)(wi + i * 0x70 + 0x38); savedAcc[i] = *a; *a = 1.0e7f; }
        savedRange = *(float*)0xB7CD64;
    }
    auto pinDone = [&] { if (pinpoint) { for (int i = 0; i < 80; ++i) *(float*)(wi + i * 0x70 + 0x38) = savedAcc[i];
                                         *(float*)0xB7CD64 = savedRange; } };
    if (g_mpFire && g_liveCam && pl && firedBy == (CEntity*)pl && !targetEnt && !aiming) {
        // Target = where the crosshair actually lands, found by a raycast down the camera ray. A fixed far point
        // is NOT enough: the bullet starts at the muzzle, so aiming at a point 200 m out misses by the muzzle's
        // lateral offset at anything closer (user: "native aim hipfire lacks accuracy for distant pedestrians").
        auto los = (bool(__cdecl*)(const CVector&, const CVector&, void*, CEntity*&, bool, bool, bool, bool, bool, bool, bool, bool))0x56BA00;
        CVector to = g_liveCam->m_vecSource + g_liveCam->m_vecFront * 300.0f, s = g_liveCam->m_vecSource + g_liveCam->m_vecFront * 0.5f, tgt = to;
        CEntity* tgtEnt = nullptr;
        for (int i = 0; i < 3; ++i) {
            alignas(4) unsigned char cpt[0x2C] = {}; CEntity* hit = nullptr;
            if (!los(s, to, cpt, hit, true, true, true, true, false, false, false, false)) break;
            CVector hp = *(CVector*)cpt;
            if (hit != (CEntity*)pl) { tgt = hp; tgtEnt = hit; break; }
            s = hp + g_liveCam->m_vecFront * 0.4f;  // looking down: the camera ray crosses CJ first
        }
        // A point alone may be ignored (FireInstantHit 0x73FB10 is not decompiled); hand it the hit entity too so
        // SA takes its aimed-at-target branch instead of firing along the ped's heading.
        if (GetFileAttributesA("mp_aimlog.txt") != INVALID_FILE_ATTRIBUTES) {
            // angle between the ped's forward and the camera's forward (2D): 0 = body on the dot
            CVector cf = g_liveCam->m_vecFront, pf = pl->GetForward(); cf.z = pf.z = 0;
            float ang = (cf.Magnitude() > 0.01f && pf.Magnitude() > 0.01f)
                ? WrapPi(atan2f(pf.y, pf.x) - atan2f(cf.y, cf.x)) : 9.9f;
            Log("aim sub=1 hitent=%d ang=%.1f pin=%d wt=%d acc0=%.2f acc=%.0f rng=%.1f tgt=%.2f,%.2f,%.2f ped=%.2f,%.2f,%.2f fwd=%.2f,%.2f cam=%.2f,%.2f",
                tgtEnt ? (int)tgtEnt->m_nType + 1 : 0, ang * 57.2958f, (int)pinpoint, *(int*)w,
                savedAcc[*(int*)w], *(float*)(wi + *(int*)w * 0x70 + 0x38), *(float*)(wi + *(int*)w * 0x70 + 0x8),
                tgt.x, tgt.y, tgt.z, pl->GetPosition().x,
                pl->GetPosition().y, pl->GetPosition().z, pf.x, pf.y, cf.x, cf.y);
        }
        bool r = fire(w, firedBy, startPosn, barrelPosn, tgtEnt, &tgt, altPosn);
        pinDone();
        return r;
    }
    bool r = fire(w, firedBy, startPosn, barrelPosn, targetEnt, targetPosn, altPosn);
    pinDone();
    return r;
}

// ---- Free walk like Max: stick left = sidestep left, stick back = backpedal, body keeps
// facing the camera. How SA turns CJ (the pivot): PlayerControlZelda writes the stick's world
// angle into m_fHeadingGoal every frame, and CPed::CalculateNewVelocity (0x5E4C50, called only
// from CPed::ProcessControl at 0x5E92ED) turns m_fHeadingCurrent toward it at
// m_fHeadingChangeRate. We wrap that call and, while free-walking, replace the goal with the
// camera heading, so SA itself turns CJ to face away from the camera and never toward the
// stick. The walk/run slots are swapped (ApplyBody) to Max's clip for the stick's 45-deg sector
// and SA moves him by that clip's root travel (sideways/backwards).
// Stick input comes from CPad::GetPedWalkLeftRight/UpDown (0x53FC90/0x53FD30): GInputSA feeds
// the Xbox pad through those, NewState.LeftStick stays 0 (the 15:30 build read NewState, so it
// never switched on with the pad: no "freewalk" lines in the log).
// Small local math (build.sh links only part of the SDK; addresses for game globals).
// Was two while-loops: for a huge angle (|a| > ~1e8, e.g. a garbage camera value during a respawn) a -= 2pi
// no longer changes the float and the loop never ends = main thread at 100% CPU (the hospital freeze?).
static float CamOrientation() { return *(float*)(0xB6F028 + 0x150); }  // TheCamera.m_fOrientation
// SA's GetRadianAngleBetweenPoints(0,0,x,y) == atan2(x, -y) (checked against General.cpp)
static float StickAngle(float x, float y) { return atan2f(x, -y); }
static float CameraHeading() { return WrapPi(StickAngle(0, -1) - CamOrientation()); }
// Heading CJ keeps while free-walking. Not the live camera heading: SA's follow cam trails CJ's
// position, so a sidestep swings the camera around him; facing that made sidesteps curve into a
// circle and backpedals drift. CJ only turns with camera turns the player makes (right stick /
// mouse X); drift is ignored and the camera settles back behind him.
static float g_walkHeading = 0, g_lastCam = 0;

static void FreeWalk(CPlayerPed* p, CPad* pad) {
    bool on = g_mpLoco && g_mpControls && !g_menuOpen && p && pad && !p->m_pVehicle && !p->bInVehicle &&
              p->bIsStanding && !p->bIsDucking && !pad->DisablePlayerControls;
    int k = 0;
    if (on) {
        float x = ((short(__thiscall*)(CPad*))0x53FC90)(pad);  // GetPedWalkLeftRight
        float y = ((short(__thiscall*)(CPad*))0x53FD30)(pad);  // GetPedWalkUpDown
        bool target = ((bool(__thiscall*)(CPad*))0x540670)(pad);  // GetTarget (aim: GUNMOVE does it)
        bool eligible = !target && !CPad::NewMouseControllerState.rmb && p->m_nMoveState != PEDMOVE_SPRINT;
        on = eligible && x * x + y * y > 30 * 30;
        // Whipping the stick through its centre dropped "on" for a frame or two; every bounce
        // swapped all 12 slots back to SA and forward again, and the first frame after it showed
        // the bind pose (the "--- pose" flash, always right after 'freewalk off/on'). Hold the
        // old state ~0.17 s through a short dip instead.
        static DWORD lastOn = 0;  // wall-clock, not frames: the game may run 30..500 fps
        bool held = false;
        if (on) lastOn = GetTickCount();
        else if (eligible && g_moveActive && GetTickCount() - lastOn < 170) { on = held = true; k = g_moveDir; }
        if (on && !held) {
            // same angle maths as CTaskSimplePlayerOnFoot::PlayerControlZelda; positive = left
            float rel = WrapPi(StickAngle(-x, y) - StickAngle(0, -1));
            k = int(lroundf(-rel / (3.14159265f / 4))) & 7;
            // hysteresis: keep the current direction until the stick is 10 deg past the border
            if (g_moveActive && k != g_moveDir && fabsf(WrapPi(-rel - g_moveDir * 3.14159265f / 4)) < 3.14159265f / 8 + 0.17f)
                k = g_moveDir;
        }
    }
    if (on != g_moveActive || k != g_moveDir) Log("freewalk %s dir=%d", on ? "on" : "off", k);
    float cam = CamOrientation();
    // MPCam's yaw only moves with the look stick (no follow-cam drift like SA's), so syncing the body to the
    // camera every walking frame is what Max does and cannot curve. Tracking only "real" turns (the old
    // PlayerLooking gate, 7000/32767 = 21% deflection) left the body frozen while the camera crept around:
    // CJ ended up facing 20-45 deg off the camera and stayed there (user m01880: "locked in a diagonal
    // facing direction").
    if (on) g_walkHeading = CameraHeading();
    g_lastCam = cam;
    g_moveActive = on;
    g_moveDir = k;
}

// Standing + right stick: Max turns on the spot (camera yaw drives his heading). Native MP:
// a sustained spin plays his TurnLeft/TurnRight feet; short taps just glide (user, m00496).
// g_turnT = how long the camera has been turning; > kTurnStep s -> step clip, else glide.
static const float kTurnStep = 0.35f;
static float g_turnGoal = 0, g_turnT = 0, g_turnLast = 0; static int g_turnDir = 0;
static Slot g_turnSlot;
static void StandTurn(CPlayerPed* p, CPad* pad) {
    float cam = CamOrientation(), dc = WrapPi(cam - g_turnLast); g_turnLast = cam;
    bool idle = g_mpLoco && g_mpControls && !g_menuOpen && p && pad && !g_moveActive && !p->m_pVehicle && !p->bInVehicle &&
                p->bIsStanding && !p->bIsDucking && !pad->DisablePlayerControls &&
                !((bool(__thiscall*)(CPad*))0x540670)(pad) && !CPad::NewMouseControllerState.rmb;
    static DWORD lastLook = 0;  // mouse deltas arrive in bursts; 150 ms bridges the gaps (stick is continuous)
    // Any camera yaw change counts as the player looking: MPCam's yaw is only ever moved by the look stick or
    // the mouse (it has no follow-cam drift), so the old 7000/32767 = 21% deflection gate ignored gentle
    // nudges: the camera turned, CJ did not, and shots went 20-45 deg off the dot (user m01880).
    if (fabsf(dc) > 1e-4f) lastLook = GetTickCount();
    g_turning = idle && GetTickCount() - lastLook < 150;
    if (g_turning) {
        g_turnGoal = WrapPi(p->m_fHeadingCurrent - dc);  // same sign as g_walkHeading above
        float dt = *(float*)0xB7CB5C / 50.0f;
        // the camera must really be yawing (>0.15 rad/s): pushing the right stick up/down to pitch leaks
        // some X, which used to start the stepping TURN clip (stale g_turnDir) while the camera only tilted.
        // Smooth the rate first: MPCam's yaw lands in bursts (a zero-yaw frame next to a big one), and the
        // raw per-frame rate reset the timer on those frames, so TURN_L/R faded in and out every few frames
        // - visible as the "jolt" the user reported. ~125 ms EMA rides the burst pattern.
        static float rate = 0.0f;
        rate += ((dt > 0 ? fabsf(dc) / dt : 0.0f) - rate) * fminf(1.0f, dt * 8.0f);
        if (rate > 0.15f) { g_turnT += dt; g_turnDir = dc < 0 ? 1 : -1; }  // heading up = turning left
        else g_turnT = 0;
    } else { g_turnT = 0; g_turnDir = 0; }
    bool step = g_turning && g_turnT > kTurnStep && g_turnDir;
    static bool ls = false, lt = false; if (step != ls || g_turning != lt) Log("standturn turning=%d step=%d dir=%d t=%.2f", g_turning, step, g_turnDir, g_turnT);
    ls = step; lt = g_turning;
    // MP's own TurnLeft/TurnRight, full body (they bring the arms with them; the partial Stand
    // layer steps aside while g_turning so the arms are not frozen). Exported with %inplace so
    // the root does not slide 0.2 m per clip (stride baking used to do that - the "jolt").
    if (step) {
        // Match the idle layer's split: 2_Turn* is Max's TWO-HANDED weapon turn, Turn* his
        // one-handed/unarmed turn. Playing the two-handed take with a pistol put his off hand
        // on an invisible foregrip - the "hands in an odd manner" the user saw.
        CWeapon* w = &p->m_aWeapons[p->m_nSelectedWepSlot];
        bool two = Armed(p) && !OneHand(w->m_eWeaponType);
        PlaySlot(g_turnSlot, p->m_pRwClump, g_body,
                 g_turnDir > 0 ? (two ? "TURN_L" : "TURN1_L") : (two ? "TURN_R" : "TURN1_R"),
                 ANIMATION_LOOPED);
        if (g_turnSlot.a) g_turnSlot.a->m_nFlags |= 0x1 | 0x80;
    } else if (g_turnSlot.a) Stop(g_turnSlot);
}

// While free-walking only the clips of the current stick sector may play. With a real stick the
// sector flips often; every flip left the previous sector's clip (and the forward Run M2) alive
// at 0.4-0.7 blend next to the new one (log: M36 + M40 + M2), SA normalises the weights and the
// legs/arms become a mix of strafe, backpedal and run = the stutter and the wrong arm swing.
static void KillStrays(RpClump* c) {
    if (!g_moveActive) return;
    CAnimBlendHierarchy* ws = Anim("WALK_start", GetBlock("ped"));
    // Only drop SA's WALK_start once a Max clip is actually playing; with it as the sole
    // full-body assoc, killing it left weight 0 and the game died a frame later (3 crash logs).
    bool mpPlaying = false;
    for (auto* a = ((CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0)(c); a;
         a = ((CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0)(a))
        mpPlaying |= InBody(a->m_pHierarchy) && !(a->m_nFlags & ANIMATION_PARTIAL) && a->m_fBlendDelta >= 0 && a->m_fBlendAmount > 0.1f;
    for (auto* a = ((CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0)(c); a;
         a = ((CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0)(a)) {
        // SA's own WALK_start (ped.ifp, forward) starts a frame before free-walk is detected and
        // adds its travel to Max's clip: the 0.3-0.6 m "teleport" on every move start. Drop it.
        if (a->m_pHierarchy == ws) { if (!mpPlaying) continue; a->m_fBlendAmount = 0; a->m_fBlendDelta = -100.0f; a->m_nFlags |= 0x4; continue; }
        if (!InBody(a->m_pHierarchy) || (a->m_nFlags & ANIMATION_PARTIAL) || a->m_fBlendDelta < 0) continue;
        bool keep = false;
        for (auto* h : g_cur) keep |= (h == a->m_pHierarchy);
        if (keep) continue;
        a->m_nFlags |= 0x4;       // BLEND_AUTO_REMOVE
        a->m_fBlendDelta = -8.0f;
    }
}

// SA's own blend update snaps the new clip we create in RefreshRunning from 0 to 1.0 in ONE frame while
// the old one fades normally (log: new M21 1.00 + old M2 0.80 after frame 1 = the jolt: hard cut with
// both clips summed, EnsureFullBlend then eats the old one). Hold the new clip to a ramp of +6/s.
static void HoldFadeIn(RpClump* c) {
    if (!g_fadeIn) return;
    g_fadeT += *(float*)0xB7CB5C / 50.0f;
    float ramp = g_fadeT * 6.0f;
    if (ramp >= 1.0f) { g_fadeIn = nullptr; return; }
    auto first = (CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0;
    auto next = (CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0;
    for (auto* a = first(c); a; a = next(a)) {
        if ((a->m_nFlags & ANIMATION_PARTIAL) || a->m_fBlendDelta < 0 || a->m_fBlendAmount <= ramp) continue;
        bool cur = false;
        for (auto* h : g_cur) cur |= (h == a->m_pHierarchy);
        if (!cur) continue;
        // never clamp below what the fading clips leave over (sum < 1 pops through EnsureFullBlend)
        float others = 0;
        for (auto* o = first(c); o; o = next(o))
            if (o != a && !(o->m_nFlags & ANIMATION_PARTIAL) && InBody(o->m_pHierarchy) && o->m_fBlendDelta < 0) others += o->m_fBlendAmount;
        float want = fmaxf(ramp, 1.0f - others);
        if (a->m_fBlendAmount > want) a->m_fBlendAmount = want;
        a->m_fBlendDelta = 6.0f;
    }
}

// One shared foot phase for every locomotion clip. SA starts/duplicates/re-times walk assocs on its own
// (log: M25 0.95 -> 0.25, new MOVE clip at 0.00 while its partner is at 0.49, dup assoc at a random
// time): crossfading clips with opposite feet = the "foot swap" pop. phase = time - strike (kPhase);
// it advances by dt*speed, and every playing loco clip is forced to phase + its own strike.
static float g_phase = 0; static bool g_phaseOk = false;
static void PhaseLock(RpClump* c) {
    auto first = (CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0;
    auto next = (CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0;
    CAnimBlendAssociation* dom = nullptr;
    for (auto* a = first(c); a; a = next(a)) {
        float T = a->m_pHierarchy->m_fTotalTime;
        if ((a->m_nFlags & ANIMATION_PARTIAL) || T < 0.5f || T > 1.0f || a->m_fBlendAmount < 0.005f || PhaseOf(a->m_pHierarchy) < 0) continue;
        if (!dom || a->m_fBlendAmount > dom->m_fBlendAmount) dom = a;
    }
    if (!dom) { g_phaseOk = false; return; }
    float T = dom->m_pHierarchy->m_fTotalTime;
    if (!g_phaseOk) { g_phase = dom->m_fCurrentTime / T - PhaseOf(dom->m_pHierarchy); g_phaseOk = true; }
    else {
        // Follow the dominant clip's own clock: integrating dt*speed/T drifts from SA's anim clock, so a lone
        // strafe clip got snapped forward ~0.015 every 6 frames (log "phase lock M30 0.05 -> 0.06" x2/step =
        // the strafe jitter). Only a real SA re-time (> 0.1 off the prediction) is still overridden.
        float pred = g_phase + *(float*)0xB7CB5C / 50.0f * dom->m_fSpeed / T;
        float own = dom->m_fCurrentTime / T - PhaseOf(dom->m_pHierarchy);
        float d = own - pred; d -= floorf(d + 0.5f);
        g_phase = fabsf(d) < 0.1f ? own : pred;
    }
    g_phase -= floorf(g_phase);
    for (auto* a = first(c); a; a = next(a)) {
        float Ta = a->m_pHierarchy->m_fTotalTime, p = PhaseOf(a->m_pHierarchy);
        if ((a->m_nFlags & ANIMATION_PARTIAL) || Ta < 0.5f || Ta > 1.0f || p < 0) continue;
        float want = g_phase + p; want -= floorf(want);
        float d = a->m_fCurrentTime / Ta - want; d -= floorf(d + 0.5f);
        if (fabsf(d) > 0.01f) {
            Log("phase lock %s%d %.2f -> %.2f", "M", int(a->m_pHierarchy - (CAnimBlendHierarchy*)0xB4EA40) - g_body->startAnimation, a->m_fCurrentTime / Ta, want);
            ((void(__thiscall*)(CAnimBlendAssociation*, float))0x4CEA80)(a, want * Ta);
        }
    }
}

// ---- Pose inertialization (Max-style continuous motion) ----
// Every pop left on CJ is a discontinuity in the *pose*: SA replaces walk assocs in one frame
// (new clip at t=0, old one gone), our swaps/phase lock re-time clips, partial layers jump in.
// Instead of chasing each source, after SA blends the pose we compare it with the last shown one:
// when the animation state jumps, each bone keeps the offset (shown - new) and that offset decays
// with a short half-life, so the body glides into the new pose. Steady motion is untouched.
struct InterpFrame { float q[4]; float t[3]; };  // RpHAnimBlendInterpFrame (x,y,z,w quat + pos)
static const float kInertiaHalfLife = 0.07f;  // s; 0.07 -> ~90% settled in 0.23 s
static float QDot(const float* a, const float* b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3]; }
static void QMul(const float* a, const float* b, float* o) {  // o = a*b
    float x = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    float y = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    float z = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    float w = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
    o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}
static void QNorm(float* q) { float n = sqrtf(QDot(q, q)); if (n > 1e-6f) for (int i = 0; i < 4; ++i) q[i] /= n; else { q[0] = q[1] = q[2] = 0; q[3] = 1; } }

// Did the animation state jump since last frame? (new/removed weighted assoc, blend step, time jump)
struct AssocSnap { void* a; float blend, tn; };
static bool AnimJumped(RpClump* c, float dt) {
    static AssocSnap prev[32]; static int nPrev = 0;
    AssocSnap cur[32]; int n = 0; bool jump = false;
    for (auto* a = ((CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0)(c); a && n < 32;
         a = ((CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0)(a)) {
        float T = a->m_pHierarchy->m_fTotalTime;
        AssocSnap s{a, a->m_fBlendAmount, T > 0 ? a->m_fCurrentTime / T : 0};
        cur[n++] = s;
        const AssocSnap* p = nullptr;
        for (int i = 0; i < nPrev; ++i) if (prev[i].a == a) p = &prev[i];
        if (!p) { jump |= s.blend > 0.1f; continue; }
        float limit = 0.1f > 10.0f * dt ? 0.1f : 10.0f * dt;  // normal 4..8/s fades are no jump
        if (fabsf(s.blend - p->blend) > limit) jump = true;
        float adv = T > 0 ? dt * a->m_fSpeed / T : 0, d = s.tn - p->tn - adv;
        d -= floorf(d + 0.5f);
        if (s.blend > 0.1f && fabsf(d) > 0.08f) jump = true;
    }
    for (int i = 0; i < nPrev; ++i) {
        bool gone = true;
        for (int k = 0; k < n; ++k) gone &= cur[k].a != prev[i].a;
        jump |= gone && prev[i].blend > 0.1f;
    }
    memcpy(prev, cur, sizeof(AssocSnap) * n); nPrev = n;
    return jump;
}

static void Inertialize(RpClump* clump, float dt) {
    static const int kMax = 64;
    static float off[kMax][4], offT[kMax][3], shown[kMax][4], shownT[kMax][3], last[kMax][4];
    static RpClump* who = nullptr; static int nb = 0;
    auto* bd = *(CAnimBlendClumpData**)((char*)clump + *(int*)0xB5F878);
    if (!bd || !bd->m_pFrames || bd->m_nNumFrames > kMax) return;
    int n = bd->m_nNumFrames;
    bool jump = AnimJumped(clump, dt);
    bool reset = !g_inertia || who != clump || nb != n || dt <= 0 || dt > 0.25f;
    who = clump; nb = n;
    float k = reset ? 1.0f : 1.0f - powf(0.5f, dt / kInertiaHalfLife);  // decay this frame
    int popped = 0;
    for (int b = 0; b < n; ++b) {
        auto* f = (InterpFrame*)bd->m_pFrames[b].m_pIFrame;
        if (!f) continue;
        float* q = f->q;
        if (reset) { off[b][0] = off[b][1] = off[b][2] = 0; off[b][3] = 1; offT[b][0] = offT[b][1] = offT[b][2] = 0; }
        else {
            // a bone that turned > 0.6 rad (and > 35 rad/s; clips peak ~23) in one frame popped even if the assoc list looks fine
            float c = fabsf(QDot(q, last[b]));
            bool boneJump = c < cosf(0.3f) && 2 * acosf(c > 1 ? 1 : c) > 35.0f * dt;
            if (jump || boneJump) {  // keep showing what was shown: offset = shown * inverse(new)
                float inv[4] = {-q[0], -q[1], -q[2], q[3]};
                QMul(shown[b], inv, off[b]); QNorm(off[b]);
                for (int i = 0; i < 3; ++i) offT[b][i] = shownT[b][i] - f->t[i];
                popped += boneJump;
            }
            if (off[b][3] < 0) for (int i = 0; i < 4; ++i) off[b][i] = -off[b][i];
            for (int i = 0; i < 3; ++i) { off[b][i] *= 1 - k; offT[b][i] *= 1 - k; }
            off[b][3] += (1 - off[b][3]) * k;
            QNorm(off[b]);
        }
        memcpy(last[b], q, sizeof(float) * 4);
        float o[4]; QMul(off[b], q, o); QNorm(o);
        if (QDot(o, q) < 0) for (int i = 0; i < 4; ++i) o[i] = -o[i];
        memcpy(q, o, sizeof o);
        for (int i = 0; i < 3; ++i) f->t[i] += offT[b][i];
        memcpy(shown[b], q, sizeof o); memcpy(shownT[b], f->t, sizeof(float) * 3);
    }
    if (jump || popped) Log("inertia: %s%d bones", jump ? "anim jump " : "", popped);
}

static void __cdecl UpdateAnimationsHook(RpClump* clump, float step, bool onScreen) {
    ((void(__cdecl*)(RpClump*, float, bool))0x4D34F0)(clump, step, onScreen);  // RpAnimBlendClumpUpdateAnimations
    CPlayerPed* p = FindPlayer(-1);
    if (onScreen && p && clump == p->m_pRwClump) { ModTimer t; Inertialize(clump, step); }
}

static void __fastcall CalcVelocityHook(CPed* ped, void*) {
    if (g_moveActive && ped == FindPlayer(-1)) ped->m_fHeadingGoal = g_walkHeading;
    else if (g_turning && ped == FindPlayer(-1)) ped->m_fHeadingGoal = g_turnGoal;
    ((void(__thiscall*)(CPed*))0x5E4C50)(ped);  // CPed::CalculateNewVelocity
}

static void __cdecl UpdatePadsHook() {
    ((void(__cdecl*)())0x541DD0)();  // CPad::UpdatePads
    ModTimer timer;
    CPlayerPed* p = FindPlayer(-1);
    CPad* pad = CPad::GetPad(0);
    PadInUse();  // refresh g_padRX every frame
    ForceAim(p, pad);
    FreeWalk(p, pad);
    StandTurn(p, pad);
    if (pad) { g_camStickX = pad->NewState.RightStickX; g_camStickY = pad->NewState.RightStickY; }
    // swap the walk slots now, before this frame's ped update starts a walk: swapped later (Tick),
    // SA had already started its own WALK_start, which then cross-faded out over Max's clip for
    // 5 frames, each start/direction change lurched forward (log: +0.36 m in one frame)
    if (p && p->m_pRwClump) { ApplyBody(p->m_pRwClump); ExtractSideways(p->m_pRwClump); KillStrays(p->m_pRwClump); }
}

// ---- Head look-at off ("head looks in random directions, legs sway") ----
// SA turns CJ's head two ways, both outside the animations: (1) CPlayerPed::ProcessControl calls
// SetLookFlag(camera heading) whenever the camera is >30 deg off his facing (0x60F459), so the
// head chases a camera that bounces off walls indoors; (2) IKChainManager look-at tasks
// (interesting peds/objects, aim targets). Both off -> the head plays Max's animation only.
static void __fastcall SetLookFlagHook(CPed* ped, void*, float heading, bool a, bool b) {
    if (!g_headLook && ped == FindPlayer(-1)) {
        ((void(__thiscall*)(CPed*))0x5E1950)(ped);  // CPed::ClearLookFlag
        return;
    }
    ((void(__thiscall*)(CPed*, float, bool, bool))0x5DEDC0)(ped, heading, a, b);
}

static void BlockLookAt(CPlayerPed* p) {
    p->bDontAcceptIKLookAts = !g_headLook;
    void* ik = (void*)0xC15448;  // g_ikChainMan
    if (!g_headLook && ((bool(__thiscall*)(void*, CPed*))0x6181A0)(ik, p))   // IsLooking
        ((void(__thiscall*)(void*, CPed*, unsigned))0x618280)(ik, p, 100);  // AbortLookAt
}

// Diagnostics for "firing walks CJ forward": while fire is held, log stick, speed and state.
static void LogFire(CPlayerPed* p) {
    static unsigned last = 0;
    CPad* pad = CPad::GetPad(0);
    if (!pad || !(pad->NewState.ButtonCircle || pad->NewState.LeftShoulder1) || GetTickCount() - last < 250) return;
    last = GetTickCount();
    CVector v = p->m_vecMoveSpeed;
    Log("fire: stick=%d,%d target=%d aim=%d wstate=%d speed=%.3f,%.3f movestate=%d",
        pad->NewState.LeftStickX, pad->NewState.LeftStickY, pad->NewState.RightShoulder1,
        (int)p->bIsAimingGun, (int)p->GetWeapon()->m_nState, v.x * 50, v.y * 50, (int)p->m_nMoveState);
}

// Safety net for the "horizontal flash": after a swap/re-create the full-body weights can sum
// to ~0 for a few frames (old clip faded out, new one still at blend 0 / delta 0), and a pose
// with no animation is the bind pose (flat). Log shows empty "as" runs of 1-5 frames at every
// direction change with the pad. Top the best non-fading full-body clip up to total 1.
static void EnsureFullBlend(RpClump* c) {
    float sum = 0; CAnimBlendAssociation* best = nullptr;
    for (auto* a = ((CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0)(c); a;
         a = ((CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0)(a)) {
        if ((a->m_nFlags & ANIMATION_PARTIAL) || a->m_fBlendDelta < -50.0f) continue;  // just killed by KillStrays
        sum += a->m_fBlendAmount;
        // Only OUR clips may be topped up. SA's own chains (getting on a bike: log "blend fix: sum=0.00 ->
        // top up p483") legitimately run at weight 0 for a few frames; forcing one to 1 cut its fade-in short
        // and the body snapped into the pose = the "teleport" seen when entering a motorcycle.
        if (InBody(a->m_pHierarchy) && a->m_fBlendDelta >= 0 && (!best || a->m_fBlendAmount > best->m_fBlendAmount)) best = a;
    }
    if (sum > 1.01f) {
        // Weights >1 (log: M45 0.92 + M45 0.08 + M2 0.91 + M1 0.06 = 1.97) add up their pelvis
        // translation: the model "hikes up" toward the roof during strafes. Fading assocs give
        // back the excess so the full-body weights always total 1.
        float excess = sum - 1.0f;
        for (auto* a = ((CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0)(c); a && excess > 0;
             a = ((CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0)(a)) {
            if ((a->m_nFlags & ANIMATION_PARTIAL) || a->m_fBlendDelta >= 0 || !InBody(a->m_pHierarchy)) continue;
            float d = a->m_fBlendAmount < excess ? a->m_fBlendAmount : excess;
            a->m_fBlendAmount -= d; excess -= d;
        }
        return;
    }
    if (sum >= 0.999f) return;
    CAnimBlendAssociation* t = best;
    if (!t) return;  // no MP clip to lean on: SA is blending its own animation, leave it alone
    Log("blend fix: sum=%.2f -> top up %s%d", sum, InBody(t->m_pHierarchy) ? "M" : "p", int(t->m_pHierarchy - (CAnimBlendHierarchy*)0xB4EA40));
    t->m_fBlendAmount += 1.0f - sum;
    if (t->m_fBlendAmount > 1) t->m_fBlendAmount = 1;
}

// Hang tracer: if the main thread stops ticking for 2 s, log its registers + stack (no ptrace under Wine/yama).
static volatile DWORD g_lastTick = 0; static HANDLE g_mainThread = nullptr;
static DWORD WINAPI Watchdog(LPVOID) {
    for (;;) {
        Sleep(500);
        DWORD last = g_lastTick;
        if (!last || GetTickCount() - last < 2000) continue;
        CONTEXT cx = {}; cx.ContextFlags = CONTEXT_FULL;
        SuspendThread(g_mainThread);
        if (GetThreadContext(g_mainThread, &cx)) {
            Log("HANG eip=%08lx esp=%08lx ebp=%08lx eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx",
                cx.Eip, cx.Esp, cx.Ebp, cx.Eax, cx.Ebx, cx.Ecx, cx.Edx, cx.Esi, cx.Edi);
            DWORD* sp = (DWORD*)cx.Esp;
            for (int i = 0; i < 64 && !IsBadReadPtr(sp + i, 4); i += 4)
                Log("HANG stk %08lx: %08lx %08lx %08lx %08lx", (DWORD)(sp + i), sp[i], sp[i+1], sp[i+2], sp[i+3]);
            for (int i = 0; i < 8 && !IsBadReadPtr((void*)(cx.Eip + i*4), 4); ++i) Log("HANG code +%d %08lx", i*4, *(DWORD*)(cx.Eip + i*4));
        }
        ResumeThread(g_mainThread);
        Log("HANG dump done"); g_lastTick = 0;  // once per hang
        for (;;) { Sleep(500); if (g_lastTick) break; }  // wait for recovery
    }
}

static void Tick() {
    ModTimer timer; ++g_modN;
    g_lastTick = GetTickCount();
    if (!g_mainThread) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    }
    static unsigned lastTickLog = 0;
    unsigned tickNow = GetTickCount();
    if (tickNow - lastTickLog > 1000) { lastTickLog = tickNow; Log("tick %u perf mod=%.0f us/frame over %d frames (%.2f ms/s)", tickNow, g_modN ? g_modUs / g_modN : 0, g_modN, g_modUs / 1000); g_modUs = 0; g_modN = 0; }
    { static DWORD lastT = 0; if (lastT && tickNow - lastT > 70) Log("hitch %u ms", tickNow - lastT); lastT = tickNow; }
    LoadAll();
    static bool uiHooked = false;
    if (!uiHooked) { uiHooked = true; Events::drawingEvent += DrawAmmoHud; }
    CPlayerPed* p = FindPlayer(-1);
    MenuInput(p); g_inputDone = true;
    if (!p || !p->m_pRwClump) return;
    static bool inWorld = false; // tools/autostart.py waits for this line
    if (!inWorld) { inWorld = true; Log("in world"); }
    if (g_menuOpen) { UpdateEnemies(p); return; }

    for (int k = VK_F1; k <= VK_F12; ++k) {
        bool down = (GetAsyncKeyState(k) & 0x8000) != 0;
        if (down && !g_fkey[k - VK_F1]) Log("key F%d", k - VK_F1 + 1);
        g_fkey[k - VK_F1] = down;
    }
    if (Edge(0, VK_F9)) { g_autoPlayer = !g_autoPlayer; Log("autoPlayer=%d", g_autoPlayer); }
    if (Edge(1, VK_F10)) { g_enemyAnims = !g_enemyAnims; Log("enemyAnims=%d", g_enemyAnims); }

    // Manual test clips (full body) for checking the IFP independent of the state machine.
    bool combo = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(0x35) & 0x8000);
    if (combo && !g_dodgeCombo) { Play(p->m_pRwClump, g_dodge, "ShootDodgeForward", 0); g_dodgeUntil = GetTickCount() + 1500; Log("ctrl+5 dodge test"); }
    g_dodgeCombo = combo;
    if (Edge(2, VK_F6)) Play(p->m_pRwClump, g_full, "Stand", 0);
    if (Edge(3, VK_F7)) Play(p->m_pRwClump, g_full, "ShootBerettaDual", 0);
    if (Edge(4, VK_F8)) Play(p->m_pRwClump, g_full, "ReloadBerettaDual", 0);

    bool inAir = p->bIsInTheAir;
    if (inAir && !g_wasInAir && p->bIsAimingGun && Armed(p) && GetTickCount() >= g_dodgeUntil) {
        Play(p->m_pRwClump, g_dodge, "ShootDodgeForward", 0);
        g_dodgeUntil = GetTickCount() + 1500;
        Log("shootdodge");
    }
    g_wasInAir = inAir;

    // Rocket launchers are HEAVY in weapon.dat (flags 48214, 3rd digit 2): SA then skips the whole
    // sprint branch (TaskSimplePlayerOnFoot::PlayerControlZelda) and blocks jumping. Max sprints/jumps
    // with anything, so drop the flag (sprint = SA's group-57 slot 2, still run_rocket; our swap moved slot 1).
    for (int w = 35; w <= 36; ++w) ((CWeaponInfo*(__cdecl*)(eWeaponType, unsigned char))0x743C60)(eWeaponType(w), 1)->m_nFlags.bHeavy = 0;  // GetWeaponInfo(STD)
    ApplyBody(p->m_pRwClump);
    ExtractSideways(p->m_pRwClump);
    KillStrays(p->m_pRwClump);
    HoldFadeIn(p->m_pRwClump);
    EnsureFullBlend(p->m_pRwClump);
    PhaseLock(p->m_pRwClump);
    TraceClip(p->m_pRwClump);
    {   // Max's clips carry <=0.09 m/frame of travel, but the first frame of a new free-walk clip
        // adds a 0.25-0.65 m lunge (SA extracts velocity from a clip whose node times were just
        // forced). Clamp the step that frame; the cause is inside SA's anim code, not worth patching.
        static CVector prev; static bool havePrev = false; static bool wasMoving = false;
        CVector now = p->GetPosition();
        if (g_moveActive && havePrev) {
            CVector d = now - prev; d.z = 0;
            float m = d.Magnitude();
            if (m > 0.12f) {
                float k = 0.09f / m;
                CVector fix = prev + CVector(d.x * k, d.y * k, 0); fix.z = now.z;
                p->GetPosition() = fix;
                Log("lunge clamp %.3f m", m);
                now = fix;
            }
        }
        prev = now; havePrev = true; wasMoving = g_moveActive;
    }
    if (g_moveActive || p->m_nMoveState == PEDMOVE_SPRINT) {  // free-walk/sprint trace for automated tests: position, facing, camera, frame time
        CVector v = p->GetPosition();
        CAnimBlendAssociation* d = Dominant(p->m_pRwClump);
        float tn = (d && d->m_pHierarchy->m_fTotalTime > 0) ? d->m_fCurrentTime / d->m_pHierarchy->m_fTotalTime : -1;
        Log("mv %d %.3f %.3f h=%.3f goal=%.3f cam=%.3f dt=%.4f raw=%.4f fps=%.1f tn=%.3f", g_moveDir, v.x, v.y,
            p->m_fHeadingCurrent, g_walkHeading, CamOrientation(), *(float*)0xB7CB5C / 50.0f,
            *(float*)0xB7CB58 / 50.0f, *(float*)0xB7CB50, tn);
        // every running association: clip index (MPS/ped), blend, normalized time, speed
        char buf[512]; int o = 0;
        for (auto* a = ((CAnimBlendAssociation*(__cdecl*)(RpClump*))0x4D15E0)(p->m_pRwClump); a && o < 440;
             a = ((CAnimBlendAssociation*(__cdecl*)(CAnimBlendAssociation*))0x4D6AB0)(a)) {
            if (a->m_fBlendAmount < 0.005f) continue;
            int idx = int(a->m_pHierarchy - (CAnimBlendHierarchy*)0xB4EA40);
            bool mp = InBody(a->m_pHierarchy);
            o += snprintf(buf + o, sizeof(buf) - o, " %s%d:%s%.2f/%.2f/%.2f/d%.1f", mp ? "M" : "p",
                          idx - (mp ? g_body->startAnimation : 0), (a->m_nFlags & ANIMATION_PARTIAL) ? "P" : "",
                          a->m_fBlendAmount, a->m_pHierarchy->m_fTotalTime > 0 ? a->m_fCurrentTime / a->m_pHierarchy->m_fTotalTime : 0,
                          a->m_fSpeed, a->m_fBlendDelta);
        }
        Log("as%s", buf);
        // pose sanity: bones relative to CJ's origin; upright = head ~0.6 m over pelvis. Flags a
        // flattened/flashing skeleton (head-pelvis dz small) or a root jump (>0.15 m/frame).
        RwV3d pel, head, lf, rf;
        auto bone = [&](RwV3d& o, int id) { ((void(__thiscall*)(CPed*, RwV3d*, int, bool))0x5E4280)(p, &o, id, true); };
        bone(pel, 1); bone(head, 5); bone(lf, 43); bone(rf, 53);
        static CVector last; static bool have = false;
        float jump = have ? (p->GetPosition() - last).Magnitude() : 0; last = p->GetPosition(); have = true;
        float hz = head.z - pel.z;
        Log("pose pel=%.2f hd=%.2f lf=%.2f rf=%.2f jump=%.3f%s z=%.3f", pel.z - last.z, hz, lf.z - last.z, rf.z - last.z, jump,
            (hz < 0.35f || jump > 0.15f) ? " FLAG" : "", last.z);
        {   CCamera& cam = *(CCamera*)0xB6F028; CCam& cc = cam.m_aCams[cam.m_nActiveCam];
            Log("wpos pel=%.4f,%.4f head=%.4f,%.4f cam=%.4f,%.4f,%.4f fr=%.4f,%.4f", pel.x, pel.y, head.x, head.y,
                cc.m_vecSource.x, cc.m_vecSource.y, cc.m_vecSource.z, cc.m_vecFront.x, cc.m_vecFront.y); }  // world xy: what the eye sees
            Log("camt t=%.4f,%.4f pedAtCam=%.4f,%.4f", g_camT.x, g_camT.y, g_camPed.x, g_camPed.y);  // cam target vs ped
    }
    LogFire(p);
    BlockLookAt(p);
    UpdatePlayer(p);
    UpdateEnemies(p);
}

// Agent test helper: logs every game-state change ("gamestate N", 7 = front end, 9 = in game) and,
// when mp_autoload.txt (one line: save slot 1-8) exists in the game root, skips the intro movies and
// loads that slot by itself (the same thing the menu's LOAD GAME -> slot -> YES does; gta-reversed
// CMenuManager::SimulateGameLoad). The file is deleted on use, so normal launches are unaffected.
static DWORD WINAPI StateWatch(LPVOID) {
    volatile int& state = *(int*)0xC8D4C0;   // gGameState
    unsigned char* menu = (unsigned char*)0xBA6748;  // FrontEndMenuManager
    int slot = 0;
    if (FILE* f = fopen("mp_autoload.txt", "r")) {
        if (fscanf(f, "%d", &slot) != 1) slot = 0;
        fclose(f);
        remove("mp_autoload.txt");
        Log("autoload requested slot=%d", slot);
    }
    int last = -1;
    DWORD frontSince = 0, tried = 0;
    int tries = 0;
    for (;;) {
        int s = state;
        if (s != last) { Log("gamestate %d", s); last = s; frontSince = GetTickCount(); }
        if (slot >= 1 && slot <= 8) {
            if (s == 2) state = 3;        // Logo.mpg playing -> as if a key was pressed
            else if (s == 4) state = 5;   // title movie playing -> front end
            else if (s == 7 && menu[0x5C] && GetTickCount() - frontSince > 1500 && GetTickCount() - tried > 3000) {
                // retried every 3 s while still on the front end (a too-early write gets reset)
                Log("autoload try %d: page=%d slot=%d loadingFlag=%d loadingData=%d", ++tries, (signed char)menu[0x15D],
                    menu[0x15F], menu[0x1B3C], menu[0x60]);
                menu[0x15F] = (unsigned char)(slot - 1);  // m_SelectedSlot
                menu[0x15D] = 13;                         // m_nCurrentScreen = SCREEN_LOAD_FIRST_SAVE
                menu[0x1B3C] = 1;                         // m_CurrentlyLoading
                tried = GetTickCount();
                if (tries >= 6) { Log("autoload gave up"); slot = 0; }
            }
            if (s >= 8 && tries) { Log("autoload slot %d started", slot); slot = 0; }
        }
        if (s == 9 && slot == 0 && last == 9) Sleep(500); else Sleep(50);
    }
}

// Menu draws and reads input from a hook on the CHud::Draw call (0x53E4FF) like the CMR menu: it
// runs after the HUD/radar (nothing shows through) and keeps running while m_CodePause is set.
static void* g_hudOrig = (void*)0x58FAE0;
static void __cdecl OnHud() {
    ((void(__cdecl*)())g_hudOrig)();
    // Tick reads menu input when the game runs; while paused it may not, so read it here then.
    // Never both in one frame: MenuInput zeroes Select, a second read sees the combo released and
    // re-toggles next frame (menu flickered open/close every frame).
    if (!g_inputDone) MenuInput(FindPlayer(-1));
    g_inputDone = false;
    if (g_menuOpen) DrawMenu();
}
static void HookHud() {
    DWORD old;
    g_hudOrig = (void*)(*(DWORD*)(0x53E4FF + 1) + 0x53E4FF + 5);  // chain whatever hooked it before
    VirtualProtect((void*)(0x53E4FF + 1), 4, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD*)(0x53E4FF + 1) = (DWORD)&OnHud - (0x53E4FF + 5);
    VirtualProtect((void*)(0x53E4FF + 1), 4, old, &old);
}

struct Init {
    Init() {
        Log("maxpayne_combat loaded");
        g_menuOpen = false;
        HookHud();
        CreateThread(nullptr, 0, StateWatch, nullptr, 0, nullptr);
        Events::gameProcessEvent += Tick;
        patch::RedirectCall(0x53BEE6, UpdatePadsHook);
        patch::RedirectCall(0x60F459, SetLookFlagHook);
        patch::RedirectCall(0x527A09, FollowPedHook);
        patch::RedirectCall(0x527A95, AimCamHook);
        patch::RedirectCall(0x58FBBF, CrossHook);
        patch::RedirectCall(0x61ECCD, GunFireHook);  // FireGun -> CWeapon::Fire: hipfire shots follow the dot
        patch::RedirectJump(0x509CE0, VectorsHook);
        patch::RedirectCall(0x5E92ED, CalcVelocityHook);
        patch::RedirectCall(0x535F94, UpdateAnimationsHook);  // CEntity::UpdateAnim -> pose inertialization
    }
} g_init;
