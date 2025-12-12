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

#define LOG_SIZE     (1024 * 1024)
#define NUM_CORES    5
#define PCI_SYSFS_PATH "/sys/bus/pci/devices"
#define QUADRA_VENDOR_ID 0x1d82
#define QUADRA_DEVICE_ID 0x0401

#if defined(__linux__)

const char *const core_names[NUM_CORES] = {"np", "dp", "ep", "tp", "fp"};

typedef struct {
    char sysfs_path[256];
    int domain;
    int bus;
    int slot;
    int bar4_fd;
    unsigned char *bar4_base;
    off_t barsize;
    uint32_t core_log_offsets[NUM_CORES];
    int log_size;
} QuadraDevice;

int bdf_to_sysfs(const char* bdf, char* sysfs_path, size_t pathsz, int *domain, int *bus, int *slot)
{
    unsigned d=0, b=0, s=0, f=0;
    if (sscanf(bdf, "%x:%x:%x.%x", &d, &b, &s, &f) != 4)
        return -1;
    if (snprintf(sysfs_path, pathsz, "/sys/bus/pci/devices/%04x:%02x:%02x.%x", d, b, s, f) < 0)
        return -1;
    if (domain) *domain = (int)d;
    if (bus) *bus = (int)b;
    if (slot) *slot = (int)s;
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
    while ((entry = readdir(dir)))
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
            fclose(fv);
            continue;
        }
        fclose(fv);

        unsigned long ven = strtoul(buf, NULL, 0);
        if (ven != QUADRA_VENDOR_ID)
            continue;

        /* Read device */
        FILE *fd = fopen(dev_path, "r");
        if (!fd)
            continue;

        if (!fgets(buf, sizeof(buf), fd))
        {
            fclose(fd);
            continue;
        }
        fclose(fd);

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
                         &devs[dev_count].domain, &devs[dev_count].bus, &devs[dev_count].slot) == 0)
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
    qd->bar4_fd = open(bar4path, O_RDWR | O_SYNC);
    if (qd->bar4_fd == -1)
    {
        perror("open(bar4)");
        return -1;
    }
    if (fstat(qd->bar4_fd, &bar4stat) == 0)
    {
        qd->barsize = bar4stat.st_size;
    }
    qd->bar4_base = mmap(NULL, qd->barsize, PROT_READ | PROT_WRITE, MAP_SHARED, qd->bar4_fd, 0);
    if (qd->bar4_base == MAP_FAILED)
    {
        if (errno == EACCES)
            (void)fprintf(stderr, "Permission error: Cannot mmap %s. Run as root (sudo).\n", bar4path);
        else if (errno == EINVAL)
            (void)fprintf(stderr, "EINVAL: mmap() on BAR4 failed. Try kernel flag 'iomem=relaxed'.\n");
        else
            (void)fprintf(stderr, "Failed to mmap BAR4 at %s: %s\n", bar4path, strerror(errno));
        close(qd->bar4_fd);
        qd->bar4_fd = -1;
        return -1;
    }
    return 0;
}

void fill_core_log_offsets(QuadraDevice *qd)
{
    uint8_t *base = qd->bar4_base;
    off_t offset = 0xf8f5000;
    for (int i = 0; i < NUM_CORES; ++i)
    {
        qd->core_log_offsets[i] = *(uint32_t *)(base + offset + 4 * (size_t)i) & 0x0FFFFFFF;
    }
    qd->log_size = 1024 * 1024;
}

void fill_core_reset_log_offsets(QuadraDevice *qd)
{
    uint8_t *base = qd->bar4_base;
    off_t offset = 0xf8fd0c0;
    for (int i = 0; i < NUM_CORES; ++i)
    {
        qd->core_log_offsets[i] = *(uint32_t *)(base + offset + 4 * (size_t)i) & 0x0FFFFFFF;
    }
    qd->log_size = 64 * 1024;
}

int dump_raw_logs(QuadraDevice *qd, const char *outdir, bool core_reset_log)
{
    if (core_reset_log)
    {
        fill_core_reset_log_offsets(qd);
    }
    else
    {
        fill_core_log_offsets(qd);
    }
    int rc = 0;
    for (int core_idx = 0; core_idx < NUM_CORES; ++core_idx)
    {
        const char *core = core_names[core_idx];
        uint32_t log_offset = qd->core_log_offsets[core_idx];
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
            (void)fprintf(stderr, "Short write on %s\n", fname);
            rc = -1;
        }
        (void)fclose(fp);
        chmod(fname, 0664);
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
#endif
