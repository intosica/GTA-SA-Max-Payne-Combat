// Max Payne-style in-game menu, using the original Max Payne menu art and font atlas.
#pragma once
#include "mp_font_data.h"
#include <CSprite2d.h>
#include <CRect.h>
#include <CRGBA.h>
#include <rtpng.h>
#include <CPad.h>
#include <RenderWare.h>
#include <cstdio>

static CSprite2d g_uiBg;
static CSprite2d g_uiFont;
static CSprite2d g_uiFontBright;
static bool g_uiTried = false;
static bool g_uiOk = false;
static bool g_menuOpen = false;
static bool g_padCombo = false;
static DWORD g_menuOpenedAt = 0;  // B may not close the menu on the very press that opened it
static bool g_padUp = false, g_padDown = false, g_padA = false, g_padCircle = false;
static int g_menuSel = 0;
static int g_menuTop = 0;
static const char* g_lastClip = "none";

// flag == nullptr: CLOSE. on/off: text shown for the two states (default ON/OFF).
// mode != nullptr: cycles *mode through names[0..n-1]
static const char* const kCamNames[] = {"CURRENT", "MAX PAYNE", "GTA SA"};
struct MpMenuItem { const char* label; bool* flag; const char* on = "ON"; const char* off = "OFF"; int* mode = nullptr; const char* const* names = nullptr; int n = 0; bool* showIf = nullptr; };  // showIf: row hidden while *showIf is false
static const MpMenuItem g_menu[] = {
    {"MAX PAYNE BODY ANIMS", &g_mpLoco},                       // Max's idle/walk/run clips on CJ
    {"MOVEMENT",        &g_mpControls, "MAX PAYNE", "SA"},     // MAX PAYNE: faces ahead, stick strafes/backpedals; SA: turns to the stick
    {"STRAFE TORSO",    &g_stillSide, "STILL", "MAX SWAY"},    // upper body while sidestepping
    {"BACKPEDAL TORSO", &g_stillBack, "STILL", "MAX SWAY"},    // upper body while walking backwards
    {"GUNFIRE",         &g_mpFire, "MAX PAYNE", "SA"},         // MAX PAYNE: armed = always ready, no aiming; SA: hold R1 to aim
    {"NATIVE AIM",      &g_nativeAim, "ON", "OFF", nullptr, nullptr, 0, &g_mpFire},  // MP gunfire + R1 = SA's own aim again
    {"HIPFIRE ACCURACY", &g_mpHipAcc, "PINPOINT", "SA SPREAD"},  // no distance spread on hipfire shots
    {"AIM ACCURACY",    &g_mpAimAcc, "PINPOINT", "SA SPREAD"},  // no distance spread while aiming
    {"CROSSHAIR",       &g_mpCross, "MAX PAYNE", "SA"},        // dot whenever armed vs SA's aim-only reticle
    {"PAD CAMERA",      nullptr, "", "", &g_camMode, kCamNames, 3},  // see FollowPedHook
    {"STICK LOOK CURVE", &g_stickShape, "MAX PAYNE", "SA"},     // Xbox right-stick deadzone + cubic curve
    {"SMOOTH TRANSITIONS", &g_inertia},                       // pose inertialization (A/B the pops)
    {"IDLE HEAD",       &g_idleLook, "MAX LOOK-AROUND", "STILL"},  // Max's 1:1 idle looks right/left after stopping
    {"SA HEAD TRACKING", &g_headLook},                         // SA's look-at IK (head follows camera/peds)
    {"OLD SHOOT OVERLAYS", &g_autoPlayer},                     // previous layered aim/fire/reload poses
    {"ENEMY ANIMS",     &g_enemyAnims},
    {"CLOSE",           nullptr},
};
static const int g_menuCount = sizeof(g_menu) / sizeof(g_menu[0]);

static RwTexture* LoadPngTexture(const char* path) {
    RwImage* img = RtPNGImageRead(path);
    if (!img) { Log("ui missing %s", path); return nullptr; }
    RwInt32 w = 0, h = 0, depth = 0, flags = 0;
    RwImageFindRasterFormat(img, rwRASTERTYPETEXTURE, &w, &h, &depth, &flags);
    RwRaster* raster = RwRasterCreate(w, h, depth, flags);
    if (raster) RwRasterSetFromImage(raster, img);
    RwImageDestroy(img);
    if (!raster) return nullptr;
    RwTexture* tex = RwTextureCreate(raster);
    if (tex) RwTextureSetFilterMode(tex, rwFILTERLINEAR);
    return tex;
}

static void LoadUi() {
    if (g_uiTried) return;
    g_uiTried = true;
    g_uiBg.m_pTexture = LoadPngTexture("maxpayne_ui/menu_bg.png");
    g_uiFont.m_pTexture = LoadPngTexture("maxpayne_ui/mp_font.png");
    g_uiFontBright.m_pTexture = LoadPngTexture("maxpayne_ui/mp_font_bright.png");
    g_uiOk = g_uiBg.m_pTexture && g_uiFont.m_pTexture && g_uiFontBright.m_pTexture;
    Log("ui loaded bg=%p font=%p bright=%p", g_uiBg.m_pTexture, g_uiFont.m_pTexture, g_uiFontBright.m_pTexture);
}

static float UiScale() {
    float s = RsGlobal.maximumHeight / 480.0f;
    return s > 0.1f ? s : 1.0f;
}

static void DrawText(const char* s, float x, float y, float scale, CRGBA color, bool bright) {
    CSprite2d& sp = bright ? g_uiFontBright : g_uiFont;
    if (!sp.m_pTexture) return;
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) continue;
        // Atlas space glyph is a ½ control glyph; advance without drawing.
        if (c == ' ') { x += 10.0f * scale; continue; }
        MpGlyph g;
        // The @ run swallowed the A box, so the table is shifted by one from A onward.
        if (c == '@') g = {421, 21};
        else if (c == 'A') g = {444, 10};
        else if (c >= 'B') g = MP_GLYPHS[c - 33];
        else g = MP_GLYPHS[c - 32];
        float w = g.w * scale, h = MP_FONT_H * scale;
        float u1 = g.x / (float)MP_FONT_W;
        float u2 = (g.x + g.w) / (float)MP_FONT_W;
        sp.Draw(CRect(x, y, x + w, y + h), color, u1, 0.0f, u2, 0.0f, u1, 1.0f, u2, 1.0f);
        x += (g.w + 1) * scale;
    }
}

// Open = pause the game (CTimer::m_CodePause, like the CMR tuning menu), take window focus back
// (a paused window stops getting input) and loop the Max Payne menu theme. Close undoes all three.
#include <mmsystem.h>
static void SetMenu(bool open) {
    if (g_menuOpen == open) return;
    g_menuOpen = open;
    if (open) g_menuOpenedAt = GetTickCount();
    *(bool*)0xB7CB48 = open;
    if (open) {
        HWND w = GetActiveWindow(); if (!w) w = GetForegroundWindow();
        if (w) { SetForegroundWindow(w); SetFocus(w); }
        PlaySoundA("maxpayne_ui/menu_music.wav", nullptr, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
    } else PlaySoundA(nullptr, nullptr, 0);
    Log("menu %s", open ? "open" : "close");
}
static void MenuToggle() { SetMenu(!g_menuOpen); }

static bool RowShown(int i) { return !g_menu[i].showIf || *g_menu[i].showIf; }
static int RowStep(int i, int d) { do i = (i + d + g_menuCount) % g_menuCount; while (!RowShown(i)); return i; }
static void MenuPlay(CPlayerPed*, int idx) {
    if (idx < 0 || idx >= g_menuCount) return;
    if (int* m = g_menu[idx].mode) { *m = (*m + 1) % g_menu[idx].n; Log("%s=%d", g_menu[idx].label, *m); return; }
    bool* f = g_menu[idx].flag;
    if (!f) { SetMenu(false); return; }
    *f = !*f;
    Log("%s=%d", g_menu[idx].label, *f);
}

static void MenuInput(CPlayerPed* p) {
    CPad* pad = CPad::GetPad(0);
    bool selectHeld = pad && pad->NewState.Select > 0;
    bool selB = selectHeld && pad->NewState.ButtonCircle > 0;
    if (selB && !g_padCombo) { Log("menu combo select+b"); MenuToggle(); }
    g_padCombo = selB;

    if (Edge(5, 'M')) MenuToggle();
    bool circle = pad && pad->NewState.ButtonCircle > 0;
    if (g_menuOpen && GetTickCount() - g_menuOpenedAt > 400 && circle && !g_padCircle && !selB) { Log("menu close pad B"); MenuToggle(); }
    g_padCircle = circle;
    if (g_menuOpen && (Edge(9, 'B') || Edge(10, VK_ESCAPE))) { Log("menu close key"); MenuToggle(); }

    // Select/Back is also GTA SA's camera/replay button. Swallow it so holding
    // Select only opens our menu and never toggles the native camera/replay.
    if (selectHeld) pad->NewState.Select = 0;

    if (!g_menuOpen) return;
    static int dbg = 0;
    if ((dbg++ % 60) == 0)
        Log("pad sel=%d circle=%d cross=%d up=%d down=%d", pad ? pad->NewState.Select : -1,
            pad ? pad->NewState.ButtonCircle : -1, pad ? pad->NewState.ButtonCross : -1,
            pad ? pad->NewState.DPadUp : -1, pad ? pad->NewState.DPadDown : -1);

    if (Edge(6, VK_UP)) { g_menuSel = RowStep(g_menuSel, -1); }
    if (Edge(7, VK_DOWN)) { g_menuSel = RowStep(g_menuSel, 1); }
    if (Edge(8, VK_RETURN)) MenuPlay(p, g_menuSel);

    bool up = pad && pad->NewState.DPadUp > 0;
    bool down = pad && pad->NewState.DPadDown > 0;
    bool a = pad && pad->NewState.ButtonCross > 0;
    if (up && !g_padUp) g_menuSel = RowStep(g_menuSel, -1);
    if (down && !g_padDown) g_menuSel = RowStep(g_menuSel, 1);
    if (a && !g_padA) MenuPlay(p, g_menuSel);
    g_padUp = up; g_padDown = down; g_padA = a;

    if (!RowShown(g_menuSel)) g_menuSel = RowStep(g_menuSel, -1);  // the row we were on just hid
}

static void DrawAmmoHud() {
    if (g_menuOpen || !g_showShells || !g_autoPlayer) return;
    LoadUi();
    if (!g_uiFont.m_pTexture) return;
    float s = UiScale();
    char buf[64];
    snprintf(buf, sizeof(buf), "SHELLS %d/8", g_shells);
    DrawText(buf, 24.0f * s, 430.0f * s, 0.9f * s, g_shells > 0 ? CRGBA(230,220,200,255) : CRGBA(220,40,30,255), true);
    if (g_shells <= 0 && GetTickCount() >= g_reloadUntil)
        DrawText("RELOAD [R]", 24.0f * s, 448.0f * s, 0.8f * s, CRGBA(220,40,30,255), true);
}

static void DrawMenu() {
    static bool once = false;
    if (!once) { once = true; Log("drawMenu called"); }
    if (!g_menuOpen) return;
    LoadUi();

    float s = UiScale();
    float ox = (RsGlobal.maximumWidth - 640.0f * s) * 0.5f;
    if (ox < 0) ox = 0;
    float W = RsGlobal.maximumWidth, H = RsGlobal.maximumHeight;
    CSprite2d::DrawRect(CRect(0, 0, W, H), CRGBA(0, 0, 0, 255));
    if (g_uiBg.m_pTexture) {
        g_uiBg.Draw(CRect(ox, 0, ox + 640.0f * s, 480.0f * s), CRGBA(255, 255, 255, 255));
    }

    float x = ox + 176.0f * s;
    float y = 100.0f * s;
    DrawText("MAX PAYNE", x, y, 1.15f * s, CRGBA(190, 25, 20, 255), true);
    DrawText("COMBAT", x, y + 30.0f * s, 0.85f * s, CRGBA(230, 230, 230, 255), true);

    // Two columns, 10 rows each: every option is on screen at once (no scrolling, nothing can look "missing").
    // Navigation stays linear (DPAD up/down walks the list, left column first), so no new input to learn.
    CSprite2d::DrawRect(CRect(ox + 34 * s, 214 * s, ox + 620 * s, 472 * s), CRGBA(0, 0, 0, 150));
    for (int idx = 0, drawn = 0; idx < g_menuCount && drawn < 20; ++idx) {
        if (!RowShown(idx)) continue;
        float cx = ox + (drawn < 10 ? 44.0f : 336.0f) * s;
        float ly = (230.0f + (drawn % 10) * 22.0f) * s;
        bool sel = idx == g_menuSel;
        if (sel) {
            CSprite2d::DrawRect(CRect(cx - 8 * s, ly - 2 * s, cx + 268 * s, ly + 20 * s), CRGBA(120, 20, 15, 190));
            DrawText(">", cx - 20 * s, ly, 0.9f * s, CRGBA(220, 180, 120, 255), true);
        }
        char label[64];
        if (g_menu[idx].mode) snprintf(label, sizeof(label), "%s: %s", g_menu[idx].label, g_menu[idx].names[*g_menu[idx].mode]);
        else if (g_menu[idx].flag) snprintf(label, sizeof(label), "%s: %s", g_menu[idx].label, *g_menu[idx].flag ? g_menu[idx].on : g_menu[idx].off);
        else snprintf(label, sizeof(label), "%s", g_menu[idx].label);
        DrawText(label, cx, ly, 0.72f * s, sel ? CRGBA(255, 235, 210, 255) : CRGBA(190, 190, 185, 255), sel);
        drawn++;
    }

    char foot[96];
    snprintf(foot, sizeof(foot), "SELECT+B CLOSE   A TOGGLE   DPAD NAV%s", "");
    DrawText(foot, ox + 34 * s, 456.0f * s, 0.55f * s, CRGBA(170, 170, 165, 255), false);
}
