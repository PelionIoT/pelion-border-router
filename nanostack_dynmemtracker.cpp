/*
 * Copyright (c) 2021 Pelion. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef MBED_CONF_APP_NSDYNMEMTRACKER_PRINT_INTERVAL

#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include "ns_types.h"
#if NSDYNMEM_TRACKER_ENABLED==1
#undef NSDYNMEM_TRACKER_ENABLED
#include "nsdynmemLIB.h"
#define NSDYNMEM_TRACKER_ENABLED 1
#include "ns_trace.h"
#include "mbedtls/md5.h"
#else
#include "nsdynmemLIB.h"
#endif
#include "events/Event.h"
#include "events/mbed_shared_queues.h"
#include "nsdynmem_tracker_lib.h"

#if NSDYNMEM_TRACKER_ENABLED!=1
#error "Enable nanostack libservice support for dynamic memory tracker: nanostack-libservice.nsdynmem-tracker-enabled"
#else

#define TRACE_GROUP "dynT"

// Default 10
#define TO_PERMANENT_ALLOCATORS_COUNT       (MBED_CONF_APP_NSDYNMEMTRACKER_TOP_ALLOCATORS * 2)
// Default 5
#define TOP_ALLOCATOR_COUNT                 MBED_CONF_APP_NSDYNMEMTRACKER_TOP_ALLOCATORS
// Default 5
#define PERMANENT_ALLOCATORS_COUNT          MBED_CONF_APP_NSDYNMEMTRACKER_TOP_ALLOCATORS

#define ONE_STEP_IN_SECONDS                 10
#define STEPS_BEFORE_TO_MOVE_TO_PERM_LIST   12 // 12 * 10 = 120 Seconds

// Default 15
#define MAX_LINES_TO_PRINT_ON_INTERVAL      (MBED_CONF_APP_NSDYNMEMTRACKER_TOP_ALLOCATORS * 3)
// Default 300, 5 minutes interval
#define PRINT_INTERVAL                      MBED_CONF_APP_NSDYNMEMTRACKER_PRINT_INTERVAL

#define MEM_BLOCKS_INITIAL_COUNT            500
#define MEM_BLOCKS_MAX                      4000

#define EXT_MEM_BLOCKS_COUNT                ((1024 << MBED_CONF_APP_NSDYNMEMTRACKER_EXT_BLOCKS_SIZE) - 1)
#define EXT_MEM_BLOCK_MASK                  (EXT_MEM_BLOCKS_COUNT & ~1)

static void ns_dyn_mem_tracker_timer_callback(void);
static ns_dyn_mem_tracker_lib_mem_blocks_t *ns_dyn_mem_tracker_allocate_mem_blocks(ns_dyn_mem_tracker_lib_mem_blocks_t *blocks, uint16_t *mem_blocks_count);
static ns_dyn_mem_tracker_lib_mem_blocks_ext_t *ns_dyn_mem_tracker_allocate_mem_blocks_ext(ns_dyn_mem_tracker_lib_mem_blocks_ext_t *blocks, uint32_t *mem_blocks_count);
static uint32_t ns_dyn_mem_tracker_mem_block_index_hash(void *block, uint32_t ext_mem_blocks_count);

// Dynamic memory tracker library configuration
static ns_dyn_mem_tracker_lib_conf_t conf = {
    .mem_blocks = NULL,
    .ext_mem_blocks = NULL,
    .top_allocators = NULL,
    .permanent_allocators = NULL,
    .to_permanent_allocators = NULL,
    .max_snap_shot_allocators = NULL,
    .alloc_mem_blocks = ns_dyn_mem_tracker_allocate_mem_blocks,
    .ext_alloc_mem_blocks = ns_dyn_mem_tracker_allocate_mem_blocks_ext,
    .block_index_hash = ns_dyn_mem_tracker_mem_block_index_hash,
    .allocated_memory = 0,
    .mem_blocks_count = 0,
    .ext_mem_blocks_count = 0,
    .last_mem_block_index = 0,
    .top_allocators_count = TOP_ALLOCATOR_COUNT,
    .permanent_allocators_count = PERMANENT_ALLOCATORS_COUNT,
    .to_permanent_allocators_count = TO_PERMANENT_ALLOCATORS_COUNT,
    .max_snap_shot_allocators_count = 0,
    .to_permanent_steps_count = STEPS_BEFORE_TO_MOVE_TO_PERM_LIST
};

static events::EventQueue *equeue;

static int tracker_print_interval_seconds = PRINT_INTERVAL;
static int max_lines_to_print_on_interval = MAX_LINES_TO_PRINT_ON_INTERVAL;
static bool tracker_init_done = false;
static bool tracker_disabled = false;
static bool error_on_memory_tracker = false;
static int equeue_event_identifier = 0;
static ns_dyn_mem_tracker_lib_mem_blocks_ext_t mem_ext_blocks[EXT_MEM_BLOCKS_COUNT];

int8_t ns_dyn_mem_tracker_init(void)
{
    if (tracker_init_done) {
        return 0;
    }

    tracker_init_done = true;

    if (conf.top_allocators == NULL) {
        conf.top_allocators = (ns_dyn_mem_tracker_lib_allocators_t *) malloc(conf.top_allocators_count * sizeof(ns_dyn_mem_tracker_lib_allocators_t));
    }
    if (conf.top_allocators == NULL) {
        tr_error("Dynamic memory tracker top allocators out of memory");
        error_on_memory_tracker = true;
        return -1;
    }

    if (conf.permanent_allocators == NULL) {
        conf.permanent_allocators = (ns_dyn_mem_tracker_lib_allocators_t *) malloc(conf.permanent_allocators_count * sizeof(ns_dyn_mem_tracker_lib_allocators_t));
    }
    if (conf.permanent_allocators == NULL) {
        tr_error("Dynamic memory tracker permanent allocators out of memory");
        error_on_memory_tracker = true;
        return -1;
    }

    if (conf.to_permanent_allocators == NULL) {
        conf.to_permanent_allocators = (ns_dyn_mem_tracker_lib_allocators_t *) malloc(conf.to_permanent_allocators_count * sizeof(ns_dyn_mem_tracker_lib_allocators_t));
    }
    if (conf.to_permanent_allocators == NULL) {
        tr_error("Dynamic memory tracker to permanent out of memory");
        error_on_memory_tracker = true;
        return -1;
    }

    equeue = mbed::mbed_event_queue();
    if (equeue == NULL) {
        tr_error("Dynamic memory tracker cannot get event queue");
        error_on_memory_tracker = true;
        return -1;
    }

    // Call every second
    equeue_event_identifier = equeue->call_every(1000, ns_dyn_mem_tracker_timer_callback);
    if (equeue_event_identifier == 0) {
        tr_error("Dynamic memory tracker cannot enable event queue periodic event");
        error_on_memory_tracker = true;
        return -1;
    }

    return 0;
}

extern "C" {

// Wrapper for ns_dyn_mem_alloc
void *ns_dyn_mem_tracker_dyn_mem_alloc(ns_mem_heap_size_t alloc_size, const char *function, uint32_t line)
{
    void *caller_addr = __builtin_extract_return_addr(__builtin_return_address(0));

    if (error_on_memory_tracker) {
        return ns_dyn_mem_alloc(alloc_size);
    }

    void *block = ns_dyn_mem_alloc(alloc_size);

    if (ns_dyn_mem_tracker_lib_alloc(&conf, caller_addr, function, line, block, alloc_size) < 0) {
        error_on_memory_tracker  = true;
    }

    return block;
}

// Wrapper for ns_dyn_mem_temporary_alloc
void *ns_dyn_mem_tracker_dyn_mem_temporary_alloc(ns_mem_heap_size_t alloc_size, const char *function, uint32_t line)
{
    void *caller_addr = __builtin_extract_return_addr(__builtin_return_address(0));

    if (error_on_memory_tracker) {
        return ns_dyn_mem_temporary_alloc(alloc_size);
    }

    void *block = ns_dyn_mem_temporary_alloc(alloc_size);

    if (ns_dyn_mem_tracker_lib_alloc(&conf, caller_addr, function, line, block, alloc_size) < 0) {
        error_on_memory_tracker  = true;
    }

    return block;
}

// Wrapper for ns_dyn_mem_free
void ns_dyn_mem_tracker_dyn_mem_free(void *block, const char *function, uint32_t line)
{
    if (tracker_disabled) {
        return ns_dyn_mem_free(block);
    }

    void *caller_addr = __builtin_extract_return_addr(__builtin_return_address(0));

    if (ns_dyn_mem_tracker_lib_free(&conf, caller_addr, function, line, block) < 0) {
        error_on_memory_tracker  = true;
    }

    ns_dyn_mem_free(block);
}

}

static ns_dyn_mem_tracker_lib_mem_blocks_t *ns_dyn_mem_tracker_allocate_mem_blocks(ns_dyn_mem_tracker_lib_mem_blocks_t *blocks, uint16_t *mem_blocks_count)
{
    static uint32_t alloc_size = MEM_BLOCKS_INITIAL_COUNT;

    ns_dyn_mem_tracker_lib_mem_blocks_t *new_blocks = NULL;

    if (blocks == NULL) {
        new_blocks = (ns_dyn_mem_tracker_lib_mem_blocks_t *) malloc(alloc_size * sizeof(ns_dyn_mem_tracker_lib_mem_blocks_t));
        if (!new_blocks) {
            *mem_blocks_count = 0;
            return NULL;
        }
        memset(new_blocks, 0, alloc_size * sizeof(ns_dyn_mem_tracker_lib_mem_blocks_t));
        *mem_blocks_count = alloc_size;
    } else {
        if (alloc_size == MEM_BLOCKS_MAX) {
            return NULL;
        }
        uint32_t new_size = alloc_size * 2;
        if (new_size > MEM_BLOCKS_MAX) {
            new_size = MEM_BLOCKS_MAX;
        }
        new_blocks = (ns_dyn_mem_tracker_lib_mem_blocks_t *) malloc(new_size * sizeof(ns_dyn_mem_tracker_lib_mem_blocks_t));
        if (!new_blocks) {
            free(blocks);
            alloc_size = 0xFFFF;
            *mem_blocks_count = 0;
            return NULL;
        }
        memset(new_blocks, 0, new_size * sizeof(ns_dyn_mem_tracker_lib_mem_blocks_t));
        memcpy(new_blocks, blocks, alloc_size * sizeof(ns_dyn_mem_tracker_lib_mem_blocks_t));
        free(blocks);
        alloc_size = new_size;
        *mem_blocks_count = new_size;
    }

    return new_blocks;
}

static ns_dyn_mem_tracker_lib_mem_blocks_ext_t *ns_dyn_mem_tracker_allocate_mem_blocks_ext(ns_dyn_mem_tracker_lib_mem_blocks_ext_t *blocks, uint32_t *mem_blocks_count)
{
    static int calls = 0;
    if (calls > 0) {
        // Increase EXT_MEM_BLOCKS_COUNT if more blocks are needed
        *mem_blocks_count = 0;
        return NULL;
    }
    calls++;

    *mem_blocks_count = EXT_MEM_BLOCKS_COUNT;
    return mem_ext_blocks;
}

static uint32_t ns_dyn_mem_tracker_mem_block_index_hash(void *block, uint32_t ext_mem_blocks_count)
{
    uint32_t ret_value = 0;
    uintptr_t number = (uintptr_t)block;
    const unsigned char *hashed_string = (const unsigned char *) &number;

    static uint8_t output[32];
    static mbedtls_md5_context ctx;

    mbedtls_md5_init(&ctx);

    if (mbedtls_md5_starts_ret(&ctx) != 0) {
        goto end;
    }

    if (mbedtls_md5_update_ret(&ctx, hashed_string, sizeof(void *)) != 0) {
        goto end;
    }

    if (mbedtls_md5_finish_ret(&ctx, output) != 0) {
        goto end;
    }

    ret_value = output[0] << 24;
    ret_value |= output[1] << 16;
    ret_value |= output[2] << 8;
    ret_value |= output[3];

    ret_value &= EXT_MEM_BLOCK_MASK;

    if (ret_value >= ext_mem_blocks_count) {
        ret_value = 0;
    }

end:
    mbedtls_md5_free(&ctx);

    return ret_value;
}

static uint32_t ns_dyn_mem_tracker_trace_top_allocators(uint32_t counter)
{
    for (uint16_t list_index = 0; list_index < conf.top_allocators_count; list_index++) {
        if (conf.top_allocators[list_index].caller_addr == NULL) {
            return counter;
        }
        if (counter == 0) {
            return 0;
        }
        counter--;
        if (list_index == 0) {
            tr_info("NSDYNMEM top allocators list");
        }
        tr_info("caller: %p count: %" PRIu32 " memory: %" PRIu32 " lifetime: %" PRIu32 " func: %s %" PRIu16,
            conf.top_allocators[list_index].caller_addr,   // Caller address
            conf.top_allocators[list_index].alloc_count,   // Number of allocation
            conf.top_allocators[list_index].total_memory,  // Total memory used by allocations
            conf.top_allocators[list_index].min_lifetime * ONE_STEP_IN_SECONDS, // Shortest lifetime of the allocations
            conf.top_allocators[list_index].function,      // Function name string
            conf.top_allocators[list_index].line);         // Line on module
    }

    return counter;
}

static uint32_t ns_dyn_mem_tracker_trace_to_permanent_allocators(uint32_t counter)
{
    for (uint16_t list_index = 0; list_index < conf.to_permanent_allocators_count; list_index++) {
        if (conf.to_permanent_allocators[list_index].caller_addr == NULL) {
            return counter;
        }
        if (counter == 0) {
            return 0;
        }
        counter--;
        if (list_index == 0) {
            tr_info("NSDYNMEM to permanent allocators list");
        }
        tr_info("caller: %p count: %" PRIu32 " memory: %" PRIu32 " lifetime: %" PRIu32 " func: %s %" PRIu16,
            conf.to_permanent_allocators[list_index].caller_addr,   // Caller address
            conf.to_permanent_allocators[list_index].alloc_count,   // Number of allocation
            conf.to_permanent_allocators[list_index].total_memory,  // Total memory used by allocations
            conf.to_permanent_allocators[list_index].min_lifetime * ONE_STEP_IN_SECONDS, // Shortest lifetime of the allocations
            conf.to_permanent_allocators[list_index].function,      // Function name string
            conf.to_permanent_allocators[list_index].line);         // Line on module
    }

    return counter;
}

static uint32_t ns_dyn_mem_tracker_trace_permanent_allocators(uint32_t counter)
{
    for (uint16_t list_index = 0; list_index < conf.permanent_allocators_count; list_index++) {
        if (conf.permanent_allocators[list_index].caller_addr == NULL) {
            return counter;
        }
        if (counter == 0) {
            return 0;
        }
        counter--;
        if (list_index == 0) {
            tr_info("NSDYNMEM permanent allocators list");
        }
        tr_info("caller: %p count: %" PRIu32 " memory: %" PRIu32 " lifetime: %" PRIu32 " func: %s %" PRIu16,
            conf.permanent_allocators[list_index].caller_addr,   // Caller address
            conf.permanent_allocators[list_index].alloc_count,   // Number of allocation
            conf.permanent_allocators[list_index].total_memory,  // Total memory used by allocations
            conf.permanent_allocators[list_index].min_lifetime * ONE_STEP_IN_SECONDS, // Shortest lifetime of the allocations
            conf.permanent_allocators[list_index].function,      // Function name string
            conf.permanent_allocators[list_index].line);         // Line on module
    }

    return counter;
}

static void ns_dyn_mem_tracker_timer_callback(void)
{
    static int counter = 0;

    // Step the memory analyzer
    static int seconds_to_step = ONE_STEP_IN_SECONDS;
    if (seconds_to_step > 0) {
        seconds_to_step--;
    } else {
        ns_dyn_mem_tracker_lib_step(&conf);
        seconds_to_step = ONE_STEP_IN_SECONDS;

        if (error_on_memory_tracker) {
            tr_info("NSDYNMEM disabled %s", error_on_memory_tracker ? "ERROR ON MEM ALLOCATION" : "");
        }
    }

    static int max_lines_to_print = 0;
    if (counter == tracker_print_interval_seconds - 4) {
        // Calculate memory block usages
        if (!error_on_memory_tracker && ns_dyn_mem_tracker_lib_allocator_lists_update(&conf) < 0) {
            error_on_memory_tracker = true;
            tr_error("Dynamic memory tracker internal error on lists update");
        }

        max_lines_to_print = max_lines_to_print_on_interval;

        tr_info("NSDYNMEM total memory: %" PRIu32 " allocators: %" PRIu16 " blocks: %" PRIu32 " last: %" PRIu16 " err: %s", conf.allocated_memory, conf.mem_blocks_count, conf.ext_mem_blocks_count, conf.last_mem_block_index, error_on_memory_tracker ? "ERROR" : "NONE");
        counter++;
    } else if (counter == tracker_print_interval_seconds - 3) {
        // Trace list of allocators going to list of allocators having permanent memory blocks
        max_lines_to_print = ns_dyn_mem_tracker_trace_to_permanent_allocators(max_lines_to_print);
        counter++;
    } else if (counter == tracker_print_interval_seconds - 2) {
        // Trace list of top allocators
        max_lines_to_print = ns_dyn_mem_tracker_trace_top_allocators(max_lines_to_print);
        counter++;
    } else if (counter == tracker_print_interval_seconds - 1) {
        // Trace list of allocators having permanent memory blocks
        max_lines_to_print = ns_dyn_mem_tracker_trace_permanent_allocators(max_lines_to_print);
        counter++;
    } else if (counter == tracker_print_interval_seconds) {
        // Start again
        counter = 0;
    } else {
        counter++;
    }
}

#endif
#endif
