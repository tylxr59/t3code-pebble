#pragma once

#include <pebble.h>

#define MAX_PROJECTS 30
#define MAX_THREADS 40
#define MAX_MESSAGES 40
#define ID_LEN 96
#define TITLE_LEN 96
#define TEXT_LEN 640

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
  bool working;
  bool unseen_done;
} ThreadItem;

typedef struct {
  char id[ID_LEN];
  char role[16];
  char text[TEXT_LEN];
  char status[32];
} MessageItem;
