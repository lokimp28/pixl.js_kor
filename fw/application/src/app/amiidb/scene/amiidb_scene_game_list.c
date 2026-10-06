#include "amiibo_helper.h"
#include "amiidb_scene.h"
#include "app_amiidb.h"
#include "mui_list_view.h"
#include "nrf_log.h"

#include "db_header.h"
#include "mui_icons.h"
#include "settings.h"
#include <math.h>

static void amiidb_scene_game_list_reload(app_amiidb_t *app);

static const char *amiidb_game_display_name(const db_game_t *p_game) {
    settings_data_t *p_settings_data = settings_get_data();
    return (p_settings_data->language == LANGUAGE_KO_KR && p_game->name_cn[0] != '\0')
               ? p_game->name_cn
               : p_game->name_en;
}

static uint8_t amiidb_game_korean_priority(const db_game_t *p_game) {
    switch (p_game->game_id) {
    case 11: return 0;  // 동물의 숲
    case 76: return 1;  // 슈퍼 마리오
    case 77: return 2;  // 젤다의 전설
    case 47: return 3;  // 스매시브라더스
    case 1:  return 0;  // 젤다: 티어스 오브 더 킹덤
    case 4:  return 1;  // 젤다: 브레스 오브 더 와일드
    default: return 0xff;
    }
}

static void amiidb_scene_game_list_list_view_on_selected(mui_list_view_event_t event, mui_list_view_t *p_list_view,
                                                         mui_list_item_t *p_item) {
    uint16_t icon = p_item->icon;
    app_amiidb_t *app = mui_list_view_get_user_data(p_list_view);
    switch (icon) {
    case ICON_EXIT:
        if (app->game_id_index <= 0) {
            mui_scene_dispatcher_next_scene(app->p_scene_dispatcher, AMIIDB_SCENE_MAIN);
        } else {
            app->game_id_index--;
            amiidb_scene_game_list_reload(app);
        }
        break;

    case ICON_FOLDER: {
        const db_game_t *p_game = p_item->user_data;
        app->game_id_path[++app->game_id_index] = p_game->game_id;
        amiidb_scene_game_list_reload(app);
    } break;

    case ICON_FILE: {
        const db_amiibo_t *p_amiibo = p_item->user_data;

        if(is_valid_amiibo_v3(p_amiibo->head, p_amiibo->tail)){
            mui_toast_view_show(app->p_toast_view, _T(APP_AMIIDB_NOT_SUPPORT_V3));
        }else{
            app->cur_amiibo = p_amiibo;
            app->cur_slot_index = mui_list_view_get_focus(p_list_view);
            app->prev_scene_id = AMIIDB_SCENE_GAME_LIST;
            mui_scene_dispatcher_next_scene(app->p_scene_dispatcher, AMIIDB_SCENE_AMIIBO_DETAIL);
        }
    } break;
    }
}

static int amiidb_scene_game_list_list_view_sort_cb(const mui_list_item_t *p_item_a, const mui_list_item_t *p_item_b) {
    if (p_item_a->icon == ICON_FOLDER && p_item_b->icon == ICON_FOLDER) {
        db_game_t *p_game_a = (db_game_t *)p_item_a->user_data;
        db_game_t *p_game_b = (db_game_t *)p_item_b->user_data;
        settings_data_t *p_settings_data = settings_get_data();
        if (p_settings_data->language == LANGUAGE_KO_KR) {
            uint8_t priority_a = amiidb_game_korean_priority(p_game_a);
            uint8_t priority_b = amiidb_game_korean_priority(p_game_b);
            if (priority_a != priority_b) {
                return (int)priority_a - (int)priority_b;
            }
            return strcmp(amiidb_game_display_name(p_game_a), amiidb_game_display_name(p_game_b));
        } else if (p_settings_data->amiidb_sort_column == AMIIDB_SORT_COLUMN_NAME) {
            return strcmp(amiidb_game_display_name(p_game_a), amiidb_game_display_name(p_game_b));
        } else {
            return p_game_b->order - p_game_a->order;
        }
    } else if (p_item_a->icon == ICON_FOLDER) {
        return -1;
    } else if (p_item_b->icon == ICON_FOLDER) {
        return 1;
    } else if (p_item_a->icon == ICON_FILE && p_item_b->icon == ICON_FILE) {
        db_amiibo_t *p_amiibo_a = (db_amiibo_t *)p_item_a->user_data;
        db_amiibo_t *p_amiibo_b = (db_amiibo_t *)p_item_b->user_data;
        return strcmp(get_amiibo_display_name(p_amiibo_a), get_amiibo_display_name(p_amiibo_b));
    }
    return 0;
}

static void amiidb_scene_game_list_reload(app_amiidb_t *app) {
    settings_data_t *p_settings_data = settings_get_data();
    char txt[64];

    // clear list view
    mui_list_view_clear_items(app->p_list_view);

    // add game list TODO FIX THIS
    uint8_t cur_game_id = app->game_id_path[app->game_id_index];
    const db_game_t *p_game = game_list;
    while (p_game->game_id > 0) {
        if (p_game->parent_game_id == cur_game_id && p_game->link_cnt > 0) {
            sprintf(txt, "%s (%d)", amiidb_game_display_name(p_game),
                    p_game->link_cnt);
            mui_list_view_add_item(app->p_list_view, ICON_FOLDER, txt, (void *)p_game);
        }
        p_game++;
    }
    // Sort only the small folder list at runtime. Amiibo rows are pre-sorted in db_link.
    mui_list_view_sort(app->p_list_view, amiidb_scene_game_list_list_view_sort_cb);

    // add link list by page
    const db_link_t *p_link = link_list;
    uint16_t link_cnt = 0;
    uint16_t add_cnt = 0;
    while (p_link->game_id > 0) {
        if (p_link->game_id == cur_game_id) {
            link_cnt++;
            if (add_cnt < LIST_VIEW_ITEM_MAX_COUNT) {
                const db_amiibo_t *p_amiibo = get_amiibo_by_id(p_link->head, p_link->tail);
                if (p_amiibo) {
                    const char *name = get_amiibo_display_name(p_amiibo);
                    // Korean-only per-folder display override. NFC identity remains the shared amiibo record.
                    if (p_settings_data->language == LANGUAGE_KO_KR && p_link->note_cn[0] != '\0') {
                        name = p_link->note_cn;
                    }
                    mui_list_view_add_item(app->p_list_view, ICON_FILE, name, (void *)p_amiibo);
                } else {
                    sprintf(txt, "Amiibo[%08x:%08x]", p_link->head, p_link->tail);
                    mui_list_view_add_item(app->p_list_view, ICON_FILE, txt, (void *)p_amiibo);
                }
                add_cnt++;
            }
        }
        p_link++;
    }


    if (link_cnt > LIST_VIEW_ITEM_MAX_COUNT) {
        mui_list_view_add_item(app->p_list_view, ICON_ERROR, getLangString(_L_APP_AMIIDB_MORE), (void *)0);
    }

    mui_list_view_add_item(app->p_list_view, ICON_EXIT, getLangString(_L_APP_AMIIDB_BACK), (void *)0);
    mui_list_view_set_selected_cb(app->p_list_view, amiidb_scene_game_list_list_view_on_selected);
    mui_list_view_set_user_data(app->p_list_view, app);
    mui_list_view_set_focus(app->p_list_view, 0);
    mui_list_view_set_scroll_offset(app->p_list_view, 0);
    mui_view_dispatcher_switch_to_view(app->p_view_dispatcher, AMIIDB_VIEW_ID_LIST);
}

void amiidb_scene_game_list_on_enter(void *user_data) {
    app_amiidb_t *app = (app_amiidb_t *)user_data;
    amiidb_scene_game_list_reload(app);

    // restore states
    mui_list_view_set_focus(app->p_list_view, app->cur_focus_index);
    mui_list_view_set_scroll_offset(app->p_list_view, app->cur_scroll_offset);
}

void amiidb_scene_game_list_on_exit(void *user_data) {
    app_amiidb_t *app = (app_amiidb_t *)user_data;
    mui_list_view_clear_items(app->p_list_view);
}