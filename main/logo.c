#include "logo.h"

#include <math.h>

// Geometry matches tools/gen_logo.py, which defines the mark on a 64x64 grid
// centered at (32, 32): a ring of center-line radius 24, two chevrons
// (21,25)-(32,17)-(43,25) and its mirror, and a horizontal seam on y = 32.
// Strokes thicken as the mark shrinks; each row is used from its size up.
typedef struct {
    int32_t min_diameter;
    float ring;       // stroke widths on the 64 grid
    float chevron;
    float seam;       // 0: no seam
    float seam_x0;    // seam from x0 to 64 - x0
    float wing_x0;    // chevron wing tips at x0 and 64 - x0
    float wing_dy;    // wing tips this far from the center line
    float apex_dy;    // apex this far from the center line
} logo_row_t;

static const logo_row_t ROWS[] = {
    { 64, 3, 5, 2.5f, 12, 21, 7, 15 },
    { 40, 4, 6, 3.5f, 13, 21, 7, 15 },
    { 24, 5, 7, 4.5f, 14, 21, 7, 15 },
    { 0, 7, 9, 0, 0, 20, 8, 16 },
};

#define GRID_CENTER 32.0f
#define RING_RADIUS 24.0f

static const logo_row_t *row_for(int32_t diameter)
{
    size_t i = 0;
    while (diameter < ROWS[i].min_diameter) {
        i++;
    }
    return &ROWS[i];
}

typedef struct {
    float x;
    float y;
} vec_t;

static void draw_segment(lv_layer_t *layer, lv_draw_line_dsc_t *dsc, vec_t a, vec_t b)
{
    dsc->p1.x = (int32_t)lroundf(a.x);
    dsc->p1.y = (int32_t)lroundf(a.y);
    dsc->p2.x = (int32_t)lroundf(b.x);
    dsc->p2.y = (int32_t)lroundf(b.y);
    lv_draw_line(layer, dsc);
}

// A chevron a-b-c with a miter join. LVGL draws butt-capped segments without
// joins, so each arm runs past the apex by exactly the miter length: the two
// overhangs then lie inside each other and their union is the mitered shape.
static void draw_chevron(lv_layer_t *layer, lv_draw_line_dsc_t *dsc, vec_t a, vec_t b, vec_t c)
{
    const float len1 = hypotf(b.x - a.x, b.y - a.y);
    const float len2 = hypotf(c.x - b.x, c.y - b.y);
    const vec_t u1 = { (b.x - a.x) / len1, (b.y - a.y) / len1 };
    const vec_t u2 = { (c.x - b.x) / len2, (c.y - b.y) / len2 };
    // Interior angle phi between the arms at b; overhang = (w / 2) / tan(phi / 2).
    const float cos_phi = -(u1.x * u2.x + u1.y * u2.y);
    const float half = acosf(cos_phi) / 2;
    const float over = (float)dsc->width / 2 / tanf(half);
    draw_segment(layer, dsc, a, (vec_t){ b.x + u1.x * over, b.y + u1.y * over });
    draw_segment(layer, dsc, (vec_t){ b.x - u2.x * over, b.y - u2.y * over }, c);
}

static void logo_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const int32_t diameter = lv_area_get_width(&area);
    const logo_row_t *row = row_for(diameter);
    // The ring's outer edge is the object's edge, whatever the stroke.
    const float s = (float)diameter / (2 * RING_RADIUS + row->ring);
    const float cx = (float)area.x1 + (float)diameter / 2;
    const float cy = (float)area.y1 + (float)diameter / 2;
#define PX(gx) (cx + ((gx) - GRID_CENTER) * s)
#define PY(gy) (cy + ((gy) - GRID_CENTER) * s)

    lv_draw_arc_dsc_t ring;
    lv_draw_arc_dsc_init(&ring);
    ring.color = LOGO_COLOR_RING;
    ring.width = LV_MAX(1, (int32_t)lroundf(row->ring * s));
    ring.start_angle = 0;
    ring.end_angle = 360;
    ring.center.x = area.x1 + diameter / 2;
    ring.center.y = area.y1 + diameter / 2;
    ring.radius = (uint16_t)(diameter / 2);
    lv_draw_arc(layer, &ring);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = LOGO_COLOR_CHEVRON;
    line.width = LV_MAX(1, (int32_t)lroundf(row->chevron * s));
    for (int dir = -1; dir <= 1; dir += 2) {   // top chevron, then bottom
        const float wing_y = GRID_CENTER + (float)dir * row->wing_dy;
        const float apex_y = GRID_CENTER + (float)dir * row->apex_dy;
        draw_chevron(layer, &line,
                     (vec_t){ PX(row->wing_x0), PY(wing_y) },
                     (vec_t){ PX(GRID_CENTER), PY(apex_y) },
                     (vec_t){ PX(64 - row->wing_x0), PY(wing_y) });
    }

    if (row->seam > 0) {
        line.color = LOGO_COLOR_SEAM;
        line.width = LV_MAX(1, (int32_t)lroundf(row->seam * s));
        draw_segment(layer, &line, (vec_t){ PX(row->seam_x0), cy },
                     (vec_t){ PX(64 - row->seam_x0), cy });
    }
#undef PX
#undef PY
}

lv_obj_t *logo_create(lv_obj_t *parent, int32_t diameter)
{
    lv_obj_t *logo = lv_obj_create(parent);
    lv_obj_remove_style_all(logo);
    lv_obj_set_size(logo, diameter, diameter);
    lv_obj_set_clickable(logo, false);
    lv_obj_set_scrollable(logo, false);
    lv_obj_add_event_cb(logo, logo_draw, LV_EVENT_DRAW_MAIN, NULL);
    return logo;
}
