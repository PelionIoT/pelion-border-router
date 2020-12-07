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
#if defined(MBED_CONF_APP_WISUN_NETWORK_DNS_OPTIMIZATION) && (MBED_CONF_APP_WISUN_NETWORK_DNS_OPTIMIZATION == 1)

#include "mbed.h"
#include "WisunInterface.h"
#include "WisunBorderRouter.h"
#include "mbed-cloud-client/MbedCloudClient.h"
#include "factory_configurator_client.h"
#include "mbed-trace/mbed_trace.h"

#define TRACE_GROUP "aDoM"  //Application DNS Optimization Module

static WisunBorderRouter *ws_br;
static NetworkInterface *backbone_interface;
static char *lwm2m_server_name = NULL;
static char *bootstrap_server_name = NULL;
static char network_interface_name[10];

static int parse_address(uint8_t *raw_addr, int raw_addr_size, char **parsed_addr)
{
    int size_index;
    int end_index = -1;
    int start_index = -1;
    int parsed_addr_size = 0;

    for (size_index = 0; size_index < raw_addr_size; size_index++) {
        if (raw_addr[size_index] == ':') {
            end_index = size_index;
        }
    }
    if (end_index == -1) {
        tr_err("LWM2M Server name is not valid");
        return -1;
    } else {
        for (size_index = 0; size_index < end_index; size_index++) {
            if (raw_addr[size_index] == '/') {
                start_index = size_index;
            }
        }
        if (start_index == -1) {
            tr_err("LWM2M Server name is not valid");
            return -1;
        } else {
            start_index++; /* Pointing to the next character of '/' */
            parsed_addr_size = end_index - start_index;
            if (*parsed_addr == NULL) {
                *parsed_addr = (char *)malloc(parsed_addr_size + 1); /* Adding room for null character */
            }
            if (*parsed_addr == NULL) {
                tr_err("Could not allocate memory to store URL");
                return -1;
            }
            memcpy(*parsed_addr, raw_addr + start_index, parsed_addr_size);
            /* parsed_addr is a string, need to terminate with null character */
            *(*parsed_addr + parsed_addr_size) = '\0';
        }
    }
    return 0;
}

static void get_server_name(void)
{
    const int max_size = 256;
    size_t real_size = 0;
    uint8_t *buffer = (uint8_t *)malloc(max_size);

    if (buffer == NULL) {
        tr_err("Could not allocate memory to read URLs");
        return;
    }

    if (ccs_get_item(g_fcc_bootstrap_server_uri_name, buffer, max_size, &real_size, CCS_CONFIG_ITEM) == CCS_STATUS_SUCCESS) {
        if (parse_address(buffer, real_size, &bootstrap_server_name) == 0) {
            tr_debug("Bootstrap Server Name: %s", bootstrap_server_name);
        } else {
            tr_err("Could not parse Bootstrap server address");
        }
    } else {
        tr_err("ccs_get_item Failed to get Bootstrap Server Name");
    }

    if (ccs_get_item(g_fcc_lwm2m_server_uri_name, buffer, max_size, &real_size, CCS_CONFIG_ITEM) == CCS_STATUS_SUCCESS) {
        if (parse_address(buffer, real_size, &lwm2m_server_name) == 0) {
            tr_debug("LWM2M Server Name: %s", lwm2m_server_name);
        } else {
            tr_err("Could not parse LWM2M server address");
        }
    } else {
        tr_err("ccs_get_item Failed to get LWM2M Server Name");
    }

    free(buffer);
}

void network_dns_opt_configure(void *wisun_br, void *backbone_iface)
{
    ws_br = (WisunBorderRouter *)wisun_br;
    backbone_interface = (NetworkInterface *)backbone_iface;
    get_server_name();
    if (backbone_interface->get_interface_name(network_interface_name) == NULL) {
        tr_err("Could not get Network Interface Name");
    }
}

void bootstrap_addr_cb(nsapi_error_t result, SocketAddress *address)
{
    if (ws_br == NULL) {
        tr_err("Border router interface is NULL");
        return;
    }

    if (result != NSAPI_ERROR_OK) {
        tr_warn("Could not resolve Bootstrap Server URL");
        return;
    }

    tr_debug("Resolved Bootstrap Server Name: %s, IP: %s", bootstrap_server_name, address->get_ip_address());
    if (ws_br->set_dns_query_result(address, bootstrap_server_name) != MESH_ERROR_NONE) {
        tr_err("Could not set DNS query result for Bootstrap server");
    } else {
        tr_debug("Setting DNS Query Result for Bootstrap server: SUCCESS");
    }
}

void lwm2m_addr_cb(nsapi_error_t result, SocketAddress *address)
{
    if (ws_br == NULL) {
        tr_err("Border router interface is NULL");
        return;
    }

    if (result != NSAPI_ERROR_OK) {
        tr_warn("Could not resolve LWM2M Server URL");
        return;
    }

    tr_debug("Resolved LWM2M Server Name: %s, IP: %s", lwm2m_server_name, address->get_ip_address());
    if (ws_br->set_dns_query_result(address, lwm2m_server_name) != MESH_ERROR_NONE) {
        tr_err("Could not set DNS query result for LWM2M server");
    } else {
        tr_debug("Setting DNS Query Result for LWM2M server: SUCCESS");
    }
}

void network_dns_opt_query_set(void)
{
    nsapi_value_or_error_t ret_val = NSAPI_ERROR_OK;

    if (backbone_interface == NULL) {
        tr_err("Backbone Interface is NULL");
        return;
    }

    ret_val = backbone_interface->gethostbyname_async((const char *)bootstrap_server_name, bootstrap_addr_cb, NSAPI_IPv6, network_interface_name);
    if (ret_val < NSAPI_ERROR_OK) {
        tr_err("Could not resolve Bootstrap Server Address for %s Error: %d", bootstrap_server_name, ret_val);
    }

    ret_val = backbone_interface->gethostbyname_async((const char *)lwm2m_server_name, lwm2m_addr_cb, NSAPI_IPv6, network_interface_name);
    if (ret_val < NSAPI_ERROR_OK) {
        tr_err("Could not resolve LWM2M Server Address for %s Error: %d", lwm2m_server_name, ret_val);
    }
}
#endif  //defined(MBED_CONF_APP_WISUN_NETWORK_DNS_OPTIMIZATION) && (MBED_CONF_APP_WISUN_NETWORK_DNS_OPTIMIZATION == 1)
