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
 *  \file   ni_quadraprobe.c
 *
 *  \brief  Read the firmware logs from BAR4 space
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <pwd.h>
#include <time.h>
#include <ctype.h>

#include "ni_quadraprobe.h"
#include "ni_util.h"

/* These defines are private to this translation unit */
#define PCI_SYSFS_PATH "/sys/bus/pci/devices"
#define QUADRA_VENDOR_ID 0x1d82
#define QUADRA_DEVICE_ID 0x0401

/* artifacts BAR4 offset : 0x84F920000 (artifacts base) - 0x840000000 (window base) = 0xF920000 */
#define ARTIFACTS_BAR4_OFFSET       0xF920000       /* BAR4 offset to artifacts buffer in DDR */

#define BAR4_MIN_ADDRESS       0x840000000ULL
#define NOR_FLASH_CMD_ADDRESS  0x84F8F5020ULL
#define NOR_FLASH_CMD_OFFSET   (NOR_FLASH_CMD_ADDRESS - BAR4_MIN_ADDRESS)
#define NOR_READ_WAIT_SECONDS  100

#define CORE_LOG_PTR_TABLE_OFFSET       0xF8F5000
#define CORE_RESET_LOG_PTR_TABLE_OFFSET 0xF8FD0C0
#define CORE_LOG_SIZE                   ((size_t)1024 * 1024)  /* 1 MB per-core normal log buffer */
#define CORE_RESET_LOG_SIZE             ((size_t)64 * 1024)    /* 64 KB per-core reset log buffer */

#define FL_LOG_ENTRY_SIZE      128 /* bytes per FL log record */
#define FL_CORE_IDX            5

#if defined(__linux__)

const char *const core_names[NUM_CORES] = {"np", "dp", "ep", "tp", "fp", "fl"};

/*!*****************************************************************************
 *  \brief  Restore file ownership to the invoking user.
 *
 *  \return None
 ******************************************************************************/
static void set_log_file_ownership(const char *fname)
{
    chmod(fname, 0664);
    const char *sudo_user = getenv("SUDO_USER"); // NOLINT(concurrency-mt-unsafe) - no thread-safe alternative
    if (!sudo_user || sudo_user[0] == '\0')
        return;
    struct passwd pw_buf, *pw = NULL;
    char pwbuf[1024];
    (void)getpwnam_r(sudo_user, &pw_buf, pwbuf, sizeof(pwbuf), &pw);
    // print the error message if chown fails
    if (pw && chown(fname, pw->pw_uid, pw->pw_gid) != 0)
    {
        char errbuf[NI_ERRNO_LEN];
        (void)ni_strerror(errbuf, sizeof(errbuf), errno);
        (void)fprintf(stderr, "[WARN] chown(%s) failed: %s\n",
                      fname, errbuf);
    }
}

/*!*****************************************************************************
 *  \brief  Read the NVMe serial number for a Quadra device from sysfs.
 *
 *  \param[in]  qd      QuadraDevice whose sysfs_path to search
 *  \param[out] sn      Buffer to receive the serial number
 *  \param[in]  sn_len  Size of sn buffer
 *
 *  \return  0 on success, -1 if not found or error
 ******************************************************************************/
static int read_nvme_sn(const QuadraDevice *qd, char *sn, size_t sn_len)
{
    char nvme_dir[PATH_MAX];
    if (snprintf(nvme_dir, sizeof(nvme_dir), "%s/nvme", qd->sysfs_path) < 0)
        return -1;

    DIR *dir = opendir(nvme_dir);
    if (!dir)
        return -1;

    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) // NOLINT(concurrency-mt-unsafe) - single-threaded CLI tool
    {
        if (strncmp(entry->d_name, "nvme", 4) != 0)
            continue;

        char serial_path[PATH_MAX];
        if (snprintf(serial_path, sizeof(serial_path), "%s/%s/serial",
                     nvme_dir, entry->d_name) < 0)
            continue;

        FILE *fp = fopen(serial_path, "r");
        if (!fp)
            continue;

        char raw_sn[21] = {0};
        if (fgets(raw_sn, sizeof(raw_sn), fp))
        {
            raw_sn[strcspn(raw_sn, " \t\r\n")] = '\0';
            if (raw_sn[0])
            {
                for (char *p = raw_sn; *p; ++p)
                {
                    if (!isalnum((unsigned char)*p) && *p != '-')
                        *p = '_';
                }
                (void)snprintf(sn, sn_len, "%s", raw_sn);
                found = 1;
            }
        }
        (void)fclose(fp);
        if (found)
            break;
    }
    closedir(dir);
    return found ? 0 : -1;
}

int bdf_to_sysfs(const char* bdf, char* sysfs_path, size_t pathsz, int *domain, int *bus, int *slot, int *func)
{
    unsigned int d=0, b=0, s=0, f=0;
    if (sscanf(bdf, "%x:%x:%x.%x", &d, &b, &s, &f) != 4) // NOLINT(cert-err34-c) - return value checked
        return -1;
    if (snprintf(sysfs_path, pathsz, "/sys/bus/pci/devices/%04x:%02x:%02x.%x", d, b, s, f) < 0)
        return -1;
    if (domain) *domain = (int)d;
    if (bus) *bus = (int)b;
    if (slot) *slot = (int)s;
    if (func) *func = (int)f;
    return 0;
}

static int safe_realloc_devices(QuadraDevice **p_devs, int new_cap)
{
    QuadraDevice *new_devs = realloc(*p_devs, new_cap * sizeof(**p_devs));
    if (!new_devs)
    {
        free(*p_devs);
        *p_devs = NULL;
        return -1;
    }
    *p_devs = new_devs;
    return 0;
}

int quadra_device_find(QuadraDevice **out_devs)
{
    DIR *dir = opendir(PCI_SYSFS_PATH);
    if (!dir)
    {
        perror("opendir(sysfs)");
        return -1;
    }

    struct dirent *entry;
    QuadraDevice *devs = calloc(8, sizeof(*devs));
    int dev_count = 0, capacity = 8;
    if (!devs)
    {
        closedir(dir);
        return -1;
    }

    char path[PATH_MAX];
    while ((entry = readdir(dir))) // NOLINT(concurrency-mt-unsafe) - single-threaded CLI tool
    {
        if (entry->d_name[0] == '.')
            continue;

        if (snprintf(path, sizeof(path), "%s/%s", PCI_SYSFS_PATH, entry->d_name) < 0)
            continue;

        struct stat st;
        if (stat(path, &st) < 0 || !S_ISDIR(st.st_mode))
            continue;

        char ven_path[PATH_MAX], dev_path[PATH_MAX];
        if (snprintf(ven_path, sizeof(ven_path), "%s/vendor", path) < 0)
            continue;
        if (snprintf(dev_path, sizeof(dev_path), "%s/device", path) < 0)
            continue;

        FILE *fv = fopen(ven_path, "r");
        if (!fv)
            continue;

        char buf[32];
        if (!fgets(buf, sizeof(buf), fv))
        {
            (void)fclose(fv);
            continue;
        }
        (void)fclose(fv);

        unsigned long ven = strtoul(buf, NULL, 0);
        if (ven != QUADRA_VENDOR_ID)
            continue;

        /* Read device */
        FILE *fd = fopen(dev_path, "r");
        if (!fd)
            continue;

        if (!fgets(buf, sizeof(buf), fd))
        {
            (void)fclose(fd);
            continue;
        }
        (void)fclose(fd);

        unsigned long dev = strtoul(buf, NULL, 0);
        if (dev != QUADRA_DEVICE_ID)
            continue;

        if (dev_count >= capacity)
        {
            capacity *= 2;
            if (safe_realloc_devices(&devs, capacity) < 0)
            {
                closedir(dir);
                return -1;
            }
        }

        if (bdf_to_sysfs(entry->d_name,
                         devs[dev_count].sysfs_path, sizeof(devs[dev_count].sysfs_path),
                         &devs[dev_count].domain, &devs[dev_count].bus, &devs[dev_count].slot,
                         &devs[dev_count].func) == 0)
        {
            dev_count++;
        }
    }

    closedir(dir);

    if (dev_count == 0)
    {
        free(devs);
        return 0;
    }

    *out_devs = devs;
    return dev_count;
}

int quadra_init_mmap(QuadraDevice *qd)
{
    char bar4path[PATH_MAX];
    struct stat bar4stat;
    if (snprintf(bar4path, sizeof(bar4path), "%s/resource4", qd->sysfs_path) < 0)
        return -1;
    if (geteuid() != 0)
    {
        (void)fprintf(stderr, "Permission error: You must run as root (sudo)\n");
        return -1;
    }
    /*
     * O_RDWR here causes EROFS during card loss - BAR4 becomes read-only (QDFW-5627)
     * temporarily.
     */
    qd->bar4_fd = open(bar4path, O_RDONLY | O_SYNC);
    if (qd->bar4_fd == -1)
    {
        perror("open(bar4)");
        return -1;
    }
    if (fstat(qd->bar4_fd, &bar4stat) == 0)
    {
        qd->barsize = bar4stat.st_size;
    }
    /* if fstat reports size 0, fallback to lseek. */
    if (qd->barsize == 0)
    {
        off_t sz = lseek(qd->bar4_fd, 0, SEEK_END);
        if (sz > 0)
        {
            qd->barsize = (off_t)sz;
            (void)lseek(qd->bar4_fd, 0, SEEK_SET);
        }
    }
    if (qd->barsize == 0)
    {
        (void)fprintf(stderr, "Cannot determine BAR4 size for %s\n", bar4path);
        close(qd->bar4_fd);
        qd->bar4_fd = -1;
        return -1;
    }
    qd->bar4_base = mmap(NULL, qd->barsize, PROT_READ, MAP_SHARED, qd->bar4_fd, 0);
    if (qd->bar4_base == MAP_FAILED || qd->bar4_base == NULL)
    {
        if (qd->bar4_base != NULL && errno == EACCES)
            (void)fprintf(stderr, "Permission error: Cannot mmap %s. Run as root (sudo).\n", bar4path);
        else if (qd->bar4_base != NULL && errno == EINVAL)
            (void)fprintf(stderr, "EINVAL: mmap() on BAR4 failed. Try kernel flag 'iomem=relaxed'.\n");
        else
            (void)fprintf(stderr, "Failed to mmap BAR4 at %s: %s\n", bar4path, strerror(errno)); // NOLINT(concurrency-mt-unsafe) - single-threaded CLI tool
        close(qd->bar4_fd);
        qd->bar4_fd = -1;
        qd->bar4_base = NULL;
        return -1;
    }
    return 0;
}

void quadra_cleanup(QuadraDevice *qd)
{
    if (qd->bar4_base && qd->bar4_base != MAP_FAILED)
    {
        munmap(qd->bar4_base, qd->barsize);
        qd->bar4_base = NULL;
    }
    if (qd->bar4_fd >= 0)
    {
        close(qd->bar4_fd);
        qd->bar4_fd = -1;
    }
}

/*!*****************************************************************************
 *  \brief   Write a NOR command to the BAR4 register.
 *
 *           Uses a temporary O_RDWR mapping just for NOR command write, so the
 *           quadra_init_mmap stays read-only (QDFW-5627).
 *
 *  \return  0 - for success
 *          -1 - if bar4 is not mapped
 ******************************************************************************/
static int quadra_nor_dispatch(QuadraDevice *qd, uint32_t command)
{
    char bar4path[PATH_MAX];
    if (snprintf(bar4path, sizeof(bar4path), "%s/resource4", qd->sysfs_path) < 0)
        return -1;

    char errbuf[NI_ERRNO_LEN];
    int fd = open(bar4path, O_RDWR | O_SYNC);
    if (fd == -1)
    {
        (void)ni_strerror(errbuf, sizeof(errbuf), errno);
        (void)fprintf(stderr, "Failed to open BAR4 (O_RDWR) at %s for NOR command: %s\n",
                      bar4path, errbuf);
        return -1;
    }

    void *map = mmap(NULL, qd->barsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
    {
        (void)ni_strerror(errbuf, sizeof(errbuf), errno);
        (void)fprintf(stderr, "Failed to mmap BAR4 (RW) at %s for NOR command: %s\n",
                      bar4path, errbuf);
        close(fd);
        return -1;
    }

    *(volatile uint32_t *)((uint8_t *)map + NOR_FLASH_CMD_OFFSET) = command;

    munmap(map, qd->barsize);
    close(fd);
    return 0;
}

/*!*****************************************************************************
 *  \brief   Block the host while firmware loads NOR logs to DDR.(~100 sec)
 *
 *  \return  None
 ******************************************************************************/
static void quadra_nor_read_wait(void)
{
    for (int elapsed = 0; elapsed < NOR_READ_WAIT_SECONDS; elapsed += 10)
    {
        (void)fprintf(stderr, "\r[INFO] NOR flash operation in progress... %d%%",
                      ((elapsed + 1) * 100) / NOR_READ_WAIT_SECONDS);
        ni_usleep(10 * 1000000LL);
    }
    (void)fprintf(stderr, "\r[INFO] NOR flash operation in progress... 100%%\n");
}

/*!*****************************************************************************
 *  \brief   Dispatch a NOR command and wait for completion - read command.
 *
 *           Nor read blocks ~100 s while firmware copies NOR to DDR;
 *           write/erase/exception return immediately (fire-and-forget).
 *
 *  \return  0 - on success
 *          -1 - if BAR4 is not mapped
 ******************************************************************************/
int quadra_nor_command(QuadraDevice *qd, uint32_t command)
{
    if (quadra_nor_dispatch(qd, command) != 0)
        return -1;

    switch (command)
    {
    case NOR_CMD_READ:
        quadra_nor_read_wait();
        (void)fprintf(stderr, "[INFO] NOR flash command complete.\n");
        break;
    case NOR_CMD_ERASE:
        (void)fprintf(stderr,
            "[INFO] NOR flash erase command dispatched.\n"
            "[INFO] Erasing NOR flash logs may take up to 100 seconds to complete.\n");
        break;
    case NOR_CMD_WRITE:
        (void)fprintf(stderr,
            "[INFO] NOR flash write command dispatched.\n"
            "[INFO] Writing DDR logs to NOR flash may take up to 100 seconds to complete.\n");
        break;
    case NOR_CMD_EXCEPTION:
        (void)fprintf(stderr, "[INFO] NOR flash exception command dispatched.\n");
        break;
    default:
        (void)fprintf(stderr, "[INFO] Unsupported NOR flash command: %u\n", command);
        break;
    }

    return 0;
}

void fill_core_log_offsets(QuadraDevice *qd)
{
    uint8_t *base = qd->bar4_base;
    off_t offset = CORE_LOG_PTR_TABLE_OFFSET;
    for (int i = 0; i < NUM_CORES; ++i)
    {
        uint32_t log_offset = *(uint32_t *)(base + offset + 4 * (size_t)i) & 0x0FFFFFFF;
        if ((size_t)log_offset + CORE_LOG_SIZE > (size_t)qd->barsize)
        {
            (void)fprintf(stderr,
                "[WARN] Core %s log offset 0x%x + 0x%zx exceeds BAR4 size 0x%lx, skipping\n",
                core_names[i], log_offset, CORE_LOG_SIZE, (unsigned long)qd->barsize);
            log_offset = 0;
        }
        qd->core_log_offsets[i] = log_offset;
    }
    qd->log_size = CORE_LOG_SIZE;
    qd->log_artifacts_offset = ARTIFACTS_BAR4_OFFSET;
}

void fill_core_reset_log_offsets(QuadraDevice *qd)
{
    uint8_t *base = qd->bar4_base;
    off_t offset = CORE_RESET_LOG_PTR_TABLE_OFFSET;
    for (int i = 0; i < NUM_CORES_NO_FL; ++i)
    {
        uint32_t log_offset = *(uint32_t *)(base + offset + 4 * (size_t)i) & 0x0FFFFFFF;
        if ((size_t)log_offset + CORE_RESET_LOG_SIZE > (size_t)qd->barsize)
        {
            (void)fprintf(stderr,
                "[WARN] Core %s reset-log offset 0x%x + 0x%zx exceeds BAR4 size 0x%lx, skipping\n",
                core_names[i], log_offset, CORE_RESET_LOG_SIZE, (unsigned long)qd->barsize);
            log_offset = 0;
        }
        qd->core_log_offsets[i] = log_offset;
    }
    qd->log_size = CORE_RESET_LOG_SIZE;
    qd->log_artifacts_offset = ARTIFACTS_BAR4_OFFSET;
}

/*!*****************************************************************************
 *  \brief   Dump Firmware Loader logs
 *
 *           Reads the FL signature at the core's BAR4 offset; if it does not
 *           match FL_SIGNATURE the core is skipped, otherwise read from BAR4,
 *           FL logs never read from NOR flash.
 *
 *  \param[in] qd      Initialized QuadraDevice - BAR4 mapped.
 *  \param[in] outdir  Destination directory.
 *
 *  \return  0 - on success,
 *          -1 - on file-open failure.
 ******************************************************************************/
static int dump_fl_core(QuadraDevice *qd, const char *outdir)
{
    uint32_t fl_offset = qd->core_log_offsets[FL_CORE_IDX];
    if ((size_t)fl_offset + 8 > (size_t)qd->barsize)
    {
        (void)fprintf(stderr, "[WARN] FL offset 0x%x out of BAR4 range, skipping\n", fl_offset);
        return -1;
    }
    uint8_t *base = qd->bar4_base + fl_offset;

    uint32_t signature;
    memcpy(&signature, base, sizeof(signature));
    (void)fprintf(stderr, "[INFO] FL Signature 0x%08x\n", signature);

    if (signature != FL_SIGNATURE)
    {
        (void)fprintf(stderr, "[WARN] FL log: invalid signature, skipping\n");
        return 0;
    }

    uint32_t len_field;
    memcpy(&len_field, base + 4, sizeof(len_field));
    uint32_t log_len = len_field & 0xFFFFFF;
    size_t payload_size = (size_t)FL_LOG_ENTRY_SIZE * log_len;
    if ((size_t)fl_offset + 8 + payload_size > (size_t)qd->barsize)
    {
        (void)fprintf(stderr,
            "[ERR] FL log payload_size %zu exceeds BAR4 bounds (%lu)\n",
            payload_size, (unsigned long)qd->barsize);
        return -1;
    }

    char fname[PATH_MAX];
    if (snprintf(fname, sizeof(fname), "%s/raw_fl_slot_%02x_%04x.bin",
        outdir, qd->bus, qd->domain) < 0)
        return -1;

    uint8_t *raw = base + 8;
    FILE *fp = fopen(fname, "wb");
    if (!fp)
    {
        perror("fopen");
        return -1;
    }

    for (size_t i = 0; i < payload_size; ++i)
    {
        if (raw[i] != 0x00)
            (void)fputc(raw[i], fp);
    }

    (void)fclose(fp);
    set_log_file_ownership(fname);
    (void)fprintf(stderr, "[INFO] Dumped log: %s\n", fname);
    return 0;
}

/*!*****************************************************************************
 *  \brief   Dump per-core firmware logs from BAR4 to files.
 *
 *  \param[in] qd              Initialized QuadraDevice - bar4 mapped.
 *  \param[in] outdir          Destination directory for the log files.
 *  \param[in] core_reset_log  true  -> dump core-reset logs
 *                             false -> dump running firmware logs (including FL)
 *
 *  \return  0 - on success,
 *          -1 - one or more cores failed
 ******************************************************************************/
int dump_raw_logs(QuadraDevice *qd, const char *outdir, bool core_reset_log)
{
    int num_cores;
    if (core_reset_log)
    {
        fill_core_reset_log_offsets(qd);
        num_cores = NUM_CORES_NO_FL;
    }
    else
    {
        fill_core_log_offsets(qd);
        num_cores = NUM_CORES;
    }

    char sn[21] = {0};
    (void)read_nvme_sn(qd, sn, sizeof(sn));

    char ts[16] = {0};
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info = localtime_r(&now, &tm_buf);
    if (tm_info)
        (void)strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", tm_info);

    int rc = 0;
    for (int core_idx = 0; core_idx < num_cores; ++core_idx)
    {
        const char *core = core_names[core_idx];

        if (core_idx == FL_CORE_IDX)
        {
            rc |= (dump_fl_core(qd, outdir) != 0) ? -1 : 0;
            continue;
        }

        uint32_t log_offset = qd->core_log_offsets[core_idx];
        if ((size_t)log_offset + (size_t)qd->log_size > (size_t)qd->barsize)
        {
            (void)fprintf(stderr,
                "[ERROR] %s log offset 0x%x out of BAR4 range, skipping\n",
                core, log_offset);
            rc = -1;
            continue;
        }
        char fname[PATH_MAX];
        int n;
        if (sn[0] && ts[0])
            n = snprintf(fname, sizeof(fname), "%s/raw_%s_%04x_%02x_%02x_%x_%s_%s.bin",
                         outdir, core, (unsigned)qd->domain, (unsigned)qd->bus,
                         (unsigned)qd->slot, (unsigned)qd->func, sn, ts);
        else if (sn[0])
            n = snprintf(fname, sizeof(fname), "%s/raw_%s_%04x_%02x_%02x_%x_%s.bin",
                         outdir, core, (unsigned)qd->domain, (unsigned)qd->bus,
                         (unsigned)qd->slot, (unsigned)qd->func, sn);
        else if (ts[0])
            n = snprintf(fname, sizeof(fname), "%s/raw_%s_%04x_%02x_%02x_%x_%s.bin",
                         outdir, core, (unsigned)qd->domain, (unsigned)qd->bus,
                         (unsigned)qd->slot, (unsigned)qd->func, ts);
        else
            n = snprintf(fname, sizeof(fname), "%s/raw_%s_%04x_%02x_%02x_%x.bin",
                         outdir, core, (unsigned)qd->domain, (unsigned)qd->bus,
                         (unsigned)qd->slot, (unsigned)qd->func);
        if (n < 0 || (size_t)n >= sizeof(fname))
        {
            (void)fprintf(stderr,
                "[ERROR] %s log filename truncated or encoding error, skipping\n", core);
            rc = -1;
            continue;
        }
        FILE *fp = fopen(fname, "wb");
        if (!fp)
        {
            perror("fopen");
            rc = -1;
            continue;
        }
        if (fwrite(qd->bar4_base + log_offset, 1, (size_t)qd->log_size, fp) != (size_t)qd->log_size)
        {
            (void)fprintf(stderr, "[ERROR] Short write on %s\n", fname);
            (void)fclose(fp);
            set_log_file_ownership(fname);
            rc = -1;
            continue;
        }
        (void)fclose(fp);
        set_log_file_ownership(fname);
        if (memcmp(qd->bar4_base + log_offset, "hashid", 6) == 0)
        {
            char hstr[17] = {0};
            memcpy(hstr, qd->bar4_base + log_offset, 16);
            (void)fprintf(stderr, "%s: %s\n", core, hstr);
        }
        (void)fprintf(stderr, "[INFO] Dumped log: %s\n", fname);
    }
    return rc;
}

/*!*****************************************************************************
 *  \brief   Read nor-persistent logs to BAR4 and write them to files.
 *           NOR read command triggers core log read from nor flash to BAR4.
 *
 *  \param[in] qd      Initialized QuadraDevice
 *  \param[in] outdir  Destination directory for the log files.
 *
 *  \return  0 - log dump successful
 *          -1 - one or more cores failed
 ******************************************************************************/
int dump_nor_logs(QuadraDevice *qd, const char *outdir)
{
    int rc = 0;
    for (int core_idx = 0; core_idx < NUM_CORES_NO_FL; ++core_idx)
    {
        const char *core = core_names[core_idx];

        uint32_t log_offset = qd->core_log_offsets[core_idx];
        if ((size_t)log_offset + (size_t)qd->log_size > (size_t)qd->barsize)
        {
            (void)fprintf(stderr,
                "[ERROR] %s log offset 0x%x out of BAR4 range, skipping\n",
                core, log_offset);
            rc = -1;
            continue;
        }
        char fname[PATH_MAX];
        if (snprintf(fname, sizeof(fname), "%s/raw_%s_slot_%02x_%04x.bin",
            outdir, core, qd->bus, qd->domain) < 0) continue;
        FILE *fp = fopen(fname, "wb");
        if (!fp)
        {
            perror("fopen");
            rc = -1;
            continue;
        }
        if (fwrite(qd->bar4_base + log_offset, 1, (size_t)qd->log_size, fp) != (size_t)qd->log_size)
        {
            (void)fprintf(stderr, "[ERROR] Short write on %s\n", fname);
            (void)fclose(fp);
            set_log_file_ownership(fname);
            rc = -1;
            continue;
        }
        (void)fclose(fp);
        set_log_file_ownership(fname);
        if (memcmp(qd->bar4_base + log_offset, "hashid", 6) == 0)
        {
            char hstr[17] = {0};
            memcpy(hstr, qd->bar4_base + log_offset, 16);
            (void)fprintf(stderr, "%s: %s\n", core, hstr);
        }
        (void)fprintf(stderr, "[INFO] Dumped persistent log: %s\n", fname);
    }
    return rc;
}

/*!*****************************************************************************
 *  \brief   Check whether a PCI sysfs path points to a Quadra device.
 *
 *           Compare sysfs vendor and device IDs against the NETINT
 *           vendor ID (0x1d82) and Quadra device ID (0x0401).
 *
 *  \param[in] sysfs_path  Device sysfs path, ex:/sys/bus/pci/devices/D:B:D:F).
 *
 *  \return   0 - the path is a Quadra device,
 *           -1 - missing path / wrong vendor/device ID
 ******************************************************************************/
static int validate_quadra_sysfs(const char *sysfs_path)
{
    char vpath[PATH_MAX], dpath[PATH_MAX], buf[32];

    if (snprintf(vpath, sizeof(vpath), "%s/vendor", sysfs_path) < 0 ||
        snprintf(dpath, sizeof(dpath), "%s/device", sysfs_path) < 0)
        return -1;

    FILE *f = fopen(vpath, "r");
    if (!f)
    {
        (void)fprintf(stderr, "[ERROR] No PCI device at %s\n", sysfs_path);
        return -1;
    }
    if (!fgets(buf, sizeof(buf), f)) { (void)fclose(f); return -1; }
    (void)fclose(f);
    if (strtoul(buf, NULL, 0) != QUADRA_VENDOR_ID)
    {
        (void)fprintf(stderr, "[ERROR] Device at %s is not a NETINT device.\n",
                      sysfs_path);
        return -1;
    }

    f = fopen(dpath, "r");
    if (!f) return -1;
    if (!fgets(buf, sizeof(buf), f)) { (void)fclose(f); return -1; }
    (void)fclose(f);
    if (strtoul(buf, NULL, 0) != QUADRA_DEVICE_ID)
    {
        (void)fprintf(stderr, "[ERROR] Device at %s is not a Quadra device.\n",
                      sysfs_path);
        return -1;
    }
    return 0;
}

/*!*****************************************************************************
 *  \brief   Find a single Quadra device by PCI slot (and domain)
 *
 *  \param[in]  domain_str  domain, default "0000".
 *  \param[in]  slot_str    slot e.g. "01"
 *  \param[out] out         allocated QuadraDevice structure
 *
 *  \return   0 - device found
 *           -1 - failure
 ******************************************************************************/
int quadra_device_find_by_bdf(const char *domain_str, const char *slot_str,
                               QuadraDevice **out)
{
    if (!slot_str || !out)
        return -1;

    unsigned int domain = domain_str ? (unsigned int)strtoul(domain_str, NULL, 16) : 0;
    unsigned int bus    = (unsigned int)strtoul(slot_str, NULL, 16);

    if (domain > 0xFFFF || bus > 0xFF)
    {
        (void)fprintf(stderr, "[ERROR] --domain or -s value out of range.\n");
        return -1;
    }

    char bdf[32];
    (void)snprintf(bdf, sizeof(bdf), "%04x:%02x:00.0", domain, bus);

    char sysfs_path[PATH_MAX];
    (void)snprintf(sysfs_path, sizeof(sysfs_path), "%s/%s", PCI_SYSFS_PATH, bdf);

    if (validate_quadra_sysfs(sysfs_path) != 0)
        return -1;

    QuadraDevice *qd = calloc(1, sizeof(*qd));
    if (!qd) return -1;

    if (bdf_to_sysfs(bdf, qd->sysfs_path, sizeof(qd->sysfs_path),
                     &qd->domain, &qd->bus, &qd->slot, &qd->func) != 0)
    {
        free(qd);
        return -1;
    }
    qd->bar4_fd = -1;
    *out = qd;
    return 0;
}

/*!*****************************************************************************
 *  \brief   Find a single Quadra device by its NVMe block-device name.
 *
 *  \param[in]  blkdev  NVMe block-device name
 *  \param[out] out     allocated QuadraDevice struct
 *
 *  \return   0 - device found,
 *           -1 - not a valid Quadra card
 ******************************************************************************/
int quadra_device_find_by_block_dev(const char *blkdev, QuadraDevice **out)
{
    if (!blkdev || !out)
        return -1;

    /* use readlink system call */
    char link_path[PATH_MAX];
    if (snprintf(link_path, sizeof(link_path),
                 "/sys/block/%s/device/device", blkdev) < 0)
        return -1;

    char link_target[PATH_MAX];
    ssize_t len = readlink(link_path, link_target, sizeof(link_target) - 1);
    if (len < 0)
    {
        (void)fprintf(stderr, "[ERROR] Block device '%s' not found.\n", blkdev);
        return -1;
    }
    link_target[len] = '\0';

    /* Extract the last path component (the BDF). */
    char *last_slash = strrchr(link_target, '/');
    const char *bdf = last_slash ? last_slash + 1 : link_target;

    char sysfs_path[PATH_MAX];
    if (snprintf(sysfs_path, sizeof(sysfs_path), "%s/%s", PCI_SYSFS_PATH, bdf) < 0)
        return -1;

    if (validate_quadra_sysfs(sysfs_path) != 0)
    {
        (void)fprintf(stderr, "[ERROR] '%s' does not correspond to a Quadra device.\n",
                      blkdev);
        return -1;
    }

    QuadraDevice *qd = calloc(1, sizeof(*qd));
    if (!qd) return -1;

    if (bdf_to_sysfs(bdf, qd->sysfs_path, sizeof(qd->sysfs_path),
                     &qd->domain, &qd->bus, &qd->slot, &qd->func) != 0)
    {
        free(qd);
        return -1;
    }
    qd->bar4_fd = -1;
    *out = qd;
    return 0;
}

/*!*****************************************************************************
 *  \brief  Read log artifacts tar.gz from DDR via BAR4 and write to disk.
 *
 *   [0 .. 3]              - uint32_t artifacts data size
 *   [4 .. 4+size-1]       - tar.gz payload
 *
 *  Returns 0 when:
 *   - size header is 0  (firmware too old / artifacts not loaded)
 *   - size exceeds maximum (stale DDR data)
 *   - gzip magic mismatch (stale DDR data / unsupported firmware)
 *   - success
 *  Returns -1 on I/O error.
 ******************************************************************************/
static int dump_artifacts(QuadraDevice *qd, const char *outdir)
{
    const uint8_t *artifacts_base = qd->bar4_base + qd->log_artifacts_offset;

    uint32_t artifacts_size;
    memcpy(&artifacts_size, artifacts_base, sizeof(artifacts_size));

    if (artifacts_size == 0)
    {
        (void)fprintf(stderr, "[INFO] Artifacts: size header is 0 - artifacts were not loaded into DDR.\n");
        return 0;
    }

    uint32_t max_data_size = LOG_ARTIFACTS_MAX_SIZE - CPU_LOG_ARTIFACTS_HEADER_SIZE;
    if (artifacts_size > max_data_size)
    {
        (void)fprintf(stderr, "[WARN] Artifacts: size %u exceeds max %u size - stale DDR data or artifacts not loaded.\n",
                      artifacts_size, max_data_size);
        return 0;
    }

    const uint8_t *data = artifacts_base + CPU_LOG_ARTIFACTS_HEADER_SIZE;
    if (data[0] != 0x1f || data[1] != 0x8b)
    {
        (void)fprintf(stderr, "[WARN] Artifacts: invalid gzip magic 0x%02x 0x%02x - stale DDR data or artifacts not loaded\n",
                      data[0], data[1]);
        return 0;
    }

    char fname[PATH_MAX];
    if (snprintf(fname, sizeof(fname), "%s/artifacts_slot_%02x_%04x.tar.gz",
                 outdir, qd->bus, qd->domain) < 0)
        return -1;

    FILE *fp = fopen(fname, "wb");
    if (!fp)
    {
        perror("fopen(artifacts)");
        return -1;
    }
    if (fwrite(data, 1, artifacts_size, fp) != artifacts_size)
    {
        (void)fprintf(stderr, "[ERROR] artifacts write failed on %s\n", fname);
        (void)fclose(fp);
        return -1;
    }
    (void)fclose(fp);
    set_log_file_ownership(fname);
    (void)fprintf(stderr, "[INFO] Artifacts saved: %s (%u bytes)\n", fname, artifacts_size);
    return 0;
}

int ni_rsrc_log_dump(const char *outdir, bool core_reset_log)
{
    QuadraDevice *devs = NULL;
    int ndev = quadra_device_find(&devs);
    if (ndev <= 0)
    {
        free(devs);
        (void)fprintf(stderr, "[WARN] No Quadra devices found in sysfs (vendor 1d82).\n");
        return 1;
    }
    int fail_count = 0;
    for (int i = 0; i < ndev; ++i)
    {
        if (quadra_init_mmap(&devs[i]) == 0)
        {
            int rc = dump_raw_logs(&devs[i], outdir ? outdir : ".", core_reset_log);
            fail_count += (rc != 0);
            dump_artifacts(&devs[i], outdir ? outdir : ".");
            quadra_cleanup(&devs[i]);
        }
        else
        {
            (void)fprintf(stderr, "[ERROR] Failed to mmap device %d\n", i);
            fail_count++;
        }
    }
    free(devs);
    return (fail_count == 0) ? 0 : 2;
}

/*!*****************************************************************************
 *  \brief   Convert a user nor command string to NOR_CMD_* command.
 *
 *  \param[in]  nor_cmd  "w", "r", "e", or "ex"
 *  \param[out] out_cmd  Receives the NOR_CMD_*
 *
 *  \return  0 - on success,
 *          -1 - on unknown command.
 ******************************************************************************/
int nor_cmd_decode(const char *nor_cmd, uint32_t *out_cmd)
{
    if (!nor_cmd || !out_cmd)
        return -1;

    if (strcmp(nor_cmd, "w") == 0)
    {
        *out_cmd = NOR_CMD_WRITE;
        return 0;
    }
    if (strcmp(nor_cmd, "r") == 0)
    {
        *out_cmd = NOR_CMD_READ;
        return 0;
    }
    if (strcmp(nor_cmd, "e") == 0)
    {
        *out_cmd = NOR_CMD_ERASE;
        return 0;
    }
    if (strcmp(nor_cmd, "ex") == 0)
    {
        *out_cmd = NOR_CMD_EXCEPTION;
        return 0;
    }

    (void)fprintf(stderr,
        "[ERROR] Unknown NOR command '%s'. Use w, r, e, or ex.\n", nor_cmd);
    return -1;
}

/*!*****************************************************************************
 *  \brief   Run a NOR command across an identified list of Quadra devices.
 *
 *           NOR_CMD_READ - waits for 100s, single wait for all cards
 *           NOR_CMD_WRITE / ERASE / EXCEPTION - returns immediately
 *
 *  \param[in] devs    Identified Quadra device list
 *  \param[in] ndev    Number of devices
 *  \param[in] cmd     NOR_CMD_*
 *  \param[in] outdir  Destination directory
 *
 *  \return   0 - command succeeded on every device,
 *            1 - invalid args,
 *            2 - one or more devices failed,
 *           -1 - failure.
 ******************************************************************************/
int ni_rsrc_nor_cmd_run(QuadraDevice *devs, int ndev,
                        uint32_t cmd, const char *outdir)
{
    if (!devs || ndev <= 0)
        return 1;

    const char *odir = outdir ? outdir : ".";
    int fail_count = 0;

    if (cmd == NOR_CMD_READ)
    {
        /* NOR read: 100s wait so nor read & dump from BAR4 happens fully */
        bool *ready = (bool *)calloc((size_t)ndev, sizeof(bool));
        if (!ready)
            return -1;

        /* mmap all devices and dispatch NOR_CMD_READ to each. */
        for (int i = 0; i < ndev; ++i)
        {
            if (quadra_init_mmap(&devs[i]) != 0)
            {
                (void)fprintf(stderr,
                              "[ERROR] Failed to mmap device %d\n", i);
                fail_count++;
                continue;
            }
            if (quadra_nor_dispatch(&devs[i], NOR_CMD_READ) != 0)
            {
                (void)fprintf(stderr,
                              "[ERROR] NOR read dispatch failed on device %d\n",
                              i);
                quadra_cleanup(&devs[i]);
                fail_count++;
                continue;
            }
            (void)fprintf(stderr,
                          "[INFO] NOR read dispatched to device %d\n", i);
            ready[i] = true;
        }

        /* Single wait — covers all dispatched cards in parallel */
        quadra_nor_read_wait();

        /* Dump logs from every device that dispatched successfully */
        for (int i = 0; i < ndev; ++i)
        {
            if (!ready[i])
                continue;
            fill_core_log_offsets(&devs[i]);
            int rc = dump_nor_logs(&devs[i], odir);
            fail_count += (rc != 0);
            (void)fprintf(stderr, "[INFO] NOR flash command complete.\n");
            quadra_cleanup(&devs[i]);
        }

        free(ready);
    }
    else
    {
        /* w / e / ex: each call returns immediately */
        for (int i = 0; i < ndev; ++i)
        {
            if (quadra_init_mmap(&devs[i]) != 0)
            {
                (void)fprintf(stderr,
                              "[ERROR] Failed to mmap device %d\n", i);
                fail_count++;
                continue;
            }
            if (quadra_nor_command(&devs[i], cmd) != 0)
            {
                (void)fprintf(stderr,
                              "[ERROR] NOR command failed on device %d\n", i);
                quadra_cleanup(&devs[i]);
                fail_count++;
                continue;
            }
            quadra_cleanup(&devs[i]);
        }
    }

    return (fail_count == 0) ? 0 : 2;
}

/*!*****************************************************************************
 *  \brief  Execute a NOR flash persistent log command on all detected devices.
 *          Enumerate every Quadra card on the host, decodes nor_commands (read,
 *          write, erase, exception) and dispatches them.
 *
 *  \param[in] nor_cmd      "w", "r", "e", or "ex"
 *  \param[in] outdir       Output directory
 *
 *  \return  0 - all succeeded,
 *           1 - no devices,
 *           2 - partial failure,
 *          -1 - invalid args.
 ******************************************************************************/
int ni_rsrc_nor_log_command(const char *nor_cmd, const char *outdir)
{
    uint32_t cmd;
    if (nor_cmd_decode(nor_cmd, &cmd) != 0)
        return -1;

    QuadraDevice *devs = NULL;
    int ndev = quadra_device_find(&devs);
    if (ndev <= 0)
    {
        free(devs);
        (void)fprintf(stderr,
            "[WARN] No Quadra devices found in sysfs (vendor 1d82).\n");
        return 1;
    }

    int rc = ni_rsrc_nor_cmd_run(devs, ndev, cmd, outdir);
    free(devs);
    return rc;
}
#endif
