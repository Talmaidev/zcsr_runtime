/* player_main.c — standalone package player entry.
 *
 * The player target deliberately does not compile editor_app.cpp, Dear ImGui,
 * EditorKernel, or the editor page manager. A package built by the editor
 * copies this executable to player.exe; with no arguments it opens the
 * project.uireproj beside it.
 */
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#endif

extern int zcsr_player_main(int argc, char **argv);

#if defined(_WIN32)
/* Hide only the console this process owns; inherited terminals stay visible. */
static void player_hide_own_console(void) {
    DWORD owners[8];
    DWORD count;
    HWND console;
    if (GetConsoleWindow() == NULL) return;
    count = GetConsoleProcessList(owners, (DWORD)(sizeof(owners) / sizeof(owners[0])));
    if (count != 1u) return;
    console = GetConsoleWindow();
    if (console != NULL) ShowWindow(console, SW_HIDE);
}
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    player_hide_own_console();
#endif
    if (argc > 1) return zcsr_player_main(argc, argv);
    const char *default_argv[3] = { argv[0], "--play", "project.uireproj" };
    return zcsr_player_main(3, (char **)default_argv);
}
