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

#include "mbed.h"
#include "mbed-cloud-client/MbedCloudClient.h" // Required for new MbedCloudClient()
#include "factory_configurator_client.h"       // Required for fcc_* functions and FCC_* defines
#include "mbed-trace/mbed_trace.h"             // Required for mbed_trace_*

#define TRACE_GROUP "CC_h"

void print_fcc_status(int fcc_status)
{
#ifndef DISABLE_ERROR_DESCRIPTION
    const char *error;
    switch (fcc_status) {
        case FCC_STATUS_SUCCESS:
            return;
        case FCC_STATUS_ERROR :
            error = "Operation ended with an unspecified error.";
            break;
        case FCC_STATUS_MEMORY_OUT:
            error = "An out-of-memory condition occurred.";
            break;
        case FCC_STATUS_INVALID_PARAMETER:
            error = "A parameter provided to the function was invalid.";
            break;
        case FCC_STATUS_STORE_ERROR:
            error = "Storage internal error.";
            break;
        case FCC_STATUS_INTERNAL_ITEM_ALREADY_EXIST:
            error = "Current item already exists in storage.";
            break;
        case FCC_STATUS_CA_ERROR:
            error = "CA Certificate already exist in storage (currently only bootstrap CA)";
            break;
        case FCC_STATUS_ROT_ERROR:
            error = "ROT already exist in storage";
            break;
        case FCC_STATUS_ENTROPY_ERROR:
            error = "Entropy already exist in storage";
            break;
        case FCC_STATUS_FACTORY_DISABLED_ERROR:
            error = "FCC flow was disabled - denial of service error.";
            break;
        case FCC_STATUS_INVALID_CERTIFICATE:
            error = "Invalid certificate found.";
            break;
        case FCC_STATUS_INVALID_CERT_ATTRIBUTE:
            error = "Operation failed to get an attribute.";
            break;
        case FCC_STATUS_INVALID_CA_CERT_SIGNATURE:
            error = "Invalid ca signature.";
            break;
        case FCC_STATUS_EXPIRED_CERTIFICATE:
            error = "LWM2M or Update certificate is expired.";
            break;
        case FCC_STATUS_INVALID_LWM2M_CN_ATTR:
            error = "Invalid CN field of certificate.";
            break;
        case FCC_STATUS_KCM_ERROR:
            error = "KCM basic functionality failed.";
            break;
        case FCC_STATUS_KCM_STORAGE_ERROR:
            error = "KCM failed to read, write or get size of item from/to storage.";
            break;
        case FCC_STATUS_KCM_FILE_EXIST_ERROR:
            error = "KCM tried to create existing storage item.";
            break;
        case FCC_STATUS_KCM_CRYPTO_ERROR:
            error = "KCM returned error upon cryptographic check of an certificate or key.";
            break;
        case FCC_STATUS_NOT_INITIALIZED:
            error = "FCC failed or did not initialized.";
            break;
        case FCC_STATUS_BUNDLE_ERROR:
            error = "Protocol layer general error.";
            break;
        case FCC_STATUS_BUNDLE_RESPONSE_ERROR:
            error = "Protocol layer failed to create response buffer.";
            break;
        case FCC_STATUS_BUNDLE_UNSUPPORTED_GROUP:
            error = "Protocol layer detected unsupported group was found in a message.";
            break;
        case FCC_STATUS_BUNDLE_INVALID_GROUP:
            error = "Protocol layer detected invalid group in a message.";
            break;
        case FCC_STATUS_BUNDLE_INVALID_SCHEME:
            error = "The scheme version of a message in the protocol layer is wrong.";
            break;
        case FCC_STATUS_ITEM_NOT_EXIST:
            error = "Current item wasn't found in the storage";
            break;
        case FCC_STATUS_EMPTY_ITEM:
            error = "Current item's size is 0";
            break;
        case FCC_STATUS_WRONG_ITEM_DATA_SIZE:
            error = "Current item's size is different then expected";
            break;
        case FCC_STATUS_URI_WRONG_FORMAT:
            error = "Current URI is different than expected.";
            break;
        case FCC_STATUS_FIRST_TO_CLAIM_NOT_ALLOWED:
            error = "Can't use first to claim without bootstrap or with account ID";
            break;
        case FCC_STATUS_BOOTSTRAP_MODE_ERROR:
            error = "Wrong value of bootstrapUse mode.";
            break;
        case FCC_STATUS_OUTPUT_INFO_ERROR:
            error = "The process failed in output info creation.";
            break;
        case FCC_STATUS_WARNING_CREATE_ERROR:
            error = "The process failed in output info creation.";
            break;
        case FCC_STATUS_UTC_OFFSET_WRONG_FORMAT:
            error = "Current UTC is wrong.";
            break;
        case FCC_STATUS_CERTIFICATE_PUBLIC_KEY_CORRELATION_ERROR:
            error = "Certificate's public key failed do not matches to corresponding private key";
            break;
        case FCC_STATUS_BUNDLE_INVALID_KEEP_ALIVE_SESSION_STATUS:
            error = "The message status is invalid.";
            break;
        default:
            error = "UNKNOWN";
    }
    tr_err("\nFactory Configurator Client [ERROR]: %s\r\n", error);
#endif
}

int platform_reset_storage(void)
{
#if defined(MBED_CONF_APP_DEVELOPER_MODE) && (MBED_CONF_APP_DEVELOPER_MODE == 1)
    printf("Resets storage to an empty state.\r\n");
    int status = fcc_storage_delete();
    if (status != FCC_STATUS_SUCCESS) {
        printf("Failed to delete storage - %d\r\n", status);
    }
    return status;
#else
    return FCC_STATUS_SUCCESS;
#endif
}

int verify_cloud_config(void)
{
    int status;
    int result = 0;

#if defined(MBED_CONF_APP_DEVELOPER_MODE) && (MBED_CONF_APP_DEVELOPER_MODE == 1)
    status = fcc_developer_flow();
    if (status == FCC_STATUS_KCM_FILE_EXIST_ERROR) {
        printf("Developer credentials already exist, continuing..\r\n");
        result = 0;
    } else if (status != FCC_STATUS_SUCCESS) {
        printf("Failed to load developer credentials\r\n");
        result = -1;
    }
#endif
#if MBED_CONF_APP_DEVELOPER_MODE == 1
    status = fcc_verify_device_configured_4mbed_cloud();
    print_fcc_status(status);
    if (status != FCC_STATUS_SUCCESS && status != FCC_STATUS_EXPIRED_CERTIFICATE) {
        result = -1;
    }
#endif
    return result;
}
