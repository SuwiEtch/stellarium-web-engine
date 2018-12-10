/* Stellarium Web Engine - Copyright (c) 2018 - Noctua Software Ltd
 *
 * This program is licensed under the terms of the GNU AGPL v3, or
 * alternatively under a commercial licence.
 *
 * The terms of the AGPL v3 license can be found in the main directory of this
 * repository.
 */

#include "swe.h"

#if DEFINED(SWE_GUI)

#include <stdarg.h>

static void menu_main(void *user);
static void time_widget(void *user);
static void info_widget(obj_t *obj);

static int gui_init_(obj_t *obj, json_value *args);
static void gui_del(obj_t *obj);
static int gui_render(const obj_t *obj, const painter_t *painter);

typedef struct gui {
    obj_t       obj;
    bool        visible;
    bool        initialized;
} gui_t;
static gui_t *g_gui;

static obj_klass_t gui_klass = {
    .id = "gui",
    .size = sizeof(gui_t),
    .flags = OBJ_MODULE,
    .init = gui_init_,
    .del = gui_del,
    .render = gui_render,
    .render_order = 200,
    .create_order = -1,     // Create before anything else.
};
OBJ_REGISTER(gui_klass)

static int gui_init_(obj_t *obj, json_value *args)
{
    g_gui = (void*)obj;
    return 0;
}

static void gui_del(obj_t *obj)
{
    gui_release();
}

void gui_text(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char *txt;
    vasprintf(&txt, fmt, args);
    gui_text_unformatted(txt);
    free(txt);
    va_end(args);
}

bool gui_item(const gui_item_t *item)
{
    const attribute_t *attr = NULL;
    bool ret, b;
    double f;
    char buffer[256];

    if (item->attr && !item->small) {
        assert(item->obj);
        attr = obj_get_attr_(item->obj, item->attr);
        if (attr->type[0] == 'b') { // Boolean attribute
            obj_get_attr(item->obj, item->attr, "b", &b);
            ret = gui_toggle(item->label, &b);
            if (ret) obj_set_attr(item->obj, item->attr, "b", b);
            return ret;
        }
        if (attr->type[0] == 'f') { // Double attribute
            obj_get_attr(item->obj, item->attr, "f", &f);
            ret = gui_double(item->label, &f, item->default_value);
            if (ret) obj_set_attr(item->obj, item->attr, "f", f);
            return ret;
        }
        return false;
    }
    if (item->attr && item->small) {
        attr = obj_get_attr_(item->obj, item->attr);
        obj_get_attr(item->obj, item->attr, "s", buffer);
        gui_label(attr->name, buffer);
    }

    // A link item.
    if (item->menu) {
        ret = gui_link(item->label, item->sub_label);
        // if (ret) gui_push_menu(item->menu, item->user ?: (void*)item);
        return ret;
    }
    // An int item.
    if (item->value.d) {
        return gui_int(item->label, item->value.d);
    }

    return false;
}

static void search_widget(void)
{
    static char buf[128];
    const char *suggestions[9] = {};
    obj_t *obj;
    char can[128];
    int i = 0;
    const char *cat, *canv, *value;
    uint64_t oid;
    if (strlen(buf) >= 3) {
        identifiers_make_canonical(buf, can, sizeof(can));
        IDENTIFIERS_ITER(0, NULL, &oid, NULL, &cat, NULL, &canv, &value,
                         NULL, NULL) {
            if (oid_is_catalog(oid, "CITY")) continue;
            if (!strstr(canv, can)) continue;
            suggestions[i++] = value;
            if (i >= ARRAY_SIZE(suggestions) - 1) break;
        }
    }
    if (gui_input("Search", buf, 128, suggestions)) {
        obj = obj_get(NULL, buf, 0);
        if (obj) {
            memset(buf, 0, sizeof(buf));
            obj_update(obj, core->observer, 0);
            obj_set_attr(&core->obj, "selection", "p", obj);
            obj_set_attr(&core->obj, "lock", "p", obj);
        }
    }
}

static void city_widget(void)
{
    static char buf[128];
    char can[128];
    int i = 0;
    const char *value, *canv;
    const char *suggestions[9] = {};
    uint64_t oid;
    obj_t *city;

    if (strlen(buf) >= 3) {
        identifiers_make_canonical(buf, can, sizeof(can));
        IDENTIFIERS_ITER(0, "NAME", &oid, NULL, NULL, NULL, &canv, &value,
                         NULL, NULL) {
            if (!oid_is_catalog(oid, "CITY")) continue;
            if (!strstr(canv, can)) continue;
            suggestions[i++] = value;
            if (i >= ARRAY_SIZE(suggestions) - 1) break;
        }
    }

    if (gui_input("City", buf, 128, suggestions)) {
        IDENTIFIERS_ITER(0, "NAME", &oid, NULL, NULL, NULL, NULL, &value,
                         NULL, NULL) {
            if (oid_is_catalog(oid, "CITY") && str_equ(value, buf)) {
                city = obj_get_by_oid(NULL, oid, 0);
                obj_set_attr(&core->observer->obj, "city", "p", city);
                break;
            }
        }
    }
}

static void on_progressbar(void *user, const char *id, const char *label,
                           int v, int total)
{
    gui_text("%s %d/%d", label, v, total);
}

static void menu_main(void *user)
{
    int i;
    double f;
    obj_t *module;
    double pos[2] = {0, 0};
    double size[2] = {300, 0};

    // XXX: replace all by modules gui hook method.
    const char *modules[][3] = {
        {"core.atmosphere", "visible", "Atmosphere"},
        {"core.landscapes", "visible", "Landscape"},
        {"core.milkyway", "visible", "Milkyway"},
        {"core.constellations.lines", "visible", "Cst Lines"},
        {"core.constellations.images", "visible", "Cst Art"},
        {"core.constellations.bounds", "visible", "Cst Bounds"},
        {"core.constellations", "show_all", "Cst show all"},
        {"core.dsos", "visible", "DSO"},
        {"core.dss",  "visible", "DSS"},
    };

    gui_panel_begin("main", pos, size);

    static char tab[128] = "Selection";
    gui_tabs(tab);

    if (gui_tab("Selection")) {
        if (core->selection) info_widget(core->selection);
        else gui_text("No object selected");
        gui_tab_end();
    }

    if (gui_tab("Observer")) {
        city_widget();
        obj_get_attr(&core->obj, "fov", "f", &f);
        f *= DR2D;
        if (gui_double("FOV", &f, NAN)) {
            f = clamp(f, 0.1, 360);
            f *= DD2R;
            obj_set_attr(&core->obj, "fov", "f", f);
        }
        gui_tab_end();
    }

    if (gui_tab("telescope")) {
        gui_double("s linear", &core->star_linear_scale, NAN);
        gui_double("s relative", &core->star_relative_scale, NAN);

        gui_text("Telescope:");
        gui_text("diameter: %.0fmm", core->telescope.diameter);
        gui_text("f-ratio: %.1f",
                core->telescope.focal_e / core->telescope.diameter);
        gui_toggle("auto", &core->telescope_auto);
        gui_tab_end();
    }

    if (gui_tab("View")) {
        for (i = 0; i < ARRAY_SIZE(modules); i++) {
            gui_item(&(gui_item_t){
                    .label = modules[i][2],
                    .obj = obj_get(NULL, modules[i][0], 0),
                    .attr = modules[i][1]});
        }
        gui_item(&(gui_item_t){
                .label = "Hints max mag",
                .obj = obj_get(NULL, "core.hints", 0),
                .attr = "max_mag",
                .default_value = 7.0,
                });
        gui_item(&(gui_item_t){
                .label = "Refraction",
                .obj = obj_get(NULL, "core.observer", 0),
                .attr = "refraction",
                });
        gui_tab_end();
    }

    if (gui_tab("Time")) {
        time_widget(user);
        gui_tab_end();
    }
    if (DEBUG && gui_tab("Debug")) {
        extern bool debug_stars_show_all;
        gui_toggle("Stars show all", &debug_stars_show_all);
        gui_text("Progress:");
        progressbar_list(NULL, on_progressbar);
        gui_tab_end();
    }

    DL_FOREACH(core->obj.children, module) {
        if (module->klass->gui) module->klass->gui(module, 0);
    }
    gui_tabs_end();
    search_widget();

    gui_panel_end();
}

static void time_widget(void *user)
{
    int iy, im, id, ihmsf[4], new_ihmsf[4], utc_offset;
    int r = 0;
    double djm0, utc, new_utc;
    utc_offset = core->utc_offset / 60.;
    utc = core->observer->utc + utc_offset / 24.;
    eraD2dtf("UTC", 0, DJM0, utc, &iy, &im, &id, ihmsf);
    if (gui_date(&utc))
        obj_set_attr(&core->observer->obj, "utc", "f", utc - utc_offset / 24.);
    memcpy(new_ihmsf, ihmsf, sizeof(ihmsf));

    gui_separator();
    if (gui_int("Hour", &new_ihmsf[0])) r = 1;
    if (gui_int("Minute", &new_ihmsf[1])) r = 1;
    if (gui_int("Second", &new_ihmsf[2])) r = 1;
    if (r) {
        r = eraDtf2d("UTC", iy, im, id,
                     new_ihmsf[0], new_ihmsf[1], new_ihmsf[2], &djm0,
                     &new_utc);
        if (r == 0) {
            obj_set_attr(&core->observer->obj, "utc", "f",
                         djm0 - DJM0 + new_utc - utc_offset / 24.);
        } else {
            // We cannot convert to MJD.  This can happen if for example we set
            // the minutes to 60.  In that case we use the delta to the
            // previous value.
            utc += (new_ihmsf[0] - ihmsf[0]) / 24. +
                   (new_ihmsf[1] - ihmsf[1]) / (60. * 24.) +
                   (new_ihmsf[2] - ihmsf[2]) / (60. * 60. * 24.);
            obj_set_attr(&core->observer->obj, "utc", "f",
                     utc - utc_offset / 24.);
        }
    }
    gui_separator();
    if (gui_item(&(gui_item_t){.label = "UTC offset",
                               .value.d = &utc_offset})) {
        obj_set_attr(&core->obj, "utcoffset", "d", utc_offset * 60);
    }
    if (gui_button("Set to now", -1)) {
        obj_set_attr(&core->observer->obj, "utc", "f",
                                unix_to_mjd(sys_get_unix_time()));
    }

}

static void info_widget(obj_t *obj)
{
    const char *value, *cat;
    char buf[256], buf1[64], buf2[64];
    double icrs[4], cirs[4], observed[4];
    double v, ra, dec, az, alt;

    if (!obj) return;
    obj_update(obj, core->observer, 0);
    obj_get_attr(obj, "name", "s", buf);
    gui_text_unformatted(buf);

    gui_label("ID", obj->id); // XXX: to remove.
    IDENTIFIERS_ITER(obj->oid, NULL, NULL, NULL, &cat, &value,
                     NULL, NULL, NULL, NULL)
        gui_label(cat, value);

    obj_get_attr(obj, "radec", "v4", icrs);
    convert_framev4(NULL, FRAME_ICRF, FRAME_CIRS, icrs, cirs);
    convert_framev4(NULL, FRAME_ICRF, FRAME_OBSERVED, icrs, observed);
    eraC2s(cirs, &ra, &dec);
    ra = eraAnp(ra);
    dec = eraAnpm(dec);
    eraC2s(observed, &az, &alt);

    if (obj_get_attr(obj, "type", "s", buf1) == 0)
        gui_label("TYPE", type_to_str(buf1));
    if (obj_get_attr(obj, "vmag", "f", &v) == 0) {
        sprintf(buf, "%f", v);
        gui_label("VMAG", buf);
    }
    if (obj_get_attr(obj, "distance", "s", buf) == 0)
        gui_label("DIST", buf);

    sprintf(buf, "%s/%s", format_hangle(buf1, ra), format_dangle(buf2, dec));
    gui_label("RA/DE", buf);
    sprintf(buf, "%s/%s", format_dangle(buf1, az), format_dangle(buf2, alt));
    gui_label("AZ/AL", buf);

    find_constellation_at(obj->pvo[0], buf);
    gui_label("CST", buf);

    if (obj_has_attr(obj, "phase")) {
        obj_get_attr(obj, "phase", "f", &v);
        if (!isnan(v)) {
            sprintf(buf, "%.0f%%", v * 100);
            gui_label("PHASE", buf);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void gui_render_menu(void)
{
    menu_main(NULL);
}

static int gui_render(const obj_t *obj, const painter_t *painter)
{
    gui_t *gui = (gui_t*)obj;
#ifdef __EMSCRIPTEN__
    return 0;
#endif

    if (!gui->initialized) {
        gui_init(gui);
        gui->initialized = true;
    }
    paint_flush(painter);
    gui_render_prepare();
    double pos[2] = {0, 0};
    double size[2] = {300, 0};
    double shift = 0;
    double utc;
    char buf[256];

    if (gui->visible) {
        shift = gui_panel_begin("menu", pos, size);
        menu_main(NULL);
        gui_panel_end();
    }

    vec2_set(pos, shift, 0);
    vec2_set(size, -1, 48);
    gui_panel_begin("location", pos, size);
    if (gui_button("-", 0)) {
        gui->visible = !gui->visible;
    }
    gui_same_line();
    gui_text("%.1f°/%.1f°", core->observer->phi * DR2D,
                            core->observer->elong * DR2D);
    gui_same_line();
    obj_get_attr(&core->observer->obj, "utc", "f", &utc);
    gui_text("%s", format_time(buf, utc, core->utc_offset / 60. / 24., NULL));
    gui_same_line();
    gui_text("FOV: %.1f°", core->fov * DR2D);
    gui_same_line();
    gui_text("FPS: %.0f", core->prof.fps);
    gui_same_line();
    gui_text("lwa: %f cd/m2", core->lwa);
    gui_same_line();
    gui_text("cst: %s", core->observer->pointer.cst);
    gui_panel_end();

    gui_render_finish();
    return 0;
}

#endif // SWE_GUI
