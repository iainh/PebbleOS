# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources
  action_chaining_window.c
  alerts.c
  alerts_preferences.c
  ancs/ancs_filtering.c
  ancs/ancs_item.c
  ancs/ancs_notifications.c
  ancs/ancs_notifications_util.c
  ancs/ancs_phone_call.c
  ancs/nexmo.c
  do_not_disturb.c
  do_not_disturb_toggle.c
  notification_image.c
  notification_storage.c
  notifications.c
)

if(CONFIG_NOTIFICATION_COMPACTION_RUST)
  include(rust)
  pbl_rust_object(pebble_notification_compaction
    ${CMAKE_CURRENT_SOURCE_DIR}/notification-compaction-rust rust_object)
  pbl_library_named(notification_compaction_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(notification_compaction_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(notification_compaction_rust pebble_notification_compaction_rust_build)
endif()

pbl_library()
pbl_library_sources(${sources})
pbl_service_gettext(notifications ${sources})
