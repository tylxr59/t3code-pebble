#include "ui.h"

#define LIST_ROW_HEIGHT 42
#define LIST_TOP_PADDING 2
#define LIST_SIDE_PADDING 8
#define LIST_MARKER_WIDTH 8
#define COLOR_BAR_HEIGHT 3

static Layer *s_accent_layer;
static Layer *s_loading_layer;
static Layer *s_list_layer;
static Layer *s_message_layer;
static Layer *s_blank_layer;
static Layer *s_footer_bg_layer;
static TextLayer *s_header_layer;
static TextLayer *s_body_layer;
static TextLayer *s_footer_layer;
static AppTimer *s_loading_timer;
static UiState *s_state;

static const GColor s_accent_colors[] = {
    {.argb = GColorPictonBlueARGB8},
    {.argb = GColorCelesteARGB8},
    {.argb = GColorMintGreenARGB8},
    {.argb = GColorIcterineARGB8},
    {.argb = GColorRajahARGB8},
};

static bool prv_is_user_message(const MessageItem *msg) {
  return msg && strcmp(msg->role, "user") == 0;
}

static const char *prv_message_label(const MessageItem *msg) {
  return prv_is_user_message(msg) ? "You" : "AI";
}

static const char *prv_title_for_selection(void) {
  if (!s_state) return "";
  if (s_state->view == VIEW_PROJECTS && s_state->project_count > 0) {
    return s_state->projects[s_state->selected].title;
  }
  if (s_state->view == VIEW_THREADS && s_state->selected > 0 &&
      s_state->selected <= s_state->thread_count) {
    return s_state->threads[s_state->selected - 1].title;
  }
  if ((s_state->view == VIEW_MESSAGES || s_state->view == VIEW_EXPANDED) &&
      s_state->message_count > 0) {
    return prv_message_label(&s_state->messages[s_state->selected]);
  }
  return "";
}

static bool prv_is_list_view(void) {
  return s_state && !s_state->loading &&
         (s_state->view == VIEW_PROJECTS || s_state->view == VIEW_THREADS);
}

static bool prv_is_nav_loading(void) {
  return s_state && s_state->loading &&
         (s_state->view == VIEW_PROJECTS || s_state->view == VIEW_THREADS ||
          (s_state->view == VIEW_MESSAGES && strcmp(s_state->status, "Loading messages") == 0));
}

static bool prv_is_message_view(void) {
  return s_state && !s_state->loading && s_state->view == VIEW_MESSAGES &&
         s_state->message_count > 0;
}

static bool prv_is_blank_thread_view(void) {
  return s_state && !s_state->loading && s_state->view == VIEW_MESSAGES &&
         s_state->message_count == 0;
}

static const char *prv_list_title(int index) {
  if (s_state->view == VIEW_PROJECTS && index >= 0 && index < s_state->project_count) {
    return s_state->projects[index].title;
  }
  if (s_state->view == VIEW_THREADS) {
    if (index == 0) return "+";
    int thread_index = index - 1;
    if (thread_index >= 0 && thread_index < s_state->thread_count) {
      return s_state->threads[thread_index].title;
    }
  }
  return "";
}

static const char *prv_list_subtitle(int index) {
  if (s_state->view == VIEW_PROJECTS && index >= 0 && index < s_state->project_count) {
    return strlen(s_state->projects[index].status) > 0 ? s_state->projects[index].status
                                                       : "Tap SELECT to view threads";
  }
  if (s_state->view == VIEW_THREADS) {
    if (index == 0) return "";
    int thread_index = index - 1;
    if (thread_index >= 0 && thread_index < s_state->thread_count) {
      return strlen(s_state->threads[thread_index].status) > 0
                 ? s_state->threads[thread_index].status
                 : "Tap SELECT to view messages";
    }
  }
  return "";
}

static const ThreadItem *prv_thread_for_row(int index) {
  if (!s_state || s_state->view != VIEW_THREADS || index <= 0) return NULL;
  int thread_index = index - 1;
  if (thread_index < 0 || thread_index >= s_state->thread_count) return NULL;
  return &s_state->threads[thread_index];
}

static void prv_draw_working_icon(GContext *ctx, GRect rect, GColor color) {
  int x = rect.origin.x;
  int y = rect.origin.y;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_line(ctx, GPoint(x + 2, y + 14), GPoint(x + 8, y + 8));
  graphics_context_set_stroke_width(ctx, 6);
  graphics_draw_line(ctx, GPoint(x + 6, y + 4), GPoint(x + 12, y + 10));
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(x + 10, y + 3), GPoint(x + 13, y + 6));
  graphics_context_set_stroke_width(ctx, 1);
}

static void prv_draw_done_dot(GContext *ctx, GRect rect, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(rect.origin.x + 7, rect.origin.y + 8), 4);
}

static void prv_set_header(const char *title) {
  text_layer_set_text(s_header_layer, title);
}

static void prv_sync_loading_timer(void);

static int prv_current_count(void) {
  if (!s_state) return 0;
  if (s_state->view == VIEW_PROJECTS) return s_state->project_count;
  if (s_state->view == VIEW_THREADS) return s_state->thread_count + 1;
  if (s_state->view == VIEW_MESSAGES || s_state->view == VIEW_EXPANDED) {
    return s_state->message_count;
  }
  return 0;
}

int ui_list_visible_rows(void) {
  if (!s_list_layer) return 4;
  GRect bounds = layer_get_bounds(s_list_layer);
  int visible_rows = bounds.size.h / LIST_ROW_HEIGHT;
  return visible_rows < 1 ? 1 : visible_rows;
}

static void prv_list_clamp_page(void) {
  int count = prv_current_count();
  int visible_rows = ui_list_visible_rows();
  if (count <= 0) {
    s_state->list_first = 0;
    s_state->selected = 0;
    return;
  }
  if (s_state->selected >= count) s_state->selected = count - 1;
  if (s_state->selected < 0) s_state->selected = 0;
  if (s_state->list_first > count - 1) s_state->list_first = count - 1;
  if (s_state->list_first < 0) s_state->list_first = 0;
  if (s_state->selected < s_state->list_first ||
      s_state->selected >= s_state->list_first + visible_rows) {
    s_state->list_first = (s_state->selected / visible_rows) * visible_rows;
  }
}

void ui_render(void) {
  static char body[900];
  static char footer[96];
  int count = prv_current_count();

  if (s_state->view == VIEW_PROJECTS) {
    prv_set_header("Projects");
  } else if (s_state->view == VIEW_THREADS) {
    prv_set_header("Threads");
  } else if (s_state->view == VIEW_MESSAGES) {
    prv_set_header("Messages");
  } else {
    prv_set_header(prv_title_for_selection());
  }

  if (prv_is_nav_loading()) {
    body[0] = '\0';
  } else if (s_state->loading) {
    snprintf(body, sizeof(body), "\n\n%s", s_state->status);
  } else if (count == 0) {
    if (s_state->view == VIEW_MESSAGES) {
      body[0] = '\0';
    } else {
      snprintf(body, sizeof(body), "\n\nNo items");
    }
  } else if (prv_is_list_view()) {
    body[0] = '\0';
  } else if (s_state->view == VIEW_EXPANDED) {
    const MessageItem *msg = &s_state->messages[s_state->selected];
    int len = strlen(msg->text);
    int start = s_state->scroll;
    if (start > len) start = len;
    snprintf(body, sizeof(body), "%s", msg->text + start);
  } else if (s_state->view == VIEW_MESSAGES) {
    body[0] = '\0';
  } else if (s_state->view == VIEW_THREADS) {
    const ThreadItem *thread = &s_state->threads[s_state->selected];
    if (strlen(thread->status) > 0) {
      snprintf(body, sizeof(body), "%s\n\nSession: %s", thread->title, thread->status);
    } else {
      snprintf(body, sizeof(body), "%s", thread->title);
    }
  } else {
    const ProjectItem *project = &s_state->projects[s_state->selected];
    snprintf(body, sizeof(body), "%s", project->title);
  }

  if (prv_is_nav_loading()) {
    footer[0] = '\0';
  } else if (s_state->view == VIEW_EXPANDED) {
    snprintf(footer, sizeof(footer), "UP/DN scroll  SEL back");
  } else if (count > 0) {
    if (s_state->view == VIEW_PROJECTS) {
      snprintf(footer, sizeof(footer), "%d/%d  SELECT opens threads", s_state->selected + 1, count);
    } else if (s_state->view == VIEW_THREADS) {
      if (s_state->selected == 0) {
        snprintf(footer, sizeof(footer), "+  SELECT new thread");
      } else {
        snprintf(footer,
                 sizeof(footer),
                 "%d/%d  SELECT opens messages",
                 s_state->selected,
                 s_state->thread_count);
      }
    } else if (s_state->view == VIEW_MESSAGES) {
      if (strlen(s_state->status) > 0) {
        snprintf(footer, sizeof(footer), "%s", s_state->status);
      } else {
        snprintf(
            footer, sizeof(footer), "%d/%d  UP older  DOWN newer", s_state->selected + 1, count);
      }
    } else {
      snprintf(footer, sizeof(footer), "Msg %d/%d  SEL view", s_state->selected + 1, count);
    }
  } else {
    snprintf(footer, sizeof(footer), "%s", s_state->status);
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
  s_state->loading_frame++;
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
      {0, -14},
      {10, -10},
      {14, 0},
      {10, 10},
      {0, 14},
      {-10, 10},
      {-14, 0},
      {-10, -10},
  };
  int active = s_state->loading_frame % 8;

  for (int i = 0; i < 8; i++) {
    int distance = (i - active + 8) % 8;
    GColor color = distance == 0   ? GColorCobaltBlue
                   : distance <= 2 ? GColorCeleste
                                   : GColorLightGray;
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
  int first = s_state->list_first;

  for (int row = 0; row < visible_rows; row++) {
    int index = first + row;
    if (index >= count) break;

    int y = LIST_TOP_PADDING + row * LIST_ROW_HEIGHT;
    GRect row_rect = GRect(0, y, bounds.size.w, LIST_ROW_HEIGHT);
    bool highlighted = index == s_state->selected;
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
      graphics_draw_line(ctx,
                         GPoint(LIST_SIDE_PADDING, y + LIST_ROW_HEIGHT - 1),
                         GPoint(bounds.size.w - LIST_SIDE_PADDING, y + LIST_ROW_HEIGHT - 1));
    }

    if (s_state->view == VIEW_THREADS && index == 0) {
      graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorCobaltBlue);
      graphics_draw_text(ctx,
                         "+",
                         fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                         row_rect,
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentCenter,
                         NULL);
      continue;
    }

    if (has_marker) {
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx,
                         ">",
                         fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                         GRect(LIST_SIDE_PADDING - 1, y + 10, LIST_MARKER_WIDTH + 2, 18),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentCenter,
                         NULL);
    }

    const ThreadItem *thread = prv_thread_for_row(index);
    bool has_thread_icon = thread && (thread->working || thread->unseen_done);
    int text_x = LIST_SIDE_PADDING + (has_marker ? LIST_MARKER_WIDTH + 4 : 0);
    if (has_thread_icon) {
      GColor icon_color =
          highlighted ? GColorWhite : (thread->working ? GColorCobaltBlue : GColorDarkGray);
      GRect icon_rect = GRect(text_x, y + 5, 14, 16);
      if (thread->working) {
        prv_draw_working_icon(ctx, icon_rect, icon_color);
      } else {
        prv_draw_done_dot(ctx, icon_rect, icon_color);
      }
      text_x += 17;
    }
    int text_w = bounds.size.w - text_x - LIST_SIDE_PADDING;
    bool project_row = s_state->view == VIEW_PROJECTS;
    GRect title_rect =
        project_row ? GRect(text_x, y + 9, text_w, 24) : GRect(text_x, y + 1, text_w, 22);
    GRect sub_rect = GRect(text_x, y + 21, text_w, 18);

    graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
    graphics_draw_text(ctx,
                       prv_list_title(index),
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       title_rect,
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft,
                       NULL);
    if (project_row) continue;

    graphics_context_set_text_color(ctx, highlighted ? GColorCeleste : GColorDarkGray);
    graphics_draw_text(ctx,
                       prv_list_subtitle(index),
                       fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       sub_rect,
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft,
                       NULL);
  }
}

static void prv_message_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (s_state->message_count <= 0 || s_state->selected < 0 ||
      s_state->selected >= s_state->message_count) {
    return;
  }

  const MessageItem *msg = &s_state->messages[s_state->selected];
  bool is_user = prv_is_user_message(msg);
  int bubble_w = bounds.size.w - 24;
  int bubble_x = is_user ? bounds.size.w - bubble_w - 6 : 6;
  GTextAlignment align = is_user ? GTextAlignmentRight : GTextAlignmentLeft;

  GRect label_rect = GRect(8, 0, bounds.size.w - 16, 20);
  graphics_context_set_text_color(ctx, is_user ? GColorDarkGray : GColorCobaltBlue);
  graphics_draw_text(ctx,
                     prv_message_label(msg),
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     label_rect,
                     GTextOverflowModeTrailingEllipsis,
                     align,
                     NULL);

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

  GRect text_rect = GRect(bubble_rect.origin.x + 6,
                          bubble_rect.origin.y + 4,
                          bubble_rect.size.w - 12,
                          bubble_rect.size.h - 8);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx,
                     msg->text,
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     text_rect,
                     GTextOverflowModeTrailingEllipsis,
                     align,
                     NULL);
}

static void prv_blank_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GRect title_rect = GRect(8, bounds.size.h / 2 - 36, bounds.size.w - 16, 30);
  GRect hint_rect = GRect(14, bounds.size.h / 2 - 6, bounds.size.w - 28, 48);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx,
                     "Blank thread",
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     title_rect,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter,
                     NULL);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx,
                     "Hold SELECT to dictate a message",
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     hint_rect,
                     GTextOverflowModeWordWrap,
                     GTextAlignmentCenter,
                     NULL);
}

void ui_create(Window *window, UiState *state) {
  s_state = state;
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

  s_footer_bg_layer = layer_create(GRect(0, 199, bounds.size.w, 29));
  layer_set_update_proc(s_footer_bg_layer, prv_footer_bg_update_proc);
  layer_add_child(root, s_footer_bg_layer);

  s_footer_layer = text_layer_create(GRect(0, 205, bounds.size.w, 23));
  text_layer_set_font(s_footer_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_footer_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_footer_layer, GColorClear);
  text_layer_set_text_color(s_footer_layer, GColorWhite);
  layer_add_child(root, text_layer_get_layer(s_footer_layer));
}

void ui_destroy(void) {
  if (s_loading_timer) {
    app_timer_cancel(s_loading_timer);
    s_loading_timer = NULL;
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
  s_state = NULL;
}
