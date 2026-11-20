/* Real shell-key routing test for the settings overlay.
 *
 * A synthetic key is queued into the shell's one zcsr_input ring through
 * zcsr_runtime_shell_inject_key(); it is consumed by the normal
 * zcsr_input_poll()/keyboard-routing branch in zcsr_window_game_run(). This
 * covers the shell's ZCSR_KEY_S / ZCSR_KEY_R / settings Esc/Tab/arrow routing,
 * not just zcsr_game_settings_handle_key() in isolation.
 */
#include "zcsr_runtime_shell.h"
#include "zcsr/input.h"

#include <stdio.h>
#include <string.h>

static int frame_count;
static int failures;
static int init_count;
static int shutdown_count;
static int module_key_count;
static zcsr_keycode last_module_key;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL: %s (frame %d)\n", (msg), frame_count);             \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static bool dummy_init(zcsr_gl_renderer* r, int cw, int ch, void* user) {
    (void)r; (void)cw; (void)ch; (void)user;
    ++init_count;
    return true;
}

static void dummy_shutdown(zcsr_gl_renderer* r, void* user) {
    (void)r; (void)user;
    ++shutdown_count;
}

static void inject(zcsr_keycode key, zcsr_key_action action) {
    CHECK(zcsr_runtime_shell_inject_key(key, action), "shell key injection");
}

static bool dummy_key(int key, void* user) {
    (void)user;
    last_module_key = (zcsr_keycode)key;
    ++module_key_count;
    /* The module consumes only SPACE; S/R/navigation must fall through to the
     * shell settings overlay. */
    return key == ZCSR_KEY_SPACE;
}

static void dummy_frame(zcsr_gl_renderer* r, int cw, int ch, void* user) {
    (void)r; (void)cw; (void)ch; (void)user;

    switch (frame_count) {
    case 3:  inject(ZCSR_KEY_S, ZCSR_KEY_DOWN); break;
    case 4:  inject(ZCSR_KEY_S, ZCSR_KEY_UP); break;
    case 5:
        CHECK(zcsr_runtime_shell_test_settings_open(), "S opens settings in shell");
        inject(ZCSR_KEY_E, ZCSR_KEY_DOWN);
        break;
    case 6:  inject(ZCSR_KEY_E, ZCSR_KEY_UP); break;
    case 7:
        CHECK(zcsr_runtime_shell_test_settings_tab() == 1, "E switches settings tab in shell");
        inject(ZCSR_KEY_Q, ZCSR_KEY_DOWN);
        break;
    case 8:  inject(ZCSR_KEY_Q, ZCSR_KEY_UP); break;
    case 9:
        CHECK(zcsr_runtime_shell_test_settings_tab() == 0, "Q switches settings tab back in shell");
        inject(ZCSR_KEY_ARROW_DOWN, ZCSR_KEY_DOWN);
        break;
    case 10: inject(ZCSR_KEY_ARROW_DOWN, ZCSR_KEY_UP); break;
    case 11:
        CHECK(zcsr_runtime_shell_test_settings_last_key() == ZCSR_KEY_ARROW_DOWN,
              "Down reaches settings shell route");
        inject(ZCSR_KEY_TAB, ZCSR_KEY_DOWN);
        break;
    case 12: inject(ZCSR_KEY_TAB, ZCSR_KEY_UP); break;
    case 13:
        CHECK(zcsr_runtime_shell_test_settings_last_key() == ZCSR_KEY_TAB,
              "Tab reaches settings shell route");
        zcsr_runtime_shell_test_settings_set_video_value(1, 2);
        inject(ZCSR_KEY_R, ZCSR_KEY_DOWN);
        break;
    case 14: inject(ZCSR_KEY_R, ZCSR_KEY_UP); break;
    case 15:
        CHECK(zcsr_runtime_shell_test_settings_video_value(1) == 0,
              "R resets settings defaults in shell");
        inject(ZCSR_KEY_ESCAPE, ZCSR_KEY_DOWN);
        break;
    case 16: inject(ZCSR_KEY_ESCAPE, ZCSR_KEY_UP); break;
    case 17:
        CHECK(!zcsr_runtime_shell_test_settings_open(), "Esc closes settings in shell");
        inject(ZCSR_KEY_S, ZCSR_KEY_DOWN);
        break;
    case 18: inject(ZCSR_KEY_S, ZCSR_KEY_UP); break;
    case 19:
        CHECK(zcsr_runtime_shell_test_settings_open(), "S reopens settings in shell");
        inject(ZCSR_KEY_ENTER, ZCSR_KEY_DOWN);
        break;
    case 20: inject(ZCSR_KEY_ENTER, ZCSR_KEY_UP); break;
    case 21:
        CHECK(zcsr_runtime_shell_test_settings_last_key() == ZCSR_KEY_ENTER,
              "Enter reaches settings shell route");
        if (zcsr_runtime_shell_test_settings_open()) {
            inject(ZCSR_KEY_ESCAPE, ZCSR_KEY_DOWN);
        }
        break;
    case 22: inject(ZCSR_KEY_ESCAPE, ZCSR_KEY_UP); break;
    case 23:
        CHECK(!zcsr_runtime_shell_test_settings_open(), "settings closed after Enter/Esc");
        break;
    default:
        break;
    }
    ++frame_count;
}

int main(void) {
    zcsr_runtime_shell_module mod;
    char* argv[] = { (char*)"zcsr_runtime_shell_settings_keys", (char*)"autoquit", NULL };
    int rc;

    memset(&mod, 0, sizeof(mod));
    mod.title = "settings-keys";
    mod.init = dummy_init;
    mod.shutdown = dummy_shutdown;
    mod.frame = dummy_frame;
    mod.key = dummy_key;

    rc = zcsr_window_game_run(&mod, 1, 2, argv);
    CHECK(rc == 0, "shell exit code");
    CHECK(init_count == 1, "module init once");
    CHECK(shutdown_count == 1, "module shutdown once");
    CHECK(frame_count >= 150, "real shell ran to autoquit");
    CHECK(failures == 0, "no shell key-routing failures");

    if (failures) {
        printf("runtime_shell_settings_keys: %d failure(s)\n", failures);
        return 1;
    }
    printf("runtime_shell_settings_keys: PASS frames=%d shell_routes=%d\n",
           frame_count, module_key_count);
    return 0;
}
