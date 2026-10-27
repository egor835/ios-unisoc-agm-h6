/*
 * AGM H6: what Mesa's panfrost takes from libdrm's xf86drm.h, for a system without Linux's DRM
 * (after topaz/HackiOS redwood-drm). The functions are in agm_drm.c: they carry panfrost's
 * requests to the kernel's driver (the service AgmMaliGPU, iokit/Kernel/arm/AgmMaliGPU.cpp) as
 * calls of its user client. Request numbers and structures are Linux's (include/drm-uapi).
 */
#ifndef AGM_XF86DRM_H
#define AGM_XF86DRM_H

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "drm-uapi/drm.h"

#define DRM_NODE_PRIMARY        0
#define DRM_NODE_RENDER         2
#define DRM_RDWR                O_RDWR

typedef struct _drmVersion {
	int version_major, version_minor, version_patchlevel;
	int name_len;
	char *name;
	int date_len;
	char *date;
	int desc_len;
	char *desc;
} drmVersion, *drmVersionPtr;

int drmOpenWithType(const char *name, const char *busid, int type);
int drmIoctl(int fd, unsigned long request, void *arg);
drmVersionPtr drmGetVersion(int fd);
void drmFreeVersion(drmVersionPtr version);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
int drmCloseBufferHandle(int fd, uint32_t handle);

/* An IOSurface comes in where Linux's dma-buf would: its ID for the file descriptor. */
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmPrimeFDToHandle(int fd, int surface, uint32_t *handle);

/* Sync objects: kept here, each the sequence number of the submission it was last given. */
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count);
int drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t count);
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec, unsigned flags,
    uint32_t *first_signaled);
int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned num_handles, int64_t timeout_nsec,
    unsigned flags, uint32_t *first_signaled);
int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point, uint32_t src_handle, uint64_t src_point,
    uint32_t flags);
int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle);
int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd);
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);

/* Not libdrm's: a buffer's memory in this process, in place of mmap() on the device. */
void *agm_drm_map(int fd, uint32_t handle, uint64_t size);
void agm_drm_unmap(int fd, uint32_t handle, void *address);

/* Not libdrm's either: what an IOSurface would be like as a buffer (no buffer made). */
struct agm_drm_surface {
	uint32_t surface;                       /* the IOSurface's ID */
	uint32_t handle;                        /* (the kernel's) */
	uint64_t size;                          /* back: how big the buffer is, */
	uint32_t offset, row_bytes;             /* where the pixels start and how long a row is, */
	uint32_t width, height, pixel_format, planes;   /* and what they are */
	uint64_t gpu_address;
};
int agm_drm_surface(int fd, struct agm_drm_surface *surface);

#endif
