/*******************************************************************************
 *
 * Copyright (C) 2022 NETINT Technologies
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

/*!*****************************************************************************
 *  \file   ni_rsrc_namespace.c
 *
 *  \brief  This utility aims to set the NVMe namespace number for a Quadra NVMe
 *          block device. It can operate on physical devices (PCIe physical
 *          function) or virtual devices (PCIe virtual function). Before setting
 *          namespace number, use SR-IOV to create the PCIe virtual function.
 *          Note that only block device name is accepted for this utility.
 *
 *          To effect the name space change, reload the NVMe driver:
 *              sudo modprobe -r nvme
 *              sudo modprobe nvme
 *              sudo nvme list  #check the result with nvme list
 ******************************************************************************/

#include "ni_device_api.h"
#include "ni_nvme.h"
#include "ni_util.h"
#ifdef _WIN32
#include "ni_getopt.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#endif

#define NI_NAMESPACE_SZ                 32

typedef struct {
    int namespace_id;
    float allowance;
} namespace_allowance_t;

static void usage(void)
{
    printf("usage: ni_rsrc_namespace [OPTION]\n"
           "Provides NETINT QUADRA NVMe block device namespace IO operations.\n"
           "  -v                      show version.\n"
           "  -d                      the nvme block namespace. Only PF with nsid 1 allowed\n"
           "  -D                      the nvme block namespace for target over provision setting\n"
           "  -n                      the nvme block namespace count.\n"
           "                          Default: 0\n"
           "  -p                      overprovision percent. Use exclusive of -n and -s and -q\n"
           "  -q                      namespace QoS setting. Use exclusive of -n and -s and -p\n"
           "                            Default: 0 disabled.\n"
           "                            1 enable qos\n"
           "                            2 enable qos with overprovision\n"
           "  -a <allowance>          Set allowance for specific namespace (0.0-100.0) Requires -D <target_ns_device>\n"
           "  -s                      Index of virtual PCIe functions in SR-IOV tied to the \n"
           "                          physical PCIe function. '0' to select physical PCIe \n"
           "                          function.\n"
           "                          Eg. '1' to select the first virtual SR-IOV device tied \n"
           "                          to the physical block device defined by '-d' option.\n"
           "                          Default: 0\n"
           "  -c                      Persist the configuration of the namespace\n"
           "  -h                      help info.\n");
}

int send_config_ns_command(char* dev, int ns, int sr)
{
    ni_device_handle_t handle = ni_device_open2(dev, NI_DEVICE_READ_WRITE);
    ni_retcode_t retval;
    char errmsg[NI_ERRNO_LEN] = {0};
    if (handle == NI_INVALID_DEVICE_HANDLE)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: open %s failure for %s\n", dev,
                errmsg);
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", dev);
    }

    retval = ni_device_config_namespace_num(handle, ns, sr);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: Config setting failure for %s\n",
                errmsg);
    }
    ni_device_close(handle);
    return retval;
}

int send_config_qos_mode(char *dev, int value)
{
    ni_device_handle_t handle = ni_device_open2(dev, NI_DEVICE_READ_WRITE);
    ni_retcode_t retval;
    char errmsg[NI_ERRNO_LEN] = {0};
    if (handle == NI_INVALID_DEVICE_HANDLE)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: open %s failure for %s\n", dev,
                errmsg);
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", dev);
    }

    retval = ni_device_config_qos(handle, value);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: Config setting failure for %s\n",
                errmsg);
    }
    ni_device_close(handle);
    return retval;
}

int send_config_qos_op(char *dev, char *devt, int op)
{
    ni_retcode_t retval;
    ni_device_handle_t handle = ni_device_open2(dev, NI_DEVICE_READ_WRITE);
    char errmsg[NI_ERRNO_LEN] = {0};
    if (handle == NI_INVALID_DEVICE_HANDLE)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: open %s failure for %s\n", dev,
                errmsg);
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", dev);
    }
    ni_device_handle_t handle_t = ni_device_open2(devt, NI_DEVICE_READ_WRITE);
    if (handle_t == NI_INVALID_DEVICE_HANDLE)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: open %s failure for %s\n", devt,
                errmsg);
        ni_device_close(handle);
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", devt);
    }

    retval = ni_device_config_qos_op(handle, handle_t, op);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: Config setting failure for %s\n",
                errmsg);
    }
    ni_device_close(handle);
    ni_device_close(handle_t);
    return retval;
}
int send_config_qos_allowance(char *dev, char *devt, int allowance)
{
    ni_retcode_t retval;
    ni_device_handle_t handle = ni_device_open2(dev, NI_DEVICE_READ_WRITE);
    char errmsg[NI_ERRNO_LEN] = {0};
    if (handle == NI_INVALID_DEVICE_HANDLE)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: open %s failure for %s\n", dev,
                errmsg);
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", dev);
    }
    ni_device_handle_t handle_t = ni_device_open2(devt, NI_DEVICE_READ_WRITE);
    if (handle_t == NI_INVALID_DEVICE_HANDLE)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: open %s failure for %s\n", devt,
                errmsg);
        ni_device_close(handle);
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", devt);
    }

    retval = ni_device_config_qos_allowance(handle, handle_t, allowance);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_strerror(errmsg, NI_ERRNO_LEN, NI_ERRNO);
        fprintf(stderr, "ERROR: Config setting failure for %s\n",
                errmsg);
    }
    ni_device_close(handle);
    ni_device_close(handle_t);
    return retval;
}

static int get_ns_id_from_path(const char *path)
{
    const char *n_pos = strrchr(path, 'n');
    if (!n_pos || n_pos[1] == '\0')
        return -1;
    int id = atoi(n_pos + 1);
    return (id >= 1 && id <= NI_NAMESPACE_MAX_NUM) ? id : -1;
}

int main(int argc, char *argv[])
{
    ni_retcode_t retval;
    int opt;
    char device_namespace[NI_NAMESPACE_SZ] = {'\0'};
    char device_namespace_OP[NI_NAMESPACE_SZ] = {'\0'};
    int namespace_num = 1;
    float over_provision_percent = -1;
    int i32_over_provision_percent = 0;
    int sriov_index = 0;
    int qos_mode = -1;
    int persistence = 0;
#ifdef __linux__
    struct stat sb;
#endif
    namespace_allowance_t allowances = {0};
    int allowance_count = 0;
    float pending_allowance = -1.0f;
    int i32_allowance = 0;
    float allowance = 0;
    int ns_id;
    int current_qos_mode = -1;
    bool has_zero_allowance = false;
    float total_all_namespaces = 0.0f;
    ni_qos_header_info_log_page_t qos_header = {0};

    while ((opt = getopt(argc, argv, "d:D:n:p:q:s:a:chv")) != EOF)
    {
        switch (opt)
        {
            case 'h':
                usage();
                exit(0);
            case 'v':
                printf("Release ver: %s\n"
                       "API ver:     %s\n"
                       "Date:        %s\n"
                       "ID:          %s\n",
                       NI_XCODER_REVISION, LIBXCODER_API_VERSION,
                       NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
                exit(0);
            case 'd':
                ni_strcpy(device_namespace, NI_NAMESPACE_SZ, optarg);
#ifdef __linux__
                if (lstat(device_namespace, &sb) != 0 ||
                    (sb.st_mode & S_IFMT) != S_IFBLK)
                {
                    fprintf(stderr, "ERROR: Only block device is supported! "
                            "%s is not block device!\n", device_namespace);
                    exit(-1);
                }
#endif
                break;
            case 'D':
                ni_strcpy(device_namespace_OP, NI_NAMESPACE_SZ, optarg);
                if (pending_allowance >= 0.0f)
                {
                    ns_id = get_ns_id_from_path(device_namespace_OP);
                    if (ns_id < 1)
                    {
                        fprintf(stderr, "ERROR: Cannot get namespace ID from -D path %s\n", device_namespace_OP);
                        exit(-1);
                    }
                    allowances.namespace_id = ns_id;
                    allowances.allowance = pending_allowance;
                    allowance_count = 1;
                    pending_allowance = -1.0f;
                }
                break;
            case 'n':
                // A maximum of 64 namespaces are supported for firmware
                namespace_num = atoi(optarg);
                if (namespace_num < 0 || namespace_num > NI_NAMESPACE_MAX_NUM)
                {
                    fprintf(stderr, "ERROR: The number of namespace cannot "
                            "exceed %d\n", NI_NAMESPACE_MAX_NUM);
                    exit(-1);
                }
                break;
            case 'p':
                // set overprovision %
                over_provision_percent = (float)atof(optarg);
                if (over_provision_percent > 100 || over_provision_percent < 0)
                {
                    fprintf(stderr, "ERROR: Overprovision percent cannot "
                                    "exceed 100%% or become negative\n");
                    exit(-1);
                }
                break;
            case 'q':
                // 0 disabled, 1 enabled - no idle sharing
                qos_mode = atoi(optarg);
                if (qos_mode < QOS_MODE_DISABLED ||
                    qos_mode > QOS_MODE_ENABLED_SHARE)
                {
                    fprintf(stderr,
                            "ERROR: QoS mode %d not supported\n",
                            qos_mode);
                    exit(-1);
                }
                break;
            case 's':
                sriov_index = atoi(optarg);
                if (sriov_index < 0)
                {
                    fprintf(stderr, "ERROR: Invalid SR-IOV device index: %d\n",
                            sriov_index);
                    exit(-1);
                }
                break;
            case 'c':
                persistence = 1;
                break;
            case 'a':
                allowance = (float)atof(optarg);
                if (allowance < 0.0f || allowance > 100.0f)
                {
                    fprintf(stderr, "ERROR: Allowance %.2f out of range [0-100]\n", allowance);
                    exit(-1);
                }
                if (device_namespace_OP[0] != '\0')
                {
                    ns_id = get_ns_id_from_path(device_namespace_OP);
                    if (ns_id < 1)
                    {
                        fprintf(stderr, "ERROR: Cannot get namespace ID from -D path %s\n", device_namespace_OP);
                        exit(-1);
                    }
                    allowances.namespace_id = ns_id;
                    allowances.allowance = allowance;
                    allowance_count = 1;
                }
                else
                    pending_allowance = allowance;
            break;
        default:
                fprintf(stderr, "ERROR: Invalid option: %c\n", opt);
                exit(-1);
        }
    }

    if (pending_allowance >= 0.0f)
    {
        fprintf(stderr, "ERROR: -a requires -D (e.g. -d /dev/nvme0n1 -a 80 -D /dev/nvme0n2)\n");
        exit(-1);
    }

    if (allowance_count > 0)
    {
        if (allowances.allowance == 0.0f)
            has_zero_allowance = true;

        ni_device_handle_t ns_handle = ni_device_open2(device_namespace_OP, NI_DEVICE_READ_ONLY);
        if (ns_handle == NI_INVALID_DEVICE_HANDLE)
        {
            fprintf(stderr, "ERROR: Failed to open device %s\n", device_namespace_OP);
            return NI_RETCODE_FAILURE;
        }

        ni_device_capability_t device_capability = {0};
        if (ni_device_capability_query2(ns_handle, &device_capability, false) != NI_RETCODE_SUCCESS)
        {
            ni_device_close(ns_handle);
            fprintf(stderr, "ERROR: Failed to query device capability for %s\n", device_namespace_OP);
            return NI_RETCODE_FAILURE;
        }

        if (ni_query_qos_info(ns_handle, &qos_header, device_capability.fw_rev) != NI_RETCODE_SUCCESS)
        {
            ni_device_close(ns_handle);
            fprintf(stderr, "ERROR: Failed to query QoS info for %s\n", device_namespace_OP);
            return NI_RETCODE_FAILURE;
        }

        if (has_zero_allowance)
        {
            current_qos_mode = (uint8_t)(qos_header.ui16QosState & 0xFF);
            if (current_qos_mode != QOS_MODE_ENABLED_SHARE)
            {
                ni_device_close(ns_handle);
                fprintf(stderr, "ERROR: 0%% allowance only allowed with QoS mode 2 (current mode: %d)\n",
                        current_qos_mode);
                return NI_RETCODE_FAILURE;
            }
        }

        total_all_namespaces = qos_header.fTotalAllowance;

        ni_qos_namespace_info_log_page_t qos_ns_info = {0};
        if (ni_query_qos_namespace_info(ns_handle, &qos_ns_info, device_capability.fw_rev) != NI_RETCODE_SUCCESS)
        {
            ni_device_close(ns_handle);
            fprintf(stderr, "ERROR: Failed to query QoS namespace info for %s\n", device_namespace_OP);
            return NI_RETCODE_FAILURE;
        }
        ni_device_close(ns_handle);

        total_all_namespaces = total_all_namespaces - qos_ns_info.fAllowance + allowances.allowance;

        if (total_all_namespaces > 100.0f)
        {
            fprintf(stderr, "ERROR: Total allowance will be %.2f%% (exceeds 100%%)\n", total_all_namespaces);
            return NI_RETCODE_FAILURE;
        }
        if (total_all_namespaces < 100.0f)
            fprintf(stderr, "INFO: Total allowance will be %.2f%% (under 100%%)\n", total_all_namespaces);

        memcpy(&i32_allowance, &allowances.allowance, sizeof(int32_t));
        retval = send_config_qos_allowance(device_namespace, device_namespace_OP, i32_allowance);
        if (retval != NI_RETCODE_SUCCESS)
        {
            fprintf(stderr, "ERROR: Failed to set allowance for namespace %d\n", allowances.namespace_id);
            return NI_RETCODE_FAILURE;
        }
        printf("Set namespace %d allowance to %.2f%%\n",
               allowances.namespace_id, allowances.allowance);
        printf("Total allowance across all namespaces: %.2f%%\n", total_all_namespaces);
        return NI_RETCODE_SUCCESS;
    }

    if (device_namespace[0] == '\0')
    {
        fprintf(stderr, "ERROR: missing argument for -d\n");
        exit(-1);
    }
    if (strlen(device_namespace) < 3 ||
        strcmp(device_namespace + strlen(device_namespace) - 2, "n1") != 0)
    {
        fprintf(stderr, "ERROR: Invalid device name %s, need n1, no vf\n", device_namespace);
        exit(-1);
    }

    if (qos_mode != -1)
    {
        if (namespace_num != 1 || sriov_index || over_provision_percent != -1)
        {
            fprintf(stderr,
                    "ERROR: QoS mode -q mutually exclusive of namespace and "
                    "SR-IOV\n");
            exit(-1);
        }
        retval = send_config_qos_mode(device_namespace, qos_mode);
        if (retval == NI_RETCODE_SUCCESS)
        {
            printf("QoS mode setting succeed with number of %d\n", qos_mode);
        }
        return retval;
    }

    if (over_provision_percent != -1)
    {
        if (device_namespace_OP[0] == '\0')
        {
            fprintf(stderr, "ERROR: missing argument for -D\n");
            exit(-1);
        }
        if (namespace_num != 1 || sriov_index || qos_mode != -1)
        {
            fprintf(stderr,
                            "ERROR: Overprovision percent -p mutually exclusive of "
                            "namespace and SR-IOV and QOS mode\n");
            exit(-1);
        }
        memcpy(&i32_over_provision_percent, &over_provision_percent, sizeof(int32_t));
        retval = send_config_qos_op(device_namespace, device_namespace_OP,
                                    i32_over_provision_percent);
        if (retval == NI_RETCODE_SUCCESS)
        {
            printf("Overprovision percent setting succeed with number of %f\n",
                   over_provision_percent);
        }
        return retval;
    }

    if (persistence) namespace_num += 256;
    retval = send_config_ns_command(device_namespace, namespace_num, sriov_index);
    if (retval == NI_RETCODE_SUCCESS)
    {
        printf("Namespace setting succeed with number of %d and SR-IOV "
               "index %d\n",
               namespace_num, sriov_index);
    }
    return retval;
}
