/*
 * agm-shelld: a root shell over TCP for the AGM H6 iOS port (no auth: only reachable over the USB
 * cable, ue0 192.168.7.2, or through usbmux from localhost).
 *   on the PC:  socat -,raw,echo=0 TCP:192.168.7.2:2323        (tools/agm-sh.sh)
 * Each connection gets a pty running bash from the Procursus bootstrap (/var/jb) when it is
 * installed, else toybox sh (toysh, still incomplete). At start the toybox applets are linked into
 * /var/agm/bin; PATH: the bootstrap's directories, then /var/agm/bin, then the system's.
 * At start the root volume is grown over its partition (/usr/bin/hfsgrow max). Also at start, as a jailbreak's loader does: the bootstrap's own daemons (sshd...) from
 * /var/jb/Library/LaunchDaemons are loaded with its launchctl (launchd skips jobs outside /System
 * in its launchd.plist). Before that, once: /var/jb/.agm-firstboot.sh if it is there (the root image's
 * bootstrap setup that needs iOS running: prep_bootstrap.sh, Sileo's package, uicache; it removes itself).
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <util.h>

#define PORT 2323
#define TOYBOX "/usr/bin/toybox"
#define BINDIR "/var/agm/bin"
#define JB_BASH "/var/jb/bin/bash"
#define JB_PATH "/var/jb/usr/local/bin:/var/jb/usr/bin:/var/jb/bin:/var/jb/usr/sbin:/var/jb/sbin:"

extern char **environ;

static void
link_applets(void)
{
	int p[2];
	mkdir("/var/agm", 0755);
	mkdir(BINDIR, 0755);
	if (pipe(p) != 0) {
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		dup2(p[1], 1);
		close(p[0]);
		execl(TOYBOX, "toybox", (char *)NULL);
		_exit(127);
	}
	close(p[1]);
	char buf[16384];
	size_t n = 0;
	ssize_t r;
	while (n < sizeof(buf) - 1 && (r = read(p[0], buf + n, sizeof(buf) - 1 - n)) > 0) {
		n += (size_t)r;
	}
	buf[n] = 0;
	close(p[0]);
	waitpid(pid, NULL, 0);
	int count = 0;
	for (char *t = strtok(buf, " \n\t"); t != NULL; t = strtok(NULL, " \n\t")) {
		char path[256];
		snprintf(path, sizeof(path), BINDIR "/%s", t);
		if (symlink(TOYBOX, path) == 0) {
			count++;
		}
	}
	fprintf(stderr, "agm-shelld: %d applets linked into " BINDIR "\n", count);
}

/* a freshly flashed 16 GiB root grows over its whole partition (hfsgrow max; quiet when it has) */
static void
grow_root(void)
{
	if (access("/usr/bin/hfsgrow", X_OK) != 0) {
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		execl("/usr/bin/hfsgrow", "hfsgrow", "max", (char *)NULL);
		_exit(127);
	}
	if (pid > 0) {
		int st;
		waitpid(pid, &st, 0);
	}
}

static void
run_firstboot(void)
{
	const char *script = "/var/jb/.agm-firstboot.sh";
	if (access(script, R_OK) != 0 || access(JB_BASH, X_OK) != 0) {
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		int fd = open("/var/agm-firstboot.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd >= 0) {
			dup2(fd, 1);
			dup2(fd, 2);
		}
		setenv("PATH", JB_PATH "/usr/bin:/bin:/usr/sbin:/sbin", 1);
		execl(JB_BASH, "bash", script, (char *)NULL);
		_exit(127);
	}
	if (pid > 0) {
		int st;
		waitpid(pid, &st, 0);
		fprintf(stderr, "agm-shelld: first-boot setup done (status %d)\n", st);
	}
}

static void
load_jb_daemons(void)
{
	if (access("/var/jb/usr/bin/launchctl", X_OK) != 0 || access("/var/jb/Library/LaunchDaemons", R_OK) != 0) {
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		execl("/var/jb/usr/bin/launchctl", "launchctl", "load", "/var/jb/Library/LaunchDaemons", (char *)NULL);
		_exit(127);
	}
	if (pid > 0) {
		int st;
		waitpid(pid, &st, 0);
		fprintf(stderr, "agm-shelld: /var/jb/Library/LaunchDaemons loaded (status %d)\n", st);
	}
}

static void
session(int s)
{
	int master;
	struct winsize ws = { .ws_row = 40, .ws_col = 160 };
	pid_t pid = forkpty(&master, NULL, NULL, &ws);
	if (pid < 0) {
		close(s);
		return;
	}
	if (pid == 0) {
		close(s);
		bool jb = access(JB_BASH, X_OK) == 0;
		setenv("PATH", jb ? JB_PATH BINDIR ":/usr/bin:/bin:/usr/sbin:/sbin:/usr/libexec" :
		    BINDIR ":/usr/bin:/bin:/usr/sbin:/sbin:/usr/libexec", 1);
		setenv("HOME", "/var/root", 1);
		setenv("TERM", "xterm-256color", 1);
		setenv("SHELL", jb ? JB_BASH : BINDIR "/sh", 1);
		setenv("USER", "root", 1);
		chdir("/var/root");
		if (jb) {
			execl(JB_BASH, "-bash", (char *)NULL);          /* a login shell: /var/jb/etc/profile */
		}
		execl(TOYBOX, "sh", "-i", (char *)NULL);
		_exit(127);
	}
	struct pollfd fds[2] = { { .fd = s, .events = POLLIN }, { .fd = master, .events = POLLIN } };
	char buf[8192];
	for (;;) {
		if (poll(fds, 2, -1) < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
			ssize_t n = read(s, buf, sizeof(buf));
			if (n <= 0 || write(master, buf, (size_t)n) != n) {
				break;
			}
		}
		if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
			ssize_t n = read(master, buf, sizeof(buf));
			if (n <= 0 || write(s, buf, (size_t)n) != n) {
				break;
			}
		}
	}
	kill(pid, SIGHUP);
	close(master);
	close(s);
	waitpid(pid, NULL, 0);
}

int
main(void)
{
	signal(SIGPIPE, SIG_IGN);
	link_applets();
	grow_root();
	run_firstboot();
	load_jb_daemons();
	int l = socket(AF_INET, SOCK_STREAM, 0), one = 1;
	setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in a = { .sin_len = sizeof(a), .sin_family = AF_INET, .sin_port = htons(PORT) };
	if (bind(l, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(l, 4) != 0) {
		perror("agm-shelld: bind");
		return 1;
	}
	fprintf(stderr, "agm-shelld: listening on port %d\n", PORT);
	for (;;) {
		int s = accept(l, NULL, NULL);
		if (s < 0) {
			continue;
		}
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		pid_t pid = fork();
		if (pid == 0) {
			close(l);
			session(s);
			_exit(0);
		}
		close(s);
		while (waitpid(-1, NULL, WNOHANG) > 0) {
		}
	}
}
