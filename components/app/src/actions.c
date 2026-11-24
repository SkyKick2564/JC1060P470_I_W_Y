#include "actions.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#include "lvgl.h"
#include "ui.h"
#include "eez_object_map.h"

#define APP_GRID_COLUMNS 6
#define APP_GRID_ROWS    3
#define APP_ICON_CAPACITY 18
#define HOME_OBJECT_CAPACITY 18
#define HOME_LAYOUT_ID_LENGTH 64
#define HOME_LAYOUT_ITEM_CAPACITY (APP_GRID_COLUMNS * APP_GRID_ROWS)

typedef struct {
    lv_obj_t *button;
    const char *identifier;
    int32_t column;
    int32_t row;
    int32_t default_column;
    int32_t default_row;
    bool restored;
} app_icon_slot_t;

typedef struct {
    lv_obj_t *object;
    const char *identifier;
    int32_t column;
    int32_t row;
    int32_t column_span;
    int32_t row_span;
    int32_t default_column;
    int32_t default_row;
    bool restored;
} home_widget_slot_t;

static app_icon_slot_t s_app_icons[APP_ICON_CAPACITY];
static size_t s_app_icon_count;
static home_widget_slot_t s_home_widgets[HOME_OBJECT_CAPACITY];
static size_t s_home_widget_count;
static bool s_home_discovery_overflow;

static void action_icon_set_grid_cell(lv_obj_t *button, int32_t column, int32_t row);
static void action_widget_set_grid_cell(home_widget_slot_t *widget, int32_t column, int32_t row);
static esp_err_t action_home_layout_place_objects(void);
static void action_layout_drag_take_snapshot(void);
static void action_layout_drag_restore_snapshot(void);
static bool action_layout_drag_preview(
    lv_obj_t *dragged_icon,
    home_widget_slot_t *dragged_widget,
    int32_t column,
    int32_t row);



#define HOME_LAYOUT_NVS_NAMESPACE "ui_layout"
#define HOME_LAYOUT_NVS_KEY       "positions"
#define HOME_LAYOUT_MAGIC         0x484C4159UL
#define HOME_LAYOUT_VERSION       2

typedef struct {
    char identifier[HOME_LAYOUT_ID_LENGTH];
    int8_t column;
    int8_t row;
} home_layout_item_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    home_layout_item_t items[HOME_LAYOUT_ITEM_CAPACITY];
} home_layout_blob_t;

static const char *action_home_get_identifier(lv_obj_t *object)
{
    for (size_t i = 0; i < sizeof(s_eez_object_map) / sizeof(s_eez_object_map[0]); i++) {
        if (*s_eez_object_map[i].object == object) {
            return s_eez_object_map[i].identifier;
        }
    }

    return NULL;
}

static void action_home_layout_restore(void)
{
    nvs_handle_t nvs;
    home_layout_blob_t blob;
    size_t blob_size = sizeof(blob);

    if (nvs_open(HOME_LAYOUT_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }

    esp_err_t err = nvs_get_blob(nvs, HOME_LAYOUT_NVS_KEY, &blob, &blob_size);
    nvs_close(nvs);

    if (err != ESP_OK || blob_size != sizeof(blob) ||
        blob.magic != HOME_LAYOUT_MAGIC ||
        blob.version != HOME_LAYOUT_VERSION ||
        blob.count > HOME_LAYOUT_ITEM_CAPACITY) {
        return;
    }

    for (size_t i = 0; i < blob.count; i++) {
        const home_layout_item_t *item = &blob.items[i];

        if (item->identifier[0] == '\0' ||
            item->column < 0 || item->column >= APP_GRID_COLUMNS ||
            item->row < 0 || item->row >= APP_GRID_ROWS) {
            continue;
        }

        for (size_t j = 0; j < s_app_icon_count; j++) {
            if (s_app_icons[j].identifier != NULL &&
                strcmp(s_app_icons[j].identifier, item->identifier) == 0) {
                s_app_icons[j].column = item->column;
                s_app_icons[j].row = item->row;
                s_app_icons[j].restored = true;
                action_icon_set_grid_cell(
                    s_app_icons[j].button, item->column, item->row);
            }
        }

        for (size_t j = 0; j < s_home_widget_count; j++) {
            if (s_home_widgets[j].identifier != NULL &&
                strcmp(s_home_widgets[j].identifier, item->identifier) == 0) {
                s_home_widgets[j].column = item->column;
                s_home_widgets[j].row = item->row;
                s_home_widgets[j].restored = true;
            }
        }
    }
}

static void action_home_layout_save(void)
{
    nvs_handle_t nvs;
    home_layout_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = HOME_LAYOUT_MAGIC;
    blob.version = HOME_LAYOUT_VERSION;

    for (size_t i = 0; i < s_app_icon_count &&
         blob.count < HOME_LAYOUT_ITEM_CAPACITY; i++) {
        const app_icon_slot_t *icon = &s_app_icons[i];
        if (icon->identifier == NULL ||
            icon->identifier[0] == '\0' ||
            icon->column < 0 || icon->column >= APP_GRID_COLUMNS ||
            icon->row < 0 || icon->row >= APP_GRID_ROWS) {
            continue;
        }

        home_layout_item_t *item = &blob.items[blob.count++];
        strncpy(item->identifier, icon->identifier,
            sizeof(item->identifier) - 1);
        item->column = (int8_t)icon->column;
        item->row = (int8_t)icon->row;
    }

    for (size_t i = 0; i < s_home_widget_count &&
         blob.count < HOME_LAYOUT_ITEM_CAPACITY; i++) {
        const home_widget_slot_t *widget = &s_home_widgets[i];
        if (widget->identifier == NULL ||
            widget->identifier[0] == '\0' ||
            widget->column < 0 || widget->column >= APP_GRID_COLUMNS ||
            widget->row < 0 || widget->row >= APP_GRID_ROWS) {
            continue;
        }

        home_layout_item_t *item = &blob.items[blob.count++];
        strncpy(item->identifier, widget->identifier,
            sizeof(item->identifier) - 1);
        item->column = (int8_t)widget->column;
        item->row = (int8_t)widget->row;
    }

    if (nvs_open(HOME_LAYOUT_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }

    /*
     * Keep one bounded dataset at ui_layout/positions.
     * Skip flash writes when the current serialized layout is identical.
     */
    home_layout_blob_t current_blob;
    size_t current_size = sizeof(current_blob);
    esp_err_t read_err = nvs_get_blob(
        nvs, HOME_LAYOUT_NVS_KEY, &current_blob, &current_size);

    if (read_err == ESP_OK &&
        current_size == sizeof(blob) &&
        memcmp(&current_blob, &blob, sizeof(blob)) == 0) {
        nvs_close(nvs);
        return;
    }

    esp_err_t write_err = nvs_set_blob(
        nvs, HOME_LAYOUT_NVS_KEY, &blob, sizeof(blob));
    if (write_err == ESP_OK) {
        write_err = nvs_commit(nvs);
    }

    nvs_close(nvs);
}

static struct {
    home_widget_slot_t *widget;
    lv_indev_t *indev;
    lv_point_t start_point;
    lv_coord_t start_x;
    lv_coord_t start_y;
    int32_t grab_column_offset;
    int32_t grab_row_offset;
    bool preview_valid;
    bool active;
} s_widget_drag;

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
    bool preview_valid;
} s_icon_drag;

/*
 * One immutable layout snapshot is shared by App and Widget drags.
 * Every preview is rebuilt from this snapshot, never from the last preview.
 */
static struct {
    int32_t icon_columns[APP_ICON_CAPACITY];
    int32_t icon_rows[APP_ICON_CAPACITY];
    int32_t widget_columns[HOME_OBJECT_CAPACITY];
    int32_t widget_rows[HOME_OBJECT_CAPACITY];
} s_layout_drag_snapshot;

static app_icon_slot_t *action_icon_find_slot(lv_obj_t *button)
{
    for (size_t i = 0; i < APP_ICON_CAPACITY; i++) {
        if (s_app_icons[i].button == button) {
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
    int32_t column_widths[APP_GRID_COLUMNS];
    int32_t row_heights[APP_GRID_ROWS];

    if (grid == NULL || column == NULL || row == NULL) {
        return false;
    }

    /*
     * LVGL's Grid content coordinates already exclude the container padding.
     * Keep that origin so the 30 px home-screen padding remains part of the
     * coordinate calculation rather than treating the screen origin as Grid 0.
     */
    lv_obj_get_content_coords(grid, &content_area);

    int32_t content_width = lv_obj_get_content_width(grid);
    int32_t content_height = lv_obj_get_content_height(grid);
    int32_t gap_x = lv_obj_get_style_pad_column(grid, LV_PART_MAIN);
    int32_t gap_y = lv_obj_get_style_pad_row(grid, LV_PART_MAIN);

    int32_t free_width = content_width - gap_x * (APP_GRID_COLUMNS - 1);
    int32_t free_height = content_height - gap_y * (APP_GRID_ROWS - 1);
    if (free_width <= 0 || free_height <= 0) {
        return false;
    }

    /*
     * Match LVGL 9.5 calc_cols()/calc_rows() for equal LV_GRID_FR(1)
     * tracks: round each share to nearest, then distribute the remaining
     * pixels over the tracks that have not yet been calculated.
     */
    int32_t remaining_fr = APP_GRID_COLUMNS;
    for (int32_t i = 0; i < APP_GRID_COLUMNS; i++) {
        column_widths[i] = (free_width + remaining_fr / 2) / remaining_fr;
        free_width -= column_widths[i];
        remaining_fr--;
    }

    remaining_fr = APP_GRID_ROWS;
    for (int32_t i = 0; i < APP_GRID_ROWS; i++) {
        row_heights[i] = (free_height + remaining_fr / 2) / remaining_fr;
        free_height -= row_heights[i];
        remaining_fr--;
    }

    int32_t relative_x = point.x - content_area.x1;
    int32_t relative_y = point.y - content_area.y1;
    if (relative_x < 0 || relative_y < 0 ||
        relative_x >= content_width || relative_y >= content_height) {
        return false;
    }

    /*
     * Resolve the boundary halfway through each inter-track gap, matching
     * the existing drop behavior while using LVGL's actual rounded track
     * widths/heights instead of one truncated average size.
     */
    int32_t track_start = 0;
    *column = -1;
    for (int32_t i = 0; i < APP_GRID_COLUMNS; i++) {
        int32_t boundary = track_start + column_widths[i] + gap_x / 2;
        if (relative_x < boundary || i == APP_GRID_COLUMNS - 1) {
            *column = i;
            break;
        }
        track_start += column_widths[i] + gap_x;
    }

    track_start = 0;
    *row = -1;
    for (int32_t i = 0; i < APP_GRID_ROWS; i++) {
        int32_t boundary = track_start + row_heights[i] + gap_y / 2;
        if (relative_y < boundary || i == APP_GRID_ROWS - 1) {
            *row = i;
            break;
        }
        track_start += row_heights[i] + gap_y;
    }

    return *column >= 0 && *column < APP_GRID_COLUMNS &&
        *row >= 0 && *row < APP_GRID_ROWS;
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
    for (size_t i = 0; i < HOME_OBJECT_CAPACITY; i++) {
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

static bool action_home_cell_is_free(
    int32_t column,
    int32_t row,
    lv_obj_t *ignore_button)
{
    if (column < 0 || column >= APP_GRID_COLUMNS ||
        row < 0 || row >= APP_GRID_ROWS) {
        return false;
    }

    for (size_t i = 0; i < s_app_icon_count; i++) {
        const app_icon_slot_t *icon = &s_app_icons[i];
        if (icon->button != NULL && icon->button != ignore_button &&
            icon->column == column && icon->row == row) {
            return false;
        }
    }

    return action_widget_at_cell(column, row) == NULL;
}

static bool action_home_find_free_cell(
    lv_obj_t *button,
    int32_t *column,
    int32_t *row)
{
    for (int32_t y = 0; y < APP_GRID_ROWS; y++) {
        for (int32_t x = 0; x < APP_GRID_COLUMNS; x++) {
            if (action_home_cell_is_free(x, y, button)) {
                *column = x;
                *row = y;
                return true;
            }
        }
    }

    return false;
}

static esp_err_t action_home_layout_place_objects(void)
{
    int32_t app_target_columns[APP_ICON_CAPACITY];
    int32_t app_target_rows[APP_ICON_CAPACITY];
    int32_t widget_target_columns[HOME_OBJECT_CAPACITY];
    int32_t widget_target_rows[HOME_OBJECT_CAPACITY];

    if (s_app_icon_count + s_home_widget_count > HOME_LAYOUT_ITEM_CAPACITY) {
        return ESP_ERR_NO_MEM;
    }

    /* Preserve each object's EEZ default or restored NVS position as its
     * preferred target, then release the whole grid for one unified placement. */
    for (size_t i = 0; i < s_app_icon_count; i++) {
        app_target_columns[i] = s_app_icons[i].restored
            ? s_app_icons[i].column : s_app_icons[i].default_column;
        app_target_rows[i] = s_app_icons[i].restored
            ? s_app_icons[i].row : s_app_icons[i].default_row;
        s_app_icons[i].column = -1;
        s_app_icons[i].row = -1;
    }
    for (size_t i = 0; i < s_home_widget_count; i++) {
        widget_target_columns[i] = s_home_widgets[i].restored
            ? s_home_widgets[i].column : s_home_widgets[i].default_column;
        widget_target_rows[i] = s_home_widgets[i].restored
            ? s_home_widgets[i].row : s_home_widgets[i].default_row;
        s_home_widgets[i].column = -1;
        s_home_widgets[i].row = -1;
    }

    /* Process in EEZ child order. Each preferred rectangle gets priority;
     * if occupied, the shared layout planner relocates the colliding objects
     * while preserving their complete widget footprints. */
    uint32_t child_count = lv_obj_get_child_cnt(objects.home);
    for (uint32_t child_index = 0; child_index < child_count; child_index++) {
        lv_obj_t *child = lv_obj_get_child(objects.home, (int32_t)child_index);
        bool placed = false;
        int32_t preferred_column = -1;
        int32_t preferred_row = -1;
        int32_t preferred_span_columns = 1;
        int32_t preferred_span_rows = 1;
        app_icon_slot_t *icon = NULL;
        home_widget_slot_t *widget = NULL;

        for (size_t i = 0; i < s_app_icon_count; i++) {
            if (s_app_icons[i].button == child) {
                icon = &s_app_icons[i];
                preferred_column = app_target_columns[i];
                preferred_row = app_target_rows[i];
                break;
            }
        }
        if (icon == NULL) {
            for (size_t i = 0; i < s_home_widget_count; i++) {
                if (s_home_widgets[i].object == child) {
                    widget = &s_home_widgets[i];
                    preferred_column = widget_target_columns[i];
                    preferred_row = widget_target_rows[i];
                    preferred_span_columns = widget->column_span;
                    preferred_span_rows = widget->row_span;
                    break;
                }
            }
        }
        if (icon == NULL && widget == NULL) {
            continue;
        }

        action_layout_drag_take_snapshot();
        if (preferred_column >= 0 && preferred_row >= 0 &&
            preferred_column + preferred_span_columns <= APP_GRID_COLUMNS &&
            preferred_row + preferred_span_rows <= APP_GRID_ROWS) {
            placed = action_layout_drag_preview(
                icon != NULL ? icon->button : NULL,
                widget,
                preferred_column,
                preferred_row);
        }

        /* If the EEZ/NVS target cannot be made available, try every legal
         * rectangle. No object is committed at an invalid or overlapping cell. */
        if (!placed) {
            action_layout_drag_restore_snapshot();
            for (int32_t row = 0; row <= APP_GRID_ROWS - preferred_span_rows && !placed; row++) {
                for (int32_t column = 0;
                     column <= APP_GRID_COLUMNS - preferred_span_columns && !placed;
                     column++) {
                    if (column == preferred_column && row == preferred_row) {
                        continue;
                    }
                    action_layout_drag_take_snapshot();
                    placed = action_layout_drag_preview(
                        icon != NULL ? icon->button : NULL,
                        widget,
                        column,
                        row);
                    if (!placed) {
                        action_layout_drag_restore_snapshot();
                    }
                }
            }
        }

        if (!placed) {
            action_layout_drag_restore_snapshot();
            return ESP_ERR_NO_MEM;
        }
    }

    lv_obj_update_layout(objects.home);
    return ESP_OK;
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

static void action_layout_drag_take_snapshot(void)
{
    for (size_t i = 0; i < s_app_icon_count; i++) {
        s_layout_drag_snapshot.icon_columns[i] = s_app_icons[i].column;
        s_layout_drag_snapshot.icon_rows[i] = s_app_icons[i].row;
    }

    for (size_t i = 0; i < HOME_OBJECT_CAPACITY; i++) {
        s_layout_drag_snapshot.widget_columns[i] = s_home_widgets[i].column;
        s_layout_drag_snapshot.widget_rows[i] = s_home_widgets[i].row;
    }
}

static void action_layout_drag_restore_snapshot(void)
{
    for (size_t i = 0; i < s_app_icon_count; i++) {
        s_app_icons[i].column = s_layout_drag_snapshot.icon_columns[i];
        s_app_icons[i].row = s_layout_drag_snapshot.icon_rows[i];
        if (s_app_icons[i].button != NULL) {
            action_icon_set_grid_cell(
                s_app_icons[i].button,
                s_app_icons[i].column,
                s_app_icons[i].row);
        }
    }

    for (size_t i = 0; i < HOME_OBJECT_CAPACITY; i++) {
        if (s_home_widgets[i].object == NULL) {
            continue;
        }
        action_widget_set_grid_cell(
            &s_home_widgets[i],
            s_layout_drag_snapshot.widget_columns[i],
            s_layout_drag_snapshot.widget_rows[i]);
    }

    if (objects.home != NULL) {
        lv_obj_update_layout(objects.home);
    }
}

/*
 * Build one complete candidate layout:
 * - reserve the dragged object's candidate footprint;
 * - leave its original footprint vacant;
 * - identify every object covered by the candidate;
 * - place displaced widgets first, then displaced apps, into any available
 *   cells/rectangles (including the footprint vacated by the dragged object).
 * No live positions are changed unless the entire candidate can be placed.
 */
static bool action_layout_drag_preview(
    lv_obj_t *dragged_icon,
    home_widget_slot_t *dragged_widget,
    int32_t column,
    int32_t row)
{
    int32_t icon_columns[APP_ICON_CAPACITY];
    int32_t icon_rows[APP_ICON_CAPACITY];
    int32_t widget_columns[HOME_OBJECT_CAPACITY];
    int32_t widget_rows[HOME_OBJECT_CAPACITY];
    bool displaced_icons[APP_ICON_CAPACITY] = { false };
    bool displaced_widgets[HOME_OBJECT_CAPACITY] = { false };
    bool occupied[APP_GRID_ROWS][APP_GRID_COLUMNS] = { false };
    int32_t span_columns = dragged_widget != NULL
        ? dragged_widget->column_span : 1;
    int32_t span_rows = dragged_widget != NULL
        ? dragged_widget->row_span : 1;
    int32_t origin_column = -1;
    int32_t origin_row = -1;
    int32_t origin_span_columns = span_columns;
    int32_t origin_span_rows = span_rows;

    if ((dragged_icon == NULL) == (dragged_widget == NULL) ||
        column < 0 || row < 0 ||
        column + span_columns > APP_GRID_COLUMNS ||
        row + span_rows > APP_GRID_ROWS) {
        return false;
    }

    if (dragged_widget != NULL) {
        size_t widget_index = (size_t)(dragged_widget - s_home_widgets);
        if (widget_index >= HOME_OBJECT_CAPACITY) {
            return false;
        }
        origin_column = s_layout_drag_snapshot.widget_columns[widget_index];
        origin_row = s_layout_drag_snapshot.widget_rows[widget_index];
    } else {
        bool found_dragged_icon = false;
        for (size_t i = 0; i < s_app_icon_count; i++) {
            if (s_app_icons[i].button == dragged_icon) {
                origin_column = s_layout_drag_snapshot.icon_columns[i];
                origin_row = s_layout_drag_snapshot.icon_rows[i];
                found_dragged_icon = true;
                break;
            }
        }
        if (!found_dragged_icon) {
            return false;
        }
    }

    for (size_t i = 0; i < s_app_icon_count; i++) {
        icon_columns[i] = s_layout_drag_snapshot.icon_columns[i];
        icon_rows[i] = s_layout_drag_snapshot.icon_rows[i];
        if (s_app_icons[i].button == dragged_icon) {
            icon_columns[i] = column;
            icon_rows[i] = row;
            continue;
        }
        if (s_app_icons[i].button == NULL) {
            continue;
        }
        displaced_icons[i] =
            action_cell_in_rect(icon_columns[i], icon_rows[i],
                column, row, span_columns, span_rows);
    }

    for (size_t i = 0; i < HOME_OBJECT_CAPACITY; i++) {
        widget_columns[i] = s_layout_drag_snapshot.widget_columns[i];
        widget_rows[i] = s_layout_drag_snapshot.widget_rows[i];
        if (&s_home_widgets[i] == dragged_widget) {
            widget_columns[i] = column;
            widget_rows[i] = row;
            continue;
        }
        if (s_home_widgets[i].object == NULL ||
            widget_columns[i] < 0 || widget_rows[i] < 0) {
            continue;
        }

        for (int32_t y = row; y < row + span_rows; y++) {
            for (int32_t x = column; x < column + span_columns; x++) {
                if (action_cell_in_rect(x, y,
                        widget_columns[i], widget_rows[i],
                        s_home_widgets[i].column_span,
                        s_home_widgets[i].row_span)) {
                    displaced_widgets[i] = true;
                }
            }
        }
    }

    /* Reserve the dragged object's destination before placing anything else. */
    for (int32_t y = row; y < row + span_rows; y++) {
        for (int32_t x = column; x < column + span_columns; x++) {
            occupied[y][x] = true;
        }
    }

    /* Keep all objects not covered by the candidate at their original cells. */
    for (size_t i = 0; i < s_app_icon_count; i++) {
        if (s_app_icons[i].button == NULL ||
            s_app_icons[i].button == dragged_icon ||
            displaced_icons[i] || icon_columns[i] < 0 || icon_rows[i] < 0) {
            continue;
        }
        int32_t x = icon_columns[i];
        int32_t y = icon_rows[i];
        if (x < 0 || x >= APP_GRID_COLUMNS ||
            y < 0 || y >= APP_GRID_ROWS || occupied[y][x]) {
            return false;
        }
        occupied[y][x] = true;
    }

    for (size_t i = 0; i < HOME_OBJECT_CAPACITY; i++) {
        if (s_home_widgets[i].object == NULL ||
            &s_home_widgets[i] == dragged_widget ||
            displaced_widgets[i] || widget_columns[i] < 0 || widget_rows[i] < 0) {
            continue;
        }
        int32_t x0 = widget_columns[i];
        int32_t y0 = widget_rows[i];
        int32_t x1 = x0 + s_home_widgets[i].column_span;
        int32_t y1 = y0 + s_home_widgets[i].row_span;
        if (x0 < 0 || y0 < 0 ||
            x1 > APP_GRID_COLUMNS || y1 > APP_GRID_ROWS) {
            return false;
        }
        for (int32_t y = y0; y < y1; y++) {
            for (int32_t x = x0; x < x1; x++) {
                if (occupied[y][x]) {
                    return false;
                }
                occupied[y][x] = true;
            }
        }
    }

    /* Widgets retain their full footprint, so allocate them before single-cell apps. */
    for (size_t i = 0; i < HOME_OBJECT_CAPACITY; i++) {
        if (!displaced_widgets[i]) {
            continue;
        }

        home_widget_slot_t *moving = &s_home_widgets[i];
        bool found = false;
        for (int32_t pass = 0; pass < 2 && !found; pass++) {
            for (int32_t y = 0;
                 y <= APP_GRID_ROWS - moving->row_span && !found; y++) {
                for (int32_t x = 0;
                     x <= APP_GRID_COLUMNS - moving->column_span; x++) {
                    bool inside_origin =
                        x >= origin_column && y >= origin_row &&
                        x + moving->column_span <=
                            origin_column + origin_span_columns &&
                        y + moving->row_span <=
                            origin_row + origin_span_rows;
                    if ((pass == 0 && !inside_origin) ||
                        (pass == 1 && inside_origin)) {
                        continue;
                    }

                    bool free_rect = true;
                    for (int32_t yy = y;
                         yy < y + moving->row_span && free_rect; yy++) {
                        for (int32_t xx = x;
                             xx < x + moving->column_span; xx++) {
                            if (occupied[yy][xx]) {
                                free_rect = false;
                                break;
                            }
                        }
                    }
                    if (!free_rect) {
                        continue;
                    }

                    widget_columns[i] = x;
                    widget_rows[i] = y;
                    for (int32_t yy = y;
                         yy < y + moving->row_span; yy++) {
                        for (int32_t xx = x;
                             xx < x + moving->column_span; xx++) {
                            occupied[yy][xx] = true;
                        }
                    }
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return false;
        }
    }

    /* Place each displaced app into the first remaining free cell. */
    for (size_t i = 0; i < s_app_icon_count; i++) {
        if (!displaced_icons[i]) {
            continue;
        }

        bool found = false;
        for (int32_t pass = 0; pass < 2 && !found; pass++) {
            for (int32_t y = 0; y < APP_GRID_ROWS && !found; y++) {
                for (int32_t x = 0; x < APP_GRID_COLUMNS; x++) {
                    bool inside_origin =
                        x >= origin_column &&
                        x < origin_column + origin_span_columns &&
                        y >= origin_row &&
                        y < origin_row + origin_span_rows;
                    if ((pass == 0 && !inside_origin) ||
                        (pass == 1 && inside_origin) ||
                        occupied[y][x]) {
                        continue;
                    }

                    icon_columns[i] = x;
                    icon_rows[i] = y;
                    occupied[y][x] = true;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return false;
        }
    }

    /* Apply only after every displaced object has a valid destination. */
    for (size_t i = 0; i < s_app_icon_count; i++) {
        if (s_app_icons[i].button == NULL) {
            continue;
        }
        s_app_icons[i].column = icon_columns[i];
        s_app_icons[i].row = icon_rows[i];
        action_icon_set_grid_cell(
            s_app_icons[i].button, icon_columns[i], icon_rows[i]);
    }

    for (size_t i = 0; i < HOME_OBJECT_CAPACITY; i++) {
        if (s_home_widgets[i].object == NULL) {
            continue;
        }
        action_widget_set_grid_cell(
            &s_home_widgets[i], widget_columns[i], widget_rows[i]);
    }

    if (objects.home != NULL) {
        lv_obj_update_layout(objects.home);
    }
    return true;
}

static void action_widget_drag_restore_snapshot(void)
{
    action_layout_drag_restore_snapshot();
}

static void action_widget_drag_take_snapshot(void)
{
    action_layout_drag_take_snapshot();
}

static void action_widget_drag_finish(home_widget_slot_t *widget)
{
    if (widget == NULL || !s_widget_drag.active ||
        s_widget_drag.widget != widget) {
        return;
    }

    if (!s_widget_drag.preview_valid) {
        action_widget_drag_restore_snapshot();
    }

    lv_obj_set_style_transform_scale_x(
        widget->object, 256, LV_PART_MAIN);
    lv_obj_set_style_transform_scale_y(
        widget->object, 256, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(
        widget->object, LV_STYLE_X, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(
        widget->object, LV_STYLE_Y, LV_PART_MAIN);
    lv_obj_remove_flag(widget->object, LV_OBJ_FLAG_IGNORE_LAYOUT);

    if (objects.home != NULL) {
        lv_obj_update_layout(objects.home);
    }

    s_widget_drag.active = false;
    s_widget_drag.widget = NULL;
    s_widget_drag.indev = NULL;
    action_home_layout_save();
}

static void action_widget_drag_pressed(lv_event_t *event)
{
    lv_indev_t *indev = lv_event_get_indev(event);

    /* One active drag owns the entire Home screen until it is released. */
    if (indev == NULL || s_icon_drag.active || s_widget_drag.active) {
        lv_event_stop_processing(event);
        return;
    }
}

static void action_widget_drag_long_pressed(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    home_widget_slot_t *widget = NULL;
    int32_t column;
    int32_t row;

    /* Never allow another pointer or object to start a second drag. */
    if (indev == NULL || s_icon_drag.active || s_widget_drag.active) {
        lv_event_stop_processing(event);
        return;
    }

    for (size_t i = 0; i < HOME_OBJECT_CAPACITY; i++) {
        if (s_home_widgets[i].object == object) {
            widget = &s_home_widgets[i];
            break;
        }
    }
    if (widget == NULL) {
        return;
    }

    lv_obj_remove_local_style_prop(object, LV_STYLE_X, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(object, LV_STYLE_Y, LV_PART_MAIN);
    lv_obj_update_layout(objects.home);

    lv_indev_get_point(indev, &s_widget_drag.start_point);
    if (!action_icon_get_cell_from_point(
            s_widget_drag.start_point, &column, &row)) {
        return;
    }

    s_widget_drag.widget = widget;
    s_widget_drag.indev = indev;
    s_widget_drag.start_x = lv_obj_get_x(object);
    s_widget_drag.start_y = lv_obj_get_y(object);
    s_widget_drag.grab_column_offset = column - widget->column;
    s_widget_drag.grab_row_offset = row - widget->row;
    action_widget_drag_take_snapshot();
    s_widget_drag.preview_valid = true;
    s_widget_drag.active = true;

    lv_obj_add_flag(object, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(object, s_widget_drag.start_x, s_widget_drag.start_y);
    lv_obj_move_foreground(object);

    lv_obj_set_style_transform_pivot_x(
        object, lv_obj_get_width(object) / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(
        object, lv_obj_get_height(object) / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_scale_x(object, 280, LV_PART_MAIN);
    lv_obj_set_style_transform_scale_y(object, 280, LV_PART_MAIN);
}

static void action_widget_drag_pressing(lv_event_t *event)
{
    home_widget_slot_t *widget = s_widget_drag.widget;
    lv_indev_t *indev = lv_event_get_indev(event);
    lv_point_t point;
    int32_t column;
    int32_t row;

    if (!s_widget_drag.active || widget == NULL ||
        indev == NULL || indev != s_widget_drag.indev) {
        return;
    }

    lv_indev_get_point(indev, &point);
    lv_coord_t new_x = s_widget_drag.start_x +
        point.x - s_widget_drag.start_point.x;
    lv_coord_t new_y = s_widget_drag.start_y +
        point.y - s_widget_drag.start_point.y;

    if (lv_obj_get_x(widget->object) != new_x ||
        lv_obj_get_y(widget->object) != new_y) {
        lv_obj_set_pos(widget->object, new_x, new_y);
    }

    if (!action_icon_get_cell_from_point(point, &column, &row)) {
        action_widget_drag_restore_snapshot();
        s_widget_drag.preview_valid = false;
        return;
    }

    column -= s_widget_drag.grab_column_offset;
    row -= s_widget_drag.grab_row_offset;

    /*
     * Keep the current preview while the pointer remains in its cell.
     * When it enters another cell, rebuild the preview from the original
     * layout so items displaced by the previous preview return to their
     * original positions before the new candidate is evaluated.
     */
    if (s_widget_drag.preview_valid &&
        column == widget->column && row == widget->row) {
        return;
    }

    s_widget_drag.preview_valid = action_layout_drag_preview(
        NULL, widget, column, row);
    if (!s_widget_drag.preview_valid) {
        action_widget_drag_restore_snapshot();
    }
}

static void action_widget_drag_released(lv_event_t *event)
{
    lv_indev_t *indev = lv_event_get_indev(event);
    if (s_widget_drag.active &&
        (indev == NULL || indev == s_widget_drag.indev)) {
        action_widget_drag_finish(s_widget_drag.widget);
    }
}

static void action_widget_drag_press_lost(lv_event_t *event)
{
    action_widget_drag_released(event);
}

static void action_widget_block_click_during_icon_drag(lv_event_t *event)
{
    if (s_icon_drag.active || s_widget_drag.active) {
        lv_event_stop_processing(event);
    }
}

static void action_widget_enable_event_bubble(lv_obj_t *object)
{
    uint32_t child_count;

    if (object == NULL) {
        return;
    }

    child_count = lv_obj_get_child_cnt(object);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(object, (int32_t)i);
        if (child == NULL) {
            continue;
        }
        lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(
            child, action_widget_block_click_during_icon_drag,
            LV_EVENT_CLICKED, NULL);
        action_widget_enable_event_bubble(child);
    }
}

static esp_err_t action_widget_drag_attach(lv_obj_t *object)
{
    if (object == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Make empty widget areas touchable and bubble gestures from nested
     * controls to this root. Short clicks still reach their original child.
     */
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(
        object, action_widget_block_click_during_icon_drag,
        LV_EVENT_CLICKED, NULL);
    action_widget_enable_event_bubble(object);
    lv_obj_add_event_cb(
        object, action_widget_drag_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(
        object, action_widget_drag_long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(
        object, action_widget_drag_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(
        object, action_widget_drag_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(
        object, action_widget_drag_press_lost, LV_EVENT_PRESS_LOST, NULL);
    return ESP_OK;
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

    if (!s_icon_drag.preview_valid) {
        action_layout_drag_restore_snapshot();
    }

    image = lv_obj_get_child(button, 1);
    if (image != NULL) {
        lv_obj_set_style_transform_scale_x(image, 256, LV_PART_MAIN);
        lv_obj_set_style_transform_scale_y(image, 256, LV_PART_MAIN);
    }

    /*
     * The shared preview planner has already placed every displaced object.
     * Commit only the dragged icon's candidate when the preview is valid.
     */
    app_icon_slot_t *slot = action_icon_find_slot(button);
    if (slot != NULL && s_icon_drag.preview_valid) {
        slot->column = s_icon_drag.current_column;
        slot->row = s_icon_drag.current_row;
        action_icon_set_grid_cell(
            button, slot->column, slot->row);
    }

    /*
     * lv_obj_set_pos() stores X/Y as local style properties. Grid layout
     * updates object coordinates directly but does not clear those styles.
     * Leaving them behind lets a later style/layout refresh resurrect the
     * previous drag coordinates when IGNORE_LAYOUT is enabled again.
     */
    lv_obj_remove_local_style_prop(button, LV_STYLE_X, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(button, LV_STYLE_Y, LV_PART_MAIN);

    lv_obj_remove_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT);
    if (objects.home != NULL) {
        lv_obj_update_layout(objects.home);
    }

    s_icon_drag.active = false;
    s_icon_drag.button = NULL;
    action_home_layout_save();
    s_icon_drag.indev = NULL;
}

static void action_icon_pressed(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target(event);
    lv_indev_t *indev = lv_event_get_indev(event);

    /*
     * A new press from the same pointer means the previous gesture has
     * ended, even if RELEASED/PRESS_LOST was missed. Close that stale drag
     * before the next long-press can reuse its old anchor coordinates.
     * Do not let a second pointer steal an active drag.
     */
    /* Ignore all additional presses while any App/Widget drag is active. */
    if (indev == NULL || s_icon_drag.active || s_widget_drag.active) {
        lv_event_stop_processing(event);
        return;
    }

    s_icon_drag.long_pressed = false;
    action_icon_set_button_color(button, true);
}

static void action_icon_clicked(lv_event_t *event)
{
    /*
     * LVGL sends LV_EVENT_CLICKED on release even after LONG_PRESSED.
     * A long press is the drag gesture, not a normal click.
     */
    if (s_icon_drag.long_pressed || s_icon_drag.active || s_widget_drag.active) {
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

    indev = lv_event_get_indev(event);
    if (indev == NULL || s_icon_drag.active || s_widget_drag.active) {
        lv_event_stop_processing(event);
        return;
    }

    /*
     * Only the pointer that started a drag may finish/recover it. A second
     * pointer must not replace the active gesture or its start coordinates.
     */
    if (s_icon_drag.active) {
        if (s_icon_drag.indev != indev) {
            return;
        }
        action_icon_finish(s_icon_drag.button);
    }

    slot = action_icon_find_slot(button);
    if (slot == NULL || lv_obj_get_parent(button) != objects.home) {
        return;
    }

    /*
     * EEZ-generated Grid children also receive an initial lv_obj_set_pos().
     * Those X/Y local styles remain after Grid has positioned the child.
     * Clear them while Grid still owns the position; otherwise enabling
     * IGNORE_LAYOUT below can briefly restore the generated coordinates
     * (often 0,0) before the first PRESSING event moves the icon.
     */
    lv_obj_remove_local_style_prop(button, LV_STYLE_X, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(button, LV_STYLE_Y, LV_PART_MAIN);

    /* Resolve Grid layout after clearing generated X/Y, then take the anchor. */
    lv_obj_update_layout(objects.home);

    s_icon_drag.button = button;
    s_icon_drag.indev = indev;
    s_icon_drag.long_pressed = true;
    s_icon_drag.start_x = lv_obj_get_x(button);
    s_icon_drag.start_y = lv_obj_get_y(button);
    s_icon_drag.current_column = slot->column;
    s_icon_drag.current_row = slot->row;
    lv_indev_get_point(indev, &s_icon_drag.start_point);
    action_layout_drag_take_snapshot();
    s_icon_drag.preview_valid = true;
    s_icon_drag.active = true;

    /*
     * A Grid normally owns the position of its children. Ignore its layout
     * only while this icon is being dragged, so it can float under the
     * finger without changing the generated EEZ layout.
     */
    lv_obj_add_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_foreground(button);

    /*
     * Re-apply the exact Grid-resolved anchor immediately after detaching
     * this child from Grid positioning. This prevents the first rendered
     * frame of the enlarged shortcut from using stale generated X/Y values.
     */
    lv_obj_set_pos(button, s_icon_drag.start_x, s_icon_drag.start_y);

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
        action_layout_drag_restore_snapshot();
        s_icon_drag.preview_valid = false;
        s_icon_drag.current_column = -1;
        s_icon_drag.current_row = -1;
        return;
    }

    if (column == s_icon_drag.current_column &&
        row == s_icon_drag.current_row) {
        return;
    }

    s_icon_drag.current_column = column;
    s_icon_drag.current_row = row;
    s_icon_drag.preview_valid = action_layout_drag_preview(
        s_icon_drag.button, NULL, column, row);

    if (!s_icon_drag.preview_valid) {
        action_layout_drag_restore_snapshot();
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
    lv_obj_t *button = lv_event_get_current_target(event);
    lv_indev_t *indev = lv_event_get_indev(event);

    if (s_icon_drag.active && s_icon_drag.button == button &&
        (indev == NULL || indev == s_icon_drag.indev)) {
        action_icon_finish(button);
    } else if (!s_icon_drag.active) {
        action_icon_set_button_color(button, false);
    }
}

static void action_icon_press_lost(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target(event);
    lv_indev_t *indev = lv_event_get_indev(event);

    if (s_icon_drag.active && s_icon_drag.button == button &&
        (indev == NULL || indev == s_icon_drag.indev)) {
        action_icon_finish(button);
    } else if (!s_icon_drag.active) {
        action_icon_set_button_color(button, false);
    }
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

static bool action_icon_is_home_app(lv_obj_t *object)
{
    lv_obj_t *image;

    if (object == NULL || !lv_obj_check_type(object, &lv_button_class) ||
        lv_obj_get_child_cnt(object) < 2) {
        return false;
    }

    image = lv_obj_get_child(object, 1);
    return image != NULL &&
        lv_obj_check_type(image, &lv_image_class) &&
        action_home_get_identifier(object) != NULL;
}

static void action_icon_discover_home_apps(void)
{
    uint32_t child_count = lv_obj_get_child_cnt(objects.home);
    s_app_icon_count = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(objects.home, (int32_t)i);
        if (!action_icon_is_home_app(child)) {
            continue;
        }
        if (s_app_icon_count >= APP_ICON_CAPACITY) {
            s_home_discovery_overflow = true;
            continue;
        }

        s_app_icons[s_app_icon_count].button = child;
        s_app_icons[s_app_icon_count].identifier = action_home_get_identifier(child);
        if (s_app_icons[s_app_icon_count].identifier == NULL) {
            continue;
        }
        s_app_icons[s_app_icon_count].column =
            lv_obj_get_style_grid_cell_column_pos(child, LV_PART_MAIN);
        s_app_icons[s_app_icon_count].row =
            lv_obj_get_style_grid_cell_row_pos(child, LV_PART_MAIN);
        s_app_icons[s_app_icon_count].default_column =
            s_app_icons[s_app_icon_count].column;
        s_app_icons[s_app_icon_count].default_row =
            s_app_icons[s_app_icon_count].row;
        s_app_icons[s_app_icon_count].restored = false;
        s_app_icon_count++;
    }
}

static void action_widget_discover_home_widgets(void)
{
    uint32_t child_count = lv_obj_get_child_cnt(objects.home);
    s_home_widget_count = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(objects.home, (int32_t)i);

        if (child == NULL || action_icon_is_home_app(child)) {
            continue;
        }

        const char *identifier = action_home_get_identifier(child);
        if (identifier == NULL) {
            continue;
        }
        if (s_home_widget_count >= HOME_OBJECT_CAPACITY) {
            s_home_discovery_overflow = true;
            continue;
        }

        home_widget_slot_t *widget = &s_home_widgets[s_home_widget_count++];
        widget->object = child;
        widget->identifier = identifier;
        widget->column = lv_obj_get_style_grid_cell_column_pos(
            child, LV_PART_MAIN);
        widget->row = lv_obj_get_style_grid_cell_row_pos(
            child, LV_PART_MAIN);
        widget->column_span = lv_obj_get_style_grid_cell_column_span(
            child, LV_PART_MAIN);
        widget->row_span = lv_obj_get_style_grid_cell_row_span(
            child, LV_PART_MAIN);
        if (widget->column_span < 1) widget->column_span = 1;
        if (widget->row_span < 1) widget->row_span = 1;
        widget->default_column = widget->column;
        widget->default_row = widget->row;
        widget->restored = false;
    }
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
        s_widget_drag.widget = NULL;
    s_widget_drag.indev = NULL;
    s_widget_drag.active = false;

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

    if (objects.home == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        return err;
    }

    lv_obj_update_layout(objects.home);
    s_home_discovery_overflow = false;
    action_icon_discover_home_apps();
    action_widget_discover_home_widgets();
    if (s_home_discovery_overflow ||
        s_app_icon_count + s_home_widget_count > HOME_LAYOUT_ITEM_CAPACITY) {
        return ESP_ERR_NO_MEM;
    }
    if (s_app_icon_count == 0 && s_home_widget_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < s_home_widget_count; i++) {
        home_widget_slot_t *widget = &s_home_widgets[i];
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

    action_home_layout_restore();
    err = action_home_layout_place_objects();
    if (err != ESP_OK) return err;
    action_home_layout_save();

    s_control_center_drag.target = NULL;
    s_control_center_drag.indev = NULL;
    s_control_center_drag.active = false;
    s_control_center_opa_initialized = false;

    for (size_t i = 0; i < s_app_icon_count; i++) {
        err = action_icon_attach(s_app_icons[i].button);
        if (err != ESP_OK) return err;
    }

    for (size_t i = 0; i < s_home_widget_count; i++) {
        err = action_widget_drag_attach(s_home_widgets[i].object);
        if (err != ESP_OK) return err;
    }

    err = action_control_center_attach(objects.control_center_overlay);
    if (err != ESP_OK) return err;
    err = action_control_center_attach(objects.control_center_touch_area);
    if (err != ESP_OK) return err;

    return ESP_OK;
}
