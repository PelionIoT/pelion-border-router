/*
 * Copyright (c) 2020 ARM Limited. All rights reserved.
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

#include "nsdynmemLIB.h"

/* Enable nanostack extended heap only for specific targets and toolchains */
#if (MBED_CONF_APP_NANOSTACK_EXTENDED_HEAP == true)

#if defined(TARGET_K64F)
#define NANOSTACK_EXTENDED_HEAP_REGION_SIZE (60*1024)
#endif

#if defined(TARGET_NUCLEO_F429ZI)
#define NANOSTACK_EXTENDED_HEAP_REGION_SIZE (60*1024)
#endif

#if defined(TARGET_DISCO_F769NI)
#define NANOSTACK_EXTENDED_HEAP_REGION_SIZE (250*1024)
#endif

#if defined(TARGET_MIMXRT1050_EVK)
#define NANOSTACK_EXTENDED_HEAP_REGION_SIZE (4*1024*1024)
#endif

#if defined(__IAR_SYSTEMS_ICC__) || defined(__IAR_SYSTEMS_ASM__) || defined(__ICCARM__)
// currently - no IAR suport
#undef NANOSTACK_EXTENDED_HEAP_REGION_SIZE
#endif

#endif // MBED_CONF_APP_NANOSTACK_EXTENDED_HEAP


#ifdef NANOSTACK_EXTENDED_HEAP_REGION_SIZE

// Heap region for GCC_ARM, ARMCC and ARMCLANG
static uint8_t nanostack_extended_heap_region[NANOSTACK_EXTENDED_HEAP_REGION_SIZE];

void nanostack_heap_region_add(void)
{
    ns_dyn_mem_region_add(nanostack_extended_heap_region, NANOSTACK_EXTENDED_HEAP_REGION_SIZE);
}

#else // NANOSTACK_EXTENDED_HEAP_REGION_SIZE

void nanostack_heap_region_add(void)
{
}

#endif // NANOSTACK_EXTENDED_HEAP_REGION_SIZE
