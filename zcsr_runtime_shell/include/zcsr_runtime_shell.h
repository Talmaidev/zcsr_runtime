#ifndef ZCSR_RUNTIME_SHELL_H
#define ZCSR_RUNTIME_SHELL_H
/* zcsr_runtime_shell.h — single OS-window / main-loop owner.
 *
 * window ownership belongs here, never to a module. A single module is the
 * application itself (editor/player): its title becomes the OS window title and
 * the shell enters it directly. Multiple modules use the neutral home screen
 * with one button per module plus built-in SETTINGS and EXIT.
 */
#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional settings/page-manager ownership:
 * zcsr_runtime_shell does not own page content. If
 * ZCSR_RUNTIME_WITH_SETTINGS_PAGE is enabled, the shell may link the UIM
 * page manager and expose a settings overlay; otherwise the shell has no
 * page-manager dependency and the SETTINGS entry is absent. */

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
    /* Optional pointer callback. pressed/released are this-frame button edges;
     * buttons is the current mask. Called only while this module is active. */
    void (*pointer)(int x, int y, uint32_t buttons, uint32_t pressed,
                    uint32_t released, void* user);
    /* Optional UTF-8 text produced by WM_CHAR/WM_IME_CHAR since the previous
     * frame. Editor chrome uses this for Inspector/text fields; player modules
     * may ignore it. */
    void (*text)(const char* utf8, void* user);
    /* Optional close veto: false keeps the window open (e.g. unsaved edits).
     * Called before the shell consumes the OS close request. */
    bool (*close_request)(void* user);
} zcsr_runtime_shell_module;

/* Boots window/renderer/inputs/settings, runs the single main loop until the
 * user exits, tears everything down. Returns the process exit code.
 * argv is forwarded so "autoquit" runs the headless self-test. */
int zcsr_runtime_shell_run(const zcsr_runtime_shell_module* modules, int module_count,
                     int argc, char** argv);

/* Canonical game/player host entry. It is the same single OS-window and
 * main-loop owner as zcsr_runtime_shell_run, named at the Runtime boundary so
 * game, player and editor-Play all enter through one host contract. It does
 * not create a second window or a second loop. */
int zcsr_window_game_run(const zcsr_runtime_shell_module* modules, int module_count,
                         int argc, char** argv);

/* 外部设置鼠标位置（Editor Runtime 用 input_ext 更新后传给模块） */
void zcsr_runtime_shell_set_mouse_position(int x, int y);

/* 获取当前鼠标位置（模块用） */
void zcsr_runtime_shell_get_mouse_position(int* x, int* y);

/* 模块共享 shell 自己的窗口/渲染器，不再另建第二套 OS window。
 * surface 只在 shell 运行期间有效；module init/frame/shutdown 内可读。 */
zcsr_surface* zcsr_runtime_shell_surface(void);

/* Runtime-owned worker pool. Valid only while zcsr_runtime_shell_run is active;
 * modules may submit background work. NULL when the pool could not be created. */
typedef struct zcsr_task_scheduler zcsr_task_scheduler;
zcsr_task_scheduler* zcsr_runtime_shell_tasks(void);

/* 模块读取 shell 已泵过的键盘状态（Ctrl/Shift 等），不另建 input pump。 */
bool zcsr_runtime_shell_key_down(zcsr_keycode key);

/* 模块消费本帧滚轮增量；读取后清零，保证一个 tick 只结算一次。 */
int zcsr_runtime_shell_take_wheel_delta(void);

/* 模块请求退出应用（例如 File→Exit）。仍然由 shell 收尾、销毁窗口与模块；
 * 调用前模块自己负责未保存确认。 */
void zcsr_runtime_shell_request_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_RUNTIME_SHELL_H */
