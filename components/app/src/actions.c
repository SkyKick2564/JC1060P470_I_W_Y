#include "actions.h"

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"
#include "ui.h"

#define APP_GRID_COLUMNS 6
#define APP_GRID_ROWS    3
#define APP_ICON_COUNT   3
#define HOME_WIDGET_COUNT 2

typedef struct {
    lv_obj_t *button;
    int32_t column;
    int32_t row;
} app_icon_slot_t;

typedef struct {
    lv_obj_t *object;
    int32_t column;
    int32_t row;
    int32_t column_span;
    int32_t row_span;
} home_widget_slot_t;

static app_icon_slot_t s_app_icons[APP_ICON_COUNT];
static home_widget_slot_t s_home_widgets[HOME_WIDGET_COUNT];

static struct {
    lv_obj_t *button;
    lv_indev_t *indev;
    lv_point_t start_point;
    lv_coord_t start_x;
    lv_coord_t start_y;
    int32_t current_column;
    int32_t current_row;
    bool active;
    bool long_pressed;
} s_icon_drag;

static app_icon_slot_t *action_icon_find_slot(lv_obj_t *button)
{
    for (size_t i = 0; i < APP_ICON_COUNT; i++) {
        if (s_app_icons[i].button == button) {
            return &s_app_icons[i];
        }
    }

    return NULL;
}

static app_icon_slot_t *action_icon_find_slot_at(
    int32_t column,
    int32_t row,
    lv_obj_t *ignore_button)
{
    for (size_t i = 0; i < APP_ICON_COUNT; i++) {
        if (s_app_icons[i].button != NULL &&
            s_app_icons[i].button != ignore_button &&
            s_app_icons[i].column == column &&
            s_app_icons[i].row == row) {
            return &s_app_icons[i];
        }
    }

    return NULL;
}

static void action_icon_set_grid_cell(
    lv_obj_t *button,
    int32_t column,
    int32_t row)
{
    lv_obj_set_style_grid_cell_column_pos(
        button, column, LV_PART_MAIN);
    lv_obj_set_style_grid_cell_row_pos(
        button, row, LV_PART_MAIN);
}

static bool action_icon_get_cell_from_point(
    lv_point_t point,
    int32_t *column,
    int32_t *row)
{
    lv_obj_t *grid = objects.home;
    lv_area_t content_area;

    if (grid == NULL || column == NULL || row == NULL) {
        return false;
    }

    lv_obj_get_content_coords(grid, &content_area);

    int32_t content_width = lv_obj_get_content_width(grid);
    int32_t content_height = lv_obj_get_content_height(grid);
    int32_t gap_x = lv_obj_get_style_pad_column(grid, LV_PART_MAIN);
    int32_t gap_y = lv_obj_get_style_pad_row(grid, LV_PART_MAIN);

    int32_t track_width =
        (content_width - gap_x * (APP_GRID_COLUMNS - 1)) /
        APP_GRID_COLUMNS;
    int32_t track_height =
        (content_height - gap_y * (APP_GRID_ROWS - 1)) /
        APP_GRID_ROWS;

    if (track_width <= 0 || track_height <= 0) {
        return false;
    }

    int32_t relative_x = point.x - content_area.x1;
    int32_t relative_y = point.y - content_area.y1;

    if (relative_x < 0 || relative_y < 0 ||
        relative_x >= content_width || relative_y >= content_height) {
        return false;
    }

    int32_t column_step = track_width + gap_x;
    int32_t row_step = track_height + gap_y;

    *column = (relative_x + gap_x / 2) / column_step;
    *row = (relative_y + gap_y / 2) / row_step;

    if (*column < 0 || *column >= APP_GRID_COLUMNS ||
        *row < 0 || *row >= APP_GRID_ROWS) {
        return false;
    }

    return true;
}

static bool action_cell_in_rect(
    int32_t column,
    int32_t row,
    int32_t rect_column,
    int32_t rect_row,
    int32_t column_span,
    int32_t row_span)
{
    return column >= rect_column &&
        column < rect_column + column_span &&
        row >= rect_row &&
        row < rect_row + row_span;
}

static home_widget_slot_t *action_widget_at_cell(
    int32_t column,
    int32_t row)
{
    for (size_t i = 0; i < HOME_WIDGET_COUNT; i++) {
        home_widget_slot_t *widget = &s_home_widgets[i];
        if (widget->object != NULL &&
            action_cell_in_rect(
                column, row, widget->column, widget->row,
                widget->column_span, widget->row_span)) {
            return widget;
        }
    }

    return NULL;
}

static bool action_widget_rect_is_free(
    home_widget_slot_t *moving_widget,
    int32_t column,
    int32_t row,
    int32_t reserved_column,
    int32_t reserved_row)
{
    if (column < 0 || row < 0 ||
        column + moving_widget->column_span > APP_GRID_COLUMNS ||
        row + moving_widget->row_span > APP_GRID_ROWS) {
        return false;
    }

    for (int32_t y = row; y < row + moving_widget->row_span; y++) {
        for (int32_t x = column; x < column + moving_widget->column_span; x++) {
            if (x == reserved_column && y == reserved_row) {
                return false;
            }

            for (size_t i = 0; i < APP_ICON_COUNT; i++) {
                app_icon_slot_t *app = &s_app_icons[i];
                if (app->button != NULL &&
                    app->button != s_icon_drag.button &&
                    app->column == x && app->row == y) {
                    return false;
                }
            }

            for (size_t i = 0; i < HOME_WIDGET_COUNT; i++) {
                home_widget_slot_t *other = &s_home_widgets[i];
                if (other == moving_widget || other->object == NULL) {
                    continue;
                }
                if (action_cell_in_rect(
                        x, y, other->column, other->row,
                        other->column_span, other->row_span)) {
                    return false;
                }
            }
        }
    }

    return true;
}

static bool action_widget_find_free_rect(
    home_widget_slot_t *widget,
    int32_t reserved_column,
    int32_t reserved_row,
    int32_t *new_column,
    int32_t *new_row)
{
    /*
     * Search the Grid in row-major order. The dragged icon's destination
     * cell is reserved, and all other app/widget footprints remain occupied.
     */
    for (int32_t row = 0; row <= APP_GRID_ROWS - widget->row_span; row++) {
        for (int32_t column = 0;
             column <= APP_GRID_COLUMNS - widget->column_span;
             column++) {
            if (action_widget_rect_is_free(
                    widget, column, row,
                    reserved_column, reserved_row)) {
                *new_column = column;
                *new_row = row;
                return true;
            }
        }
    }

    return false;
}

static void action_widget_set_grid_cell(
    home_widget_slot_t *widget,
    int32_t column,
    int32_t row)
{
    widget->column = column;
    widget->row = row;
    lv_obj_set_style_grid_cell_column_pos(
        widget->object, column, LV_PART_MAIN);
    lv_obj_set_style_grid_cell_row_pos(
        widget->object, row, LV_PART_MAIN);
}

static bool action_widget_pack_apps_in_old_rect(
    home_widget_slot_t *widget,
    int32_t reserved_column,
    int32_t reserved_row)
{
    size_t app_index = 0;

    /*
     * If apps are the only reason a widget cannot move, pack the other
     * shortcuts into the space the widget is about to vacate. The dragged
     * shortcut keeps the exact cell under the finger.
     */
    for (int32_t row = widget->row;
         row < widget->row + widget->row_span; row++) {
        for (int32_t column = widget->column;
             column < widget->column + widget->column_span; column++) {
            if (column == reserved_column && row == reserved_row) {
                continue;
            }

            while (app_index < APP_ICON_COUNT &&
                   (s_app_icons[app_index].button == NULL ||
                    s_app_icons[app_index].button == s_icon_drag.button)) {
                app_index++;
            }

            if (app_index >= APP_ICON_COUNT) {
                return true;
            }

            s_app_icons[app_index].column = column;
            s_app_icons[app_index].row = row;
            action_icon_set_grid_cell(
                s_app_icons[app_index].button, column, row);
            app_index++;
        }
    }

    while (app_index < APP_ICON_COUNT) {
        if (s_app_icons[app_index].button != NULL &&
            s_app_icons[app_index].button != s_icon_drag.button) {
            return false;
        }
        app_index++;
    }

    return true;
}

static void action_icon_set_button_color(lv_obj_t *button, bool pressed)
{
    lv_obj_t *label;
    lv_obj_t *image;

    if (button == NULL) {
        return;
    }

    label = lv_obj_get_child(button, 0);
    image = lv_obj_get_child(button, 1);

    if (label == NULL || image == NULL) {
        return;
    }

    lv_obj_set_style_text_color(
        label,
        pressed ? lv_color_hex(0x6e6e6e) : lv_color_hex(0xffffff),
        LV_PART_MAIN
    );

    /*
     * Avoid LVGL software image recolor: the renderer was stalling in
     * recolor_only() and starving IDLE0. Keep pressed feedback by fading
     * the image instead of running the recolor path.
     */
    lv_obj_set_style_image_opa(
        image,
        pressed ? LV_OPA_50 : LV_OPA_COVER,
        LV_PART_MAIN
    );
}

static void action_icon_finish(lv_obj_t *button)
{
    lv_obj_t *image;

    action_icon_set_button_color(button, false);

    if (!s_icon_drag.active || s_icon_drag.button != button) {
        return;
    }

    image = lv_obj_get_child(button, 1);
    if (image != NULL) {
        lv_obj_set_style_transform_scale_x(image, 256, LV_PART_MAIN);
        lv_obj_set_style_transform_scale_y(image, 256, LV_PART_MAIN);
    }

    /*
     * Commit the dragged icon to its current Grid cell before returning it
     * to the Grid layout. The other icon, if any, has already been moved
     * into the cell that the dragged icon left.
     */
    app_icon_slot_t *slot = action_icon_find_slot(button);
    if (slot != NULL) {
        slot->column = s_icon_drag.current_column;
        slot->row = s_icon_drag.current_row;
        action_icon_set_grid_cell(
            button, slot->column, slot->row);
    }

    lv_obj_remove_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT);
    if (objects.home != NULL) {
        lv_obj_update_layout(objects.home);
    }

    s_icon_drag.active = false;
    s_icon_drag.button = NULL;
    s_icon_drag.indev = NULL;
}

static void action_icon_pressed(lv_event_t *event)
{
    s_icon_drag.long_pressed = false;
    action_icon_set_button_color(lv_event_get_current_target(event), true);
}

static void action_icon_clicked(lv_event_t *event)
{
    /*
     * LVGL sends LV_EVENT_CLICKED on release even after LONG_PRESSED.
     * A long press is the drag gesture, not a normal click.
     */
    if (s_icon_drag.long_pressed) {
        lv_event_stop_processing(event);
    }
}

static void action_icon_long_pressed(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target(event);
    lv_obj_t *image = lv_obj_get_child(button, 1);
    lv_indev_t *indev;
    app_icon_slot_t *slot;

    if (image == NULL) {
        return;
    }

    /*
     * Recover from a missed release/press-lost event before starting
     * another icon drag. Only one icon can own the drag state at a time.
     */
    if (s_icon_drag.active && s_icon_drag.button != button) {
        action_icon_finish(s_icon_drag.button);
    }

    if (s_icon_drag.active) {
        return;
    }

    slot = action_icon_find_slot(button);
    if (slot == NULL || lv_obj_get_parent(button) != objects.home) {
        return;
    }

    indev = lv_event_get_indev(event);
    if (indev == NULL) {
        return;
    }

    s_icon_drag.button = button;
    s_icon_drag.indev = indev;
    s_icon_drag.long_pressed = true;
    s_icon_drag.start_x = lv_obj_get_x(button);
    s_icon_drag.start_y = lv_obj_get_y(button);
    s_icon_drag.current_column = slot->column;
    s_icon_drag.current_row = slot->row;
    lv_indev_get_point(indev, &s_icon_drag.start_point);
    s_icon_drag.active = true;

    /*
     * A Grid normally owns the position of its children. Ignore its layout
     * only while this icon is being dragged, so it can float under the
     * finger without changing the generated EEZ layout.
     */
    lv_obj_add_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_foreground(button);

    lv_obj_set_style_transform_pivot_x(
        image, lv_obj_get_width(image) / 2, LV_PART_MAIN
    );
    lv_obj_set_style_transform_pivot_y(
        image, lv_obj_get_height(image) / 2, LV_PART_MAIN
    );
    lv_obj_set_style_transform_scale_x(image, 300, LV_PART_MAIN);
    lv_obj_set_style_transform_scale_y(image, 300, LV_PART_MAIN);
}

static void action_icon_update_drop_cell(lv_point_t point)
{
    int32_t column;
    int32_t row;

    if (!action_icon_get_cell_from_point(point, &column, &row)) {
        return;
    }

    if (column == s_icon_drag.current_column &&
        row == s_icon_drag.current_row) {
        return;
    }

    /*
     * Dropping over another app swaps the two app cells. Dropping over a
     * widget moves that whole widget footprint to a free rectangle first;
     * the widget keeps its original span and the dragged app takes the
     * selected cell in the space it vacated.
     */
    app_icon_slot_t *occupant = action_icon_find_slot_at(
        column, row, s_icon_drag.button);

    if (occupant != NULL) {
        occupant->column = s_icon_drag.current_column;
        occupant->row = s_icon_drag.current_row;
        action_icon_set_grid_cell(
            occupant->button, occupant->column, occupant->row);
    } else {
        home_widget_slot_t *widget = action_widget_at_cell(column, row);
        if (widget != NULL) {
            int32_t free_column;
            int32_t free_row;

            if (!action_widget_find_free_rect(
                    widget, column, row, &free_column, &free_row)) {
                int32_t old_columns[APP_ICON_COUNT];
                int32_t old_rows[APP_ICON_COUNT];

                for (size_t i = 0; i < APP_ICON_COUNT; i++) {
                    old_columns[i] = s_app_icons[i].column;
                    old_rows[i] = s_app_icons[i].row;
                }

                bool packed = action_widget_pack_apps_in_old_rect(
                    widget, column, row);
                bool found = packed && action_widget_find_free_rect(
                    widget, column, row, &free_column, &free_row);

                if (!found) {
                    for (size_t i = 0; i < APP_ICON_COUNT; i++) {
                        s_app_icons[i].column = old_columns[i];
                        s_app_icons[i].row = old_rows[i];
                        if (s_app_icons[i].button != NULL &&
                            s_app_icons[i].button != s_icon_drag.button) {
                            action_icon_set_grid_cell(
                                s_app_icons[i].button,
                                old_columns[i], old_rows[i]);
                        }
                    }
                    return;
                }
            }

            action_widget_set_grid_cell(widget, free_column, free_row);
        }
    }

    s_icon_drag.current_column = column;
    s_icon_drag.current_row = row;

    if (objects.home != NULL) {
        lv_obj_update_layout(objects.home);
    }
}

static void action_icon_pressing(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target(event);
    lv_indev_t *indev;
    lv_point_t point;

    if (!s_icon_drag.active || s_icon_drag.button != button) {
        return;
    }

    indev = lv_event_get_indev(event);
    if (indev == NULL || indev != s_icon_drag.indev) {
        return;
    }

    lv_indev_get_point(indev, &point);

    /*
     * Move only when the pointer changes position. IGNORE_LAYOUT is set
     * during the drag, so lv_obj_set_pos() is no longer overridden by Grid.
     */
    lv_coord_t new_x = s_icon_drag.start_x +
        point.x - s_icon_drag.start_point.x;
    lv_coord_t new_y = s_icon_drag.start_y +
        point.y - s_icon_drag.start_point.y;

    if (lv_obj_get_x(button) != new_x || lv_obj_get_y(button) != new_y) {
        lv_obj_set_pos(button, new_x, new_y);
    }

    action_icon_update_drop_cell(point);
}

static void action_icon_released(lv_event_t *event)
{
    action_icon_finish(lv_event_get_current_target(event));
}

static void action_icon_press_lost(lv_event_t *event)
{
    action_icon_finish(lv_event_get_current_target(event));
}

static esp_err_t action_icon_attach(lv_obj_t *button)
{
    lv_obj_t *image;

    if (button == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    image = lv_obj_get_child(button, 1);
    if (image == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Preserve all EEZ-generated styles and layout. Only override the
     * recolor properties that can trigger LVGL's slow software recolor path.
     */
    /*
     * The button can enter LV_STATE_PRESSED while its child image is
     * rendered. Clear inherited recolor on both objects for every state;
     * otherwise LVGL may still select recolor_only() for the child image.
     */
    lv_obj_set_style_image_recolor_opa(
        button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_ANY
    );
    lv_obj_set_style_image_recolor_opa(
        image, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_ANY
    );
    lv_obj_set_style_image_opa(
        image, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_ANY
    );

    lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);

    lv_obj_add_event_cb(button, action_icon_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(button, action_icon_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(button, action_icon_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(button, action_icon_press_lost, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(button, action_icon_long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(button, action_icon_pressing, LV_EVENT_PRESSING, NULL);

    return ESP_OK;
}

static void action_icon_init_slot(
    size_t index,
    lv_obj_t *button)
{
    s_app_icons[index].button = button;
    s_app_icons[index].column =
        lv_obj_get_style_grid_cell_column_pos(button, LV_PART_MAIN);
    s_app_icons[index].row =
        lv_obj_get_style_grid_cell_row_pos(button, LV_PART_MAIN);
}

/* Control Center drag and snap behavior */
#define CONTROL_CENTER_HIDDEN_Y       (-600)
#define CONTROL_CENTER_OPEN_Y         (0)
#define CONTROL_CENTER_ANIM_SPEED     (2000)
#define CONTROL_CENTER_FLICK_SPEED    (650)
#define CONTROL_CENTER_FLICK_MIN_MOVE (18)

static struct {
    lv_obj_t *target;
    lv_indev_t *indev;
    lv_point_t start_point;
    lv_point_t last_point;
    lv_coord_t start_y;
    uint32_t last_tick;
    int32_t velocity_y;
    bool active;
} s_control_center_drag;

static lv_opa_t s_control_center_max_opa = LV_OPA_COVER;
static bool s_control_center_opa_initialized;

static void action_control_center_init_opacity(void)
{
    if (s_control_center_opa_initialized ||
        objects.control_center_overlay == NULL) {
        return;
    }
    s_control_center_max_opa = lv_obj_get_style_bg_opa(
        objects.control_center_overlay, LV_PART_MAIN);
    s_control_center_opa_initialized = true;
}

static void action_control_center_update_opacity(lv_coord_t y)
{
    lv_obj_t *overlay = objects.control_center_overlay;
    if (overlay == NULL) return;

    action_control_center_init_opacity();

    int32_t progress = y - CONTROL_CENTER_HIDDEN_Y;
    if (progress < 0) progress = 0;
    else if (progress > CONTROL_CENTER_OPEN_Y - CONTROL_CENTER_HIDDEN_Y)
        progress = CONTROL_CENTER_OPEN_Y - CONTROL_CENTER_HIDDEN_Y;

    int32_t opacity = ((int32_t)s_control_center_max_opa * progress) /
        (CONTROL_CENTER_OPEN_Y - CONTROL_CENTER_HIDDEN_Y);
    lv_obj_set_style_bg_opa(overlay, (lv_opa_t)opacity, LV_PART_MAIN);
}

static bool action_control_center_is_event_target(lv_obj_t *target)
{
    return target != NULL &&
        (target == objects.control_center_overlay ||
         target == objects.control_center_touch_area);
}

static void action_control_center_anim_exec(void *obj, int32_t y)
{
    lv_obj_t *overlay = (lv_obj_t *)obj;
    lv_obj_set_y(overlay, (lv_coord_t)y);
    action_control_center_update_opacity((lv_coord_t)y);
}

static void action_control_center_animate(lv_coord_t target_y)
{
    lv_obj_t *overlay = objects.control_center_overlay;
    if (overlay == NULL) return;

    lv_coord_t current_y = lv_obj_get_y(overlay);
    int32_t distance = target_y - current_y;
    lv_anim_delete(overlay, action_control_center_anim_exec);

    if (distance == 0) {
        action_control_center_update_opacity(target_y);
        return;
    }

    int32_t abs_distance = distance >= 0 ? distance : -distance;
    int32_t duration = (abs_distance * 1000) / CONTROL_CENTER_ANIM_SPEED;
    if (duration < 1) duration = 1;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, overlay);
    lv_anim_set_exec_cb(&anim, action_control_center_anim_exec);
    lv_anim_set_values(&anim, current_y, target_y);
    lv_anim_set_duration(&anim, (uint32_t)duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);
}

static void action_control_center_update_velocity(lv_point_t point, uint32_t now)
{
    uint32_t elapsed = now - s_control_center_drag.last_tick;
    if (elapsed > 0) {
        int32_t delta_y = point.y - s_control_center_drag.last_point.y;
        int32_t new_velocity = (delta_y * 1000) / (int32_t)elapsed;
        s_control_center_drag.velocity_y =
            (s_control_center_drag.velocity_y * 2 + new_velocity) / 3;
    }
    s_control_center_drag.last_point = point;
    s_control_center_drag.last_tick = now;
}

static void action_control_center_pressed(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    if (objects.control_center_overlay == NULL ||
        !action_control_center_is_event_target(target) ||
        s_control_center_drag.active) return;

    action_control_center_init_opacity();
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) return;

    lv_anim_delete(objects.control_center_overlay, action_control_center_anim_exec);
    s_control_center_drag.target = target;
    s_control_center_drag.indev = indev;
    s_control_center_drag.start_y = lv_obj_get_y(objects.control_center_overlay);
    lv_indev_get_point(indev, &s_control_center_drag.start_point);
    s_control_center_drag.last_point = s_control_center_drag.start_point;
    s_control_center_drag.last_tick = lv_tick_get();
    s_control_center_drag.velocity_y = 0;
    s_control_center_drag.active = true;
    lv_obj_move_foreground(objects.control_center_overlay);
}

static void action_control_center_pressing(lv_event_t *event)
{
    if (!s_control_center_drag.active ||
        !action_control_center_is_event_target(lv_event_get_target(event))) return;

    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL || indev != s_control_center_drag.indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);
    action_control_center_update_velocity(point, lv_tick_get());

    lv_coord_t new_y = s_control_center_drag.start_y +
        point.y - s_control_center_drag.start_point.y;
    if (new_y < CONTROL_CENTER_HIDDEN_Y) new_y = CONTROL_CENTER_HIDDEN_Y;
    else if (new_y > CONTROL_CENTER_OPEN_Y) new_y = CONTROL_CENTER_OPEN_Y;

    lv_obj_t *overlay = objects.control_center_overlay;
    if (overlay != NULL && lv_obj_get_y(overlay) != new_y) {
        lv_obj_set_y(overlay, new_y);
        action_control_center_update_opacity(new_y);
    }
}

static void action_control_center_finish(lv_event_t *event)
{
    if (!s_control_center_drag.active ||
        !action_control_center_is_event_target(lv_event_get_target(event))) return;

    lv_obj_t *overlay = objects.control_center_overlay;
    if (overlay == NULL || s_control_center_drag.indev == NULL) {
        s_control_center_drag.active = false;
        s_control_center_drag.target = NULL;
        s_control_center_drag.indev = NULL;
        return;
    }

    lv_point_t point;
    lv_indev_get_point(s_control_center_drag.indev, &point);
    action_control_center_update_velocity(point, lv_tick_get());

    lv_coord_t current_y = lv_obj_get_y(overlay);
    int32_t total_move = point.y - s_control_center_drag.start_point.y;
    int32_t velocity_y = s_control_center_drag.velocity_y;

    bool fast_down = total_move >= CONTROL_CENTER_FLICK_MIN_MOVE &&
        velocity_y >= CONTROL_CENTER_FLICK_SPEED;
    bool fast_up = total_move <= -CONTROL_CENTER_FLICK_MIN_MOVE &&
        velocity_y <= -CONTROL_CENTER_FLICK_SPEED;

    lv_coord_t destination;
    if (fast_down) destination = CONTROL_CENTER_OPEN_Y;
    else if (fast_up) destination = CONTROL_CENTER_HIDDEN_Y;
    else destination = current_y > (CONTROL_CENTER_HIDDEN_Y / 2)
        ? CONTROL_CENTER_OPEN_Y : CONTROL_CENTER_HIDDEN_Y;

    s_control_center_drag.active = false;
    s_control_center_drag.target = NULL;
    s_control_center_drag.indev = NULL;
    s_control_center_drag.velocity_y = 0;
    action_control_center_animate(destination);
}

static void action_control_center_released(lv_event_t *event)
{
    action_control_center_finish(event);
}

static void action_control_center_press_lost(lv_event_t *event)
{
    action_control_center_finish(event);
}

static esp_err_t action_control_center_attach(lv_obj_t *object)
{
    if (object == NULL) return ESP_ERR_INVALID_STATE;

    lv_obj_add_flag(object, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(object, action_control_center_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(object, action_control_center_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(object, action_control_center_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(object, action_control_center_press_lost, LV_EVENT_PRESS_LOST, NULL);
    return ESP_OK;
}

esp_err_t app_actions_init(void)
{
    esp_err_t err;

    s_icon_drag.button = NULL;
    s_icon_drag.indev = NULL;
    s_icon_drag.active = false;
    s_icon_drag.long_pressed = false;

    if (objects.home == NULL ||
        objects.camera_btn == NULL ||
        objects.media_btn == NULL ||
        objects.setting_btn == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_update_layout(objects.home);
    action_icon_init_slot(0, objects.camera_btn);
    action_icon_init_slot(1, objects.media_btn);
    action_icon_init_slot(2, objects.setting_btn);

    s_home_widgets[0].object = objects.card_network;
    s_home_widgets[1].object = objects.container_media;
    for (size_t i = 0; i < HOME_WIDGET_COUNT; i++) {
        home_widget_slot_t *widget = &s_home_widgets[i];
        if (widget->object == NULL) {
            return ESP_ERR_INVALID_STATE;
        }
        widget->column = lv_obj_get_style_grid_cell_column_pos(
            widget->object, LV_PART_MAIN);
        widget->row = lv_obj_get_style_grid_cell_row_pos(
            widget->object, LV_PART_MAIN);
        widget->column_span = lv_obj_get_style_grid_cell_column_span(
            widget->object, LV_PART_MAIN);
        widget->row_span = lv_obj_get_style_grid_cell_row_span(
            widget->object, LV_PART_MAIN);
        if (widget->column_span < 1) widget->column_span = 1;
        if (widget->row_span < 1) widget->row_span = 1;
    }

    s_control_center_drag.target = NULL;
    s_control_center_drag.indev = NULL;
    s_control_center_drag.active = false;
    s_control_center_opa_initialized = false;

    err = action_icon_attach(objects.camera_btn);
    if (err != ESP_OK) return err;
    err = action_icon_attach(objects.media_btn);
    if (err != ESP_OK) return err;
    err = action_icon_attach(objects.setting_btn);
    if (err != ESP_OK) return err;

    err = action_control_center_attach(objects.control_center_overlay);
    if (err != ESP_OK) return err;
    err = action_control_center_attach(objects.control_center_touch_area);
    if (err != ESP_OK) return err;

    return ESP_OK;
}
