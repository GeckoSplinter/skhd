#ifndef SERVICE_H
#define SERVICE_H

#include <spawn.h>
#include <sys/stat.h>

#define MAXLEN 512

#define _PATH_LAUNCHCTL   "/bin/launchctl"
#define _NAME_SKHD_PLIST "com.koekeishiya.skhd"
#define _PATH_SKHD_PLIST "%s/Library/LaunchAgents/"_NAME_SKHD_PLIST".plist"

#define _SKHD_PLIST \
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n" \
    "<plist version=\"1.0\">\n" \
    "<dict>\n" \
    "    <key>Label</key>\n" \
    "    <string>"_NAME_SKHD_PLIST"</string>\n" \
    "    <key>ProgramArguments</key>\n" \
    "    <array>\n" \
    "        <string>%s</string>\n" \
    "    </array>\n" \
    "    <key>EnvironmentVariables</key>\n" \
    "    <dict>\n" \
    "        <key>PATH</key>\n" \
    "        <string>%s</string>\n" \
    "    </dict>\n" \
    "    <key>RunAtLoad</key>\n" \
    "    <true/>\n" \
    "    <key>KeepAlive</key>\n" \
    "    <true/>\n" \
    "    <key>StandardOutPath</key>\n" \
    "    <string>/tmp/skhd_%s.out.log</string>\n" \
    "    <key>StandardErrorPath</key>\n" \
    "    <string>/tmp/skhd_%s.err.log</string>\n" \
    "    <key>ThrottleInterval</key>\n" \
    "    <integer>30</integer>\n" \
    "    <key>ProcessType</key>\n" \
    "    <string>Interactive</string>\n" \
    "    <key>Nice</key>\n" \
    "    <integer>-20</integer>\n" \
    "</dict>\n" \
    "</plist>"

static bool file_exists(char *filename);

static int safe_exec(char *const argv[])
{
    pid_t pid;
    int status = posix_spawn(&pid, argv[0], NULL, NULL, argv, NULL);
    if (status) return 1;

    while ((waitpid(pid, &status, 0) == -1) && (errno == EINTR)) {
        usleep(1000);
    }

    if (WIFSIGNALED(status)) {
        return 1;
    } else if (WIFSTOPPED(status)) {
        return 1;
    } else {
        return WEXITSTATUS(status);
    }
}

static void populate_plist_path(char *skhd_plist_path, int size)
{
    char *home = getenv("HOME");
    if (!home) {
        error("skhd: 'env HOME' not set! abort..\n");
    }

    snprintf(skhd_plist_path, size, _PATH_SKHD_PLIST, home);
}

static void populate_plist(char *skhd_plist, int size)
{
    char *user = getenv("USER");
    if (!user) {
        error("skhd: 'env USER' not set! abort..\n");
    }

    char *path_env = getenv("PATH");
    if (!path_env) {
        error("skhd: 'env PATH' not set! abort..\n");
    }

    char exe_path[4096];
    unsigned int exe_path_size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &exe_path_size) < 0) {
        error("skhd: unable to retrieve path of executable! abort..\n");
    }

    snprintf(skhd_plist, size, _SKHD_PLIST, exe_path, path_env, user, user);
}


static inline bool directory_exists(char *filename)
{
    struct stat buffer;

    if (stat(filename, &buffer) != 0) {
        return false;
    }

    return S_ISDIR(buffer.st_mode);
}

static inline void ensure_directory_exists(char *skhd_plist_path)
{
    //
    // NOTE(koekeishiya): Temporarily remove filename.
    // We know the filepath will contain a slash, as
    // it is controlled by us, so don't bother checking
    // the result..
    //

    char *last_slash = strrchr(skhd_plist_path, '/');
    *last_slash = '\0';

    if (!directory_exists(skhd_plist_path)) {
        mkdir(skhd_plist_path, 0755);
    }

    //
    // NOTE(koekeishiya): Restore original filename.
    //

    *last_slash = '/';
}

static int service_install_internal(char *skhd_plist_path)
{
    char skhd_plist[4096];
    populate_plist(skhd_plist, sizeof(skhd_plist));
    ensure_directory_exists(skhd_plist_path);

    FILE *handle = fopen(skhd_plist_path, "w");
    if (!handle) return 1;

    size_t bytes = fwrite(skhd_plist, strlen(skhd_plist), 1, handle);
    int result = bytes == 1 ? 0 : 1;
    fclose(handle);

    return result;
}

static int service_install(void)
{
    char skhd_plist_path[MAXLEN];
    populate_plist_path(skhd_plist_path, sizeof(skhd_plist_path));

    if (file_exists(skhd_plist_path)) {
        error("skhd: service file '%s' is already installed! abort..\n", skhd_plist_path);
    }

    return service_install_internal(skhd_plist_path);
}

static int service_uninstall(void)
{
    char skhd_plist_path[MAXLEN];
    populate_plist_path(skhd_plist_path, sizeof(skhd_plist_path));

    if (!file_exists(skhd_plist_path)) {
        error("skhd: service file '%s' is not installed! abort..\n", skhd_plist_path);
    }

    return unlink(skhd_plist_path) == 0 ? 0 : 1;
}

static int service_start(void)
{
    char skhd_plist_path[MAXLEN];
    populate_plist_path(skhd_plist_path, sizeof(skhd_plist_path));

    if (!file_exists(skhd_plist_path)) {
        warn("skhd: service file '%s' is not installed! attempting installation..\n", skhd_plist_path);

        int result = service_install_internal(skhd_plist_path);
        if (result) {
            error("skhd: service file '%s' could not be installed! abort..\n", skhd_plist_path);
        }
    }

    const char *const args[] = { _PATH_LAUNCHCTL, "load", "-w", skhd_plist_path, NULL };
    return safe_exec((char *const*)args);
}

static int service_restart(void)
{
    char skhd_plist_path[MAXLEN];
    populate_plist_path(skhd_plist_path, sizeof(skhd_plist_path));

    if (!file_exists(skhd_plist_path)) {
        error("skhd: service file '%s' is not installed! abort..\n", skhd_plist_path);
    }

    char skhd_service_id[MAXLEN];
    snprintf(skhd_service_id, sizeof(skhd_service_id), "gui/%d/%s", getuid(), _NAME_SKHD_PLIST);

    const char *const args[] = { _PATH_LAUNCHCTL, "kickstart", "-k", skhd_service_id, NULL };
    return safe_exec((char *const*)args);
}

static int service_stop(void)
{
    char skhd_plist_path[MAXLEN];
    populate_plist_path(skhd_plist_path, sizeof(skhd_plist_path));

    if (!file_exists(skhd_plist_path)) {
        error("skhd: service file '%s' is not installed! abort..\n", skhd_plist_path);
    }

    const char *const args[] = { _PATH_LAUNCHCTL, "unload", "-w", skhd_plist_path, NULL };
    return safe_exec((char *const*)args);
}

#endif
