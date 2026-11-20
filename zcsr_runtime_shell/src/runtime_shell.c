/* zcsr_runtime_shell.c — the single OS-window and main-loop owner.
 *
 * One module = the application itself (editor/player): the shell enters that
 * module directly and uses its title as the OS window title. Multiple modules
 * use the neutral home screen with one button per module plus SETTINGS/EXIT.
 * The shell owns:
 *   - a NORMAL OS window: taskbar icon, Alt-Tab switchable, never topmost, mouse
 *     cursor visible (the platform's default surface is a topmost fullscreen
 *     overlay; we switch to WINDOWED immediately after create)
 *   - the single main loop and full boot/teardown
 *   - the settings overlay machinery, entirely shell-internal: the panel is
 *     rendered into an offscreen texture (zcsr_gl_set_target) and composited
 *     back over the dimmed page at an ADJUSTABLE opacity (Interface tab)
 *
 * Run with "autoquit" for the headless self-test (home -> module -> toggle ->
 * settings overlay -> close -> exit).
 */
#include "zcsr_runtime_shell.h"
#include "zcsr_frame_loop.h"
#include "zcsr_task_scheduler.h"
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
/* Settings page has exactly one implementation: the UIM Page Manager settings
 * page. Runtime shell only compiles it when that optional block is selected. */
#include "zcsr_ui_page_manager/ui_game_settings.h"
#endif
#include "zcsr/audio_mix.h"
#include "zcsr/platform.h"
#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include "zcsr/input_ext.h"
#include "zcsr/window_event.h"

#include "zcsr/clock.h"
#include "zcsr/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
static void shell_sleep_ms(int ms) { Sleep((DWORD)ms); }
#  endif
#else
#  include <unistd.h>
#  if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
static void shell_sleep_ms(int ms) { usleep((useconds_t)(ms * 1000)); }
#  endif
#endif

#define SHELL_W 1280
#define SHELL_H 720
#define SHELL_MAX_MODULES 4

/* Offscreen texture cap for the settings panel (composite source). Windows larger
 * than this get a clamped panel. 1920x1080x4 = 8 MiB of zero-initialized BSS. */
#define SHELL_PANEL_TEX_MAX_W 1920
#define SHELL_PANEL_TEX_MAX_H 1080

/* Shell visual identity: near-black + slate + amber (distinct from the settings panel). */
static const float SHELL_BG[4]     = { 0.020f, 0.022f, 0.030f, 1.0f };
static const float SHELL_BTN[4]    = { 0.090f, 0.100f, 0.130f, 1.0f };
static const float SHELL_BTN_HOV[4]= { 0.160f, 0.150f, 0.110f, 1.0f };
static const float SHELL_ACCENT[4] = { 0.920f, 0.620f, 0.180f, 1.0f };
static const float SHELL_TEXT[4]   = { 0.850f, 0.860f, 0.880f, 1.0f };

/* ================================================================== */
/*  Optional settings overlay (UIM Page Manager)                       */
/* ================================================================== */
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
/* ================================================================== */
/*  Settings items (complete demo set, Borderless restored)           */
/* ================================================================== */
enum {
    V_DISPLAY = 0,
    V_MODE,
    V_RESOLUTION,
    V_REFRESH,
    V_VSYNC,
    V_FRAMELIMIT,
    V_BRIGHTNESS,
    V_SEP_COMBAT,
    V_FOV,
    V_SHARPNESS,
    V_COUNT,
};

static const char* mode_opts[]    = { "Windowed", "Borderless", "Fullscreen" };
static const char* res_opts[]     = { "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160" };
static const char* refresh_opts[] = { "60Hz", "75Hz", "120Hz", "144Hz", "165Hz", "240Hz" };
static const char* fps_opts[]     = { "30", "60", "120", "144", "Unlimited" };

static zcsr_game_settings_item g_video[V_COUNT];

enum {
    G_PRESET = 0,
    G_TEXTURE,
    G_SHADOW,
    G_AA,
    G_EFFECTS,
    G_POST,
    G_VIEWDIST,
    G_COUNT,
};

static const char* preset_opts[]  = { "Low", "Medium", "High", "Ultra" };
static const char* tex_opts[]     = { "Low", "Medium", "High", "Ultra" };
static const char* shadow_opts[]  = { "Off", "Low", "Medium", "High" };
static const char* aa_opts[]      = { "Off", "FXAA", "TAA", "MSAA 2x", "MSAA 4x" };
static const char* effect_opts[]  = { "Low", "Medium", "High" };
static const char* post_opts[]    = { "Off", "Low", "Medium", "High" };

static zcsr_game_settings_item g_graphics[G_COUNT];

enum {
    A_MASTER = 0,
    A_MUSIC,
    A_SFX,
    A_VOICE,
    A_SEP_DEV,
    A_OUTPUT,
    A_INPUT,
    A_COUNT,
};

/* Output Device 的真实端点名：默认 + 枚举到的设备（最多 4 个）。
 * 值 0 = 系统默认；1..n 对应 g_outdev_ids[n-1] 的端点 ID。 */
static char g_outdev_names[4][ZCSR_MIX_DEVICE_NAME_CAP];
static char g_outdev_ids[4][ZCSR_MIX_DEVICE_ID_CAP];
static int  g_outdev_count = 0;
static const char* outdev_opts[5] = { "Default", "", "", "", "" };
static const char* indev_opts[]  = { "Default", "Microphone", "Line In" };

static zcsr_game_settings_item g_audio[A_COUNT];

/* Interface tab: shell-owned look-and-feel of the settings overlay itself. */
enum {
    I_OPACITY = 0, /* settings panel composite opacity (%) */
    I_DIM,         /* background dim under the overlay (%) */
    I_COUNT,
};

static zcsr_game_settings_item g_interface[I_COUNT];

static char g_display_names[8][64];
static const char* g_display_opts[8];
static int g_display_count = 0;

static void init_display_list(void) {
    int i;
    g_display_count = zcsr_display_count();
    if (g_display_count > 8) g_display_count = 8;
    for (i = 0; i < g_display_count; i++) {
        zcsr_display_info info;
        if (zcsr_display_get(i, &info)) {
            snprintf(g_display_names[i], sizeof(g_display_names[i]),
                     "%s (%dx%d)", info.name, info.width, info.height);
        } else {
            snprintf(g_display_names[i], sizeof(g_display_names[i]), "Display %d", i);
        }
        g_display_opts[i] = g_display_names[i];
    }
}

static void init_items(void) {
    zcsr_mix_device_info devs[4];
    int n = zcsr_mix_list_output_devices(devs, 4);
    g_outdev_count = n < 0 ? 0 : (n > 4 ? 4 : n);
    for (int di = 0; di < g_outdev_count; ++di) {
        snprintf(g_outdev_names[di], sizeof(g_outdev_names[di]), "%s", devs[di].name);
        snprintf(g_outdev_ids[di], sizeof(g_outdev_ids[di]), "%s", devs[di].id);
        outdev_opts[di + 1] = g_outdev_names[di];
    }
    memset(g_video, 0, sizeof(g_video));
    memset(g_graphics, 0, sizeof(g_graphics));
    memset(g_audio, 0, sizeof(g_audio));
    memset(g_interface, 0, sizeof(g_interface));

    g_video[V_DISPLAY] = (zcsr_game_settings_item){
        .label="Display", .description="Select the monitor for output.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=g_display_opts, .option_count=g_display_count, .value=0 };
    g_video[V_MODE] = (zcsr_game_settings_item){
        .label="Display Mode", .description="Windowed: normal window (taskbar, Alt-Tab). Borderless: covers the whole monitor without a frame. Fullscreen: exclusive fullscreen.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=mode_opts, .option_count=3, .value=0 };
    g_video[V_RESOLUTION] = (zcsr_game_settings_item){
        .label="Resolution", .description="Window client resolution (applies in Windowed mode).",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=res_opts, .option_count=5, .value=0 };
    g_video[V_REFRESH] = (zcsr_game_settings_item){
        .label="Refresh Rate", .description="Preferred display refresh rate.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=refresh_opts, .option_count=6, .value=0 };
    g_video[V_VSYNC] = (zcsr_game_settings_item){
        .label="V-Sync", .description="Synchronize the frame rate with the display.",
        .type=ZCSR_GAME_SETTINGS_TOGGLE, .toggle_value=true };
    g_video[V_FRAMELIMIT] = (zcsr_game_settings_item){
        .label="Frame Rate Limit", .description="Cap the maximum frame rate. Unlimited removes the cap.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=fps_opts, .option_count=5, .value=4 };
    g_video[V_BRIGHTNESS] = (zcsr_game_settings_item){
        .label="Brightness", .description="Adjust overall screen brightness.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=50.0f };
    g_video[V_SEP_COMBAT] = (zcsr_game_settings_item){
        .label="--- Combat View ---", .type=ZCSR_GAME_SETTINGS_SEPARATOR };
    g_video[V_FOV] = (zcsr_game_settings_item){
        .label="Default FOV", .description="Field of view in degrees.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=60.0f,.max=120.0f,.step=1.0f,.slider_value=90.0f };
    g_video[V_SHARPNESS] = (zcsr_game_settings_item){
        .label="Sharpness", .description="Post-process sharpening filter strength.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=50.0f };

    g_graphics[G_PRESET] = (zcsr_game_settings_item){
        .label="Quality Preset", .description="Global graphics quality preset.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=preset_opts, .option_count=4, .value=2 };
    g_graphics[G_TEXTURE] = (zcsr_game_settings_item){
        .label="Texture Quality", .description="Texture resolution and filtering quality.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=tex_opts, .option_count=4, .value=2 };
    g_graphics[G_SHADOW] = (zcsr_game_settings_item){
        .label="Shadow Quality", .description="Shadow map resolution and filtering.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=shadow_opts, .option_count=4, .value=2 };
    g_graphics[G_AA] = (zcsr_game_settings_item){
        .label="Anti-Aliasing", .description="Smooths jagged edges.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=aa_opts, .option_count=5, .value=1 };
    g_graphics[G_EFFECTS] = (zcsr_game_settings_item){
        .label="Effects Quality", .description="Particle effects and weather visual quality.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=effect_opts, .option_count=3, .value=1 };
    g_graphics[G_POST] = (zcsr_game_settings_item){
        .label="Post Processing", .description="Bloom, depth of field, color grading.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=post_opts, .option_count=4, .value=1 };
    g_graphics[G_VIEWDIST] = (zcsr_game_settings_item){
        .label="View Distance", .description="Maximum distance at which objects render.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=10.0f,.max=100.0f,.step=1.0f,.slider_value=70.0f };

    g_audio[A_MASTER] = (zcsr_game_settings_item){
        .label="Master Volume", .description="Overall audio output volume.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=80.0f };
    g_audio[A_MUSIC] = (zcsr_game_settings_item){
        .label="Music Volume", .description="Background music volume.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=60.0f };
    g_audio[A_SFX] = (zcsr_game_settings_item){
        .label="SFX Volume", .description="Sound effects volume.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=90.0f };
    g_audio[A_VOICE] = (zcsr_game_settings_item){
        .label="Voice Volume", .description="Voice chat and character dialogue volume.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=100.0f };
    g_audio[A_SEP_DEV] = (zcsr_game_settings_item){
        .label="--- Devices ---", .type=ZCSR_GAME_SETTINGS_SEPARATOR };
    g_audio[A_OUTPUT] = (zcsr_game_settings_item){
        .label="Output Device", .description="Audio output device.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=outdev_opts, .option_count=g_outdev_count + 1, .value=0 };
    g_audio[A_INPUT] = (zcsr_game_settings_item){
        .label="Input Device", .description="Microphone / audio input device.",
        .type=ZCSR_GAME_SETTINGS_DROPDOWN, .options=indev_opts, .option_count=3, .value=0 };

    g_interface[I_OPACITY] = (zcsr_game_settings_item){
        .label="Settings Opacity", .description="How opaque the settings overlay panel is over the page. Takes effect immediately.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=30.0f,.max=100.0f,.step=5.0f,.slider_value=100.0f };
    g_interface[I_DIM] = (zcsr_game_settings_item){
        .label="Background Dim", .description="How dark the page behind the settings overlay is. Takes effect immediately.",
        .type=ZCSR_GAME_SETTINGS_SLIDER, .min=0.0f,.max=90.0f,.step=1.0f,.slider_value=62.0f };
}
#endif /* ZCSR_RUNTIME_WITH_SETTINGS_PAGE: settings items */

/* ================================================================== */
/*  Shell state                                                        */
/* ================================================================== */
static unsigned char renderer_buf[8 * 1024 * 1024];
static unsigned char input_buf[4 * 1024];
static unsigned char inputext_buf[8 * 1024];
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
static unsigned char settings_buf[8192];
static zcsr_game_settings_tab w_tabs[4];
static unsigned char panel_zero[SHELL_PANEL_TEX_MAX_W * SHELL_PANEL_TEX_MAX_H * 4];
#endif

static zcsr_surface*     w_surface  = NULL;
static zcsr_gl_renderer* w_renderer = NULL;
static zcsr_input*       w_input    = NULL;
static zcsr_input_ext*   w_inputext = NULL;
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
static zcsr_game_settings*          w_settings = NULL;
#endif

/* Mouse-position bridge for modules (read-only). Lets a module follow the cursor
 * without changing the zcsr_runtime_shell_module callback ABI. */
static int g_runtime_shell_mouse_x = 0;
static int g_runtime_shell_mouse_y = 0;
void zcsr_runtime_shell_get_mouse_position(int* x, int* y) {
    if (x) *x = g_runtime_shell_mouse_x;
    if (y) *y = g_runtime_shell_mouse_y;
}
void zcsr_runtime_shell_set_mouse_position(int x, int y) {
    g_runtime_shell_mouse_x = x;
    g_runtime_shell_mouse_y = y;
}

static int g_runtime_shell_wheel = 0;

zcsr_surface* zcsr_runtime_shell_surface(void) {
    return w_surface;
}

bool zcsr_runtime_shell_key_down(zcsr_keycode key) {
    if (!w_input) return false;
    return zcsr_input_is_down(w_input, key);
}

int zcsr_runtime_shell_take_wheel_delta(void) {
    int delta = g_runtime_shell_wheel;
    g_runtime_shell_wheel = 0;
    return delta;
}

static bool w_running = true;

void zcsr_runtime_shell_request_exit(void) {
    w_running = false;
}

#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
/* Offscreen composite source for the settings panel (opaque content; the adjustable
 * transparency happens at composite time via sprite color alpha). */
static zcsr_gl_texture w_panel_tex = 0;
static int w_panel_tex_w = 0;
static int w_panel_tex_h = 0;
#endif

static const zcsr_runtime_shell_module* w_modules = NULL;
static int w_module_count = 0;

typedef struct ShellFrameHost {
    zcsr_gl_renderer* renderer;
    int cw;
    int ch;
} ShellFrameHost;

static ShellFrameHost w_frame_host;
static zcsr_frame_loop* w_frame_loop = NULL;
static zcsr_task_scheduler* w_tasks = NULL;

static bool shell_frame_init(void* host, void* user) {
    ShellFrameHost* h = (ShellFrameHost*)host;
    const zcsr_runtime_shell_module* m = (const zcsr_runtime_shell_module*)user;
    if (h == NULL || m == NULL) return false;
    if (m->init == NULL) return true;
    return m->init(h->renderer, h->cw, h->ch, m->user);
}

static bool shell_frame_tick(void* host, double dt, void* user) {
    ShellFrameHost* h = (ShellFrameHost*)host;
    const zcsr_runtime_shell_module* m = (const zcsr_runtime_shell_module*)user;
    (void)dt;
    if (h == NULL || m == NULL) return false;
    if (m->frame != NULL) m->frame(h->renderer, h->cw, h->ch, m->user);
    return true;
}

static void shell_frame_shutdown(void* host, void* user) {
    ShellFrameHost* h = (ShellFrameHost*)host;
    const zcsr_runtime_shell_module* m = (const zcsr_runtime_shell_module*)user;
    if (h == NULL || m == NULL || m->shutdown == NULL) return;
    m->shutdown(h->renderer, m->user);
}

zcsr_task_scheduler* zcsr_runtime_shell_tasks(void) {
    return w_tasks;
}

static int  w_active_module = -1;   /* -1 = home screen */
static bool w_settings_open = false;
static int  w_cw = SHELL_W, w_ch = SHELL_H;
static unsigned char w_prev_mouse_buttons = 0;
static bool w_autoquit = false;
static int  w_frame = 0;

/* Settings overlay geometry: panel occupies the main window (margins), clamped to
 * the offscreen texture cap. Same layout everywhere (render, mouse, resize). */
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
static void layout_settings_overlay(int* px, int* py, int* pw, int* ph) {
    int w = w_cw - 80;
    int h = w_ch - 100;
    if (w > SHELL_PANEL_TEX_MAX_W) w = SHELL_PANEL_TEX_MAX_W;
    if (h > SHELL_PANEL_TEX_MAX_H) h = SHELL_PANEL_TEX_MAX_H;
    if (w < 320) w = 320;
    if (h < 200) h = 200;
    *pw = w;
    *ph = h;
    *px = (w_cw - w) / 2;
    *py = (w_ch - h) / 2;
}
#endif

/* Home-screen big buttons: modules, then optional SETTINGS, then EXIT. */
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
static int home_button_count(void) { return w_module_count + 2; }
#else
static int home_button_count(void) { return w_module_count + 1; }
#endif

static void home_button_rect(int index, float* x, float* y, float* bw, float* bh) {
    *x = 90.0f;
    *y = 150.0f + (float)index * 82.0f;
    *bw = 360.0f;
    *bh = 64.0f;
}

static bool point_in_rect(int px, int py, float x, float y, float bw, float bh) {
    float fx = (float)px;
    float fy = (float)py;
    return fx >= x && fx <= x + bw && fy >= y && fy <= y + bh;
}

/* Back button shown while a module page is active. */
static void home_back_rect(float* x, float* y, float* bw, float* bh) {
    *x = 12.0f;
    *y = 10.0f;
    *bw = 110.0f;
    *bh = 30.0f;
}

/* ================================================================== */
/*  Settings apply / reset / overlay open-close                        */
/* ================================================================== */
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
static void apply_settings(void) {
    int mode = g_video[V_MODE].value;
    zcsr_window_mode wm = ZCSR_WINDOW_WINDOWED;
    if (mode == 1) wm = ZCSR_WINDOW_BORDERLESS;
    else if (mode == 2) wm = ZCSR_WINDOW_FULLSCREEN;
    zcsr_surface_set_mode(w_surface, wm);
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "[apply] display mode: %s\n", mode_opts[mode]);

    if (mode == 0) {
        static const int rw[] = { 1280, 1600, 1920, 2560, 3840 };
        static const int rh[] = { 720, 900, 1080, 1440, 2160 };
        int ri = g_video[V_RESOLUTION].value;
        zcsr_surface_set_client_size(w_surface, rw[ri], rh[ri]);
        zcsr_log_write(ZCSR_LOG_INFO, "shell", "[apply] resolution: %dx%d\n", rw[ri], rh[ri]);
    }

    {
        int di = g_video[V_DISPLAY].value;
        if (di < g_display_count) {
            zcsr_display_info info;
            if (zcsr_display_get(di, &info)) {
                zcsr_surface_move_to_display(w_surface, info.id);
                zcsr_log_write(ZCSR_LOG_INFO, "shell", "[apply] display: %s\n", info.name);
            }
        }
    }

    zcsr_gl_set_vsync(w_renderer, g_video[V_VSYNC].toggle_value);
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "[apply] vsync: %s\n", g_video[V_VSYNC].toggle_value ? "On" : "Off");

    zcsr_log_write(ZCSR_LOG_INFO, "shell", "[apply] interface: settings opacity %.0f%% dim %.0f%%\n",
           g_interface[I_OPACITY].slider_value, g_interface[I_DIM].slider_value);

    /* Texture Quality 以前只是存了个索引。现在它真的改默认采样：
     * Low -> GL_NEAREST（像素风/省带宽），Medium 及以上 -> GL_LINEAR。
     * 只影响此后创建的纹理，已有的纹理保持各自设置（图集/面板 FBO 自己指定）。 */
    {
        static const char* tex_names[] = { "Low", "Medium", "High", "Ultra" };
        int ti = g_graphics[G_TEXTURE].value;
        bool linear = (ti > 0);
        zcsr_gl_set_texture_filter_default(w_renderer, linear);
        zcsr_log_write(ZCSR_LOG_INFO, "shell", "[apply] texture quality: %s -> default filter %s\n",
               tex_names[ti < 0 || ti > 3 ? 0 : ti], linear ? "LINEAR" : "NEAREST");
    }

    {
        int cw = 0, ch = 0;
        zcsr_surface_get_client_size(w_surface, &cw, &ch);
        if (cw > 0 && ch > 0) { w_cw = cw; w_ch = ch; }
        zcsr_log_write(ZCSR_LOG_INFO, "shell", "[apply] client size now %dx%d\n", w_cw, w_ch);
    }

    zcsr_log_write(ZCSR_LOG_INFO, "shell", "[apply] graphics/audio values applied (pipeline print-only)\n");
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "[apply] ---\n");
}

static void reset_defaults(void) {
    g_video[V_DISPLAY].value    = 0;
    g_video[V_MODE].value       = 0; /* Windowed */
    g_video[V_RESOLUTION].value = 0;
    g_video[V_REFRESH].value    = 0;
    g_video[V_VSYNC].toggle_value   = true;
    g_video[V_FRAMELIMIT].value = 4;
    g_video[V_BRIGHTNESS].slider_value = 50.0f;
    g_video[V_FOV].slider_value     = 90.0f;
    g_video[V_SHARPNESS].slider_value = 50.0f;

    g_graphics[G_PRESET].value  = 2;
    g_graphics[G_TEXTURE].value = 2;
    g_graphics[G_SHADOW].value  = 2;
    g_graphics[G_AA].value      = 1;
    g_graphics[G_EFFECTS].value = 1;
    g_graphics[G_POST].value    = 1;
    g_graphics[G_VIEWDIST].slider_value = 70.0f;

    g_audio[A_MASTER].slider_value = 80.0f;
    g_audio[A_MUSIC].slider_value  = 60.0f;
    g_audio[A_SFX].slider_value    = 90.0f;
    g_audio[A_VOICE].slider_value  = 100.0f;
    g_audio[A_OUTPUT].value = 0;
    g_audio[A_INPUT].value  = 0;

    g_interface[I_OPACITY].slider_value = 100.0f;
    g_interface[I_DIM].slider_value = 62.0f;

    zcsr_game_settings_reset(w_settings);
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "[reset] defaults restored\n");
}

/* Makes sure the offscreen panel texture exists at the overlay layout size. */
static void panel_tex_ensure(void) {
    int px, py, pw, ph;
    layout_settings_overlay(&px, &py, &pw, &ph);
    if (pw * ph * 4 > (int)sizeof(panel_zero)) return; /* layout guarantees the cap */
    if (w_panel_tex != 0 && w_panel_tex_w == pw && w_panel_tex_h == ph) return;
    if (w_panel_tex == 0) {
        w_panel_tex = zcsr_gl_texture_create(w_renderer, panel_zero, pw, ph);
        if (w_panel_tex != 0) zcsr_gl_texture_filter(w_renderer, w_panel_tex, true);
    } else if (!zcsr_gl_texture_upload(w_renderer, w_panel_tex, panel_zero, pw, ph)) {
        return;
    }
    if (w_panel_tex != 0) {
        w_panel_tex_w = pw;
        w_panel_tex_h = ph;
    }
}

static void open_settings_overlay(void) {
    int px, py, pw, ph;
    layout_settings_overlay(&px, &py, &pw, &ph);
    panel_tex_ensure();
    w_settings_open = true;
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "[shell] settings open\n");
}

static void close_settings(bool apply) {
    if (apply) {
        zcsr_game_settings_apply(w_settings);
        apply_settings();
    } else {
        zcsr_game_settings_cancel(w_settings);
    }
    w_settings_open = false;
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "[shell] settings closed (%s)\n", apply ? "applied" : "cancelled");
}
#endif /* ZCSR_RUNTIME_WITH_SETTINGS_PAGE: apply/open/close */

/* ================================================================== */
/*  Rendering: home screen / module chrome / settings overlay          */
/* ================================================================== */
static void draw_button(zcsr_gl_renderer* r, float x, float y, float bw, float bh,
                        const char* text, bool hovered) {
    zcsr_gl_fill_rect(r, x, y, bw, bh,
                      hovered ? SHELL_BTN_HOV[0] : SHELL_BTN[0],
                      hovered ? SHELL_BTN_HOV[1] : SHELL_BTN[1],
                      hovered ? SHELL_BTN_HOV[2] : SHELL_BTN[2], 1.0f);
    /* accent stripe on the left edge */
    zcsr_gl_fill_rect(r, x, y, 6.0f, bh,
                      SHELL_ACCENT[0], SHELL_ACCENT[1], SHELL_ACCENT[2], hovered ? 1.0f : 0.65f);
    zcsr_gl_draw_text(r, text, x + 26.0f, y + bh * 0.5f - 8.0f, 2.0f,
                      SHELL_TEXT[0], SHELL_TEXT[1], SHELL_TEXT[2], 1.0f);
}

static void render_home(zcsr_gl_renderer* r, int mx, int my) {
    int i;
    int total = home_button_count();
    zcsr_gl_draw_text(r, "ZCSR // INTEGRATION SHELL", 24.0f, 48.0f, 2.4f,
                      SHELL_ACCENT[0], SHELL_ACCENT[1], SHELL_ACCENT[2], 1.0f);
    zcsr_gl_draw_text(r, "window skeleton v0 (id pending)", 24.0f, 84.0f, 1.0f,
                      0.45f, 0.47f, 0.52f, 1.0f);

    for (i = 0; i < total; ++i) {
        float x, y, bw, bh;
        const char* text;
        bool hovered;
        home_button_rect(i, &x, &y, &bw, &bh);
        if (i < w_module_count) text = w_modules[i].title;
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
        else if (i == w_module_count) text = "SETTINGS";
#endif
        else text = "EXIT";
        hovered = w_settings_open ? false : point_in_rect(mx, my, x, y, bw, bh);
        draw_button(r, x, y, bw, bh, text, hovered);
    }

#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
    zcsr_gl_draw_text(r,
        "click a category | S: settings | ESC: quit",
        24.0f, (float)w_ch - 26.0f, 1.0f, 0.42f, 0.44f, 0.48f, 1.0f);
#else
    zcsr_gl_draw_text(r,
        "click a category | ESC: quit",
        24.0f, (float)w_ch - 26.0f, 1.0f, 0.42f, 0.44f, 0.48f, 1.0f);
#endif
}

static void render_module_chrome(zcsr_gl_renderer* r, int mx, int my, int module_index) {
    float x, y, bw, bh;
    bool hovered;
    home_back_rect(&x, &y, &bw, &bh);
    hovered = w_settings_open ? false : point_in_rect(mx, my, x, y, bw, bh);
    zcsr_gl_fill_rect(r, x, y, bw, bh,
                      hovered ? SHELL_BTN_HOV[0] : SHELL_BTN[0],
                      hovered ? SHELL_BTN_HOV[1] : SHELL_BTN[1],
                      hovered ? SHELL_BTN_HOV[2] : SHELL_BTN[2], 1.0f);
    zcsr_gl_draw_text(r, "<< HOME", x + 12.0f, y + 11.0f, 1.0f,
                      SHELL_TEXT[0], SHELL_TEXT[1], SHELL_TEXT[2], 1.0f);
    if (module_index >= 0 && module_index < w_module_count) {
        zcsr_gl_draw_text(r, w_modules[module_index].title, 140.0f, 14.0f, 1.6f,
                          SHELL_ACCENT[0], SHELL_ACCENT[1], SHELL_ACCENT[2], 1.0f);
    }
}

/* Renders the settings overlay over the (already queued) page content. The sprite
 * queue is one batch, so the page is flushed to the window FIRST; the panel is then
 * drawn into the offscreen texture (queue is empty at that point), and finally the
 * dim layer + composite sprite are queued for the caller's final flush. All public
 * renderer APIs, all shell-internal. */
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
static void render_settings_overlay(zcsr_gl_renderer* r) {
    int px, py, pw, ph;
    float dim, opacity;

    layout_settings_overlay(&px, &py, &pw, &ph);
    dim = g_interface[I_DIM].slider_value * 0.01f;
    opacity = g_interface[I_OPACITY].slider_value * 0.01f;

    if (w_panel_tex == 0 || w_panel_tex_w != pw || w_panel_tex_h != ph) {
        panel_tex_ensure();
    }

    if (w_panel_tex == 0 || opacity >= 0.99f) {
        /* 不透明（默认 100%）直接画在窗口上：省掉 FBO 往返 + 纹理采样一层，
         * 文字就是像素本身，不存在"切换时糊一下/慢一帧"的观感。
         * 只有玩家把 Interface 的 Settings Opacity 调低时才走离屏合成。 */
        zcsr_gl_flush(r);
        zcsr_gl_fill_rect(r, 0.0f, 0.0f, (float)w_cw, (float)w_ch, 0.0f, 0.0f, 0.0f, dim);
        zcsr_game_settings_set_bounds(w_settings, px, py, pw, ph);
        zcsr_game_settings_render(w_settings, r);
        return;
    }

    /* 1) page content -> window */
    zcsr_gl_flush(r);

    /* 2) dim layer over the page -> window */
    zcsr_gl_fill_rect(r, 0.0f, 0.0f, (float)w_cw, (float)w_ch, 0.0f, 0.0f, 0.0f, dim);
    zcsr_gl_flush(r);

    /* 3) panel -> offscreen texture (rendered from (0,0,pw,ph); queue is empty here) */
    zcsr_game_settings_set_bounds(w_settings, 0, 0, pw, ph);
    zcsr_gl_set_target(r, w_panel_tex);
    zcsr_gl_fill_rect(r, 0.0f, 0.0f, (float)pw, (float)ph, 0.0f, 0.0f, 0.0f, 1.0f);
    zcsr_game_settings_render(w_settings, r);
    zcsr_gl_flush(r);
    zcsr_gl_set_target(r, 0);

    /* 4) composite at the adjustable opacity (queued; caller flushes).
     * FBO round-trip flip: the panel was rendered into the texture with the
     * renderer's y-down projection, so the image's TOP row sits at texture v=1
     * while a sprite quad samples its top edge at v0 — flip v to draw upright. */
    zcsr_gl_queue_atlas_sprite(r, w_panel_tex,
                               (float)px, (float)py, (float)pw, (float)ph,
                               0.0f, 1.0f, 1.0f, 0.0f,
                               1.0f, 1.0f, 1.0f, opacity);

    /* 离屏渲染把 bounds 挪到了 (0,0)：交还成窗口坐标，否则下一帧鼠标命中会再偏一次 */
    zcsr_game_settings_set_bounds(w_settings, px, py, pw, ph);
}
#endif /* ZCSR_RUNTIME_WITH_SETTINGS_PAGE: overlay render */

#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
/* 设置页的四张标签页指向同一份静态数据（g_video/g_graphics/g_audio/g_interface）。
 * 运行时 shell 和编辑器内嵌页都用这一个函数，避免两份设置数据各自漂移。 */
static void build_setting_tabs(void) {
    init_display_list();
    init_items();
    w_tabs[0] = (zcsr_game_settings_tab){ .name="Video",     .items=g_video,     .count=V_COUNT };
    w_tabs[1] = (zcsr_game_settings_tab){ .name="Graphics",  .items=g_graphics,  .count=G_COUNT };
    w_tabs[2] = (zcsr_game_settings_tab){ .name="Audio",     .items=g_audio,     .count=A_COUNT };
    w_tabs[3] = (zcsr_game_settings_tab){ .name="Interface", .items=g_interface, .count=I_COUNT };
}

zcsr_game_settings* zcsr_runtime_shell_settings_create(void* buf, size_t bytes,
                                               int x, int y, int w, int h) {
    build_setting_tabs();
    return zcsr_game_settings_create(buf, bytes,
        &(zcsr_game_settings_config){
            .tabs        = w_tabs,
            .tab_count   = 4,
            .x           = x,
            .y           = y,
            .width       = w,
            .height      = h,
            .item_height = 34,
            .text_scale  = 1.4f,
        });
}
#else
/* No page manager: the shell owns no settings page and exposes no SETTINGS
 * entry. Runtime remains fully usable; a developer can supply their own page
 * manager module later without changing shell core. */
static void open_settings_overlay(void) {}
static void close_settings(bool apply) { (void)apply; }
static void render_settings_overlay(zcsr_gl_renderer* r) { (void)r; }
static void panel_tex_ensure(void) {}
#endif

/* ================================================================== */
/*  Shell main loop                                                    */
/* ================================================================== */
int zcsr_runtime_shell_run(const zcsr_runtime_shell_module* modules, int module_count,
                     int argc, char** argv) {
    int i;
    uint64_t frame_start_ns = zcsr_clock_now_ns();
    const char* shell_title = "zcsr window shell";

    w_autoquit = false;
    if (argv != NULL) {
        int argi;
        for (argi = 1; argi < argc; ++argi) {
            if (argv[argi] != NULL && strcmp(argv[argi], "autoquit") == 0) {
                w_autoquit = true;
                break;
            }
        }
    }
    if (module_count > SHELL_MAX_MODULES) module_count = SHELL_MAX_MODULES;
    w_modules = modules;
    w_module_count = module_count;
    /* A single module is the application itself (editor/player): use its name
     * as the OS title. Multi-module shells keep the neutral test-host title. */
    if (module_count == 1 && modules && modules[0].title && modules[0].title[0])
        shell_title = modules[0].title;
    w_frame_host.renderer = NULL;
    w_frame_host.cw = SHELL_W;
    w_frame_host.ch = SHELL_H;
    w_tasks = zcsr_task_scheduler_create(4);
    w_frame_loop = zcsr_frame_loop_create((size_t)module_count + 1u);
    if (!w_frame_loop) {
        zcsr_log_write(ZCSR_LOG_ERROR, "shell", "FAIL: frame loop allocation\n");
        zcsr_task_scheduler_destroy(w_tasks);
        w_tasks = NULL;
        return 1;
    }
    for (i = 0; i < module_count; ++i) {
        zcsr_frame_module_desc desc;
        memset(&desc, 0, sizeof(desc));
        desc.title = modules[i].title;
        desc.service = false;
        desc.user = (void*)&w_modules[i];
        desc.init = shell_frame_init;
        desc.tick = shell_frame_tick;
        desc.shutdown = shell_frame_shutdown;
        if (!zcsr_frame_loop_add_module(w_frame_loop, &desc)) {
            zcsr_log_write(ZCSR_LOG_ERROR, "shell", "FAIL: frame module registration\n");
            zcsr_frame_loop_destroy(w_frame_loop);
            w_frame_loop = NULL;
            zcsr_task_scheduler_destroy(w_tasks);
            w_tasks = NULL;
            return 1;
        }
    }
    w_active_module = (module_count == 1) ? 0 : -1;

    zcsr_log_write(ZCSR_LOG_INFO, "shell", "=== %s ===\n", shell_title);
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "single-window shell; optional SETTINGS page enabled; %d module(s)\n",
           module_count);
#else
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "single-window shell; no page-manager dependency; %d module(s)\n",
           module_count);
#endif

#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
    build_setting_tabs();
#endif

    w_surface = zcsr_surface_create(shell_title,
                                    (zcsr_rect){ 80, 80, SHELL_W, SHELL_H });
    if (!w_surface) { zcsr_log_write(ZCSR_LOG_ERROR, "shell", "FAIL: no surface\n"); return 1; }

    /* The platform default is a topmost fullscreen overlay (no taskbar, no Alt-Tab,
     * cursor feels lost). Switch to a NORMAL window immediately: taskbar icon,
     * Alt-Tab switchable, never topmost, visible mouse cursor. */
    if (!zcsr_surface_set_mode(w_surface, ZCSR_WINDOW_WINDOWED)) {
        zcsr_log_write(ZCSR_LOG_ERROR, "shell", "WARN: windowed mode switch failed\n");
    }
    zcsr_surface_set_client_size(w_surface, SHELL_W, SHELL_H);

    w_renderer = zcsr_gl_create(w_surface, renderer_buf, sizeof(renderer_buf));
    w_frame_host.renderer = w_renderer;
    /* 默认字体：真实 TTF，8x8 只作兜底。
     * 解析顺序 ZCSR_FONT_TTF -> 系统 UI 字体（segoeui/arial/DejaVu）。
     * 游戏想用自带字体：zcsr_gl_font_from_file(自己的路径)，引擎不用改。 */
    if (w_renderer) {
        /* TTF 字节(约1MB) + zcsr_font 副本(约1.1MB) + atlas(4.26MB)，6MB 不够 */
        static unsigned char w_font_buf[16u * 1024u * 1024u];
        const char* pxs = getenv("ZCSR_FONT_PX");
        int px = 16;
        if (pxs && pxs[0]) {
            char* end;
            long v = strtol(pxs, &end, 10);
            if (end && *end == '\0' && v > 0 && v <= 512) px = (int)v;
        }
        if (zcsr_gl_font_default(w_renderer, px, w_font_buf, sizeof(w_font_buf))) {
            zcsr_log_write(ZCSR_LOG_INFO, "shell", "[font] default TTF bound @ %dpx\n", px);
        } else {
            zcsr_log_write(ZCSR_LOG_INFO, "shell", "[font] no system TTF found, using builtin 8x8\n");
        }
    }
    if (!w_renderer) {
        zcsr_log_write(ZCSR_LOG_ERROR, "shell", "FAIL: no renderer\n");
        zcsr_surface_destroy(w_surface);
        return 1;
    }
    zcsr_gl_set_vsync(w_renderer, true);

    w_input = zcsr_input_init(input_buf, sizeof(input_buf));
    if (!w_input) { zcsr_log_write(ZCSR_LOG_ERROR, "shell", "FAIL: no input\n"); goto cleanup; }

    w_inputext = zcsr_input_ext_init(inputext_buf, sizeof(inputext_buf));
    if (!w_inputext) { zcsr_log_write(ZCSR_LOG_ERROR, "shell", "FAIL: no input_ext\n"); goto cleanup; }

    zcsr_surface_get_client_size(w_surface, &w_cw, &w_ch);
    if (w_cw <= 0 || w_ch <= 0) { w_cw = SHELL_W; w_ch = SHELL_H; }

#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
    w_settings = zcsr_runtime_shell_settings_create(settings_buf, sizeof(settings_buf),
                                             200, 90, 880, 540);
    if (!w_settings) { zcsr_log_write(ZCSR_LOG_ERROR, "shell", "FAIL: settings create\n"); goto cleanup; }

    /* 编辑器退出编辑态写下的几何覆盖，运行时读回同一个文件 —— 这样"在编辑器里挪过的
     * 按钮"真的出现在游戏里，而不是只留在编辑器会话。文件缺失就保持公式布局。 */
    {
        const char* layout_path = getenv("ZCSR_LAYOUT_PATH");
        if (!layout_path || !layout_path[0]) layout_path = "settings_layout.txt";
        if (zcsr_game_settings_load_overrides(w_settings, layout_path))
            zcsr_log_write(ZCSR_LOG_INFO, "shell", "[settings] layout overrides loaded: %s\n", layout_path);
    }

    panel_tex_ensure();
#endif

    /* 单 module = 应用主界面（editor/player）：直接进入模块，不经过 home，
     * 也不叠加 "<< HOME" chrome，否则会盖住编辑器自己的顶栏。 */
    if (w_module_count == 1) w_active_module = 0;

#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "Ready. window %dx%d, %d display(s)\n", w_cw, w_ch, g_display_count);
#else
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "Ready. window %dx%d\n", w_cw, w_ch);
#endif
    fflush(stdout);

    while (w_running) {
        zcsr_key_event ev;
        zcsr_mouse_state mst;

        zcsr_input_pump(w_input, w_surface);
        zcsr_input_ext_pump(w_inputext, w_surface);
        zcsr_surface_pump_events(w_surface);
        if (zcsr_window_close_requested(w_surface)) {
            bool allow = true;
            if (w_active_module >= 0 && w_active_module < w_module_count &&
                w_modules[w_active_module].close_request != NULL) {
                allow = w_modules[w_active_module].close_request(
                    w_modules[w_active_module].user);
            }
            if (allow) w_running = false;
        }

        {
            int cw = 0, ch = 0;
            zcsr_surface_get_client_size(w_surface, &cw, &ch);
            if (cw > 0 && ch > 0 && (cw != w_cw || ch != w_ch)) {
                w_cw = cw;
                w_ch = ch;
                if (w_settings_open) panel_tex_ensure(); /* re-layout the overlay */
            }
        }

        mst = zcsr_input_ext_mouse(w_inputext);
        g_runtime_shell_mouse_x = mst.x;
        g_runtime_shell_mouse_y = mst.y;
        g_runtime_shell_wheel = mst.wheel;
        {
            unsigned int pressed = (unsigned int)((mst.buttons & ~w_prev_mouse_buttons) |
                                                  mst.pressed);
            unsigned int released = (unsigned int)((~mst.buttons & w_prev_mouse_buttons) |
                                                   mst.released);
            if (w_active_module >= 0 && !w_settings_open &&
                w_modules[w_active_module].pointer != NULL) {
                w_modules[w_active_module].pointer(mst.x, mst.y, mst.buttons,
                                                  pressed, released,
                                                  w_modules[w_active_module].user);
            }
            if (w_active_module >= 0 && !w_settings_open &&
                w_modules[w_active_module].text != NULL) {
                char text[256];
                size_t text_n = zcsr_input_ext_take_text(w_inputext, text, sizeof(text));
                if (text_n) w_modules[w_active_module].text(text, w_modules[w_active_module].user);
            }
        }

        /* ---- mouse routing ---- */
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
        if (w_settings_open) {
            /* 页面自己的命中测试用的是"绝对窗口坐标"（内部所有矩形都是 s->x + 偏移），
             * 所以这里必须原样喂入，不能再减一次 px/py —— 之前减了，点击全部偏掉
             * (-200,-90)，表现就是 Apply / Cancel / Reset 和 tab 点了没反应。 */
            int maction = zcsr_game_settings_update_mouse(w_settings, &mst);
            if (maction == 1) close_settings(true);
            else if (maction == -1) close_settings(false);
        } else
#endif
        {
            bool clicked = (mst.buttons & ZCSR_MOUSE_LEFT) != 0 &&
                           (w_prev_mouse_buttons & ZCSR_MOUSE_LEFT) == 0;
            if (clicked) {
                if (w_active_module < 0) {
                    int total = home_button_count();
                    for (i = 0; i < total; ++i) {
                        float x, y, bw, bh;
                        home_button_rect(i, &x, &y, &bw, &bh);
                        if (point_in_rect(mst.x, mst.y, x, y, bw, bh)) {
                            if (i < w_module_count) {
                                w_active_module = i;
                                zcsr_log_write(ZCSR_LOG_INFO, "shell", "[shell] enter module: %s\n", w_modules[i].title);
                            }
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
                            else if (i == w_module_count) {
                                open_settings_overlay();
                            }
#endif
                            else {
                                w_running = false;
                            }
                            break;
                        }
                    }
                } else {
                    float x, y, bw, bh;
                    home_back_rect(&x, &y, &bw, &bh);
                    if (w_module_count > 1 && point_in_rect(mst.x, mst.y, x, y, bw, bh)) {
                        zcsr_log_write(ZCSR_LOG_INFO, "shell", "[shell] back to home\n");
                        w_active_module = -1;
                    }
                }
            }
        }
        w_prev_mouse_buttons = mst.buttons;

        /* ---- keyboard routing ---- */
        while (zcsr_input_poll(w_input, &ev)) {
            if (ev.action != ZCSR_KEY_DOWN || ev.text) continue;
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
            if (w_settings_open) {
                if (ev.key == ZCSR_KEY_R) { reset_defaults(); continue; }
                {
                    int result = zcsr_game_settings_handle_key(w_settings, ev.key, ev.action);
                    if (result == 1) close_settings(true);
                    else if (result == -1) close_settings(false);
                }
                continue;
            }
#endif
            if (w_active_module < 0) {
                if (ev.key == ZCSR_KEY_ESCAPE) { w_running = false; continue; }
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
                if (ev.key == ZCSR_KEY_S) { open_settings_overlay(); continue; }
#endif
            } else {
                bool consumed = false;
                /* 模块优先消费按键：editor 用 S 切自己的设置页，不能被 shell
                 * 抢先吃掉；模块不消费时 shell 才把 S 当成全局设置入口。 */
                if (w_modules[w_active_module].key != NULL) {
                    consumed = w_modules[w_active_module].key((int)ev.key,
                                                             w_modules[w_active_module].user);
                }
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
                if (!consumed && ev.key == ZCSR_KEY_S) {
                    open_settings_overlay();
                    continue;
                }
#endif
                if (!consumed && ev.key == ZCSR_KEY_ESCAPE) {
                    zcsr_log_write(ZCSR_LOG_INFO, "shell", "[shell] back to home\n");
                    w_active_module = -1;
                }
            }
        }

        /* ---- autoquit self-test script ---- */
        if (w_autoquit && w_frame == 20 && w_module_count > 0) {
            w_active_module = 0;
            zcsr_log_write(ZCSR_LOG_INFO, "shell", "autoquit: enter module 0\n");
        }
        if (w_autoquit && w_frame == 40 && w_module_count > 0) {
            if (w_modules[0].key != NULL) {
                (void)w_modules[0].key(ZCSR_KEY_SPACE, w_modules[0].user);
                zcsr_log_write(ZCSR_LOG_INFO, "shell", "autoquit: sent SPACE to module\n");
            }
        }
        if (w_autoquit && w_frame == 50 && w_module_count > 0 &&
            w_modules[0].pointer != NULL) {
            w_modules[0].pointer(300, 110, ZCSR_MOUSE_LEFT, ZCSR_MOUSE_LEFT, 0,
                                 w_modules[0].user);
            zcsr_log_write(ZCSR_LOG_INFO, "shell", "autoquit: sent pointer down\n");
        }
        if (w_autoquit && w_frame == 55 && w_module_count > 0 &&
            w_modules[0].pointer != NULL) {
            w_modules[0].pointer(300, 110, 0, 0, ZCSR_MOUSE_LEFT,
                                 w_modules[0].user);
        }
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
        if (w_autoquit && w_frame == 60) open_settings_overlay();
        if (w_autoquit && w_frame == 90) close_settings(false);
#endif
        if (w_autoquit && w_frame >= 150) w_running = false;

        /* ---- render ---- */
        zcsr_gl_begin(w_renderer);
        zcsr_gl_clear(w_renderer, SHELL_BG[0], SHELL_BG[1], SHELL_BG[2], 1.0f);

        w_frame_host.cw = w_cw;
        w_frame_host.ch = w_ch;
        (void)zcsr_frame_loop_set_active(w_frame_loop, w_active_module);
        if (w_active_module < 0) {
            render_home(w_renderer, mst.x, mst.y);
        } else {
            /* Module lifecycle/frame is owned by zcsr_frame_loop, not by shell
             * branches. The host context is only the current renderer/client size. */
            {
                uint64_t now_ns = zcsr_clock_now_ns();
                double dt = (double)(now_ns - frame_start_ns) / 1000000000.0;
                if (dt <= 0.0) dt = 1.0 / 60.0;
                if (!zcsr_frame_loop_tick(w_frame_loop, &w_frame_host, dt)) {
                    zcsr_log_write(ZCSR_LOG_ERROR, "shell", "FAIL: module frame\n");
                    w_running = false;
                    break;
                }
            }
            if (w_module_count > 1) {
                render_module_chrome(w_renderer, mst.x, mst.y, w_active_module);
            }
        }

#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
        if (w_settings_open) {
            render_settings_overlay(w_renderer);
        }
#endif

        zcsr_gl_flush(w_renderer);
        zcsr_gl_present(w_renderer);

        /* 帧率上限真正生效：按 Video 的 Frame Rate Limit 补足剩余时间。
         * 之前无条件 sleep(16) —— 设置里的这一项是纯装饰，而且把上限写死成 ~62fps。 */
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
        {
            static const int fps_targets[] = { 30, 60, 120, 144, 0 }; /* 0 = Unlimited */
            int idx = g_video[V_FRAMELIMIT].value;
            uint64_t now = zcsr_clock_now_ns();
            uint64_t budget_ns = 0;
            if (idx >= 0 && idx < 5 && fps_targets[idx] > 0)
                budget_ns = 1000000000ull / (uint64_t)fps_targets[idx];
            if (budget_ns > 0 && now - frame_start_ns < budget_ns) {
                uint64_t left_ms = (budget_ns - (now - frame_start_ns)) / 1000000ull;
                if (left_ms > 0) shell_sleep_ms((int)left_ms);
            }
            frame_start_ns = zcsr_clock_now_ns();
        }
#else
        /* No settings page: leave pacing to vsync; no hidden fixed-rate sleep. */
        frame_start_ns = zcsr_clock_now_ns();
#endif
        ++w_frame;
    }

cleanup:
    if (w_frame_loop) {
        zcsr_frame_loop_shutdown(w_frame_loop, &w_frame_host);
        zcsr_frame_loop_destroy(w_frame_loop);
        w_frame_loop = NULL;
    }
    zcsr_task_scheduler_destroy(w_tasks);
    w_tasks = NULL;
#if defined(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
    if (w_panel_tex != 0) zcsr_gl_texture_destroy(w_renderer, w_panel_tex);
    if (w_settings) zcsr_game_settings_destroy(w_settings);
#endif
    if (w_inputext) zcsr_input_ext_shutdown(w_inputext);
    if (w_renderer) zcsr_gl_destroy(w_renderer);
    if (w_surface) zcsr_surface_destroy(w_surface);
    zcsr_log_write(ZCSR_LOG_INFO, "shell", "=== window shell ended (%d frames) ===\n", w_frame);
    return 0;
}

int zcsr_window_game_run(const zcsr_runtime_shell_module* modules, int module_count,
                         int argc, char** argv) {
    return zcsr_runtime_shell_run(modules, module_count, argc, argv);
}

