/*
 * AGM H6: libagm-inject.dylib (from topaz's topaz_inject.c), inserted into backboardd and the
 * container managers by the kernel (boot-arg topaz_inject, kern.exec_inject). When the kernel
 * has published the GPU service (boot-arg agm_gpu=1: AgmMaliGPU, the Mali-G57), it loads the
 * OpenGL ES library that stands behind EAGLContext (Mesa's panfrost behind topaz's eagl.c), so
 * CoreAnimation's window server draws with the Mali instead of its software renderer.
 * gl=0 in the boot-args: never.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/ucontext.h>
#include <objc/runtime.h>
#include <mach/mach.h>
#include <stdint.h>

extern const void *_dyld_get_shared_cache_range(size_t *length);

#define TOPAZ_GL "/usr/lib/libagm-gl.dylib"

/*
 * CoreAnimation's OpenGL ES renderer hands a "function"'s numbers (a colour matrix, ...) to
 * GLContext::function_uniform(); for the functions it has no shader for they go nowhere, and our
 * library draws those with programs of its own (eagl.c) once told them. Without that the lock
 * screen's vibrantColorMatrix layer is a red veil. (Scheme from darwin-redwood inject.c; addresses
 * are this iOS 16.7.16 cache's.)
 */
#define CACHE_BASE              0x180000000UL
#define VT_GLES_CONTEXT         0x1d5b2f6e8UL   /* vtable for CA::OGL::GLESContext */
#define VSLOT(vtable, offset)   ((vtable) + 0x10 + (offset))
#define FN_FUNCTION_SUPPORTED   0x1884b2164UL   /* CA::OGL::GLContext::function_supported() const */
#define FN_FUNCTION_UNIFORM     0x1884b21acUL   /* CA::OGL::GLContext::function_uniform(unsigned, unsigned long, unsigned long, float const*) */

static uintptr_t slide;

static void *
replace_virtual(const char *what, uintptr_t slot, uintptr_t expected, void *replacement)
{
	uintptr_t *entry = (uintptr_t *)(slot + slide);
	vm_address_t page = (vm_address_t)entry & ~(vm_address_t)(vm_page_size - 1);
	void *original;

	if (*entry != expected + slide) {
		fprintf(stderr, "topaz-gl: %s: slot holds %#lx, expected %#lx: left alone\n", what, *entry - slide, expected);
		return NULL;
	}
	if (vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
		fprintf(stderr, "topaz-gl: %s: table not writable\n", what);
		return NULL;
	}
	original = (void *)*entry;
	*entry = (uintptr_t)replacement;
	vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ);
	return original;
}

static int (*orig_function_supported)(void *context);
static void (*gl_function_refused)(unsigned function);
static void (*orig_function_uniform)(void *context, unsigned index, size_t components, size_t count, const float *values);
static void (*gl_function_values)(void *context, unsigned function, unsigned index, size_t components, size_t count,
    const float *values);

static unsigned
context_function(void *context)
{
	return *(*(uint8_t **)((char *)context + 0x10) + 0x10);
}

static int
my_function_supported(void *context)
{
	static uint8_t said[256];
	int supported = orig_function_supported(context);
	unsigned function = context_function(context);

	if (!supported) {
		if (gl_function_refused != NULL) {
			gl_function_refused(function);
		}
		if (!said[function & 255]) {
			said[function & 255] = 1;
			fprintf(stderr, "topaz-gl: CoreAnimation's GL renderer has no function %u\n", function);
		}
	}
	return supported;
}

static void
my_function_uniform(void *context, unsigned index, size_t components, size_t count, const float *values)
{
	gl_function_values(context, context_function(context), index, components, count, values);
	orig_function_uniform(context, index, components, count, values);
}

static void
gl_hooks(void *lib)
{
	size_t length = 0;
	const void *cache = _dyld_get_shared_cache_range(&length);

	if (cache == NULL || lib == NULL) {
		return;
	}
	slide = (uintptr_t)cache - CACHE_BASE;
	gl_function_refused = (void (*)(unsigned))dlsym(lib, "redwood_gl_function_refused");
	gl_function_values = (void (*)(void *, unsigned, unsigned, size_t, size_t, const float *))dlsym(lib,
	    "redwood_gl_function_values");
	orig_function_supported = replace_virtual("function_supported", VSLOT(VT_GLES_CONTEXT, 0x378), FN_FUNCTION_SUPPORTED,
	    my_function_supported);
	if (gl_function_values != NULL) {
		orig_function_uniform = replace_virtual("function_uniform", VSLOT(VT_GLES_CONTEXT, 0x388), FN_FUNCTION_UNIFORM,
		    my_function_uniform);
	}
	fprintf(stderr, "topaz-gl: GLESContext hooks: function_supported %s, function_uniform %s\n",
	    orig_function_supported ? "on" : "OFF", orig_function_uniform ? "on" : "OFF");
}

/* Where a crash happened, printed to stderr (the console): image + offset + symbol. */
static void
topaz_where(const char *what, void *addr)
{
	Dl_info info;

	if (dladdr(addr, &info) && info.dli_fname != NULL) {
		fprintf(stderr, "topaz-crash: %s %p = %s+0x%lx (%s)\n", what, addr, info.dli_fname,
		    (unsigned long)((char *)addr - (char *)info.dli_fbase), info.dli_sname ? info.dli_sname : "?");
	} else {
		fprintf(stderr, "topaz-crash: %s %p (no image)\n", what, addr);
	}
}

static void
topaz_crash(int sig, siginfo_t *si, void *uc_)
{
	ucontext_t *uc = (ucontext_t *)uc_;
	void *frames[16];
	int n;

	fprintf(stderr, "topaz-crash: signal %d at address %p\n", sig, si ? si->si_addr : NULL);
	if (uc != NULL && uc->uc_mcontext != NULL) {
		topaz_where("pc", (void *)uc->uc_mcontext->__ss.__pc);
		topaz_where("lr", (void *)uc->uc_mcontext->__ss.__lr);
	}
	n = backtrace(frames, 16);
	for (int i = 0; i < n; i++) {
		topaz_where("bt", frames[i]);
	}
	signal(sig, SIG_DFL);
	raise(sig);
}

static void
topaz_crash_handlers(void)
{
	struct sigaction sa;
	int sigs[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGTRAP, SIGFPE };

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = topaz_crash;
	sa.sa_flags = SA_SIGINFO;
	for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
		sigaction(sigs[i], &sa, NULL);
	}
}

/*
 * containermanagerd (both): they ask the user manager for the user's personas, of which this
 * system has none, again and again before giving up, and everything that needs a container
 * waits with them -- extensions (posters, widgets) are not registered at all. Told personas are
 * not supported, they do not ask. (darwin-redwood inject.c; address of this 16.7.16 cache.)
 */
#define FN_PERSONAS_ARE_SUPPORTED 0x1c4c699f8UL /* +[MCMUserIdentityCache personasAreSupported] */

static BOOL
personas_are_supported(__unused id self, __unused SEL selector)
{
	return NO;
}

static void
container_manager_fixes(void)
{
	size_t length = 0;
	const void *cache = _dyld_get_shared_cache_range(&length);
	Class cls = objc_getClass("MCMUserIdentityCache");
	Method method = cls ? class_getClassMethod(cls, sel_registerName("personasAreSupported")) : NULL;
	IMP original = method ? method_getImplementation(method) : NULL;

	if (cache == NULL || original == NULL ||
	    (uintptr_t)original != FN_PERSONAS_ARE_SUPPORTED + ((uintptr_t)cache - CACHE_BASE)) {
		fprintf(stderr, "topaz-cm: +[MCMUserIdentityCache personasAreSupported] not as expected: left alone\n");
		return;
	}
	method_setImplementation(method, (IMP)personas_are_supported);
	fprintf(stderr, "topaz-cm: container manager: no personas\n");
	FILE *f = NULL;      /* (launchd sends its stderr to /dev/null) */
	if (f != NULL) {
		fprintf(f, "%s[%d]: no personas\n", getprogname(), getpid());
		fclose(f);
	}
}

/*
 * AGM H6: a sampling profiler for the window server (boot-arg agmprof=1). A thread of its own
 * looks every millisecond at the threads of this process that are running, stops each for an
 * instant to read its pc, and every ten seconds writes to standard error (the kernel log: the
 * kernel's sweetnull tee) which functions the samples fell in, the most frequent first, with
 * the image and the share of all samples, and which threads they were.
 */
#include <pthread.h>
#include <mach/thread_info.h>

#define PROF_MAX        60000
#define PROF_TOP        40
static uintptr_t prof_pc[PROF_MAX];
static uintptr_t prof_caller[PROF_MAX];         /* the first return address outside the system libraries, or 0 */
#define PROF_DEPTH      10
static uintptr_t prof_ret[PROF_MAX][PROF_DEPTH];       /* lr, then the frames' return addresses (raw) */
static uint16_t prof_thr[PROF_MAX];
static unsigned prof_n;

struct prof_sym {
	const char *image, *name;
	uintptr_t base;
	unsigned count;
};

static int
prof_cmp(const void *a, const void *b)
{
	const struct prof_sym *x = a, *y = b;
	return (int)y->count - (int)x->count;
}

/* Is this address in a library that only carries a call into the kernel? */
static int
prof_system(uintptr_t pc)
{
	Dl_info info;
	if (!dladdr((void *)pc, &info) || info.dli_fname == NULL) {
		return 0;
	}
	const char *f = info.dli_fname;
	return strstr(f, "libsystem_") != NULL || strstr(f, "/IOKit") != NULL || strstr(f, "libdispatch") != NULL ||
	       strstr(f, "/IOSurface") != NULL || strstr(f, "libxpc") != NULL;
}

static void
prof_report(unsigned ticks, thread_act_t *threads, unsigned nthreads, unsigned *per_thread)
{
	static struct prof_sym syms[4096];
	unsigned nsyms = 0, other = 0;

	for (unsigned i = 0; i < prof_n; i++) {
		Dl_info info;
		uintptr_t base = 0;
		const char *image = "?", *name = "?";
		if (dladdr((void *)prof_pc[i], &info)) {
			image = info.dli_fname ? strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname : "?";
			name = info.dli_sname ? info.dli_sname : "?";
			base = info.dli_saddr ? (uintptr_t)info.dli_saddr : (uintptr_t)info.dli_fbase;
		}
		unsigned j;
		for (j = 0; j < nsyms && syms[j].base != base; j++) {
		}
		if (j == nsyms) {
			if (nsyms == sizeof(syms) / sizeof(syms[0])) {
				other++;
				continue;
			}
			syms[nsyms].image = image;
			syms[nsyms].name = name;
			syms[nsyms].base = base;
			syms[nsyms].count = 0;
			nsyms++;
		}
		syms[j].count++;
	}
	qsort(syms, nsyms, sizeof(syms[0]), prof_cmp);
	/* the callers of the system calls, by function */
	static struct prof_sym callers[1024];
	unsigned ncallers = 0, in_kernel = 0;
	for (unsigned i = 0; i < prof_n; i++) {
		prof_caller[i] = 0;
		if (prof_system(prof_pc[i])) {
			for (int k = 0; k < PROF_DEPTH && prof_ret[i][k] != 0; k++) {
				if (!prof_system(prof_ret[i][k])) {
					prof_caller[i] = prof_ret[i][k];
					break;
				}
			}
		}
	}
	for (unsigned i = 0; i < prof_n; i++) {
		Dl_info info;
		if (prof_caller[i] == 0 || !dladdr((void *)prof_pc[i], &info) || info.dli_fname == NULL ||
		    strstr(info.dli_fname, "libsystem_kernel") == NULL) {
			continue;
		}
		in_kernel++;
		uintptr_t base = 0;
		const char *image = "?", *name = "?";
		if (dladdr((void *)prof_caller[i], &info)) {
			image = info.dli_fname ? strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname : "?";
			name = info.dli_sname ? info.dli_sname : "?";
			base = info.dli_saddr ? (uintptr_t)info.dli_saddr : prof_caller[i];
		}
		unsigned j;
		for (j = 0; j < ncallers && callers[j].base != base; j++) {
		}
		if (j == ncallers) {
			if (ncallers == sizeof(callers) / sizeof(callers[0])) {
				continue;
			}
			callers[ncallers].image = image;
			callers[ncallers].name = name;
			callers[ncallers].base = base;
			callers[ncallers].count = 0;
			ncallers++;
		}
		callers[j].count++;
	}
	qsort(callers, ncallers, sizeof(callers[0]), prof_cmp);
	fprintf(stderr, "agm-prof: %u samples in libsystem_kernel; by caller:\n", in_kernel);
	for (unsigned i = 0; i < ncallers && i < 25; i++) {
		fprintf(stderr, "agm-prof:   %5.1f%% from %s %s (+%#lx)\n", callers[i].count * 100.0 / (prof_n ? prof_n : 1),
		    callers[i].image, callers[i].name, (unsigned long)0);
	}
	fprintf(stderr, "agm-prof: %u samples in %u ticks (%.1f running threads on average)\n", prof_n, ticks,
	    ticks ? (double)prof_n / ticks : 0.0);
	for (unsigned i = 0; i < nthreads && i < 64; i++) {
		if (per_thread[i] * 50 >= prof_n && prof_n != 0) {
			char tname[64] = "";
			pthread_t pt = pthread_from_mach_thread_np(threads[i]);
			if (pt != NULL) {
				pthread_getname_np(pt, tname, sizeof(tname));
			}
			fprintf(stderr, "agm-prof: thread %u '%s': %u%%\n", i, tname, per_thread[i] * 100 / prof_n);
		}
	}
	for (unsigned i = 0; i < nsyms && i < PROF_TOP; i++) {
		fprintf(stderr, "agm-prof: %5.1f%% %s %s\n", syms[i].count * 100.0 / (prof_n ? prof_n : 1), syms[i].image,
		    syms[i].name);
	}
}

static int prof_mode = 1;                      /* 1: running threads every ms; 2: every thread every 10 ms */

/* agmprof=2: where the named threads are, waiting or not: the first frame outside the system libraries */
static void
prof_waits(thread_act_t *threads, unsigned nthreads, char names[][48])
{
	for (unsigned t = 0; t < nthreads && t < 64; t++) {
		if (names[t][0] == 0 && t != 0) {
			continue;
		}
		static struct prof_sym callers[256];
		unsigned n = 0, total = 0;
		for (unsigned i = 0; i < prof_n; i++) {
			if (prof_thr[i] != t) {
				continue;
			}
			total++;
			uintptr_t caller = prof_pc[i];
			if (prof_system(caller)) {
				caller = 0;
				for (int k = 0; k < PROF_DEPTH && prof_ret[i][k] != 0; k++) {
					if (!prof_system(prof_ret[i][k])) {
						caller = prof_ret[i][k];
						break;
					}
				}
			}
			Dl_info info;
			const char *image = "?", *name = "?";
			uintptr_t base = caller;
			if (caller != 0 && dladdr((void *)caller, &info)) {
				image = info.dli_fname ? strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname : "?";
				name = info.dli_sname ? info.dli_sname : "?";
				base = info.dli_saddr ? (uintptr_t)info.dli_saddr : caller;
			}
			unsigned j;
			for (j = 0; j < n && callers[j].base != base; j++) {
			}
			if (j == n) {
				if (n == sizeof(callers) / sizeof(callers[0])) {
					continue;
				}
				callers[n].image = image;
				callers[n].name = name;
				callers[n].base = base;
				callers[n].count = 0;
				n++;
			}
			callers[j].count++;
		}
		if (total == 0) {
			continue;
		}
		qsort(callers, n, sizeof(callers[0]), prof_cmp);
		fprintf(stderr, "agm-wait: thread %u '%s', %u samples:\n", t, names[t], total);
		for (unsigned i = 0; i < n && i < 6; i++) {
			fprintf(stderr, "agm-wait:   %3u%% %s %s (+%#lx)\n", callers[i].count * 100 / total, callers[i].image,
			    callers[i].name, (unsigned long)0);
		}
	}
}

static void *
prof_main(void *arg)
{
	(void)arg;
	pthread_setname_np("agm-prof");
	thread_act_t self = mach_thread_self();
	for (;;) {
		thread_act_array_t threads = NULL;
		mach_msg_type_number_t nthreads = 0;
		unsigned per_thread[64] = { 0 }, ticks = 0;

		prof_n = 0;
		if (task_threads(mach_task_self(), &threads, &nthreads) != KERN_SUCCESS) {
			sleep(10);
			continue;
		}
		static char names[64][48];
		for (unsigned t = 0; t < nthreads && t < 64; t++) {
			names[t][0] = 0;
			pthread_t pt = pthread_from_mach_thread_np(threads[t]);
			if (pt != NULL) {
				pthread_getname_np(pt, names[t], sizeof(names[t]));
			}
		}
		for (ticks = 0; ticks < (prof_mode == 2 ? 1000u : 10000u); ticks++) {
			for (unsigned t = 0; t < nthreads && prof_n < PROF_MAX; t++) {
				thread_basic_info_data_t bi;
				mach_msg_type_number_t cnt = THREAD_BASIC_INFO_COUNT;
				if (threads[t] == self || thread_info(threads[t], THREAD_BASIC_INFO, (thread_info_t)&bi, &cnt) != KERN_SUCCESS ||
				    (prof_mode == 1 && bi.run_state != TH_STATE_RUNNING) ||
				    (prof_mode == 2 && t != 0 && (t >= 64 || strstr(names[t], "coreanimation") == NULL))) {
					continue;
				}
				arm_thread_state64_t st;
				mach_msg_type_number_t sc = ARM_THREAD_STATE64_COUNT;
				if (thread_suspend(threads[t]) != KERN_SUCCESS) {
					continue;
				}
				if (thread_get_state(threads[t], ARM_THREAD_STATE64, (thread_state_t)&st, &sc) == KERN_SUCCESS) {
					prof_pc[prof_n] = (uintptr_t)arm_thread_state64_get_pc(st);
					prof_thr[prof_n] = (uint16_t)t;
					/*
					 * Only raw addresses while the thread is stopped (vm_read is the kernel's: no lock of
					 * this process's is taken; dladdr() here deadlocked the window server once, on
					 * dyld's lock held by the stopped thread). What they are is looked up afterwards.
					 */
					uintptr_t fp = (uintptr_t)arm_thread_state64_get_fp(st);
					prof_ret[prof_n][0] = (uintptr_t)arm_thread_state64_get_lr(st) & 0x0000000fffffffffull;
					for (int k = 1; k < PROF_DEPTH; k++) {
						uintptr_t frame[2] = { 0, 0 };
						vm_size_t got = 0;
						prof_ret[prof_n][k] = 0;
						if (fp == 0 || (fp & 7) != 0 ||
						    vm_read_overwrite(mach_task_self(), fp, sizeof(frame), (vm_address_t)frame, &got) != KERN_SUCCESS ||
						    got != sizeof(frame)) {
							break;
						}
						prof_ret[prof_n][k] = frame[1] & 0x0000000fffffffffull;
						if (frame[0] <= fp) {
							break;
						}
						fp = frame[0];
					}
					uintptr_t caller = 0;
					prof_caller[prof_n] = caller;
					prof_n++;
					if (t < 64) {
						per_thread[t]++;
					}
				}
				thread_resume(threads[t]);
			}
			usleep(prof_mode == 2 ? 10000 : 1000);
		}
		if (prof_mode == 2) {
			prof_waits(threads, nthreads, names);
		} else {
			prof_report(ticks, threads, nthreads, per_thread);
		}
		for (unsigned t = 0; t < nthreads; t++) {
			mach_port_deallocate(mach_task_self(), threads[t]);
		}
		vm_deallocate(mach_task_self(), (vm_address_t)threads, nthreads * sizeof(threads[0]));
	}
	return NULL;
}

static void
prof_start(const char *args)
{
	pthread_t t;
	if (strstr(args, " agmprof=2") != NULL) {
		prof_mode = 2;
	}
	if ((strstr(args, " agmprof=1") != NULL || prof_mode == 2) && pthread_create(&t, NULL, prof_main, NULL) == 0) {
		pthread_detach(t);
		fprintf(stderr, "agm-prof: sampling the window server every millisecond\n");
	}
}

__attribute__((constructor))
static void
topaz_inject(void)
{
	const char *name = getprogname();
	char args[1024] = "";
	size_t size = sizeof(args) - 1;
	io_service_t gpu;
	void *lib;

	if (name != NULL && strncmp(name, "containermanagerd", 17) == 0) {
		container_manager_fixes();
		return;
	}
	if (name == NULL || strcmp(name, "backboardd") != 0 || access(TOPAZ_GL, R_OK) != 0) {
		return;
	}
	if (sysctlbyname("kern.bootargs", args, &size, NULL, 0) == 0 && strstr(args, " gl=0") != NULL) {
		syslog(LOG_NOTICE, "topaz: gl=0: CoreAnimation keeps its software renderer");
		return;
	}
	/* the kernel starts the GPU once the root filesystem is there: give it a moment */
	for (int i = 0; i < 50; i++) {
		gpu = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AgmMaliGPU"));
		if (gpu != 0) {
			break;
		}
		usleep(100000);
	}
	if (gpu == 0) {
		syslog(LOG_NOTICE, "topaz: no GPU service: software renderer");
		return;
	}
	IOObjectRelease(gpu);
	topaz_crash_handlers();
	lib = dlopen(TOPAZ_GL, RTLD_NOW);
	gl_hooks(lib);
	fprintf(stderr, "topaz-crash: handlers set, GL %s\n", lib ? "loaded" : dlerror());
	prof_start(args);
	syslog(LOG_NOTICE, "topaz: %s %s", TOPAZ_GL, lib ? "loaded" : dlerror());
}
