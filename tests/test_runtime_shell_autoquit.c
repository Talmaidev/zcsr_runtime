/* Real single-window/runtime loop test: runs the shell with the "autoquit"
 * script and a dummy module, then asserts lifecycle, one surface identity,
 * key routing, pointer routing and teardown. It is intentionally not a
 * headless model test: zcsr_window_game_run boots the real OS window. */
#include "zcsr_runtime_shell.h"
#include "zcsr/platform.h"
#include "zcsr/input.h"
#include "zcsr/input_ext.h"

#include <assert.h>
#include <stdio.h>

static int init_count;
static int shutdown_count;
static int frame_count;
static int key_count;
static int pointer_down;
static int pointer_up;
static zcsr_surface* seen_surface;

static bool dummy_init(zcsr_gl_renderer* r, int cw, int ch, void* user) {
    (void)r; (void)cw; (void)ch; (void)user;
    seen_surface = zcsr_runtime_shell_surface();
    assert(seen_surface != NULL);
    assert(zcsr_surface_native_handle(seen_surface) != NULL);
    ++init_count;
    return true;
}

static void dummy_shutdown(zcsr_gl_renderer* r, void* user) {
    (void)r; (void)user;
    assert(zcsr_runtime_shell_surface() == seen_surface);
    ++shutdown_count;
}

static void dummy_frame(zcsr_gl_renderer* r, int cw, int ch, void* user) {
    (void)r; (void)cw; (void)ch; (void)user;
    assert(zcsr_runtime_shell_surface() == seen_surface);
    ++frame_count;
}

static bool dummy_key(int key, void* user) {
    (void)user;
    assert(key == ZCSR_KEY_SPACE);
    ++key_count;
    return true;
}

static void dummy_pointer(int x, int y, uint32_t buttons, uint32_t pressed,
                          uint32_t released, void* user) {
    (void)x; (void)y; (void)buttons; (void)user;
    if ((pressed & ZCSR_MOUSE_LEFT) != 0) ++pointer_down;
    if ((released & ZCSR_MOUSE_LEFT) != 0) ++pointer_up;
}

int main(void) {
    zcsr_runtime_shell_module mod;
    char* argv[] = { (char*)"zcsr_runtime_shell_autoquit", (char*)"autoquit", NULL };
    int rc;

    mod.title = "autoquit";
    mod.user = NULL;
    mod.init = dummy_init;
    mod.shutdown = dummy_shutdown;
    mod.frame = dummy_frame;
    mod.key = dummy_key;
    mod.pointer = dummy_pointer;
    mod.text = NULL;
    mod.close_request = NULL;

    rc = zcsr_window_game_run(&mod, 1, 2, argv);
    assert(rc == 0);
    assert(init_count == 1);
    assert(shutdown_count == 1);
    assert(frame_count >= 150 && frame_count <= 151);
    assert(key_count == 1);
    assert(pointer_down == 1);
    assert(pointer_up == 1);
    assert(seen_surface != NULL);

    printf("runtime_shell_autoquit: PASS init=%d frames=%d key=%d pointer=%d/%d\n",
           init_count, frame_count, key_count, pointer_down, pointer_up);
    return 0;
}
