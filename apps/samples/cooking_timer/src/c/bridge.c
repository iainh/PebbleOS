/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pebble.h>

extern void cooking_timer_render(void);
extern void cooking_timer_select(void);
extern void cooking_timer_up(void);
extern void cooking_timer_down(void);
extern void cooking_timer_tick(void);

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_time_layer;
static TextLayer *s_batch_layer;
static TextLayer *s_status_layer;
static AppTimer *s_timer;

static void prv_tick(void *context) {
  s_timer = NULL;
  cooking_timer_tick();
}

void cooking_timer_schedule_tick(void) {
  if (!s_timer) {
    s_timer = app_timer_register(1000, prv_tick, NULL);
  }
}

void cooking_timer_cancel_tick(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
}

void cooking_timer_vibe(bool final_batch) {
  if (final_batch) {
    vibes_long_pulse();
  } else {
    vibes_double_pulse();
  }
}

void cooking_timer_set_text(const char *time, const char *batch, const char *status) {
  text_layer_set_text(s_time_layer, time);
  text_layer_set_text(s_batch_layer, batch);
  text_layer_set_text(s_status_layer, status);
}

static void prv_select_click(ClickRecognizerRef recognizer, void *context) {
  cooking_timer_select();
}

static void prv_up_click(ClickRecognizerRef recognizer, void *context) {
  cooking_timer_up();
}

static void prv_down_click(ClickRecognizerRef recognizer, void *context) {
  cooking_timer_down();
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 300, prv_up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 300, prv_down_click);
}

static TextLayer *prv_add_text_layer(Layer *root, GRect frame, const char *font) {
  TextLayer *text_layer = text_layer_create(frame);
  text_layer_set_background_color(text_layer, GColorClear);
  text_layer_set_font(text_layer, fonts_get_system_font(font));
  text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(text_layer));
  return text_layer;
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  const int16_t width = bounds.size.w;

  s_title_layer = prv_add_text_layer(root, GRect(0, 4, width, 28), FONT_KEY_GOTHIC_24_BOLD);
  s_time_layer = prv_add_text_layer(root, GRect(0, 37, width, 50),
                                    FONT_KEY_LECO_38_BOLD_NUMBERS);
  s_batch_layer = prv_add_text_layer(root, GRect(0, 90, width, 28), FONT_KEY_GOTHIC_24_BOLD);
  s_status_layer = prv_add_text_layer(root, GRect(4, 121, width - 8, 44), FONT_KEY_GOTHIC_18);
  text_layer_set_text(s_title_layer, "Cooking Timer");
  cooking_timer_render();
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_status_layer);
  text_layer_destroy(s_batch_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_title_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);
}

static void prv_deinit(void) {
  cooking_timer_cancel_tick();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
