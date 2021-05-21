/*
 * Copyright (c) 2020-2021 Pelion. All rights reserved.
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

#ifndef MBED_CLOUD_CLIENT_USER_CONFIG_H
#define MBED_CLOUD_CLIENT_USER_CONFIG_H

#define MBED_CLOUD_CLIENT_ENDPOINT_TYPE         "borderrouter"
#define MBED_CLOUD_CLIENT_TRANSPORT_MODE_UDP
#define MBED_CLOUD_CLIENT_LIFETIME              86400       /* 24 hours */

#ifdef MBED_CONF_MBED_CLIENT_SN_COAP_MAX_BLOCKWISE_PAYLOAD_SIZE
    #define SN_COAP_MAX_BLOCKWISE_PAYLOAD_SIZE    MBED_CONF_MBED_CLIENT_SN_COAP_MAX_BLOCKWISE_PAYLOAD_SIZE
#else
    #define SN_COAP_MAX_BLOCKWISE_PAYLOAD_SIZE      1024
#endif

/* set flag to enable update support in mbed Cloud client */
#define MBED_CLOUD_CLIENT_SUPPORT_UPDATE
#define MBED_CLOUD_CLIENT_SUPPORT_MULTICAST_UPDATE
#define MBED_CLOUD_CLIENT_MULTICAST_BORDER_ROUTER

/* Interval between firmware fragments during multicasting. */
#define MBED_CLOUD_CLIENT_MULTICAST_INTERVAL     60

/* set download buffer size in bytes (min. 1024 bytes) */
#define MBED_CLOUD_CLIENT_UPDATE_BUFFER          2048

#define MBED_CLOUD_DEV_UPDATE_CERT
#define MBED_CLOUD_DEV_UPDATE_ID

#endif /* MBED_CLOUD_CLIENT_USER_CONFIG_H */
