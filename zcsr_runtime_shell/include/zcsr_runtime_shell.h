#ifndef ZCSR_RUNTIME_SHELL_H
#define ZCSR_RUNTIME_SHELL_H
/* integration/window.h — 独立 window 骨架 (shell) 的挂载接口.
 *
 * window.c owns the WHOLE app lifecycle: a normal OS window (taskbar icon,
 * Alt-Tab switchable, never topmost), a black home screen with categorized
 * main-menu buttons (Delta Force style: one big button per registered test
 * module, plus the built-in SETTINGS and EXIT), the complete settings panel
 * as an overlay (Video / Graphics / Audio — real window management), and the
 * single main loop. This header is the plug-in point; a standard contract id
 * under include/zcsr will be defined later, at which point this file moves.
 *
 * Usage (see testmain.c):
 *   static const zcsr_runtime_shell_module mods[] = { {...light module...} };
 *   return zcsr_runtime_shell_run(mods, 1, argc, argv);
 */
#include "zcsr/gl_render.h"
#include "zcsr_ui_page_manager/ui_game_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 用与运行时 shell 完全相同的一份设置页数据（Video/Graphics/Audio/Interface）
 * 建一个独立实例，供编辑器内嵌显示、逐控件改位置。
 * buf 至少 8192 字节（与运行时同一份），返回 NULL 表示配置无效。 */
zcsr_game_settings* zcsr_runtime_shell_settings_create(void* buf, size_t bytes,
                                               int x, int y, int w, int h);

/* One pluggable test module = one big button on the home screen.
 * All callbacks may be NULL except frame. r is the shared renderer; cw/ch are
 * the current window client size (module must adapt, resize between frames). */
typedef struct zcsr_runtime_shell_module {
    const char* title;                                              /* button label */
    void* user;                                                     /* module state */
    bool (*init)(zcsr_gl_renderer* r, int cw, int ch, void* user);  /* lazy, once */
    void (*shutdown)(zcsr_gl_renderer* r, void* user);              /* at app exit */
    void (*frame)(zcsr_gl_renderer* r, int cw, int ch, void* user); /* one frame */
    bool (*key)(int key, void* user);                /* key press; true = consumed */
} zcsr_runtime_shell_module;

/* Boots window/renderer/inputs/settings, runs the single main loop until the
 * user exits, tears everything down. Returns the process exit code.
 * argv is forwarded so "autoquit" runs the headless self-test. */
int zcsr_runtime_shell_run(const zcsr_runtime_shell_module* modules, int module_count,
                     int argc, char** argv);

/* 外部设置鼠标位置（Editor Runtime 用 input_ext 更新后传给模块） */
void zcsr_runtime_shell_set_mouse_position(int x, int y);

/* 获取当前鼠标位置（模块用） */
void zcsr_runtime_shell_get_mouse_position(int* x, int* y);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_RUNTIME_SHELL_H */

/* 设置页 -> 模块落点（Graphics / Default FOV）。模块未 init 时返回 false。 */
bool  zcsr_light2d_module_set_render_options(int shadows, int light_scale_pct,
                                             int bloom, int ray_shadows);
bool  zcsr_light2d_module_set_fov_deg(float fov_deg);
float zcsr_light2d_module_get_pitch_deg(void);
bool  zcsr_light2d_module_set_scene(int effects, float view_distance);
