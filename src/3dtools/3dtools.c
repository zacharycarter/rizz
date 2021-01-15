#include "rizz/3dtools.h"
#include "rizz/rizz.h"

#include "sx/allocator.h"
#include "sx/hash.h"
#include "sx/handle.h"
#include "sx/array.h"
#include "sx/string.h"

#include "3dtools-internal.h"

RIZZ_STATE static rizz_api_plugin* the_plugin;
RIZZ_STATE static rizz_api_asset* the_asset;

#define HASH_SEED 0x14f2d8b8

typedef struct material__data 
{
    rizz_material_data d;
    uint32_t refcount;
} material__data;

typedef struct rizz_material_lib 
{
    const sx_alloc* alloc;
    sx_handle_pool* ids;          // binds to `datas`
    material__data* datas;        // sx_array
    sx_hashtbl* hashes;           // data hash -> index to handles (sx_handle_at)
} rizz_material_lib;

typedef struct shader_permut 
{
    uint32_t bitset;
    rizz_asset aid;
    const void* user;
} shader_permut;

typedef struct shader_data 
{
    uint32_t stage;
    shader_permut* permuts;
} shader_data;

typedef struct rizz_shader_lib 
{
    const sx_alloc* alloc;
    shader_data* shaders;       // sx_array
} rizz_shader_lib;

////////////////////////////////////////////////////////////////////////////////////////////////////
// material
rizz_material material__add(rizz_material_lib* lib, const rizz_material_data* mtldata)
{
    sx_assert(lib);
    sx_assert(mtldata);

    uint32_t hash = sx_hash_xxh32(mtldata, sizeof(*mtldata), HASH_SEED);
    int hash_index = sx_hashtbl_find(lib->hashes, hash);
    if (hash_index != -1) {
        rizz_material mtl = (rizz_material){ sx_hashtbl_get(lib->hashes, hash_index) };
        material__data* mtl_data = &lib->datas[sx_handle_index(mtl.id)];
        ++mtl_data->refcount;
        return mtl;
    }

    sx_handle_t handle = sx_handle_new_and_grow(lib->ids, lib->alloc);
    int index = sx_handle_index(handle);
    material__data m = (material__data){ .d = *mtldata, .refcount = 1 };
    if (index < sx_array_count(lib->datas)) {
        lib->datas[index] = m;
    } else {
        sx_array_push(lib->alloc, lib->datas, m);
    }

    sx_hashtbl_add_and_grow(lib->hashes, hash, handle, lib->alloc);

    return (rizz_material){ handle };
}

void material__remove(rizz_material_lib* lib, rizz_material mtl)
{
    sx_assert(lib);
    sx_assert_rel(sx_handle_valid(lib->ids, mtl.id));

    material__data* mtldata = &lib->datas[sx_handle_index(mtl.id)];
    if (--mtldata->refcount == 0) {
        uint32_t hash = sx_hash_xxh32(mtldata, sizeof(*mtldata), HASH_SEED);

        sx_handle_del(lib->ids, mtl.id);
        sx_hashtbl_remove_if_found(lib->hashes, hash);
    }
}

rizz_material_lib* material__create_lib(const sx_alloc* alloc, int init_capacity)
{
    sx_assert(alloc);
    sx_assert(init_capacity > 0);

    rizz_material_lib* lib = (rizz_material_lib*)sx_calloc(alloc, sizeof(rizz_material_lib));
    if (!lib) {
        sx_memory_fail();
        return NULL;
    }

    lib->alloc = alloc;
    lib->ids = sx_handle_create_pool(alloc, init_capacity);
    sx_array_reserve(alloc, lib->datas, init_capacity);
    lib->hashes = sx_hashtbl_create(alloc, init_capacity);

    return lib;
}

void material__destroy_lib(rizz_material_lib* lib)
{
    if (lib) {
        sx_assert(lib->alloc);

        sx_hashtbl_destroy(lib->hashes, lib->alloc);
        sx_array_free(lib->alloc, lib->datas);
        sx_handle_destroy_pool(lib->ids, lib->alloc);
        sx_free(lib->alloc, lib);
    }
}

const rizz_material_data* material_get_data(const rizz_material_lib* lib, rizz_material mtl)
{
    sx_assert_always(sx_handle_valid(lib->ids, mtl.id));

    return &lib->datas[sx_handle_index(mtl.id)].d;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// shader
static bool shader_load(rizz_shader_lib* lib, const char* sgs_filepath, const sx_alloc* alloc, 
                        uint32_t stage, uint32_t permutations, const void* user)
{
    sx_assert(lib);
    sx_assert(sgs_filepath);

    // search in shaders for the stage
    int shader_idx = -1;
    for (int i = 0, c = sx_array_count(lib->shaders); i < c; i++) {
        if (lib->shaders[i].stage == stage) {
            shader_idx = i;
            break;
        }
    }

    if (shader_idx == -1) {
        shader_idx = sx_array_count(lib->shaders);
        sx_array_push(lib->alloc, lib->shaders, (shader_data) {0});
    }

    sx_assert(shader_idx != -1);
    shader_data* shader = &lib->shaders[shader_idx];
    shader->stage = stage;
    
    // make sure no duplicate permutations exist
    for (int i = 0, c = sx_array_count(shader->permuts); i < c; i++) {
        if ((shader->permuts[i].bitset & permutations) == permutations) {
            sx_assertf(0, "Permutations for shader '%s' (0x%x) already exists." 
                          "double check it, or call 'add' from smaller bitsets");
            return false;
        }
    }
    
    // load shader file
    rizz_asset shader_id = the_asset->load("shader", sgs_filepath, NULL, 0, alloc ? alloc : NULL, 0);
    if (shader_id.id == 0) {
        sx_assert_alwaysf(0, "could not load shader: %s", sgs_filepath);
        return false;
    }

    shader_permut permut = (shader_permut){ 
        .bitset = permutations, 
        .aid = shader_id,
        .user = user
    };
    sx_array_push(lib->alloc, shader->permuts, permut);

    return true;
}

static rizz_asset shader_get(const rizz_shader_lib* lib, uint32_t stage, uint32_t permutations, const void** puser)
{
    sx_assert(lib);
    for (int i = 0, c = sx_array_count(lib->shaders); i < c; i++) {
        shader_data* shader = &lib->shaders[i];
        if (shader->stage == stage) {
            for (int k = 0, kc = sx_array_count(shader->permuts); k < kc; k++) {
                if ((shader->permuts[k].bitset & permutations) == permutations) {
                    if (puser) {
                        *puser = shader->permuts[k].user;
                    }
                    return shader->permuts[k].aid;
                }
            }
        }
    }

    sx_assert_alwaysf(0, "shader (stage: 0x%x, permutations: 0x%x) does not exist", stage, permutations);
    return (rizz_asset) {0};
}

static rizz_shader_lib* shader_create_lib(const sx_alloc* alloc)
{
    sx_assert(alloc);

    rizz_shader_lib* lib = (rizz_shader_lib*)sx_calloc(alloc, sizeof(rizz_shader_lib));
    if (!lib) {
        sx_memory_fail();
        return NULL;
    }

    lib->alloc = alloc;
    return lib;
}

static void shader_destroy_lib(rizz_shader_lib* lib)
{
    if (lib) {
        sx_assert(lib->alloc);
        for (int i = 0, c = sx_array_count(lib->shaders); i < c; i++) {
            shader_data* shader = &lib->shaders[i];
            for (int k = 0, kc = sx_array_count(shader->permuts); k < kc; k++) {
                the_asset->unload(shader->permuts[k].aid);
            }
            sx_array_free(lib->alloc, lib->shaders[i].permuts);
        }
        sx_array_free(lib->alloc, lib->shaders);
        sx_free(lib->alloc, lib);
    }
}


static rizz_api_3d the__3d = { 
    .debug = {
        .set_draw_api = debug3d__set_draw_api,
        .draw_box = debug3d__draw_box,
        .draw_boxes = debug3d__draw_boxes,
        .draw_sphere = debug3d__draw_sphere,
        .draw_spheres = debug3d__draw_spheres,
        .draw_cone = debug3d__draw_cone,
        .draw_cones = debug3d__draw_cones,
        .generate_box_geometry = debug3d__generate_box_geometry,
        .generate_sphere_geometry = debug3d__generate_sphere_geometry,
        .generate_cone_geometry = debug3d__generate_cone_geometry,
        .free_geometry = debug3d__free_geometry,
        .draw_aabb = debug3d__draw_aabb,
        .draw_aabbs = debug3d__draw_aabbs,
        .draw_path = debug3d__draw_path,
        .draw_line = debug3d__draw_line,
        .draw_lines = debug3d__draw_lines,
        .grid_xyplane = debug3d__grid_xyplane,
        .grid_xzplane = debug3d__grid_xzplane,
        .grid_xyplane_cam = debug3d__grid_xyplane_cam,
        .set_max_instances = debug3d__set_max_instances,
        .set_max_vertices = debug3d__set_max_vertices,
        .set_max_indices = debug3d__set_max_indices 
    },
    .model = {
        .get = model__get,
        .set_material_lib = model__set_material_lib
    },
    .material = {
        .create_lib = material__create_lib,
        .destroy_lib = material__destroy_lib,
        .add = material__add,
        .get_data = material_get_data
    },
    .shader = {
        .create_lib = shader_create_lib,
        .destroy_lib = shader_destroy_lib,
        .get = shader_get,
        .load = shader_load
    }
};

rizz_plugin_decl_main(3dtools, plugin, e)
{
    switch (e) {
    case RIZZ_PLUGIN_EVENT_STEP:
        break;

    case RIZZ_PLUGIN_EVENT_INIT: {
        the_plugin = plugin->api;
        rizz_api_core* core = the_plugin->get_api(RIZZ_API_CORE, 0);
        rizz_api_gfx* gfx = the_plugin->get_api(RIZZ_API_GFX, 0);
        rizz_api_camera* cam = the_plugin->get_api(RIZZ_API_CAMERA, 0);
        rizz_api_asset* asset = the_plugin->get_api(RIZZ_API_ASSET, 0);
        rizz_api_imgui* imgui = the_plugin->get_api_byname("imgui", 0);
        the_asset = asset;

        if (!debug3d__init(core, gfx, cam)) {
            return -1;
        }

        if (!model__init(core, asset, gfx, imgui)) {
            return -1;
        }

        the_plugin->inject_api("3dtools", 0, &the__3d);
    } break;

    case RIZZ_PLUGIN_EVENT_LOAD:
        the_plugin->inject_api("3dtools", 0, &the__3d);
        break;

    case RIZZ_PLUGIN_EVENT_UNLOAD:
        break;

    case RIZZ_PLUGIN_EVENT_SHUTDOWN:
        the_plugin->remove_api("3dtools", 0);
        debug3d__release();
        model__release();
        break;
    }

    return 0;
}

rizz_plugin_decl_event_handler(3dtools, e) 
{
    if (e->type == RIZZ_APP_EVENTTYPE_UPDATE_APIS) {
        rizz_api_imgui* imgui = the_plugin->get_api_byname("imgui", 0);
        model__set_imgui(imgui);
    }
}

static const char* tools3d__deps[] = { "imgui" };
rizz_plugin_implement_info(3dtools, 1000, "3dtools plugin", tools3d__deps, 1);