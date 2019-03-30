#include "sx/allocator.h"
#include "sx/math.h"
#include "sx/string.h"
#include "sx/timer.h"

#include "rizz/app.h"
#include "rizz/asset.h"
#include "rizz/camera.h"
#include "rizz/core.h"
#include "rizz/entry.h"
#include "rizz/graphics.h"
#include "rizz/plugin.h"
#include "rizz/vfs.h"

#include "imgui/imgui-extra.h"
#include "imgui/imgui.h"

RIZZ_STATE static rizz_api_core* the_core;
RIZZ_STATE static rizz_api_gfx*  the_gfx;
RIZZ_STATE static rizz_api_app*  the_app;
RIZZ_STATE static rizz_api_imgui*           the_imgui;
RIZZ_STATE static rizz_api_asset*           the_asset;
RIZZ_STATE static rizz_api_imgui_extra*     the_imguix;
RIZZ_STATE static rizz_api_camera*          the_camera;
RIZZ_STATE static rizz_api_vfs*             the_vfs;

RIZZ_STATE rizz_gfx_stage g_stage;

static bool init() {
    // register main graphics stage.
    // at least one stage should be registered if you want to draw anything
    g_stage = the_gfx->stage_register("main", (rizz_gfx_stage){ .id = 0 });
    sx_assert(g_stage.id);
    return true;
}

static void shutdown() {}

static void update(float dt) {}

static void render() {
    sg_pass_action pass_action = { .colors[0] = { SG_ACTION_CLEAR, { 0.25f, 0.5f, 0.75f, 1.0f } },
                                   .depth = { SG_ACTION_CLEAR, 1.0f } };

    the_gfx->staged.begin(g_stage);
    the_gfx->staged.begin_default_pass(&pass_action, the_app->width(), the_app->height());
    the_gfx->staged.end_pass();
    the_gfx->staged.end();

    // Use imgui UI
    the_imgui->SetNextWindowContentSize(sx_vec2f(100.0f, 50.0f));
    if (the_imgui->Begin("Hello", NULL, 0)) {
        the_imgui->LabelText("Fps", "%.3f", the_core->fps());
    }
    the_imgui->End();
}

rizz_plugin_decl_main(hello, plugin, e) {
    switch (e) {
    case RIZZ_PLUGIN_EVENT_STEP:
        update((float)sx_tm_sec(the_core->delta_tick()));
        render();
        break;

    case RIZZ_PLUGIN_EVENT_INIT:
        // runs only once for application. Retreive needed APIs
        the_core = (rizz_api_core*)plugin->api->get_api(RIZZ_API_CORE, 0);
        the_gfx = (rizz_api_gfx*)plugin->api->get_api(RIZZ_API_GFX, 0);
        the_app = (rizz_api_app*)plugin->api->get_api(RIZZ_API_APP, 0);
        the_imgui = (rizz_api_imgui*)plugin->api->get_api(RIZZ_API_IMGUI, 0);

        init();
        break;

    case RIZZ_PLUGIN_EVENT_LOAD:
        break;

    case RIZZ_PLUGIN_EVENT_UNLOAD:
        break;

    case RIZZ_PLUGIN_EVENT_SHUTDOWN:
        shutdown();
        break;
    }

    return 0;
}

rizz_plugin_decl_event_handler(hello, e) {
    switch (e->type) {
    case RIZZ_APP_EVENTTYPE_SUSPENDED:
        break;
    case RIZZ_APP_EVENTTYPE_RESTORED:
        break;
    case RIZZ_APP_EVENTTYPE_MOUSE_DOWN:
        break;
    case RIZZ_APP_EVENTTYPE_MOUSE_UP:
        break;
    case RIZZ_APP_EVENTTYPE_MOUSE_MOVE:
        break;
    default:
        break;
    }
}

rizz_plugin_implement_info(hello, 100, "Hello", 0);

rizz_game_decl_config(conf) {
    conf->app_name = "Hello";
    conf->app_title = "Hello World";
    conf->window_width = 800;
    conf->window_height = 600;
    conf->core_flags |= RIZZ_CORE_FLAG_VERBOSE;
    conf->multisample_count = 4;
    conf->swap_interval = 2;
}
