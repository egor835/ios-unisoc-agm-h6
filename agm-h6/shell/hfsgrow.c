/*
 * hfsgrow: grow the mounted HFS+ root volume (the AGM H6's iosroot) in place.
 *   hfsgrow BYTES      e.g. 135291469824 = 126 GiB (the partition's 128 GiB less the reserved tail
 *                      of kernel 0995+: drop box, dumps, console log)
 *   hfsgrow max        as large as the device says it is (its block count leaves the tail out);
 *                      nothing to do (exit 0, quiet) when the volume is that large already.
 * The kernel's HFS (bsd/sweet_hfs, hfs_extendfs) does the work: fsctl HFSIOC_RESIZE_VOLUME on "/".
 */
#include <errno.h>
#include <fcntl.h>
/* sys/disk.h is not in the iOS SDK */
#define DKIOCGETBLOCKSIZE _IOR('d', 24, uint32_t)
#define DKIOCGETBLOCKCOUNT _IOR('d', 25, uint64_t)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <unistd.h>

#define HFSIOC_RESIZE_VOLUME _IOW('h', 2, u_int64_t)

static void
show(const char *when)
{
	struct statfs st;
	if (statfs("/", &st) == 0) {
		printf("%s: %s on %s, %.2f GiB (%llu blocks of %u), %.2f GiB free\n", when, st.f_mntfromname,
		    st.f_mntonname, (double)st.f_blocks * st.f_bsize / (1 << 30), (unsigned long long)st.f_blocks,
		    st.f_bsize, (double)st.f_bavail * st.f_bsize / (1 << 30));
	}
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: hfsgrow BYTES\n");
		return 2;
	}
	uint64_t size = strtoull(argv[1], NULL, 0);
	if (strcmp(argv[1], "max") == 0) {
		struct statfs st;
		uint64_t count = 0;
		uint32_t bsize = 0;
		char raw[MNAMELEN + 2];
		if (statfs("/", &st) != 0) {
			return 1;
		}
		snprintf(raw, sizeof(raw), "/dev/r%s", st.f_mntfromname + 5);       /* /dev/X -> /dev/rX */
		int fd = open(raw, O_RDONLY);
		if (fd < 0) {
			fd = open(st.f_mntfromname, O_RDONLY);
		}
		if (fd < 0 || ioctl(fd, DKIOCGETBLOCKCOUNT, &count) != 0 || ioctl(fd, DKIOCGETBLOCKSIZE, &bsize) != 0) {
			fprintf(stderr, "hfsgrow: device size of %s: %s\n", st.f_mntfromname, strerror(errno));
			return 1;
		}
		close(fd);
		size = count * bsize / st.f_bsize * st.f_bsize;
		if (size <= (uint64_t)st.f_blocks * st.f_bsize) {
			return 0;
		}
	}
	show("before");
	if (fsctl("/", HFSIOC_RESIZE_VOLUME, &size, 0) != 0) {
		fprintf(stderr, "hfsgrow: resize to %llu bytes: %s\n", (unsigned long long)size, strerror(errno));
		return 1;
	}
	show("after");
	return 0;
}
