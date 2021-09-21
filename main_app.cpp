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

#include "mbed.h"
#include "NetworkInterface.h"
#include "kv_config.h"
#include "mbed-cloud-client/MbedCloudClient.h" // Required for new MbedCloudClient()
#include "factory_configurator_client.h"       // Required for fcc_* functions and FCC_* defines
#include "key_config_manager.h"                // Required for kcm_factory_reset
#include "mesh_system.h"
#include "WisunInterface.h"
#include "WisunBorderRouter.h"
#include "MeshInterfaceNanostack.h"
#include "network_dns_optimization.h"
#include "cloud_client_helper.h"
#include "kvstore_global_api.h"
#if defined MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER && (MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER == 1)
#include "NetworkManager.h"
#endif
#ifdef MBED_CLOUD_CLIENT_SUPPORT_MULTICAST_UPDATE
#include "multicast.h"
#endif
#if defined MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE && (MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE == 1)
#include "nsdynmemLIB.h"
#endif
#ifdef MBED_CONF_APP_NSDYNMEMTRACKER_PRINT_INTERVAL
#include "nanostack_dynmemtracker.h"
#endif

#include "mbed-trace/mbed_trace.h"             // Required for mbed_trace_*

#ifdef MBED_MEM_TRACING_ENABLED
#include "mbed_mem_trace.h"
#endif

#define TRACE_GROUP "App "

#ifndef DNS_QUERY_SET_PERIOD
#define DNS_QUERY_SET_PERIOD 12*60*60*1000     // 12 Hours
#endif

#ifndef BACKHAUL_CONNECTION_RETRY_TIMEOUT
#define BACKHAUL_CONNECTION_RETRY_TIMEOUT 1000
#endif
#ifndef BACKHAUL_CONNECTION_RETRY_TIMEOUT_MAX
#define BACKHAUL_CONNECTION_RETRY_TIMEOUT_MAX 300*1000
#endif

#ifndef MESH_IFACE_START_MAX_WAIT
#ifdef MBED_CONF_APP_MESH_IFACE_START_TIMEOUT
#define MESH_IFACE_START_MAX_WAIT (MBED_CONF_APP_MESH_IFACE_START_TIMEOUT*1000)
#else
#define MESH_IFACE_START_MAX_WAIT 10*60*1000   // 10 Minutes
#endif
#endif

#define MESH_IFACE_CTRL_VAL_MAX_SIZE        16
#define APP_STATE_VAL_MAX_SIZE              32
#define MESH_IFACE_CTRL_CONTINUE            "CONTINUE"
#define MESH_IFACE_CTRL_BLOCK               "BLOCK"
#define APP_STATE_WAIT_PERMISSION           "Waiting Permission"
#define APP_STATE_WISUN_BOOTING             "Wi-SUN Booting"
#define APP_STATE_WISUN_ACTIVE              "Wi-SUN Active"

static NetworkInterface *backhaul_interface = NULL;
static WisunInterface *mesh_interface = NULL;
static WisunBorderRouter ws_border_router;
static MbedCloudClient *cloud_client = NULL;
static M2MObjectList m2m_obj_list;
#if defined MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER && (MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER == 1)
static NetworkManager ws_network_manager;
#endif

static M2MResource *m2m_get_res;
static M2MResource *m2m_put_res;
static M2MResource *m2m_post_res;
static M2MResource *m2m_deregister_res;
static M2MResource *m2m_factory_reset_res;
static M2MResource *mesh_iface_control;
static M2MResource *app_state;

static void mesh_connect(void);
static void check_mesh_iface_control(void);
static char app_mesh_control_kv_key[] = "/kv/mesh_iface_ctrl_key";
static char mesh_iface_control_value[MESH_IFACE_CTRL_VAL_MAX_SIZE] = {0, };
static char app_state_value[APP_STATE_VAL_MAX_SIZE] = {0, };
static bool mesh_interface_up = false;
rtos::Semaphore mesh_control_data_found;

static EventQueue *queue;
static EventQueue *mesh_iface_control_queue;

#if defined MBED_CONF_APP_ACTIVE_KEEP_ALIVE && (MBED_CONF_APP_ACTIVE_KEEP_ALIVE == 1)
static EventQueue *keep_alive_queue;
#endif

#if defined MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE && (MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE == 1)
#if !defined MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE_INTERVAL
#error "MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE_INTERVAL is not defined"
#endif
static EventQueue *mem_stats_queue;
extern mem_stat_t app_ns_dyn_mem_stats;
#endif
rtos::Semaphore mesh_global_ip;

typedef enum app_status {
    APP_STATUS_FAIL = -1,
    APP_STATUS_SUCCESS = 0
} app_status_t;

#if defined MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE && (MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE == 1)
static void print_ns_heap_stats(void)
{
#ifdef MBED_CONF_MBED_MESH_API_HEAP_STAT_INFO_DEFINITION
    mem_stat_t *stat = &app_ns_dyn_mem_stats;
    tr_info(
        "NS HeapSize: %lu, Res: %lu, ResMax: %lu, allocFail: %lu"
        , (unsigned long)stat->heap_sector_size
        , (unsigned long)stat->heap_sector_allocated_bytes
        , (unsigned long)stat->heap_sector_allocated_bytes_max
        , (unsigned long)stat->heap_alloc_fail_cnt);
#endif
}

static void print_mbed_heap_stats(void)
{
    mbed_stats_heap_t heap_stats;
    mbed_stats_heap_get(&heap_stats);
    tr_info(
        "MBED CurSize: %lu, MaxSize: %lu, TotalSize: %lu, ResSize: %lu, allocFail: %lu"
        , (unsigned long)heap_stats.current_size
        , (unsigned long)heap_stats.max_size
        , (unsigned long)heap_stats.total_size
        , (unsigned long)heap_stats.reserved_size
        , (unsigned long)heap_stats.alloc_fail_cnt);
}

static void print_mem_stats(void)
{
    print_ns_heap_stats();
    print_mbed_heap_stats();
}
#endif

static void print_client_ids(void)
{
    printf("Account ID: %s\n", cloud_client->endpoint_info()->account_id.c_str());
    printf("EndpointName: %s\n", cloud_client->endpoint_info()->internal_endpoint_name.c_str());
    printf("Device ID: %s\n", cloud_client->endpoint_info()->endpoint_name.c_str());
}

/* Cloud client disconnecting */
static void deregister_client(void)
{
    printf("Unregistering from the network\n");
    cloud_client->close();
}

static void client_registered(void)
{
    printf("Client registered\n");
    print_client_ids();

    if (!mesh_interface_up) {
        check_mesh_iface_control();
    } else {
#if defined MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER && (MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER == 1)
        ws_network_manager.nm_cloud_client_connect_indication();
#endif

#if defined(MBED_CONF_APP_WISUN_NETWORK_DNS_OPTIMIZATION) && (MBED_CONF_APP_WISUN_NETWORK_DNS_OPTIMIZATION == 1)
        network_dns_opt_configure(&ws_border_router, backhaul_interface);
        network_dns_opt_query_set();
#if MBED_MAJOR_VERSION > 5
        queue->call_every(std::chrono::milliseconds(DNS_QUERY_SET_PERIOD), network_dns_opt_query_set);
#else
        queue->call_every(DNS_QUERY_SET_PERIOD, network_dns_opt_query_set);
#endif
#endif
    }
}

static void client_unregistered(void)
{
    printf("Client unregistered\n");
}

static void client_error(int err)
{
    tr_err("client_error(%d) -> %s", err, cloud_client->error_description());
}

#ifdef MBED_CLOUD_CLIENT_SUPPORT_UPDATE
static void update_progress(uint32_t progress, uint32_t total)
{
    uint8_t percent = (uint8_t)((uint64_t)progress * 100 / total);
    tr_info("Update progress = %" PRIu8 "%%", percent);
}
#endif

static void get_res_update(const char * /*object_name*/)
{
    tr_info("Counter resource set to %d\n", (int)m2m_get_res->get_value_int());
}

static void put_res_update(const char * /*object_name*/)
{
    printf("PUT update %s\n", m2m_put_res->get_value_string().c_str());
}

static void execute_post(void * /*arguments*/)
{
    tr_info("POST executed\n");
}

static void deregister(void * /*arguments*/)
{
    tr_info("POST deregister executed\n");
    m2m_deregister_res->send_delayed_post_response();

    deregister_client();
}

static void factory_reset(void *)
{
    tr_info("POST factory reset executed\n");
    m2m_factory_reset_res->send_delayed_post_response();

    kcm_factory_reset();
}

static void mesh_iface_start_max_wait_cb(void)
{
    mesh_control_data_found.release();
}

static void check_mesh_iface_control(void)
{
    // if the mesh_iface_control_value is not MESH_IFACE_CTRL_BLOCK, then release the mesh_control_data_found semaphore
    if (strncmp(mesh_iface_control_value, MESH_IFACE_CTRL_BLOCK, strlen(MESH_IFACE_CTRL_BLOCK))) {
        mesh_control_data_found.release();
    } else {
        strcpy(app_state_value, APP_STATE_WAIT_PERMISSION);
        tr_warn("Waiting for mesh_iface_control_value to start the mesh interface");
        if (MESH_IFACE_START_MAX_WAIT != 0) {
#if MBED_MAJOR_VERSION > 5
            mesh_iface_control_queue->call_in(std::chrono::milliseconds(MESH_IFACE_START_MAX_WAIT), mesh_iface_start_max_wait_cb);
#else
            mesh_iface_control_queue->call_in(MESH_IFACE_START_MAX_WAIT, mesh_iface_start_max_wait_cb);
#endif
        }
    }
}

static void mesh_iface_control_cb(const char * /*object_name*/)
{
    int kv_status;
    String mesh_iface_cval = mesh_iface_control->get_value_string();

    if (mesh_iface_cval.c_str() == NULL) {
        tr_error("Received Mesh Control Data is NULL\n");
        return;
    }

    if (strlen((char *)mesh_iface_cval.c_str()) <= MESH_IFACE_CTRL_VAL_MAX_SIZE) {
        strcpy(mesh_iface_control_value, (char *)mesh_iface_cval.c_str());
    } else {
        strncpy(mesh_iface_control_value, (char *)mesh_iface_cval.c_str(), MESH_IFACE_CTRL_VAL_MAX_SIZE - 1);
    }

    tr_info("Received Mesh Control Data: %s", mesh_iface_control_value);

    /* Set Key/Value pair with unprotected clear value data */
    tr_debug("Setting mesh_iface_control_value in KVStore using key: %s\n", app_mesh_control_kv_key);
    kv_status = kv_set(app_mesh_control_kv_key, mesh_iface_control_value, strlen((char *)mesh_iface_cval.c_str()) + 1, 0);
    if (kv_status != MBED_SUCCESS) {
        tr_warn("Could not set Mesh Control Data into KVStore, Error: %d", MBED_GET_ERROR_CODE(kv_status));
    } else {
        tr_info("Mesh Control Data stored into KVStore");
    }

    if (!mesh_interface_up) {
        // if the received value is not MESH_IFACE_CTRL_BLOCK, then release the mesh_control_data_found semaphore
        if (strncmp(mesh_iface_control_value, MESH_IFACE_CTRL_BLOCK, strlen(MESH_IFACE_CTRL_BLOCK))) {
            mesh_control_data_found.release();
        } else {
            tr_warn("Mesh interface is not started");
        }
    }
}

static coap_response_code_e app_res_read_cb(const M2MResourceBase &resource,
                                            uint8_t *&buffer,
                                            size_t &buffer_size,
                                            size_t &total_size,
                                            const size_t offset,
                                            void *client_args)
{
    tr_info("GET request received for resource: %s", resource.uri_path());

    M2MBase *obj = (M2MBase *)client_args;
    if (obj == mesh_iface_control) {
        buffer = (uint8_t *)mesh_iface_control_value;
        buffer_size = strlen(mesh_iface_control_value);
        tr_debug("Setting mesh_iface_control_value to Client: %s", mesh_iface_control_value);
    }

    if (obj == app_state) {
        buffer = (uint8_t *)app_state_value;
        buffer_size = strlen(app_state_value);
        tr_debug("Setting app_state_value to Client: %s", app_state_value);
    }

    return COAP_RESPONSE_CONTENT;
}

static app_status_t PDMC_create_resource(void)
{
    // PUT/GET resource 33455/0/13
    mesh_iface_control = M2MInterfaceFactory::create_resource(m2m_obj_list, 33455, 0, 13, M2MResourceInstance::STRING, M2MBase::GET_PUT_ALLOWED);
    if (mesh_iface_control->set_value_updated_function(mesh_iface_control_cb) != true) {
        tr_error("mesh_iface_control->set_value_updated_function() failed");
        return APP_STATUS_FAIL;
    }
    mesh_iface_control->set_read_resource_function(app_res_read_cb, mesh_iface_control);

    // GET resource 33455/0/14
    app_state = M2MInterfaceFactory::create_resource(m2m_obj_list, 33455, 0, 14, M2MResourceInstance::STRING, M2MBase::GET_ALLOWED);
    app_state->set_read_resource_function(app_res_read_cb, app_state);

    // GET resource 3200/0/5501
    // PUT also allowed for resetting the resource
    m2m_get_res = M2MInterfaceFactory::create_resource(m2m_obj_list, 3200, 0, 5501, M2MResourceInstance::INTEGER, M2MBase::GET_PUT_ALLOWED);
    if (m2m_get_res->set_value(0) != true) {
        tr_info("m2m_get_res->set_value() failed\n");
        return APP_STATUS_FAIL;
    }
    if (m2m_get_res->set_value_updated_function(get_res_update) != true) {
        tr_info("m2m_get_res->set_value_updated_function() failed\n");
        return APP_STATUS_FAIL;
    }

    // PUT resource 3201/0/5853
    m2m_put_res = M2MInterfaceFactory::create_resource(m2m_obj_list, 3201, 0, 5853, M2MResourceInstance::STRING, M2MBase::GET_PUT_ALLOWED);
    if (m2m_put_res->set_value(0) != true) {
        tr_info("m2m_put_res->set_value() failed\n");
        return APP_STATUS_FAIL;
    }
    if (m2m_put_res->set_value_updated_function(put_res_update) != true) {
        tr_info("m2m_put_res->set_value_updated_function() failed\n");
        return APP_STATUS_FAIL;
    }

    // POST resource 3201/0/5850
    m2m_post_res = M2MInterfaceFactory::create_resource(m2m_obj_list, 3201, 0, 5850, M2MResourceInstance::STRING, M2MBase::POST_ALLOWED);
    if (m2m_post_res->set_execute_function(execute_post) != true) {
        tr_info("m2m_post_res->set_execute_function() failed\n");
        return APP_STATUS_FAIL;
    }

    // POST resource 5000/0/1 to trigger deregister.
    m2m_deregister_res = M2MInterfaceFactory::create_resource(m2m_obj_list, 5000, 0, 1, M2MResourceInstance::INTEGER, M2MBase::POST_ALLOWED);

    // Use delayed response
    m2m_deregister_res->set_delayed_response(true);

    if (m2m_deregister_res->set_execute_function(deregister) != true) {
        tr_info("m2m_post_res->set_execute_function() failed\n");
        return APP_STATUS_FAIL;
    }

    // optional Device resource for running factory reset for the device. Path of this resource will be: 3/0/6.
    m2m_factory_reset_res = M2MInterfaceFactory::create_device()->create_resource(M2MDevice::FactoryReset);
    if (m2m_factory_reset_res) {
        m2m_factory_reset_res->set_execute_function(factory_reset);
    }

    return APP_STATUS_SUCCESS;
}

static app_status_t PDMC_init(void)
{
    tr_info("Initializing Cloud Client");

#if defined(MBED_CONF_APP_DEVELOPER_MODE) && (MBED_CONF_APP_DEVELOPER_MODE == 1)
    // Run developer flow
    int status;
    tr_info("Start developer flow");
    status = fcc_init();
    if (status != FCC_STATUS_SUCCESS) {
        tr_err("fcc_init() failed with %d", status);
        return APP_STATUS_FAIL;
    }

#if RESET_STORAGE
    status = platform_reset_storage();
    if (status != FCC_STATUS_SUCCESS) {
        printf("application_init_fcc reset_storage failed with status %d! - exit\r\n", status);
        return APP_STATUS_FAIL;
    }
#endif

    if (verify_cloud_config() != 0) {
#ifndef MCC_MINIMAL
        // This is designed to simplify user-experience by auto-formatting the
        // primary storage if no valid certificates exist.
        // This should never be used for any kind of production devices.
#ifndef MBED_CONF_APP_MCC_NO_AUTO_FORMAT
        status = platform_reset_storage();
        if (status != FCC_STATUS_SUCCESS) {
            return APP_STATUS_FAIL;
        }
        if (verify_cloud_config() == APP_STATUS_FAIL) {
            return APP_STATUS_FAIL;
        }
#else
        return APP_STATUS_FAIL;
#endif
#endif
    }
#endif

#ifdef MBED_CLOUD_CLIENT_SUPPORT_UPDATE
    cloud_client = new MbedCloudClient(client_registered, client_unregistered, client_error, NULL, update_progress);
#else
    cloud_client = new MbedCloudClient(client_registered, client_unregistered, client_error);
#endif // MBED_CLOUD_CLIENT_SUPPORT_UPDATE
    return APP_STATUS_SUCCESS;
}

#ifdef MBED_CLOUD_CLIENT_SUPPORT_MULTICAST_UPDATE
static int8_t get_mesh_iface_id()
{
    if (mesh_interface == NULL) {
        return MESH_ERROR_UNKNOWN;
    }

    int8_t mesh_if_id = mesh_interface->get_interface_id();
    if (mesh_if_id < 0) {
        return MESH_ERROR_UNKNOWN;
    }

    tr_debug("Mesh Interface ID: %d", mesh_if_id);
    return mesh_if_id;
}
#endif

static void mesh_interface_status_callback(nsapi_event_t status, intptr_t param)
{
    SocketAddress sa;

    if (status == NSAPI_EVENT_CONNECTION_STATUS_CHANGE) {
        switch (param) {
            case NSAPI_STATUS_GLOBAL_UP:
                printf("Mesh Interface::NSAPI_STATUS_GLOBAL_UP");
                if (mesh_interface->get_ip_address(&sa) != 0) {
                    tr_err("Could not fetch Global IP of Mesh Interface");
                    break;
                }
                tr_info("Mesh Interface connected with IP %s", sa.get_ip_address());
                mesh_interface_up = true;
                strcpy(app_state_value, APP_STATE_WISUN_ACTIVE);
                mesh_global_ip.release();
                break;
            case NSAPI_STATUS_LOCAL_UP:
                tr_info("Mesh Interface::NSAPI_STATUS_LOCAL_UP");
                break;
            case NSAPI_STATUS_DISCONNECTED:
                tr_info("Mesh Interface::NSAPI_STATUS_DISCONNECTED");
                break;
            case NSAPI_STATUS_CONNECTING:
                tr_info("Mesh Interface::NSAPI_STATUS_CONNECTING");
                break;
            case NSAPI_STATUS_ERROR_UNSUPPORTED:
            default:
                tr_info("Mesh Interface::NSAPI_STATUS_ERROR_UNSUPPORTED");
                break;
        }
    }
}

static void mesh_connect(void)
{
    int status;
    tr_info("Connect to Mesh Interface");

    // Updating App state as APP_STATE_WISUN_BOOTING
    strcpy(app_state_value, APP_STATE_WISUN_BOOTING);

    mesh_interface->add_event_listener(mbed::callback(&mesh_interface_status_callback));
    status = mesh_interface->connect();
    if (status == NSAPI_ERROR_OK || status == NSAPI_ERROR_IS_CONNECTED) {
        if (backhaul_interface != NULL) {
            if (ws_border_router.start(mesh_interface, backhaul_interface) != MESH_ERROR_NONE) {
                printf("FAILED to start Border Router\n");
                return;
            }
        } else {
            tr_warn("Backhaul Interface is not yet started");
        }
    } else {
        tr_err("Mesh Interface failed to connect with %d", status);
    }
    return;
}

static app_status_t backhaul_connect(void)
{
    int status;
    SocketAddress sa;
    int retry_count = 0;
    int next_retry_interval;
    bool backhaul_connected = false;

    if (backhaul_interface == NULL) {
        tr_warn("Backhaul Interface is not Initialized yet");
        return APP_STATUS_FAIL;
    }

    while (!backhaul_connected) {
        retry_count++;
        status = backhaul_interface->connect();
        tr_info("Backhaul MAC Address: %s", backhaul_interface->get_mac_address() ? backhaul_interface->get_mac_address() : "None");
        if (status == NSAPI_ERROR_OK || status == NSAPI_ERROR_IS_CONNECTED) {
            status = backhaul_interface->get_ip_address(&sa);
            if (status != NSAPI_ERROR_OK) {
                tr_debug("Backhaul get_ip_address() - failed, status %d", status);
                goto retry;
            } else {
                backhaul_connected = true;
                continue;
            }
        }
        tr_warn("Failed to connect! error=%d. Retry %d", status, retry_count);
retry:
        (void) backhaul_interface->disconnect();
        next_retry_interval = BACKHAUL_CONNECTION_RETRY_TIMEOUT * retry_count;
        if (next_retry_interval > BACKHAUL_CONNECTION_RETRY_TIMEOUT_MAX) {
            next_retry_interval = BACKHAUL_CONNECTION_RETRY_TIMEOUT_MAX;
        }
#if MBED_MAJOR_VERSION > 5
        ThisThread::sleep_for(std::chrono::milliseconds(next_retry_interval));
#else
        ThisThread::sleep_for(next_retry_interval);
#endif
    }

    tr_info("Backhaul Interface connected with IP %s", sa.get_ip_address() ? sa.get_ip_address() : "None");
    return APP_STATUS_SUCCESS;
}

#ifndef MBED_TEST_MODE
int main(void)
{
    int status;

    queue = mbed_event_queue();
    mesh_iface_control_queue = mbed_event_queue();

#if defined MBED_CONF_APP_ACTIVE_KEEP_ALIVE && (MBED_CONF_APP_ACTIVE_KEEP_ALIVE == 1)
    keep_alive_queue = mbed_event_queue();
#endif

#if defined MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE && (MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE == 1)
    mem_stats_queue = mbed_event_queue();
#if MBED_MAJOR_VERSION > 5
    mem_stats_queue->call_every(std::chrono::milliseconds(MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE_INTERVAL), print_mem_stats);
#else
    mem_stats_queue->call_every(MBED_CONF_APP_MEM_STATS_PERIODIC_TRACE_INTERVAL, print_mem_stats);
#endif
#endif

#ifdef MBED_CONF_APP_NSDYNMEMTRACKER_PRINT_INTERVAL
    ns_dyn_mem_tracker_init();
#endif

    status = mbed_trace_init();
    if (status != 0) {
        tr_err("mbed_trace_init() failed with %d", status);
        return -1;
    }

    tr_info("Pelion Border Router Application");
#if defined(MBED_MAJOR_VERSION) && defined(MBED_MINOR_VERSION) && defined(MBED_PATCH_VERSION)
    tr_info("Mbed OS version %d.%d.%d", MBED_MAJOR_VERSION, MBED_MINOR_VERSION, MBED_PATCH_VERSION);
#else
    tr_info("Mbed OS version <UNKNOWN>");
#endif

    // Mount default kvstore
    status = kv_init_storage_config();
    if (status != MBED_SUCCESS) {
        tr_err("kv_init_storage_config() - failed, status %d", status);
        return -1;
    }

    // Backhaul Interface
    tr_info("Fetching Backhaul Interface");
    backhaul_interface = NetworkInterface::get_default_instance();
    if (backhaul_interface == NULL) {
        tr_err("Failed to get default NetworkInterface");
        return -1;
    }

    // Mesh Interface
    tr_info("Fetching Mesh Interface");
    mesh_interface = static_cast<WisunInterface *>(MeshInterface::get_default_instance());
    if (mesh_interface == NULL) {
        tr_err("Failed to get default MeshInterface");
        return -1;
    }

#if defined MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER && (MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER == 1)
    if (ws_network_manager.configure_factory_mac_address(mesh_interface, backhaul_interface) != NM_ERROR_NONE) {
        tr_err("Failed to configure Factory MAC addresses on Interfaces");
        return -1;
    }
#endif

    tr_info("Connect to Backhaul Interaface");
    if (backhaul_connect() == APP_STATUS_FAIL) {
        tr_err("Failed to connect Backhaul Interface");
        return -1;
    }

    if (PDMC_init() == APP_STATUS_FAIL) {
        tr_err("Failed to Initialize Cloud Client: Terminating Application");
        return -1;
    }

#if defined MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER && (MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER == 1)
    tr_info("Configuring Interfaces");
    if (ws_network_manager.reg_and_config_iface(mesh_interface, backhaul_interface, &ws_border_router) != NM_ERROR_NONE) {
        tr_err("Failed to register and configure Interfaces");
        return -1;
    }
#endif

    // Read default Mesh Interface Control
#ifdef MBED_CONF_APP_MESH_IFACE_START_CONTROL
    size_t actual_size = 0;
    strncpy(mesh_iface_control_value, MBED_CONF_APP_MESH_IFACE_START_CONTROL, strlen(MBED_CONF_APP_MESH_IFACE_START_CONTROL));
    kv_get(app_mesh_control_kv_key, mesh_iface_control_value, MESH_IFACE_CTRL_VAL_MAX_SIZE, &actual_size);
    if (kv_set(app_mesh_control_kv_key, mesh_iface_control_value, actual_size, 0) != MBED_SUCCESS) {
        tr_error("Could not set Default Mesh Control Data into KVStore");
    }
#endif

    if (PDMC_create_resource() == APP_STATUS_FAIL) {
        tr_err("Failed to Create Resources: Terminating Application");
        return -1;
    }

#if defined MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER && (MBED_CONF_MBED_CLOUD_CLIENT_NETWORK_MANAGER == 1)
    ws_network_manager.create_resource(&m2m_obj_list);
#endif

    cloud_client->add_objects(m2m_obj_list);
    cloud_client->setup(backhaul_interface);

    mesh_control_data_found.acquire();
    mesh_connect();
    mesh_global_ip.acquire();

#ifdef MBED_CLOUD_CLIENT_SUPPORT_MULTICAST_UPDATE
    arm_uc_multicast_interface_configure(get_mesh_iface_id());
#endif

    cloud_client->setup(backhaul_interface, true);

#ifdef MBED_MEM_TRACING_ENABLED
    mbed_mem_trace_set_callback(mbed_mem_trace_default_callback);
#endif

    queue->dispatch_forever();
}

#endif /* MBED_TEST_MODE */
