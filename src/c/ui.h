#pragma once

#include <pebble.h>

#include "model.h"

typedef struct {
  View view;
  bool loading;
  int selected;
  int scroll;
  int list_first;
  int loading_frame;
  int project_count;
  int thread_count;
  int message_count;
  ProjectItem *projects;
  ThreadItem *threads;
  MessageItem *messages;
  const char *status;
} UiState;

void ui_create(Window *window, UiState *state);
void ui_destroy(void);
void ui_render(void);
int ui_list_visible_rows(void);
