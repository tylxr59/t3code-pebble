#include <pebble.h>

#define MAX_PROJECTS 30
#define MAX_THREADS 40
#define MAX_MESSAGES 40
#define ID_LEN 96
#define TITLE_LEN 96
#define TEXT_LEN 640
#define LIST_ROW_HEIGHT 42
#define LIST_TOP_PADDING 2
#define LIST_SIDE_PADDING 8
#define LIST_MARKER_WIDTH 8
#define COLOR_BAR_HEIGHT 3

#define CMD_CONFIG 1
#define CMD_PROJECTS 2
#define CMD_THREADS 3
#define CMD_MESSAGES 4
#define CMD_SEND 5
#define CMD_ITEM 20
#define CMD_DONE 21
#define CMD_STATUS 22
#define CMD_ERROR 23

typedef enum {
  VIEW_PROJECTS,
  VIEW_THREADS,
  VIEW_MESSAGES,
  VIEW_EXPANDED
} View;

typedef struct {
  char id[ID_LEN];
  char title[TITLE_LEN];
  char status[32];
} ProjectItem;

typedef struct {
  char id[ID_LEN];
  char title[TITLE_LEN];
  char status[32];
} ThreadItem;

typedef struct {
  char id[ID_LEN];
  char role[16];
  char text[TEXT_LEN];
  char status[32];
} MessageItem;

static Window *s_window;
static Layer *s_accent_layer;
static Layer *s_loading_layer;
static Layer *s_list_layer;
static Layer *s_message_layer;
static Layer *s_blank_layer;
static Layer *s_footer_bg_layer;
static TextLayer *s_header_layer;
static TextLayer *s_body_layer;
static TextLayer *s_footer_layer;
static DictationSession *s_dictation;
static AppTimer *s_loading_timer;
static AppTimer *s_request_timer;

static View s_view = VIEW_PROJECTS;
static uint32_t s_seq = 1;
static bool s_loading = false;
static bool s_refresh_after_send = false;
static int s_selected = 0;
static int s_scroll = 0;
static int s_list_first = 0;
static int s_loading_frame = 0;

static ProjectItem s_projects[MAX_PROJECTS];
static ThreadItem s_threads[MAX_THREADS];
static MessageItem s_messages[MAX_MESSAGES];
static int s_project_count = 0;
static int s_thread_count = 0;
static int s_message_count = 0;
static char s_project_id[ID_LEN];
static char s_thread_id[ID_LEN];
static char s_status[96] = "Loading";

static void prv_request_projects(void);
static void prv_request_threads(void);
static void prv_request_messages(void);
static void prv_render(void);
static void prv_end_load(void);
static void prv_sync_loading_timer(void);
static void prv_request_timeout(void *context);

static const GColor s_accent_colors[] = {
  { .argb = GColorPictonBlueARGB8 },
  { .argb = GColorCelesteARGB8 },
  { .argb = GColorMintGreenARGB8 },
  { .argb = GColorIcterineARGB8 },
  { .argb = GColorRajahARGB8 },
};

static void prv_copy_cstr(char *dest, size_t len, const char *src) {
  if (!dest || len == 0) return;
  snprintf(dest, len, "%s", src ? src : "");
}

static void prv_send_request(uint8_t command) {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK) {
    prv_copy_cstr(s_status, sizeof(s_status), "Phone message busy");
    prv_end_load();
    prv_render();
    return;
  }
  dict_write_uint8(iter, MESSAGE_KEY_Command, command);
  dict_write_uint32(iter, MESSAGE_KEY_Seq, s_seq);
  if (command == CMD_THREADS) {
    dict_write_cstring(iter, MESSAGE_KEY_ProjectId, s_project_id);
  } else if (command == CMD_MESSAGES || command == CMD_SEND) {
    dict_write_cstring(iter, MESSAGE_KEY_ThreadId, s_thread_id);
  }
  dict_write_end(iter);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    prv_copy_cstr(s_status, sizeof(s_status), "Phone send failed");
    prv_end_load();
    prv_render();
  }
}

static void prv_send_text(uint8_t command, const char *text) {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK) {
    prv_copy_cstr(s_status, sizeof(s_status), "Phone message busy");
    prv_end_load();
    prv_render();
    return;
  }
  dict_write_uint8(iter, MESSAGE_KEY_Command, command);
  dict_write_uint32(iter, MESSAGE_KEY_Seq, s_seq);
  dict_write_cstring(iter, MESSAGE_KEY_ThreadId, s_thread_id);
  dict_write_cstring(iter, MESSAGE_KEY_ProjectId, s_project_id);
  dict_write_cstring(iter, MESSAGE_KEY_Text, text ? text : "");
  dict_write_end(iter);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    prv_copy_cstr(s_status, sizeof(s_status), "Phone send failed");
    prv_end_load();
    prv_render();
  }
}

static int prv_current_count(void) {
  if (s_view == VIEW_PROJECTS) return s_project_count;
  if (s_view == VIEW_THREADS) return s_thread_count + 1;
  if (s_view == VIEW_MESSAGES || s_view == VIEW_EXPANDED) return s_message_count;
  return 0;
}

static bool prv_is_user_message(const MessageItem *msg) {
  return msg && strcmp(msg->role, "user") == 0;
}

static const char *prv_message_label(const MessageItem *msg) {
  return prv_is_user_message(msg) ? "You" : "AI";
}

static const char *prv_title_for_selection(void) {
  if (s_view == VIEW_PROJECTS && s_project_count > 0) return s_projects[s_selected].title;
  if (s_view == VIEW_THREADS && s_selected > 0 && s_selected <= s_thread_count) {
    return s_threads[s_selected - 1].title;
  }
  if ((s_view == VIEW_MESSAGES || s_view == VIEW_EXPANDED) && s_message_count > 0) {
    return prv_message_label(&s_messages[s_selected]);
  }
  return "";
}

static bool prv_is_list_view(void) {
  return !s_loading && (s_view == VIEW_PROJECTS || s_view == VIEW_THREADS);
}

static bool prv_is_nav_loading(void) {
  return s_loading && (s_view == VIEW_PROJECTS || s_view == VIEW_THREADS ||
                       (s_view == VIEW_MESSAGES && strcmp(s_status, "Loading messages") == 0));
}

static bool prv_is_message_view(void) {
  return !s_loading && s_view == VIEW_MESSAGES && s_message_count > 0;
}

static bool prv_is_blank_thread_view(void) {
  return !s_loading && s_view == VIEW_MESSAGES && s_message_count == 0;
}

static const char *prv_list_title(int index) {
  if (s_view == VIEW_PROJECTS && index >= 0 && index < s_project_count) {
    return s_projects[index].title;
  }
  if (s_view == VIEW_THREADS) {
    if (index == 0) return "+";
    int thread_index = index - 1;
    if (thread_index >= 0 && thread_index < s_thread_count) {
      return s_threads[thread_index].title;
    }
  }
  return "";
}

static const char *prv_list_subtitle(int index) {
  if (s_view == VIEW_PROJECTS && index >= 0 && index < s_project_count) {
    return strlen(s_projects[index].status) > 0 ? s_projects[index].status : "Tap SELECT to view threads";
  }
  if (s_view == VIEW_THREADS) {
    if (index == 0) return "";
    int thread_index = index - 1;
    if (thread_index >= 0 && thread_index < s_thread_count) {
      return strlen(s_threads[thread_index].status) > 0 ? s_threads[thread_index].status : "Tap SELECT to view messages";
    }
  }
  return "";
}

static int prv_list_visible_rows(void) {
  if (!s_list_layer) return 4;
  GRect bounds = layer_get_bounds(s_list_layer);
  int visible_rows = bounds.size.h / LIST_ROW_HEIGHT;
  return visible_rows < 1 ? 1 : visible_rows;
}

static void prv_list_clamp_page(void) {
  int count = prv_current_count();
  int visible_rows = prv_list_visible_rows();
  if (count <= 0) {
    s_list_first = 0;
    s_selected = 0;
    return;
  }
  if (s_selected >= count) s_selected = count - 1;
  if (s_selected < 0) s_selected = 0;
  if (s_list_first > count - 1) s_list_first = count - 1;
  if (s_list_first < 0) s_list_first = 0;
  if (s_selected < s_list_first || s_selected >= s_list_first + visible_rows) {
    s_list_first = (s_selected / visible_rows) * visible_rows;
  }
}

static void prv_set_header(const char *title) {
  text_layer_set_text(s_header_layer, title);
}

static void prv_render(void) {
  static char body[900];
  static char footer[96];
  int count = prv_current_count();

  if (s_view == VIEW_PROJECTS) {
    prv_set_header("Projects");
  } else if (s_view == VIEW_THREADS) {
    prv_set_header("Threads");
  } else if (s_view == VIEW_MESSAGES) {
    prv_set_header("Messages");
  } else {
    prv_set_header(prv_title_for_selection());
  }

  if (prv_is_nav_loading()) {
    body[0] = '\0';
  } else if (s_loading) {
    snprintf(body, sizeof(body), "\n\n%s", s_status);
  } else if (count == 0) {
    if (s_view == VIEW_MESSAGES) {
      body[0] = '\0';
    } else {
      snprintf(body, sizeof(body), "\n\nNo items");
    }
  } else if (prv_is_list_view()) {
    body[0] = '\0';
  } else if (s_view == VIEW_EXPANDED) {
    const MessageItem *msg = &s_messages[s_selected];
    int len = strlen(msg->text);
    int start = s_scroll;
    if (start > len) start = len;
    snprintf(body, sizeof(body), "%s", msg->text + start);
  } else if (s_view == VIEW_MESSAGES) {
    body[0] = '\0';
  } else if (s_view == VIEW_THREADS) {
    const ThreadItem *thread = &s_threads[s_selected];
    if (strlen(thread->status) > 0) {
      snprintf(body, sizeof(body), "%s\n\nSession: %s", thread->title, thread->status);
    } else {
      snprintf(body, sizeof(body), "%s", thread->title);
    }
  } else {
    const ProjectItem *project = &s_projects[s_selected];
    snprintf(body, sizeof(body), "%s", project->title);
  }

  if (prv_is_nav_loading()) {
    footer[0] = '\0';
  } else if (s_view == VIEW_EXPANDED) {
    snprintf(footer, sizeof(footer), "UP/DN scroll  SEL back");
  } else if (count > 0) {
    if (s_view == VIEW_PROJECTS) {
      snprintf(footer, sizeof(footer), "%d/%d  SELECT opens threads", s_selected + 1, count);
    } else if (s_view == VIEW_THREADS) {
      if (s_selected == 0) {
        snprintf(footer, sizeof(footer), "+  SELECT new thread");
      } else {
        snprintf(footer, sizeof(footer), "%d/%d  SELECT opens messages", s_selected, s_thread_count);
      }
    } else if (s_view == VIEW_MESSAGES) {
      snprintf(footer, sizeof(footer), "%d/%d  UP older  DOWN newer", s_selected + 1, count);
    } else {
      snprintf(footer, sizeof(footer), "Msg %d/%d  SEL view", s_selected + 1, count);
    }
  } else {
    snprintf(footer, sizeof(footer), "%s", s_status);
  }

  text_layer_set_text(s_body_layer, body);
  text_layer_set_text(s_footer_layer, footer);
  layer_set_hidden(text_layer_get_layer(s_body_layer),
                   prv_is_nav_loading() || prv_is_list_view() || prv_is_message_view());
  layer_set_hidden(s_loading_layer, !prv_is_nav_loading());
  layer_set_hidden(s_list_layer, !prv_is_list_view());
  layer_set_hidden(s_message_layer, !prv_is_message_view());
  layer_set_hidden(s_blank_layer, !prv_is_blank_thread_view());
  if (s_loading_layer) layer_mark_dirty(s_loading_layer);
  if (s_list_layer) layer_mark_dirty(s_list_layer);
  if (s_message_layer) layer_mark_dirty(s_message_layer);
  if (s_blank_layer) layer_mark_dirty(s_blank_layer);
  prv_sync_loading_timer();
}

static void prv_accent_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int colors = sizeof(s_accent_colors) / sizeof(s_accent_colors[0]);
  int segment_w = bounds.size.w / colors;
  for (int i = 0; i < colors; i++) {
    graphics_context_set_fill_color(ctx, s_accent_colors[i]);
    graphics_fill_rect(ctx, GRect(i * segment_w, 0, segment_w + 1, bounds.size.h), 0, GCornerNone);
  }
}

static void prv_loading_tick(void *context) {
  s_loading_timer = NULL;
  if (!prv_is_nav_loading()) return;
  s_loading_frame++;
  if (s_loading_layer) layer_mark_dirty(s_loading_layer);
  prv_sync_loading_timer();
}

static void prv_sync_loading_timer(void) {
  if (prv_is_nav_loading()) {
    if (!s_loading_timer) {
      s_loading_timer = app_timer_register(180, prv_loading_tick, NULL);
    }
  } else if (s_loading_timer) {
    app_timer_cancel(s_loading_timer);
    s_loading_timer = NULL;
  }
}

static void prv_loading_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  static const GPoint offsets[] = {
    { 0, -14 }, { 10, -10 }, { 14, 0 }, { 10, 10 },
    { 0, 14 }, { -10, 10 }, { -14, 0 }, { -10, -10 },
  };
  int active = s_loading_frame % 8;

  for (int i = 0; i < 8; i++) {
    int distance = (i - active + 8) % 8;
    GColor color = distance == 0 ? GColorCobaltBlue :
                   distance <= 2 ? GColorCeleste : GColorLightGray;
    int radius = distance == 0 ? 4 : 3;
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(center.x + offsets[i].x, center.y + offsets[i].y), radius);
  }
}

static void prv_footer_bg_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorCobaltBlue);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
}

static void prv_list_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int count = prv_current_count();
  if (count <= 0) return;

  int visible_rows = bounds.size.h / LIST_ROW_HEIGHT;
  if (visible_rows < 1) visible_rows = 1;
  prv_list_clamp_page();
  int first = s_list_first;

  for (int row = 0; row < visible_rows; row++) {
    int index = first + row;
    if (index >= count) break;

    int y = LIST_TOP_PADDING + row * LIST_ROW_HEIGHT;
    GRect row_rect = GRect(0, y, bounds.size.w, LIST_ROW_HEIGHT);
    bool highlighted = index == s_selected;
    bool has_marker = highlighted;

    if (highlighted) {
      graphics_context_set_fill_color(ctx, GColorCobaltBlue);
      graphics_fill_rect(ctx, row_rect, 0, GCornerNone);
    } else if (row % 2 == 1) {
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_rect(ctx, row_rect, 0, GCornerNone);
    }

    if (!highlighted) {
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      graphics_draw_line(ctx, GPoint(LIST_SIDE_PADDING, y + LIST_ROW_HEIGHT - 1),
                         GPoint(bounds.size.w - LIST_SIDE_PADDING, y + LIST_ROW_HEIGHT - 1));
    }

    if (s_view == VIEW_THREADS && index == 0) {
      graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorCobaltBlue);
      graphics_draw_text(ctx, "+", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                         row_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      continue;
    }

    if (has_marker) {
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, GPoint(LIST_SIDE_PADDING + 2, y + LIST_ROW_HEIGHT / 2), 4);
    }

    int text_x = LIST_SIDE_PADDING + (has_marker ? LIST_MARKER_WIDTH + 4 : 0);
    int text_w = bounds.size.w - text_x - LIST_SIDE_PADDING;
    GRect title_rect = GRect(text_x, y + 1, text_w, 22);
    GRect sub_rect = GRect(text_x, y + 21, text_w, 18);

    graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
    graphics_draw_text(ctx, prv_list_title(index), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       title_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, highlighted ? GColorCeleste : GColorDarkGray);
    graphics_draw_text(ctx, prv_list_subtitle(index), fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       sub_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void prv_message_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (s_message_count <= 0 || s_selected < 0 || s_selected >= s_message_count) return;

  const MessageItem *msg = &s_messages[s_selected];
  bool is_user = prv_is_user_message(msg);
  int bubble_w = bounds.size.w - 24;
  int bubble_x = is_user ? bounds.size.w - bubble_w - 6 : 6;
  GTextAlignment align = is_user ? GTextAlignmentRight : GTextAlignmentLeft;

  GRect label_rect = GRect(8, 0, bounds.size.w - 16, 20);
  graphics_context_set_text_color(ctx, is_user ? GColorDarkGray : GColorCobaltBlue);
  graphics_draw_text(ctx, prv_message_label(msg), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     label_rect, GTextOverflowModeTrailingEllipsis, align, NULL);

  GRect bubble_rect = GRect(bubble_x, 21, bubble_w, bounds.size.h - 24);
  if (is_user) {
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, bubble_rect, 5, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_round_rect(ctx, bubble_rect, 5);
  } else {
    graphics_context_set_fill_color(ctx, GColorCeleste);
    graphics_fill_rect(ctx, bubble_rect, 5, GCornersAll);
  }

  GRect text_rect = GRect(bubble_rect.origin.x + 6, bubble_rect.origin.y + 4,
                          bubble_rect.size.w - 12, bubble_rect.size.h - 8);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, msg->text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     text_rect, GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void prv_blank_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GRect title_rect = GRect(8, bounds.size.h / 2 - 36, bounds.size.w - 16, 30);
  GRect hint_rect = GRect(14, bounds.size.h / 2 - 6, bounds.size.w - 28, 48);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Blank thread", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     title_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Hold SELECT to dictate a message",
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     hint_rect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void prv_begin_load(const char *status) {
  s_seq++;
  s_loading = true;
  s_refresh_after_send = false;
  s_selected = 0;
  s_scroll = 0;
  s_list_first = 0;
  s_loading_frame = 0;
  prv_copy_cstr(s_status, sizeof(s_status), status);
  if (s_request_timer) app_timer_cancel(s_request_timer);
  s_request_timer = app_timer_register(45000, prv_request_timeout, NULL);
  prv_render();
}

static void prv_end_load(void) {
  s_loading = false;
  if (s_request_timer) {
    app_timer_cancel(s_request_timer);
    s_request_timer = NULL;
  }
}

static void prv_request_timeout(void *context) {
  s_request_timer = NULL;
  if (!s_loading) return;
  prv_copy_cstr(s_status, sizeof(s_status), "Phone timeout");
  s_loading = false;
  s_refresh_after_send = false;
  prv_render();
}

static void prv_request_projects(void) {
  s_project_count = 0;
  s_view = VIEW_PROJECTS;
  prv_begin_load("Loading projects");
  prv_send_request(CMD_PROJECTS);
}

static void prv_request_threads(void) {
  s_thread_count = 0;
  s_view = VIEW_THREADS;
  prv_begin_load("Loading threads");
  prv_send_request(CMD_THREADS);
}

static void prv_request_messages(void) {
  s_message_count = 0;
  s_view = VIEW_MESSAGES;
  prv_begin_load("Loading messages");
  prv_send_request(CMD_MESSAGES);
}

static void prv_open_new_thread(void) {
  s_view = VIEW_MESSAGES;
  s_loading = false;
  s_message_count = 0;
  s_selected = 0;
  s_scroll = 0;
  s_list_first = 0;
  snprintf(s_thread_id, sizeof(s_thread_id), "pebble-thread:%lu:%lu",
           (unsigned long)time(NULL), (unsigned long)s_seq);
  prv_copy_cstr(s_status, sizeof(s_status), "");
  prv_render();
}

static void prv_up_click(ClickRecognizerRef recognizer, void *context) {
  if (s_loading) return;
  if (s_view == VIEW_EXPANDED) {
    s_scroll = s_scroll > 80 ? s_scroll - 80 : 0;
  } else if (s_view == VIEW_PROJECTS || s_view == VIEW_THREADS) {
    int count = prv_current_count();
    int visible_rows = prv_list_visible_rows();
    if (count > 0) {
      prv_list_clamp_page();
      if (s_selected > s_list_first) {
        s_selected--;
      } else if (s_list_first > 0) {
        s_list_first -= visible_rows;
        if (s_list_first < 0) s_list_first = 0;
        s_selected = s_list_first + visible_rows - 1;
        if (s_selected >= count) s_selected = count - 1;
      }
    }
  } else {
    int count = prv_current_count();
    if (count > 0) {
      if (s_view == VIEW_MESSAGES) {
        if (s_selected > 0) s_selected--;
      } else {
        s_selected = (s_selected + count - 1) % count;
      }
    }
  }
  prv_render();
}

static void prv_down_click(ClickRecognizerRef recognizer, void *context) {
  if (s_loading) return;
  if (s_view == VIEW_EXPANDED) {
    int len = strlen(s_messages[s_selected].text);
    if (s_scroll + 80 < len) s_scroll += 80;
  } else if (s_view == VIEW_PROJECTS || s_view == VIEW_THREADS) {
    int count = prv_current_count();
    int visible_rows = prv_list_visible_rows();
    if (count > 0) {
      prv_list_clamp_page();
      int page_end = s_list_first + visible_rows - 1;
      if (page_end >= count) page_end = count - 1;
      if (s_selected < page_end) {
        s_selected++;
      } else if (page_end + 1 < count) {
        s_list_first = page_end + 1;
        s_selected = s_list_first;
      }
    }
  } else {
    int count = prv_current_count();
    if (count > 0) {
      if (s_view == VIEW_MESSAGES) {
        if (s_selected + 1 < count) s_selected++;
      } else {
        s_selected = (s_selected + 1) % count;
      }
    }
  }
  prv_render();
}

static void prv_select_click(ClickRecognizerRef recognizer, void *context) {
  if (s_loading) return;
  if (s_view == VIEW_PROJECTS && s_project_count > 0) {
    prv_copy_cstr(s_project_id, sizeof(s_project_id), s_projects[s_selected].id);
    prv_request_threads();
  } else if (s_view == VIEW_THREADS && s_thread_count > 0) {
    if (s_selected == 0) {
      prv_open_new_thread();
    } else if (s_selected <= s_thread_count) {
      prv_copy_cstr(s_thread_id, sizeof(s_thread_id), s_threads[s_selected - 1].id);
      prv_request_messages();
    }
  } else if (s_view == VIEW_THREADS && s_thread_count == 0 && s_selected == 0) {
    prv_open_new_thread();
  } else if (s_view == VIEW_MESSAGES && s_message_count > 0) {
    s_view = VIEW_EXPANDED;
    s_scroll = 0;
    prv_render();
  } else if (s_view == VIEW_EXPANDED) {
    s_view = VIEW_MESSAGES;
    prv_render();
  }
}

static void prv_dictation_callback(DictationSession *session, DictationSessionStatus status,
                                   char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess && transcription && strlen(transcription) > 0) {
    prv_begin_load("Sending");
    s_refresh_after_send = true;
    prv_send_text(CMD_SEND, transcription);
  } else {
    prv_copy_cstr(s_status, sizeof(s_status), "Dictation cancelled");
    prv_render();
  }
}

static void prv_select_long_click(ClickRecognizerRef recognizer, void *context) {
  if (s_loading || s_view != VIEW_MESSAGES) return;
  if (!s_dictation) {
    s_dictation = dictation_session_create(512, prv_dictation_callback, NULL);
  }
  dictation_session_start(s_dictation);
}

static void prv_back_click(ClickRecognizerRef recognizer, void *context) {
  if (s_loading) return;
  if (s_view == VIEW_EXPANDED) {
    s_view = VIEW_MESSAGES;
    prv_render();
  } else if (s_view == VIEW_MESSAGES) {
    s_view = VIEW_THREADS;
    s_selected = 0;
    s_list_first = 0;
    prv_render();
  } else if (s_view == VIEW_THREADS) {
    s_view = VIEW_PROJECTS;
    s_selected = 0;
    s_list_first = 0;
    prv_render();
  } else {
    window_stack_pop(true);
  }
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, prv_select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click);
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *command_tuple = dict_find(iter, MESSAGE_KEY_Command);
  if (!command_tuple) return;
  int command = command_tuple->value->uint8;

  Tuple *seq_tuple = dict_find(iter, MESSAGE_KEY_Seq);
  uint32_t seq = seq_tuple ? seq_tuple->value->uint32 : 0;
  if (seq != 0 && seq < s_seq) return;

  if (command == CMD_STATUS) {
    Tuple *status_tuple = dict_find(iter, MESSAGE_KEY_Status);
    prv_copy_cstr(s_status, sizeof(s_status), status_tuple ? status_tuple->value->cstring : "");
    prv_render();
    return;
  }

  if (command == CMD_ERROR) {
    Tuple *error_tuple = dict_find(iter, MESSAGE_KEY_Error);
    prv_copy_cstr(s_status, sizeof(s_status), error_tuple ? error_tuple->value->cstring : "Error");
    prv_end_load();
    s_refresh_after_send = false;
    prv_render();
    return;
  }

  if (command == CMD_DONE) {
    bool refresh_after_send = s_refresh_after_send;
    prv_end_load();
    s_refresh_after_send = false;
    Tuple *thread_id_tuple = dict_find(iter, MESSAGE_KEY_ThreadId);
    if (thread_id_tuple) prv_copy_cstr(s_thread_id, sizeof(s_thread_id), thread_id_tuple->value->cstring);
    Tuple *status_tuple = dict_find(iter, MESSAGE_KEY_Status);
    if (status_tuple) prv_copy_cstr(s_status, sizeof(s_status), status_tuple->value->cstring);
    if (refresh_after_send && s_view == VIEW_MESSAGES) {
      prv_request_messages();
      return;
    }
    if (s_view == VIEW_MESSAGES) {
      s_selected = s_message_count > 0 ? s_message_count - 1 : 0;
    }
    prv_render();
    return;
  }

  if (command != CMD_ITEM) return;

  Tuple *index_tuple = dict_find(iter, MESSAGE_KEY_Index);
  int index = index_tuple ? (int)index_tuple->value->int32 : -1;
  if (index < 0) return;

  Tuple *project_id = dict_find(iter, MESSAGE_KEY_ProjectId);
  Tuple *thread_id = dict_find(iter, MESSAGE_KEY_ThreadId);
  Tuple *title = dict_find(iter, MESSAGE_KEY_Title);
  Tuple *role = dict_find(iter, MESSAGE_KEY_Role);
  Tuple *text = dict_find(iter, MESSAGE_KEY_Text);
  Tuple *status = dict_find(iter, MESSAGE_KEY_Status);

  if (s_view == VIEW_PROJECTS && index < MAX_PROJECTS) {
    prv_copy_cstr(s_projects[index].id, sizeof(s_projects[index].id),
                  project_id ? project_id->value->cstring : "");
    prv_copy_cstr(s_projects[index].title, sizeof(s_projects[index].title),
                  title ? title->value->cstring : "Project");
    prv_copy_cstr(s_projects[index].status, sizeof(s_projects[index].status),
                  status ? status->value->cstring : "");
    if (index >= s_project_count) s_project_count = index + 1;
  } else if (s_view == VIEW_THREADS && index < MAX_THREADS) {
    prv_copy_cstr(s_threads[index].id, sizeof(s_threads[index].id),
                  thread_id ? thread_id->value->cstring : "");
    prv_copy_cstr(s_threads[index].title, sizeof(s_threads[index].title),
                  title ? title->value->cstring : "Thread");
    prv_copy_cstr(s_threads[index].status, sizeof(s_threads[index].status),
                  status ? status->value->cstring : "");
    if (index >= s_thread_count) s_thread_count = index + 1;
  } else if (s_view == VIEW_MESSAGES && index < MAX_MESSAGES) {
    prv_copy_cstr(s_messages[index].id, sizeof(s_messages[index].id), "");
    prv_copy_cstr(s_messages[index].role, sizeof(s_messages[index].role),
                  role ? role->value->cstring : "message");
    prv_copy_cstr(s_messages[index].text, sizeof(s_messages[index].text),
                  text ? text->value->cstring : "");
    prv_copy_cstr(s_messages[index].status, sizeof(s_messages[index].status),
                  status ? status->value->cstring : "");
    if (index >= s_message_count) s_message_count = index + 1;
  }
}

static void prv_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  if (!s_loading) return;
  prv_copy_cstr(s_status, sizeof(s_status), "Phone send failed");
  prv_end_load();
  s_refresh_after_send = false;
  prv_render();
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  window_set_background_color(window, GColorWhite);

  s_accent_layer = layer_create(GRect(0, 0, bounds.size.w, COLOR_BAR_HEIGHT));
  layer_set_update_proc(s_accent_layer, prv_accent_update_proc);
  layer_add_child(root, s_accent_layer);

  s_header_layer = text_layer_create(GRect(0, COLOR_BAR_HEIGHT + 2, bounds.size.w, 26));
  text_layer_set_font(s_header_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_header_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_header_layer, GColorWhite);
  text_layer_set_text_color(s_header_layer, GColorBlack);
  layer_add_child(root, text_layer_get_layer(s_header_layer));

  s_body_layer = text_layer_create(GRect(8, 32, bounds.size.w - 16, 164));
  text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeTrailingEllipsis);
  layer_add_child(root, text_layer_get_layer(s_body_layer));

  s_loading_layer = layer_create(GRect(0, 31, bounds.size.w, 168));
  layer_set_update_proc(s_loading_layer, prv_loading_update_proc);
  layer_set_hidden(s_loading_layer, true);
  layer_add_child(root, s_loading_layer);

  s_list_layer = layer_create(GRect(0, 31, bounds.size.w, 168));
  layer_set_update_proc(s_list_layer, prv_list_update_proc);
  layer_set_hidden(s_list_layer, true);
  layer_add_child(root, s_list_layer);

  s_message_layer = layer_create(GRect(0, 31, bounds.size.w, 168));
  layer_set_update_proc(s_message_layer, prv_message_update_proc);
  layer_set_hidden(s_message_layer, true);
  layer_add_child(root, s_message_layer);

  s_blank_layer = layer_create(GRect(0, 31, bounds.size.w, 168));
  layer_set_update_proc(s_blank_layer, prv_blank_update_proc);
  layer_set_hidden(s_blank_layer, true);
  layer_add_child(root, s_blank_layer);

  s_footer_bg_layer = layer_create(GRect(0, 200, bounds.size.w, 28));
  layer_set_update_proc(s_footer_bg_layer, prv_footer_bg_update_proc);
  layer_add_child(root, s_footer_bg_layer);

  s_footer_layer = text_layer_create(GRect(0, 205, bounds.size.w, 23));
  text_layer_set_font(s_footer_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_footer_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_footer_layer, GColorClear);
  text_layer_set_text_color(s_footer_layer, GColorWhite);
  layer_add_child(root, text_layer_get_layer(s_footer_layer));

  prv_render();
  prv_request_projects();
}

static void prv_window_unload(Window *window) {
  if (s_loading_timer) {
    app_timer_cancel(s_loading_timer);
    s_loading_timer = NULL;
  }
  if (s_request_timer) {
    app_timer_cancel(s_request_timer);
    s_request_timer = NULL;
  }
  layer_destroy(s_accent_layer);
  layer_destroy(s_loading_layer);
  layer_destroy(s_list_layer);
  layer_destroy(s_message_layer);
  layer_destroy(s_blank_layer);
  layer_destroy(s_footer_bg_layer);
  text_layer_destroy(s_header_layer);
  text_layer_destroy(s_body_layer);
  text_layer_destroy(s_footer_layer);
}

static void prv_init(void) {
  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_outbox_failed(prv_outbox_failed);
  app_message_open(2048, 2048);

  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload
  });
  window_stack_push(s_window, true);
}

static void prv_deinit(void) {
  if (s_dictation) dictation_session_destroy(s_dictation);
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
