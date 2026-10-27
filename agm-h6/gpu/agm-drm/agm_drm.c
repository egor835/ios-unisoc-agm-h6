/*
 * AGM H6: libdrm for Mesa's panfrost (see xf86drm.h). Linux's DRM requests go to the kernel's
 * GPU driver as calls of its IOKit user client (AgmMaliGPU): the request's number (low byte
 * of the ioctl code) is the selector, its structure goes in and comes back. A buffer's memory
 * is the client memory of the type that is its handle.
 *
 * What differs from Linux is made up for here (the kernel's side: AgmMaliGPU.cpp):
 *  - sync objects live here: each is the sequence number of the submission it stands for (0:
 *    signalled). SUBMIT's answer brings the sequence number back in out_sync; waits on
 *    in_syncs are made before the submission (the GPU runs submissions in order anyway);
 *  - timeouts: Linux's are absolute CLOCK_MONOTONIC nanoseconds, the kernel's here relative;
 *  - a "file descriptor" of a sync file is one of /dev/null's, remembered with its number.
 *
 * panfrost knows its device by a file descriptor: it gets one of /dev/null's; there is one GPU
 * and one connection to it in a process.
 */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <IOKit/IOKitLib.h>
#include <mach/mach.h>

#include "xf86drm.h"

#define AGM_GPU_SERVICE         "AgmMaliGPU"
#define AGM_WAIT_SEQNO          0x7d
#define AGM_SURFACE             0x7e
#define AGM_DESCRIBE            0xffffffffu

#define PAN_SUBMIT              0x40            /* DRM_COMMAND_BASE + DRM_PANFROST_SUBMIT */
#define PAN_WAIT_BO             0x41
#define GEM_CLOSE               0x09

struct pan_submit {
	uint64_t jc, in_syncs;
	uint32_t in_sync_count, out_sync;
	uint64_t bo_handles;
	uint32_t bo_handle_count, requirements;
};
struct pan_wait_bo {
	uint32_t handle, pad;
	int64_t timeout_ns;
};
struct wait_seqno {
	uint64_t seqno;
	int64_t timeout_ns;
};

static io_connect_t connection;
static int connection_fd = -1;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

#define SYNCOBJS                4096
static struct {
	uint8_t used;
	uint64_t seqno;
} syncobj[SYNCOBJS];                            /* handle n is syncobj[n] (0 is no handle) */

#define SYNCFILES               256
static struct {
	int fd;
	uint64_t seqno;
} syncfile[SYNCFILES];

int
drmOpenWithType(const char *name, const char *busid, int type)
{
	io_service_t service;

	(void)busid;
	(void)type;
	if (strcmp(name, "panfrost") != 0) {
		return -1;
	}
	if (connection_fd >= 0) {
		return dup(connection_fd);
	}
	service = IOServiceGetMatchingService(0, IOServiceMatching(AGM_GPU_SERVICE));
	if (service == 0) {
		return -1;
	}
	if (IOServiceOpen(service, mach_task_self(), 0, &connection) != 0) {
		IOObjectRelease(service);
		return -1;
	}
	IOObjectRelease(service);
	connection_fd = open("/dev/null", O_RDWR);
	return dup(connection_fd);
}

static int
call(unsigned number, void *data, size_t size)
{
	size_t returned = size;
	kern_return_t result = IOConnectCallStructMethod(connection, number, data, size, data, &returned);

	switch (result) {
	case 0:
		return 0;
	case kIOReturnNoMemory:
		return ENOMEM;
	case kIOReturnBadArgument:
		return EINVAL;
	case kIOReturnTimeout:
		return ETIMEDOUT;
	case kIOReturnBusy:
		return EBUSY;
	case kIOReturnNotFound:
		return ENOENT;
	case kIOReturnUnsupported:
		return ENOSYS;
	default:
		return EIO;
	}
}

/* Linux's absolute CLOCK_MONOTONIC time as nanoseconds from now; -1: for ever. */
static int64_t
relative(int64_t absolute)
{
	struct timespec t;
	int64_t now;

	if (absolute == INT64_MAX) {
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC, &t);
	now = (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
	return absolute > now ? absolute - now : 0;
}

static int
wait_seqno(uint64_t seqno, int64_t relative_ns)
{
	struct wait_seqno w = { .seqno = seqno, .timeout_ns = relative_ns };

	return seqno == 0 ? 0 : call(AGM_WAIT_SEQNO, &w, sizeof(w));
}

static uint64_t
seqno_of(uint32_t handle)
{
	uint64_t s = 0;

	pthread_mutex_lock(&lock);
	if (handle < SYNCOBJS && syncobj[handle].used) {
		s = syncobj[handle].seqno;
	}
	pthread_mutex_unlock(&lock);
	return s;
}

static void
set_seqno(uint32_t handle, uint64_t seqno)
{
	pthread_mutex_lock(&lock);
	if (handle < SYNCOBJS && syncobj[handle].used) {
		syncobj[handle].seqno = seqno;
	}
	pthread_mutex_unlock(&lock);
}

int
drmIoctl(int fd, unsigned long request, void *arg)
{
	unsigned number = (unsigned)(request & 0xff);
	size_t size = IOCPARM_LEN(request);
	int error;

	(void)fd;
	if (number == PAN_SUBMIT) {
		struct pan_submit *s = arg, copy = *s;
		const uint32_t *in = (const uint32_t *)(uintptr_t)s->in_syncs;

		for (uint32_t i = 0; i < s->in_sync_count; i++) {
			wait_seqno(seqno_of(in[i]), -1);
		}
		error = call(number, &copy, sizeof(copy));
		if (error == 0 && s->out_sync != 0) {
			set_seqno(s->out_sync, copy.out_sync);
		}
	} else if (number == PAN_WAIT_BO) {
		struct pan_wait_bo w = *(struct pan_wait_bo *)arg;

		w.timeout_ns = relative(w.timeout_ns);
		error = call(number, &w, sizeof(w));
	} else {
		error = call(number, arg, size);
	}
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}

int
drmCloseBufferHandle(int fd, uint32_t handle)
{
	struct drm_gem_close c = { .handle = handle };

	return drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &c);
}

/* The interface of the version whose features this kernel's driver has: 1.1 (heap buffers). */
drmVersionPtr
drmGetVersion(int fd)
{
	drmVersionPtr version = calloc(1, sizeof(*version));

	(void)fd;
	if (version != NULL) {
		version->version_major = 1;
		version->version_minor = 1;
		version->name = strdup("panfrost");
		version->name_len = 8;
		version->date = strdup("");
		version->desc = strdup("Mali-G57 on Darwin (AGM H6)");
		version->desc_len = (int)strlen(version->desc);
	}
	return version;
}

void
drmFreeVersion(drmVersionPtr version)
{
	if (version != NULL) {
		free(version->name);
		free(version->date);
		free(version->desc);
		free(version);
	}
}

int
drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
	(void)fd;
	(void)capability;
	*value = 0;
	return 0;
}

/* The kernel maps the buffer write-combined, as Linux gives panfrost its buffers. */
void *
agm_drm_map(int fd, uint32_t handle, uint64_t size)
{
	mach_vm_address_t address = 0;
	mach_vm_size_t mapped = 0;

	(void)fd;
	(void)size;
	if (IOConnectMapMemory64(connection, handle, mach_task_self(), &address, &mapped, kIOMapAnywhere) != 0) {
		return (void *)-1;              /* MAP_FAILED */
	}
	return (void *)(uintptr_t)address;
}

void
agm_drm_unmap(int fd, uint32_t handle, void *address)
{
	(void)fd;
	IOConnectUnmapMemory64(connection, handle, mach_task_self(), (mach_vm_address_t)(uintptr_t)address);
}

int
agm_drm_surface(int fd, struct agm_drm_surface *surface)
{
	int error;

	(void)fd;
	surface->handle = AGM_DESCRIBE;
	error = call(AGM_SURFACE, surface, sizeof(*surface));
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}

int
drmPrimeFDToHandle(int fd, int surface, uint32_t *handle)
{
	struct agm_drm_surface request = { .surface = (uint32_t)surface };
	int error = call(AGM_SURFACE, &request, sizeof(request));

	(void)fd;
	if (error != 0) {
		errno = error;
		return -1;
	}
	*handle = request.handle;
	return 0;
}

int
drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
	(void)fd;
	(void)handle;
	(void)flags;
	(void)prime_fd;
	errno = ENOSYS;
	return -1;
}

/* ---------------------------------------------------------------- sync objects */

int
drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
	(void)fd;
	(void)flags;                            /* signalled or not: a new one stands for nothing yet */
	pthread_mutex_lock(&lock);
	for (uint32_t h = 1; h < SYNCOBJS; h++) {
		if (!syncobj[h].used) {
			syncobj[h].used = 1;
			syncobj[h].seqno = 0;
			*handle = h;
			pthread_mutex_unlock(&lock);
			return 0;
		}
	}
	pthread_mutex_unlock(&lock);
	errno = ENOMEM;
	return -1;
}

int
drmSyncobjDestroy(int fd, uint32_t handle)
{
	(void)fd;
	pthread_mutex_lock(&lock);
	if (handle < SYNCOBJS) {
		syncobj[handle].used = 0;
	}
	pthread_mutex_unlock(&lock);
	return 0;
}

int
drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count)
{
	(void)fd;
	for (uint32_t i = 0; i < count; i++) {
		set_seqno(handles[i], 0);
	}
	return 0;
}

int
drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t count)
{
	return drmSyncobjReset(fd, handles, count);
}

int
drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec, unsigned flags,
    uint32_t *first_signaled)
{
	(void)fd;
	(void)flags;                            /* in order: waiting for all is waiting for the last */
	for (unsigned i = 0; i < num_handles; i++) {
		int error = wait_seqno(seqno_of(handles[i]), relative(timeout_nsec));
		if (error != 0) {
			errno = error;
			return -1;
		}
	}
	if (first_signaled != NULL) {
		*first_signaled = 0;
	}
	return 0;
}

int
drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned num_handles, int64_t timeout_nsec,
    unsigned flags, uint32_t *first_signaled)
{
	(void)points;
	return drmSyncobjWait(fd, handles, num_handles, timeout_nsec, flags, first_signaled);
}

int
drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point, uint32_t src_handle, uint64_t src_point,
    uint32_t flags)
{
	(void)fd;
	(void)dst_point;
	(void)src_point;
	(void)flags;
	set_seqno(dst_handle, seqno_of(src_handle));
	return 0;
}

int
drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
	uint64_t s = seqno_of(handle);

	(void)fd;
	pthread_mutex_lock(&lock);
	for (int i = 0; i < SYNCFILES; i++) {
		if (syncfile[i].fd <= 0) {
			int f = open("/dev/null", O_RDONLY | O_CLOEXEC);
			if (f < 0) {
				break;
			}
			syncfile[i].fd = f;
			syncfile[i].seqno = s;
			*sync_file_fd = f;
			pthread_mutex_unlock(&lock);
			return 0;
		}
	}
	pthread_mutex_unlock(&lock);
	errno = ENOMEM;
	return -1;
}

int
drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd)
{
	uint64_t s = 0;

	(void)fd;
	pthread_mutex_lock(&lock);
	for (int i = 0; i < SYNCFILES; i++) {
		if (syncfile[i].fd == sync_file_fd) {
			s = syncfile[i].seqno;
			syncfile[i].fd = 0;     /* (whoever imports it closes it) */
			break;
		}
	}
	pthread_mutex_unlock(&lock);
	set_seqno(handle, s);
	return 0;
}

int
drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle)
{
	if (drmSyncobjCreate(fd, 0, handle) != 0) {
		return -1;
	}
	return drmSyncobjImportSyncFile(fd, *handle, obj_fd);
}

int
drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd)
{
	return drmSyncobjExportSyncFile(fd, handle, obj_fd);
}
