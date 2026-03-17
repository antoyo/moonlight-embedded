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

#include <stdbool.h>
#include <stddef.h>

/** Writes a boolean sysfs-style value to a file path. */
int write_bool(char *path, bool val);
/** Writes a complete string value to a file path. */
int write_string(char *path, const char *val);
/** Reads bytes from a file path into the provided buffer. */
int read_file(char *path, char *output, int output_len);
/** Grows a heap buffer to at least the requested size. */
bool ensure_buf_size(void **buf, size_t *buf_size, size_t required_size);
/** Returns true when the CPU supports a fast AES implementation. */
bool has_fast_aes(void);
