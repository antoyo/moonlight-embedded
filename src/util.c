/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2017 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/** Writes a boolean sysfs-style value to a file path. */
int write_bool(char *path, bool val) {
  int fd = open(path, O_RDWR);

  if(fd >= 0) {
    int ret = write(fd, val ? "1" : "0", 1);
    if (ret < 0)
      fprintf(stderr, "Failed to write %d to %s: %d\n", val ? 1 : 0, path, ret);

    close(fd);
    return 0;
  } else
    return -1;
}

/** Writes a complete string value to a file path. */
int write_string(char *path, const char *val) {
  int fd = open(path, O_RDWR);

  if (fd >= 0) {
    size_t length = strlen(val);
    ssize_t ret = write(fd, val, length);

    if (ret < 0 || (size_t) ret != length) {
      fprintf(stderr, "Failed to write '%s' to %s\n", val, path);
      close(fd);
      return -1;
    }

    close(fd);
    return 0;
  } else
    return -1;
}

/** Reads bytes from a file path into the provided buffer. */
int read_file(char *path, char* output, int output_len) {
  int fd = open(path, O_RDONLY);

  if(fd >= 0) {
    output_len = read(fd, output, output_len);
    close(fd);
    return output_len;
  } else
    return -1;
}

/** Grows a heap buffer to at least the requested size. */
bool ensure_buf_size(void **buf, size_t *buf_size, size_t required_size) {
  if (*buf_size >= required_size)
    return false;

  *buf_size = required_size;
  *buf = realloc(*buf, *buf_size);
  if (!*buf) {
    fprintf(stderr, "Failed to allocate %zu bytes\n", *buf_size);
    abort();
  }

  return true;
}
