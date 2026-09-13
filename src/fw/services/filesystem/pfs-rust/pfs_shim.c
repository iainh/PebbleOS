/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "console/prompt.h"
#include "kernel/pbl_malloc.h"
#include "pbl/kernel/mutex.h"
#include "pbl/logging/logging.h"
#include "pbl/services/analytics/analytics.h"
#include "pbl/services/filesystem/flash_translation.h"
#include "pbl/services/filesystem/pfs.h"
#include "system/hexdump.h"

#include <stdlib.h>

static PBL_MUTEX_DEFINE(s_pfs_rust_mutex);

void pfs_rust_lock(void) {
  pbl_mutex_lock(&s_pfs_rust_mutex, PBL_FOREVER);
}
void pfs_rust_unlock(void) {
  pbl_mutex_unlock(&s_pfs_rust_mutex);
}
void *pfs_rust_alloc(size_t size) {
  return kernel_malloc(size);
}
void pfs_rust_free(void *ptr) {
  kernel_free(ptr);
}

// Rust is built with panic=abort, but libcore retains an unused unwind reference.
void rust_eh_personality(void) {}

void pbl_analytics_external_collect_pfs_stats(void) {
  PBL_ANALYTICS_SET_UNSIGNED(pfs_space_free_kb, get_available_pfs_space() / 1024);
}

void pfs_command_fs_format(const char *erase_headers) {
  pfs_format(atoi(erase_headers) == 1);
}

void pfs_command_dump_hdr(const char *page) {
  uint32_t pg = strtoul(page, NULL, 10);
  if (pg >= pfs_get_size() / 4096) {
    prompt_send_response("ERROR");
    return;
  }
  uint8_t header[86];
  ftl_read(header, sizeof(header), pg * 4096);
  PBL_HEXDUMP_D_SERIAL(LOG_LEVEL_DEBUG, header, sizeof(header));
}

void pfs_command_fs_ls(void) {
  char display_buf[80];
  PFSFileListEntry *head = pfs_create_file_list(NULL);
  for (PFSFileListEntry *entry = head; entry;
       entry = (PFSFileListEntry *)list_get_next(&entry->list_node)) {
    int fd = pfs_open(entry->name, OP_FLAG_READ | OP_FLAG_SKIP_HDR_CRC_CHECK, 0, 0);
    size_t size = fd >= 0 ? pfs_get_file_size(fd) : 0;
    if (fd >= 0) {
      pfs_close(fd);
    }
    prompt_send_response_fmt(display_buf, sizeof(display_buf), "%s\t%lu", entry->name,
                             (unsigned long)size);
  }
  pfs_delete_file_list(head);
  prompt_send_response_fmt(display_buf, sizeof(display_buf), "%lu bytes available",
                           (unsigned long)get_available_pfs_space());
}

void pfs_command_crc(const char *filename) {
  char buffer[32];
  int fd = pfs_open(filename, OP_FLAG_READ, 0, 0);
  if (fd < 0) {
    prompt_send_response_fmt(buffer, sizeof(buffer), "fd open err: %d", fd);
    return;
  }
  uint32_t crc = pfs_crc_calculate_file(fd, 0, pfs_get_file_size(fd));
  pfs_close(fd);
  prompt_send_response_fmt(buffer, sizeof(buffer), "CRC: %lx", (unsigned long)crc);
}
