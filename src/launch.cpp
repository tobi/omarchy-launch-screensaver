#include "launch.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static constexpr const char kAppId[] = "org.omarchy.screensaver";

static bool command_exists(const char *name) {
    if (!name || !*name)
        return false;
    if (std::strchr(name, '/'))
        return access(name, X_OK) == 0;
    const char *path = std::getenv("PATH");
    if (!path)
        return false;
    std::string p = path;
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find(':', start);
        if (end == std::string::npos)
            end = p.size();
        std::string dir = p.substr(start, end - start);
        if (!dir.empty()) {
            std::string full = dir + "/" + name;
            if (access(full.c_str(), X_OK) == 0)
                return true;
        }
        if (end == p.size())
            break;
        start = end + 1;
    }
    return false;
}

static int run_quiet(const char *cmd) {
    return std::system(cmd);
}

static bool cmdline_has_app_id(pid_t pid) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%d/cmdline", (int)pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0)
        return false;
    buf[n] = 0;
    // cmdline is NUL-separated; search the raw bytes
    for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == 0)
            buf[i] = ' ';
    }
    return std::strstr(buf, kAppId) != nullptr;
}

bool screensaver_already_running() {
    const pid_t self = getpid();
    DIR *d = opendir("/proc");
    if (!d)
        return false;
    bool found = false;
    while (auto *ent = readdir(d)) {
        if (ent->d_name[0] < '1' || ent->d_name[0] > '9')
            continue;
        pid_t pid = (pid_t)std::atoi(ent->d_name);
        if (pid <= 0 || pid == self)
            continue;
        if (cmdline_has_app_id(pid)) {
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

bool acquire_instance_lock() {
    static int fd = -1;
    if (fd >= 0)
        return true;
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    std::string path = runtime && *runtime ? std::string(runtime) + "/omarchy-screensaver.lock"
                                           : "/tmp/omarchy-screensaver.lock";
    fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0)
        return true; // don't block launch if we can't lock
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        fd = -1;
        return false;
    }
    if (ftruncate(fd, 0) == 0) {
        char buf[32];
        int n = std::snprintf(buf, sizeof buf, "%d\n", (int)getpid());
        if (n > 0)
            (void)write(fd, buf, (size_t)n);
    }
    return true;
}

bool screensaver_toggled_off() {
    if (command_exists("omarchy-toggle-enabled")) {
        int rc = run_quiet("omarchy-toggle-enabled screensaver-off >/dev/null 2>&1");
        if (WIFEXITED(rc))
            return WEXITSTATUS(rc) == 0;
    }
    const char *home = std::getenv("HOME");
    if (!home)
        return false;
    std::string path = std::string(home) + "/.local/state/omarchy/toggles/screensaver-off";
    return access(path.c_str(), F_OK) == 0;
}

bool session_is_locked() {
    if (!command_exists("omarchy-shell"))
        return false;
    const char *cmd = command_exists("timeout")
                          ? "timeout --kill-after=1s 2s omarchy-shell lock isLocked 2>/dev/null"
                          : "omarchy-shell lock isLocked 2>/dev/null";
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return false;
    char buf[128] = {};
    if (!std::fgets(buf, sizeof buf, fp)) {
        pclose(fp);
        return false;
    }
    pclose(fp);
    for (char *p = buf; *p; ++p) {
        if (*p == '\n' || *p == '\r' || *p == ' ')
            *p = 0;
    }
    return std::strcmp(buf, "true") == 0 || std::strcmp(buf, "True") == 0 ||
           std::strcmp(buf, "1") == 0;
}

void quiet_walker() {
    if (!command_exists("walker"))
        return;
    (void)run_quiet("walker -q >/dev/null 2>&1");
}


void claim_screensaver_cmdline(char *argv0) {
    if (!argv0)
        return;
    const size_t room = std::strlen(argv0);
    if (room >= sizeof(kAppId) - 1) {
        std::memcpy(argv0, kAppId, sizeof(kAppId));
        if (room > sizeof(kAppId) - 1)
            std::memset(argv0 + sizeof(kAppId) - 1, 0, room - (sizeof(kAppId) - 1));
        return;
    }
    char *base = std::strrchr(argv0, '/');
    base = base ? base + 1 : argv0;
    if (std::strlen(base) >= sizeof(kAppId) - 1)
        std::memcpy(base, kAppId, sizeof(kAppId));
}
