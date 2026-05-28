#include <pebble.h>

#include "model.h"
#include "ui.h"

#define CMD_CONFIG 1
#define CMD_PROJECTS 2
#define CMD_THREADS 3
#define CMD_MESSAGES 4
#define CMD_SEND 5
#define CMD_CANCEL 6
#define CMD_ITEM 20
#define CMD_DONE 21
#define CMD_STATUS 22
#define CMD_ERROR 23

static Window *s_window;
static DictationSession *s_dictation;
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
static UiState s_ui_state;

static void prv_request_projects(void);
static void prv_request_threads(void);
static void prv_request_messages(void);
static void prv_render(void);
static void prv_end_load(void);
static void prv_send_cancel_poll(void);
static void prv_request_timeout(void *context);
static void prv_clear_thread_done_marker(const char *thread_id);

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
  } else if (command == CMD_MESSAGES || command == CMD_SEND || command == CMD_CANCEL) {
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

static void prv_send_cancel_poll(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_Command, CMD_CANCEL);
  dict_write_uint32(iter, MESSAGE_KEY_Seq, s_seq);
  dict_write_cstring(iter, MESSAGE_KEY_ThreadId, s_thread_id);
  dict_write_end(iter);
  app_message_outbox_send();
}

static int prv_current_count(void) {
  if (s_view == VIEW_PROJECTS) return s_project_count;
  if (s_view == VIEW_THREADS) return s_thread_count + 1;
  if (s_view == VIEW_MESSAGES || s_view == VIEW_EXPANDED) return s_message_count;
  return 0;
}

static void prv_sync_ui_state(void) {
  s_ui_state.view = s_view;
  s_ui_state.loading = s_loading;
  s_ui_state.selected = s_selected;
  s_ui_state.scroll = s_scroll;
  s_ui_state.list_first = s_list_first;
  s_ui_state.loading_frame = s_loading_frame;
  s_ui_state.project_count = s_project_count;
  s_ui_state.thread_count = s_thread_count;
  s_ui_state.message_count = s_message_count;
  s_ui_state.projects = s_projects;
  s_ui_state.threads = s_threads;
  s_ui_state.messages = s_messages;
  s_ui_state.status = s_status;
}

static void prv_render(void) {
  prv_sync_ui_state();
  ui_render();
}

static int prv_list_visible_rows(void) {
  prv_sync_ui_state();
  return ui_list_visible_rows();
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
  memset(s_threads, 0, sizeof(s_threads));
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
  snprintf(s_thread_id,
           sizeof(s_thread_id),
           "pebble-thread:%lu:%lu",
           (unsigned long)time(NULL),
           (unsigned long)s_seq);
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
      prv_clear_thread_done_marker(s_thread_id);
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

static void prv_clear_thread_done_marker(const char *thread_id) {
  if (!thread_id || strlen(thread_id) == 0) return;
  for (int i = 0; i < s_thread_count; i++) {
    if (strcmp(s_threads[i].id, thread_id) == 0) {
      s_threads[i].unseen_done = false;
      return;
    }
  }
}

static void prv_dictation_callback(DictationSession *session,
                                   DictationSessionStatus status,
                                   char *transcription,
                                   void *context) {
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
    s_seq++;
    prv_send_cancel_poll();
    s_view = VIEW_THREADS;
    s_selected = 0;
    s_list_first = 0;
    prv_render();
  } else if (s_view == VIEW_THREADS) {
    s_seq++;
    prv_send_cancel_poll();
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
    if (thread_id_tuple)
      prv_copy_cstr(s_thread_id, sizeof(s_thread_id), thread_id_tuple->value->cstring);
    Tuple *status_tuple = dict_find(iter, MESSAGE_KEY_Status);
    Tuple *total_tuple = dict_find(iter, MESSAGE_KEY_Total);
    if (s_view == VIEW_MESSAGES && total_tuple) {
      s_message_count = (int)total_tuple->value->int32;
      prv_copy_cstr(s_status, sizeof(s_status), status_tuple ? status_tuple->value->cstring : "");
    } else if (s_view == VIEW_THREADS && total_tuple) {
      s_thread_count = (int)total_tuple->value->int32;
      if (status_tuple) prv_copy_cstr(s_status, sizeof(s_status), status_tuple->value->cstring);
    } else if (s_view == VIEW_PROJECTS && total_tuple) {
      s_project_count = (int)total_tuple->value->int32;
      if (status_tuple) prv_copy_cstr(s_status, sizeof(s_status), status_tuple->value->cstring);
    } else if (status_tuple) {
      prv_copy_cstr(s_status, sizeof(s_status), status_tuple->value->cstring);
    }
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
  Tuple *working = dict_find(iter, MESSAGE_KEY_Working);
  Tuple *unseen_done = dict_find(iter, MESSAGE_KEY_UnseenDone);

  if (s_view == VIEW_PROJECTS && index < MAX_PROJECTS) {
    prv_copy_cstr(s_projects[index].id,
                  sizeof(s_projects[index].id),
                  project_id ? project_id->value->cstring : "");
    prv_copy_cstr(s_projects[index].title,
                  sizeof(s_projects[index].title),
                  title ? title->value->cstring : "Project");
    prv_copy_cstr(s_projects[index].status,
                  sizeof(s_projects[index].status),
                  status ? status->value->cstring : "");
    if (index >= s_project_count) s_project_count = index + 1;
  } else if (s_view == VIEW_THREADS && index < MAX_THREADS) {
    prv_copy_cstr(s_threads[index].id,
                  sizeof(s_threads[index].id),
                  thread_id ? thread_id->value->cstring : "");
    prv_copy_cstr(s_threads[index].title,
                  sizeof(s_threads[index].title),
                  title ? title->value->cstring : "Thread");
    prv_copy_cstr(s_threads[index].status,
                  sizeof(s_threads[index].status),
                  status ? status->value->cstring : "");
    s_threads[index].working = working && working->value->int32 != 0;
    s_threads[index].unseen_done = unseen_done && unseen_done->value->int32 != 0;
    if (index >= s_thread_count) s_thread_count = index + 1;
  } else if (s_view == VIEW_MESSAGES && index < MAX_MESSAGES) {
    prv_copy_cstr(s_messages[index].id, sizeof(s_messages[index].id), "");
    prv_copy_cstr(s_messages[index].role,
                  sizeof(s_messages[index].role),
                  role ? role->value->cstring : "message");
    prv_copy_cstr(
        s_messages[index].text, sizeof(s_messages[index].text), text ? text->value->cstring : "");
    prv_copy_cstr(s_messages[index].status,
                  sizeof(s_messages[index].status),
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
  prv_sync_ui_state();
  ui_create(window, &s_ui_state);
  prv_render();
  prv_request_projects();
}

static void prv_window_unload(Window *window) {
  if (s_request_timer) {
    app_timer_cancel(s_request_timer);
    s_request_timer = NULL;
  }
  ui_destroy();
}

static void prv_init(void) {
  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_outbox_failed(prv_outbox_failed);
  app_message_open(2048, 2048);

  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(
      s_window, (WindowHandlers){.load = prv_window_load, .unload = prv_window_unload});
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
