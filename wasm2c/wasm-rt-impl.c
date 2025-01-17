/*
 * Copyright 2018 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Sandbox: move implementation to wasm-rt
#include "wasm-rt.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Sandbox: remove signal handler, introduce WASM_GUARDPAGE_MODEL
#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__)) && \
    defined(__WORDSIZE) && __WORDSIZE == 64
#define WASM_GUARDPAGE_MODEL
#endif

#if defined(WASM_GUARDPAGE_MODEL)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define PAGE_SIZE 65536

void wasm_rt_trap(wasm_rt_trap_t code) {
  assert(code != WASM_RT_TRAP_NONE);
  abort();
}

static bool func_types_are_equal(wasm_func_type_t* a, wasm_func_type_t* b) {
  if (a->param_count != b->param_count || a->result_count != b->result_count)
    return 0;
  uint32_t i;
  for (i = 0; i < a->param_count; ++i)
    if (a->params[i] != b->params[i])
      return 0;
  for (i = 0; i < a->result_count; ++i)
    if (a->results[i] != b->results[i])
      return 0;
  return 1;
}

// Sandbox: use wasm_func_type_t and p_func_type
uint32_t wasm_rt_register_func_type(wasm_func_type_t** p_func_type_structs,
                                    uint32_t* p_func_type_count,
                                    uint32_t param_count,
                                    uint32_t result_count,
                                    wasm_rt_type_t* types) {
  wasm_func_type_t func_type;
  func_type.param_count = param_count;
  func_type.params = malloc(param_count * sizeof(wasm_rt_type_t));
  func_type.result_count = result_count;
  func_type.results = malloc(result_count * sizeof(wasm_rt_type_t));

  uint32_t i;
  for (i = 0; i < param_count; ++i)
    func_type.params[i] = types[i];
  for (i = 0; i < result_count; ++i)
    func_type.results[i] = types[(uint64_t)(param_count) + i];

  for (i = 0; i < *p_func_type_count; ++i) {
    wasm_func_type_t* func_types = *p_func_type_structs;
    if (func_types_are_equal(&func_types[i], &func_type)) {
      free(func_type.params);
      free(func_type.results);
      return i + 1;
    }
  }

  uint32_t idx = (*p_func_type_count)++;
  // Sandbox: realloc works fine even if *p_func_type_structs is null

  *p_func_type_structs = realloc(*p_func_type_structs,
                                 *p_func_type_count * sizeof(wasm_func_type_t));
  (*p_func_type_structs)[idx] = func_type;
  return idx + 1;
}

void wasm_rt_allocate_memory(wasm_rt_memory_t* memory,
                             uint32_t initial_pages,
                             uint32_t max_pages) {
  uint32_t byte_length = initial_pages * PAGE_SIZE;
#if defined(WASM_GUARDPAGE_MODEL)
  /* Reserve 8GiB. */
  void* addr =
      mmap(NULL, 0x200000000ul, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == (void*)-1) {
    perror("mmap failed");
    abort();
  }
  mprotect(addr, byte_length, PROT_READ | PROT_WRITE);
  memory->data = addr;
#else
  memory->data = calloc(byte_length, 1);
#endif
  memory->size = byte_length;
  memory->pages = initial_pages;
  memory->max_pages = max_pages;
}

uint32_t wasm_rt_grow_memory(wasm_rt_memory_t* memory, uint32_t delta) {
  uint32_t old_pages = memory->pages;
  uint32_t new_pages = memory->pages + delta;
  if (new_pages == 0) {
    return 0;
  }
  if (new_pages < old_pages || new_pages > memory->max_pages) {
    return (uint32_t)-1;
  }
  uint32_t old_size = old_pages * PAGE_SIZE;
  uint32_t new_size = new_pages * PAGE_SIZE;
  uint32_t delta_size = delta * PAGE_SIZE;

#if defined(WASM_GUARDPAGE_MODEL)
  uint8_t* new_data = memory->data;
  mprotect(new_data + old_size, delta_size, PROT_READ | PROT_WRITE);
#else
  uint8_t* new_data = realloc(memory->data, new_size);
  if (new_data == NULL) {
    return (uint32_t)-1;
  }
#if !WABT_BIG_ENDIAN
  memset(new_data + old_size, 0, delta_size);
#endif
#endif
#if WABT_BIG_ENDIAN
  memmove(new_data + new_size - old_size, new_data, old_size);
  memset(new_data, 0, delta_size);
#endif
  memory->pages = new_pages;
  memory->size = new_size;
  memory->data = new_data;
  return old_pages;
}

void wasm_rt_allocate_table(wasm_rt_table_t* table,
                            uint32_t elements,
                            uint32_t max_elements) {
  assert(max_elements >= elements);
  table->size = elements;
  table->max_size = max_elements;
  table->data = calloc(table->max_size, sizeof(wasm_rt_elem_t));
}

// Sandbox
#undef WASM_GUARDPAGE_MODEL
#undef PAGE_SIZE