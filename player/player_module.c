#include "uim_project.h"
#include "uim_component_registry.h"
#include "uim_page_render.h"
#include "uim_component_runtime.h"
#include "uim_page_view.h"
#include "uim_zcsr_runtime_bridge.h"
#include "zcsr_runtime_shell.h"

#include "zcsr/clock.h"

#include "zcsr/gl_render.h"
#include "zcsr/image.h"
#include "zcsr/input_ext.h"
#include "zcsr/platform.h"
#include "zcsr/window_event.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
static void player_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#  include <unistd.h>
static void player_sleep_ms(int ms) { usleep((useconds_t)(ms * 1000)); }
#endif

#define PLAYER_WIN_W 1280
#define PLAYER_WIN_H 800
#define PLAYER_MAX_DRAW_NODES 100000

static unsigned char player_renderer_buf[8u * 1024u * 1024u];
static unsigned char player_font_buf[8u * 1024u * 1024u];
static unsigned char player_input_buf[8192];

#define PLAYER_TAB_Y 34.0f
#define PLAYER_TAB_H 22.0f
#define PLAYER_TAB_MIN_W 84.0f

typedef struct PlayerNode {
    uim_NodeId id;
    int32_t z_order;
} PlayerNode;

static int player_node_compare(const void *a, const void *b) {
    const PlayerNode *na = (const PlayerNode *)a;
    const PlayerNode *nb = (const PlayerNode *)b;
    if (na->z_order < nb->z_order) return -1;
    if (na->z_order > nb->z_order) return 1;
    if (na->id < nb->id) return -1;
    if (na->id > nb->id) return 1;
    return 0;
}

static void player_world_pos(const UIM_ComponentManager *manager, uim_NodeId id,
                             float *out_x, float *out_y) {
    float x = 0.0f;
    float y = 0.0f;
    int guard = 0;
    while (id != 0u && guard++ < 128) {
        uim_NodeInfo info;
        if (uim_node_get(manager, id, &info) != uim_OK) break;
        x += info.local_rect.x;
        y += info.local_rect.y;
        id = info.parent_id;
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static uim_NodeId player_page_root(const UIM_ComponentManager *manager, uim_PageId page) {
    size_t count = 0u;
    size_t i;
    if (uim_page_get_node_count(manager, page, &count) != uim_OK) return uim_NODE_INVALID;
    for (i = 0; i < count; ++i) {
        uim_NodeInfo info;
        if (uim_page_get_node_at(manager, page, i, &info) == uim_OK &&
            info.parent_id == uim_NODE_INVALID) return info.id;
    }
    return uim_NODE_INVALID;
}

static void player_layout(const UIM_ComponentManager *manager, uim_PageId page, uim_NodeId root,
                          int win_w, int win_h, float *out_scale, float *out_offset_x,
                          float *out_offset_y, float *out_base_w, float *out_base_h) {
    uim_NodeInfo root_info;
    float base_w = 1280.0f;
    float base_h = 800.0f;
    float scale;
    float ox = 0.0f;
    float oy = 0.0f;
    (void)page;
    if (uim_node_get(manager, root, &root_info) == uim_OK) {
        if (root_info.local_rect.width > 0.0f && root_info.local_rect.height > 0.0f) {
            base_w = root_info.local_rect.width;
            base_h = root_info.local_rect.height;
        }
        player_world_pos(manager, root, &ox, &oy);
        base_w = root_info.local_rect.width > 0.0f ? root_info.local_rect.width : base_w;
        base_h = root_info.local_rect.height > 0.0f ? root_info.local_rect.height : base_h;
    }
    scale = (float)win_w / base_w;
    if ((float)win_h / base_h < scale) scale = (float)win_h / base_h;
    if (scale <= 0.0f) scale = 1.0f;
    if (out_scale) *out_scale = scale;
    if (out_offset_x) *out_offset_x = ((float)win_w - base_w * scale) * 0.5f;
    if (out_offset_y) *out_offset_y = ((float)win_h - base_h * scale) * 0.5f;
    if (out_base_w) *out_base_w = base_w;
    if (out_base_h) *out_base_h = base_h;
}

static void player_draw_tabs(zcsr_gl_renderer *renderer, const UIM_ComponentManager *manager,
                             uim_PageId active, int win_w) {
    size_t count = 0u;
    size_t i;
    float x = 12.0f;
    if (uim_page_count(manager, &count) != uim_OK) return;
    for (i = 0; i < count; ++i) {
        uim_PageId page = uim_PAGE_INVALID;
        const char *key = NULL;
        float tab_w;
        if (uim_page_get_at(manager, i, &page) != uim_OK) continue;
        (void)uim_page_get_key(manager, page, &key);
        tab_w = PLAYER_TAB_MIN_W + (float)(key ? strlen(key) : 0u) * 6.0f;
        if (x + tab_w > (float)win_w - 8.0f) break;
        zcsr_gl_fill_rect(renderer, x, PLAYER_TAB_Y, tab_w, PLAYER_TAB_H,
                          page == active ? 0.20f : 0.12f,
                          page == active ? 0.30f : 0.16f,
                          page == active ? 0.45f : 0.22f, 1.0f);
        zcsr_gl_draw_rect(renderer, x, PLAYER_TAB_Y, tab_w, PLAYER_TAB_H, 1.0f,
                          0.55f, 0.62f, 0.72f, 0.7f);
        zcsr_gl_draw_text(renderer, key ? key : "page", x + 8.0f, PLAYER_TAB_Y + 4.0f,
                          0.62f, 0.92f, 0.96f, 1.0f, 1.0f);
        x += tab_w + 4.0f;
    }
}

static bool player_tab_click(const UIM_ComponentManager *manager, uim_PageId *page,
                             uim_NodeId *root, float mx, float my, int win_w) {
    size_t count = 0u;
    size_t i;
    float x = 12.0f;
    if (my < PLAYER_TAB_Y || my > PLAYER_TAB_Y + PLAYER_TAB_H) return false;
    if (uim_page_count(manager, &count) != uim_OK) return false;
    for (i = 0; i < count; ++i) {
        uim_PageId candidate = uim_PAGE_INVALID;
        const char *key = NULL;
        float tab_w;
        if (uim_page_get_at(manager, i, &candidate) != uim_OK) continue;
        (void)uim_page_get_key(manager, candidate, &key);
        tab_w = PLAYER_TAB_MIN_W + (float)(key ? strlen(key) : 0u) * 6.0f;
        if (x + tab_w > (float)win_w - 8.0f) break;
        if (mx >= x && mx <= x + tab_w) {
            uim_NodeId candidate_root = player_page_root(manager, candidate);
            if (candidate_root != uim_NODE_INVALID) {
                *page = candidate;
                *root = candidate_root;
                return true;
            }
        }
        x += tab_w + 4.0f;
    }
    return false;
}

static bool player_node_click(UIM_ComponentManager *manager, uim_PageId *page,
                              uim_NodeId *root, uim_PageId current_page, uim_NodeId current_root,
                              float mx, float my, int win_w, int win_h) {
    size_t count = 0u;
    size_t i;
    PlayerNode *nodes;
    float scale;
    float offset_x;
    float offset_y;
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    if (uim_page_get_node_count(manager, current_page, &count) != uim_OK || count == 0u ||
        count > PLAYER_MAX_DRAW_NODES) return false;
    player_layout(manager, current_page, current_root, win_w, win_h, &scale, &offset_x,
                  &offset_y, NULL, NULL);
    player_world_pos(manager, current_root, &origin_x, &origin_y);
    /* 3D billboards live in page/world space, not local child space. Check them
     * first with the shared bridge projector; 2D keeps its manual world walk. */
    {
        float page_x = (mx - offset_x) / (scale > 0.0f ? scale : 1.0f) + origin_x;
        float page_y = (my - offset_y) / (scale > 0.0f ? scale : 1.0f) + origin_y;
        uim_NodeId hit_3d = uim_NODE_INVALID;
        int32_t hit_z = 0;
        size_t n;
        for (n = 0; n < count; ++n) {
            uim_NodeInfo info;
            uim_Rect projected;
            if (uim_page_get_node_at(manager, current_page, n, &info) != uim_OK) continue;
            if (info.space != uim_SPACE_3D || !info.visible || !info.enabled) continue;
            if (!uim_zcsr_runtime_bridge_project_node_rect(manager, &info, &projected))
                continue;
            if (page_x < projected.x || page_y < projected.y ||
                page_x > projected.x + projected.width ||
                page_y > projected.y + projected.height) continue;
            if (hit_3d == uim_NODE_INVALID || info.z_order > hit_z ||
                (info.z_order == hit_z && info.id > hit_3d)) {
                hit_3d = info.id;
                hit_z = info.z_order;
            }
        }
        if (hit_3d != uim_NODE_INVALID) {
            uim_NodeInfo hit_info;
            (void)uim_runtime_fire_click(manager, current_page, hit_3d);
            if (uim_node_get(manager, hit_3d, &hit_info) == uim_OK &&
                hit_info.action_id > 0u) {
                size_t page_count = 0u;
                if (uim_page_count(manager, &page_count) == uim_OK &&
                    (size_t)hit_info.action_id <= page_count) {
                    uim_PageId next = uim_PAGE_INVALID;
                    uim_NodeId next_root;
                    if (uim_page_get_at(manager, (size_t)hit_info.action_id - 1u,
                                        &next) == uim_OK) {
                        next_root = player_page_root(manager, next);
                        if (next_root != uim_NODE_INVALID) {
                            *page = next;
                            *root = next_root;
                            return true;
                        }
                    }
                }
            }
            return true;
        }
    }
    nodes = (PlayerNode *)malloc(count * sizeof(*nodes));
    if (!nodes) return false;
    for (i = 0; i < count; ++i) {
        uim_NodeInfo info;
        nodes[i].id = 0u;
        nodes[i].z_order = 0;
        if (uim_page_get_node_at(manager, current_page, i, &info) == uim_OK) {
            nodes[i].id = info.id;
            nodes[i].z_order = info.z_order;
        }
    }
    qsort(nodes, count, sizeof(*nodes), player_node_compare);
    for (i = count; i > 0; --i) {
        uim_NodeInfo info;
        float wx = 0.0f;
        float wy = 0.0f;
        float sx;
        float sy;
        float sw;
        float sh;
        if (nodes[i - 1u].id == 0u ||
            uim_node_get(manager, nodes[i - 1u].id, &info) != uim_OK || !info.visible)
            continue;
        player_world_pos(manager, info.id, &wx, &wy);
        sx = offset_x + (wx - origin_x) * scale;
        sy = offset_y + (wy - origin_y) * scale;
        sw = info.local_rect.width * scale;
        sh = info.local_rect.height * scale;
        if (mx < sx || my < sy || mx > sx + sw || my > sy + sh) continue;
        /* Rule：点中带 click 触发器的节点就地结算（伤害/移动/销毁/切页请求）。 */
        (void)uim_runtime_fire_click(manager, current_page, info.id);
        if (info.action_id > 0u) {
            size_t page_count = 0u;
            if (uim_page_count(manager, &page_count) == uim_OK &&
                (size_t)info.action_id <= page_count) {
                uim_PageId next = uim_PAGE_INVALID;
                uim_NodeId next_root;
                if (uim_page_get_at(manager, (size_t)info.action_id - 1u, &next) == uim_OK) {
                    next_root = player_page_root(manager, next);
                    if (next_root != uim_NODE_INVALID) {
                        *page = next;
                        *root = next_root;
                        free(nodes);
                        return true;
                    }
                }
            }
        }
        break;
    }
    free(nodes);
    return false;
}

typedef struct PlayerModule {
    UIM_ComponentManager *manager;
    uim_PageId page;
    uim_NodeId root;
    char project_path[UIR_PROJECT_PATH_CAP];
    char project_root[UIR_PROJECT_PATH_CAP];
    char project_name[UIR_PROJECT_NAME_CAP];
    bool prev_down;
    uint64_t last_ns;
    int cw;
    int ch;
} PlayerModule;

static bool player_module_init(zcsr_gl_renderer *r, int cw, int ch, void *user) {
    PlayerModule *m = (PlayerModule *)user;
    uim_Result rc;
    if (!m || !m->project_path[0]) return false;
    rc = uir_project_load(m->project_path, &m->manager, &m->page, &m->root,
                          m->project_root, sizeof(m->project_root),
                          m->project_name, sizeof(m->project_name));
    if (rc != uim_OK || !m->manager) {
        fprintf(stderr, "player: project load failed rc=%d path=%s\n",
                (int)rc, m->project_path);
        return false;
    }
    /* Product layout pass: resolve authored anchors against the root viewport
     * before play snapshot. This mutates only the in-memory project copy; the
     * player never saves it. Editor preview stays authoring-space. */
    {
        uim_NodeInfo root_info;
        if (uim_node_get(m->manager, m->root, &root_info) == uim_OK &&
            root_info.local_rect.width > 0.0f && root_info.local_rect.height > 0.0f) {
            (void)uim_page_resolve_layout(m->manager, m->page,
                                          root_info.local_rect.width,
                                          root_info.local_rect.height);
        }
    }
    if (!zcsr_gl_font_default(r, 16, player_font_buf, sizeof(player_font_buf))) {
        /* Keep built-in bitmap font. */
    }
    (void)uim_runtime_begin(m->manager, m->page);
    m->last_ns = zcsr_clock_now_ns();
    m->cw = cw;
    m->ch = ch;
    return true;
}

static void player_module_frame(zcsr_gl_renderer *r, int cw, int ch, void *user) {
    PlayerModule *m = (PlayerModule *)user;
    uint64_t now;
    double dt;
    if (!m || !m->manager || m->page == uim_PAGE_INVALID) return;
    m->cw = cw;
    m->ch = ch;
    now = zcsr_clock_now_ns();
    dt = (double)(now - m->last_ns) / 1000000000.0;
    m->last_ns = now;
    uim_runtime_tick(m->manager, m->page, dt);
    (void)zcsr_page_render(r, m->manager, m->page, 0, 0, cw, ch, m->project_root);
    player_draw_tabs(r, m->manager, m->page, cw);
    {
        char title[256];
        snprintf(title, sizeof(title), "%s  |  %s  |  close window to exit",
                 m->project_name[0] ? m->project_name : "project", m->project_path);
        zcsr_gl_draw_text(r, title, 12.0f, 10.0f, 0.8f, 0.85f, 0.9f, 0.95f, 1.0f);
    }
}

static void player_module_pointer(int x, int y, uint32_t buttons, uint32_t pressed,
                                  uint32_t released, void *user) {
    PlayerModule *m = (PlayerModule *)user;
    bool down = (buttons & ZCSR_MOUSE_LEFT) != 0u;
    (void)pressed;
    if (!m || !m->manager) return;
    if ((m->prev_down && !down) || released) {
        uim_PageId page_before = m->page;
        if (!player_tab_click(m->manager, &m->page, &m->root, (float)x, (float)y, m->cw))
            (void)player_node_click(m->manager, &m->page, &m->root, m->page, m->root,
                                    (float)x, (float)y, m->cw, m->ch);
        {
            int goto_page = uim_runtime_take_goto_request();
            if (goto_page > 0) {
                size_t page_count = 0u;
                if (uim_page_count(m->manager, &page_count) == uim_OK &&
                    (size_t)goto_page <= page_count) {
                    uim_PageId next = uim_PAGE_INVALID;
                    if (uim_page_get_at(m->manager, (size_t)goto_page - 1u, &next) == uim_OK) {
                        m->page = next;
                        m->root = player_page_root(m->manager, next);
                    }
                }
            }
        }
        if (m->page != page_before) (void)uim_runtime_begin(m->manager, m->page);
    }
    m->prev_down = down;
}

static void player_module_shutdown(zcsr_gl_renderer *r, void *user) {
    PlayerModule *m = (PlayerModule *)user;
    (void)r;
    if (!m) return;
    uim_runtime_end();
    if (m->manager) uim_component_manager_destroy(m->manager);
    m->manager = NULL;
}

int zcsr_player_main(int argc, char **argv) {
    static PlayerModule module;
    zcsr_runtime_shell_module mod;
    if (argc < 3 || strcmp(argv[1], "--play") != 0 || !argv[2] || !argv[2][0]) {
        fprintf(stderr, "usage: zcsr_player --play <project.uireproj>\n");
        return 1;
    }
    memset(&module, 0, sizeof(module));
    snprintf(module.project_path, sizeof(module.project_path), "%s", argv[2]);
    memset(&mod, 0, sizeof(mod));
    mod.title = "ZCSR Player";
    mod.user = &module;
    mod.init = player_module_init;
    mod.frame = player_module_frame;
    mod.pointer = player_module_pointer;
    mod.shutdown = player_module_shutdown;
    /* player 不再自建窗口、GL、输入泵和主循环：它是 runtime shell 的
     * 一个 module，窗口生命周期归 zcsr_runtime_shell。 */
    return zcsr_window_game_run(&mod, 1, argc, argv);
}
