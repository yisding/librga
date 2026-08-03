/*
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 * Authors:
 *  Cerf Yu <cerf.yu@rock-chips.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <getopt.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>
#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/eventfd.h>

#include <sched.h>
#include <pthread.h>

#include <stdint.h>
#include <memory.h>
#include <sys/time.h>

#include "dma_alloc.h"

typedef unsigned long long __u64;
typedef  unsigned int __u32;

struct dma_heap_allocation_data {
	__u64 len;
	__u32 fd;
	__u32 fd_flags;
	__u64 heap_flags;
};

#define DMA_HEAP_IOC_MAGIC		'H'
#define DMA_HEAP_IOCTL_ALLOC	_IOWR(DMA_HEAP_IOC_MAGIC, 0x0,\
				      struct dma_heap_allocation_data)

#define DMA_BUF_SYNC_READ      (1 << 0)
#define DMA_BUF_SYNC_WRITE     (2 << 0)
#define DMA_BUF_SYNC_RW        (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START     (0 << 2)
#define DMA_BUF_SYNC_END       (1 << 2)

struct dma_buf_sync {
	__u64 flags;
};

#define DMA_BUF_BASE		'b'
#define DMA_BUF_IOCTL_SYNC	_IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

#define CMA_HEAP_SIZE	1024 * 1024

int dma_sync_device_to_cpu(int fd) {
    struct dma_buf_sync sync = {0};

    sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;
    return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

int dma_sync_cpu_to_device(int fd) {
    struct dma_buf_sync sync = {0};

    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
    return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

/*
 * Strip one heap-name qualifier ("-uncached", "-dma32") out of a heap path,
 * writing the result to out. Returns 0 when the qualifier was present.
 */
static int heap_path_drop(const char *path, const char *qualifier, char *out, size_t out_len) {
    const char *hit = strstr(path, qualifier);
    size_t head;

    if (hit == NULL)
        return -1;

    head = (size_t)(hit - path);
    if (head + strlen(hit + strlen(qualifier)) + 1 > out_len)
        return -1;

    memcpy(out, path, head);
    strcpy(out + head, hit + strlen(qualifier));

    return 0;
}

/*
 * Open a dma_heap, falling back when the requested one does not exist.
 *
 * The uncached and dma32 heap variants are Rockchip BSP additions; a mainline
 * kernel only exports "system" and CMA, so a hardcoded uncached/dma32 path
 * fails outright there. Degrade instead of dying, loudly, because both
 * qualifiers carry meaning the caller may depend on: dropping "-uncached"
 * yields cachable memory, so CPU access has to be bracketed with
 * dma_sync_cpu_to_device()/dma_sync_device_to_cpu(); dropping "-dma32" may
 * place the buffer above 4G, which the kernel refuses to hand to RGA2 and
 * will instead route to an RGA3 core.
 */
static int dma_heap_open(const char *path) {
    char candidates[4][128];
    int count = 0;
    int i;

    snprintf(candidates[count++], sizeof(candidates[0]), "%s", path);
    if (heap_path_drop(path, "-dma32", candidates[count], sizeof(candidates[0])) == 0)
        count++;
    if (heap_path_drop(path, "-uncached", candidates[count], sizeof(candidates[0])) == 0)
        count++;
    if (count > 2 &&
        heap_path_drop(candidates[count - 1], "-dma32", candidates[count], sizeof(candidates[0])) == 0)
        count++;

    for (i = 0; i < count; i++) {
        int heap_fd = open(candidates[i], O_RDWR);

        if (heap_fd >= 0) {
            if (i > 0)
                printf("dma_heap %s is absent, falling back to %s "
                       "(cachable memory needs explicit dma_sync_*; "
                       "above-4G memory is not usable by RGA2)\n",
                       path, candidates[i]);
            return heap_fd;
        }
    }

    printf("open %s fail, and no fallback heap is available!\n", path);

    return -1;
}

int dma_buf_alloc(const char *path, size_t size, int *fd, void **va) {
    int ret;
    void *mmap_va;
    int dma_heap_fd = -1;
    struct dma_heap_allocation_data buf_data;

    /* open dma_heap fd */
    dma_heap_fd = dma_heap_open(path);
    if (dma_heap_fd < 0) {
        return dma_heap_fd;
    }

    /* alloc buffer */
    memset(&buf_data, 0x0, sizeof(struct dma_heap_allocation_data));

    buf_data.len = size;
    buf_data.fd_flags = O_CLOEXEC | O_RDWR;
    ret = ioctl(dma_heap_fd, DMA_HEAP_IOCTL_ALLOC, &buf_data);
    if (ret < 0) {
        printf("RK_DMA_HEAP_ALLOC_BUFFER failed\n");

        close(dma_heap_fd);
        return ret;
    }

    /* mmap contiguors buffer to user */
    mmap_va = (void *)mmap(NULL, buf_data.len, PROT_READ | PROT_WRITE, MAP_SHARED, buf_data.fd, 0);
    if (mmap_va == MAP_FAILED) {
        printf("mmap failed: %s\n", strerror(errno));

        close(buf_data.fd);
        close(dma_heap_fd);
        return -errno;
    }

    *va = mmap_va;
    *fd = buf_data.fd;

    close(dma_heap_fd);

    return 0;
}

void dma_buf_free(size_t size, int *fd, void *va) {
    int len;

    len =  size;
    munmap(va, len);

    close(*fd);
    *fd = -1;
}



