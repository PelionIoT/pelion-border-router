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

#ifndef NS_DYN_MEM_TRACKER_H
#define NS_DYN_MEM_TRACKER_H

#ifdef MBED_CONF_APP_NSDYNMEMTRACKER_PRINT_INTERVAL

int8_t ns_dyn_mem_tracker_init(void);

#else

#define ns_dyn_mem_tracker_init()

#endif

#endif /* NS_DYN_MEM_TRACKER_H */
