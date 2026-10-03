#include "downloader.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void append_log(Downloader *downloader, const char *text) {
    size_t text_len = strlen(text);
    pthread_mutex_lock(&downloader->log_mutex);

    size_t needed = downloader->log_len + text_len + 1;
    if (needed > downloader->log_cap) {
        size_t next_cap = downloader->log_cap ? downloader->log_cap : 4096;
        while (next_cap < needed) next_cap *= 2;

        char *next = realloc(downloader->log, next_cap);
        if (!next) {
            pthread_mutex_unlock(&downloader->log_mutex);
            return;
        }

        downloader->log = next;
        downloader->log_cap = next_cap;
    }

    memcpy(downloader->log + downloader->log_len, text, text_len);
    downloader->log_len += text_len;
    downloader->log[downloader->log_len] = '\0';

    pthread_mutex_unlock(&downloader->log_mutex);
}

static void append_log_bytes(Downloader *downloader, const char *bytes, size_t bytes_len) {
    pthread_mutex_lock(&downloader->log_mutex);

    size_t needed = downloader->log_len + bytes_len + 1;
    if (needed > downloader->log_cap) {
        size_t next_cap = downloader->log_cap ? downloader->log_cap : 4096;
        while (next_cap < needed) next_cap *= 2;

        char *next = realloc(downloader->log, next_cap);
        if (!next) {
            pthread_mutex_unlock(&downloader->log_mutex);
            return;
        }

        downloader->log = next;
        downloader->log_cap = next_cap;
    }

    memcpy(downloader->log + downloader->log_len, bytes, bytes_len);
    downloader->log_len += bytes_len;
    downloader->log[downloader->log_len] = '\0';

    pthread_mutex_unlock(&downloader->log_mutex);
}

static bool executable_dir(char *buffer, size_t buffer_len) {
    uint32_t path_len = (uint32_t)buffer_len;
    if (_NSGetExecutablePath(buffer, &path_len) != 0) return false;

    char *dir = dirname(buffer);
    if (!dir) return false;

    memmove(buffer, dir, strlen(dir) + 1);
    return true;
}

static void build_ytdlp_path(char *buffer, size_t buffer_len) {
    char dir[2048];
    if (executable_dir(dir, sizeof(dir))) {
        snprintf(buffer, buffer_len, "%s/yt-dlp", dir);
    } else {
        snprintf(buffer, buffer_len, "./yt-dlp");
    }
}

static bool build_ffmpeg_dir(char *buffer, size_t buffer_len) {
    char dir[2048];
    if (!executable_dir(dir, sizeof(dir))) return false;

    snprintf(buffer, buffer_len, "%s/../Resources/ffmpeg", dir);

    char ffmpeg_path[2048];
    char ffprobe_path[2048];
    snprintf(ffmpeg_path, sizeof(ffmpeg_path), "%s/ffmpeg", buffer);
    snprintf(ffprobe_path, sizeof(ffprobe_path), "%s/ffprobe", buffer);
    if (access(ffmpeg_path, X_OK) == 0 && access(ffprobe_path, X_OK) == 0) return true;
    // A development build can use Homebrew; packaged apps use their own tools.
    const char *locations[] = {"/opt/homebrew/bin", "/usr/local/bin"};
    for (size_t i = 0; i < sizeof(locations) / sizeof(locations[0]); i++) {
        snprintf(ffmpeg_path, sizeof(ffmpeg_path), "%s/ffmpeg", locations[i]);
        snprintf(ffprobe_path, sizeof(ffprobe_path), "%s/ffprobe", locations[i]);
        if (access(ffmpeg_path, X_OK) == 0 && access(ffprobe_path, X_OK) == 0) {
            snprintf(buffer, buffer_len, "%s", locations[i]);
            return true;
        }
    }
    return false;
}

static int run_process(Downloader *downloader, const char *const *argv);
static int convert_mp4(Downloader *downloader, const char *source, const char *ffmpeg_dir);

static void set_status(Downloader *d, const char *message) {
    pthread_mutex_lock(&d->log_mutex);
    snprintf(d->status, sizeof(d->status), "%s", message);
    pthread_mutex_unlock(&d->log_mutex);
    append_log(d, message);
    append_log(d, "\n");
}

// Each attempt keeps the user's output and conversion choices intact.
static int run_attempt(Downloader *downloader, int browser, bool fallback_format, bool ipv4) {
    char ytdlp_path[2048];
    char ffmpeg_dir[2048];
    append_log(downloader, "Preparing yt-dlp command...\n");
    build_ytdlp_path(ytdlp_path, sizeof(ytdlp_path));
    bool has_bundled_ffmpeg = build_ffmpeg_dir(ffmpeg_dir, sizeof(ffmpeg_dir));

    const char *qualities[] = {
        "bv*+ba/b",
        "bv*+ba/b",
        "bv*+ba/b",
        "bv*+ba/b",
        "wv*+wa/w",
    };
    const char *sort_orders[] = {
        "",
        "res:1080",
        "res:720",
        "res:480",
        "",
    };
    const char *audio_formats[] = {
        "best",
        "m4a",
        "mp3",
        "opus",
        "aac",
        "flac",
        "wav",
    };
    const char *audio_qualities[] = {
        "",
        "320K",
        "192K",
        "128K",
        "64K",
    };
    const char *video_containers[] = {
        "",
        "mp4",
        "mkv",
        "webm",
    };
    const char *cookie_browsers[] = {
        "",
        "safari",
        "chrome",
        "firefox",
        "edge",
        "brave",
    };
    int quality_index = downloader->quality_index;
    if (quality_index < 0) quality_index = 0;
    if (quality_index >= (int)(sizeof(qualities) / sizeof(qualities[0]))) quality_index = 0;
    int audio_format_index = downloader->audio_format_index;
    if (audio_format_index < 0) audio_format_index = 0;
    if (audio_format_index >= (int)(sizeof(audio_formats) / sizeof(audio_formats[0]))) audio_format_index = 0;
    int audio_quality_index = downloader->audio_quality_index;
    if (audio_quality_index < 0) audio_quality_index = 0;
    if (audio_quality_index >= (int)(sizeof(audio_qualities) / sizeof(audio_qualities[0]))) audio_quality_index = 0;
    int video_container_index = downloader->video_container_index;
    if (video_container_index < 0) video_container_index = 0;
    if (video_container_index >= (int)(sizeof(video_containers) / sizeof(video_containers[0]))) video_container_index = 0;
    if (!downloader->convert_output) {
        audio_format_index = 0;
        video_container_index = 0;
    }
    int cookie_browser_index = browser;
    if (cookie_browser_index < 0 || cookie_browser_index >= (int)(sizeof(cookie_browsers) / sizeof(cookie_browsers[0]))) cookie_browser_index = 0;

    // yt-dlp's video recoder skips files already named .mp4, even with AV1/Opus
    // inside. Capture final paths and encode MP4 ourselves to guarantee codecs.
    bool encode_mp4 = !downloader->audio_only && video_container_index == 1;
    char manifest_path[] = "/tmp/vidya-conversion.XXXXXX";
    if (encode_mp4) {
        if (!has_bundled_ffmpeg) {
            append_log(downloader, "ERROR: Conversion failed: ffmpeg and ffprobe were not found.\n");
            return -1;
        }
        int fd = mkstemp(manifest_path);
        if (fd < 0) {
            append_log(downloader, "ERROR: Conversion failed: could not prepare the output file list.\n");
            return -1;
        }
        close(fd);
    }

    const char *argv[80];
    int argc = 0;
    argv[argc++] = ytdlp_path;
    argv[argc++] = "--ignore-config";
    argv[argc++] = downloader->download_playlist ? "--yes-playlist" : "--no-playlist";
    argv[argc++] = "--socket-timeout";
    argv[argc++] = "15";
    argv[argc++] = "--retries";
    argv[argc++] = "2";
    argv[argc++] = "--extractor-retries";
    argv[argc++] = "1";
    argv[argc++] = "--fragment-retries";
    argv[argc++] = "2";
    if (ipv4) argv[argc++] = "--force-ipv4";

    if (cookie_browser_index > 0 && cookie_browsers[cookie_browser_index][0] != '\0') {
        argv[argc++] = "--cookies-from-browser";
        argv[argc++] = cookie_browsers[cookie_browser_index];
    }


    if (downloader->audio_only) {
        argv[argc++] = "-x";
        argv[argc++] = "--audio-format";
        argv[argc++] = audio_formats[audio_format_index];
        if (audio_format_index > 0 && audio_format_index < 5 && audio_qualities[audio_quality_index][0] != '\0') {
            argv[argc++] = "--audio-quality";
            argv[argc++] = audio_qualities[audio_quality_index];
        }
        if (audio_format_index == 6) {
            argv[argc++] = "--postprocessor-args";
            argv[argc++] = "ExtractAudio+ffmpeg_o:-c:a pcm_s24le -ac 2 -ar 48000";
        }
    }

    argv[argc++] = "-f";
    argv[argc++] = fallback_format ? "best" : (downloader->audio_only ? "bestaudio/best" : qualities[quality_index]);

    if (!downloader->audio_only && sort_orders[quality_index][0] != '\0') {
        argv[argc++] = "-S";
        argv[argc++] = sort_orders[quality_index];
    }

    if (encode_mp4) {
        argv[argc++] = "--print-to-file";
        argv[argc++] = "after_move:%(filepath)s";
        argv[argc++] = manifest_path;
    } else if (!downloader->audio_only && video_containers[video_container_index][0] != '\0') {
        argv[argc++] = "--recode-video";
        argv[argc++] = video_containers[video_container_index];
    }

    if (downloader->output_dir[0] != '\0') {
        argv[argc++] = "-P";
        argv[argc++] = downloader->output_dir;
    }

    if (downloader->filename_template[0] != '\0') {
        argv[argc++] = "-o";
        argv[argc++] = downloader->filename_template;
    }

    if (downloader->download_subs) {
        argv[argc++] = "--write-subs";
        argv[argc++] = "--sub-langs";
        argv[argc++] = "en";
    }

    if (downloader->embed_metadata) {
        argv[argc++] = "--embed-metadata";
    }

    if (downloader->embed_thumbnail) {
        if (downloader->audio_only && (audio_format_index == 1 || audio_format_index == 2 || audio_format_index == 3 || audio_format_index == 5)) {
            argv[argc++] = "--embed-thumbnail";
        } else {
            argv[argc++] = "--write-thumbnail";
        }
        argv[argc++] = "--convert-thumbnails";
        argv[argc++] = "jpg";
    }


    if (downloader->ignore_errors) {
        argv[argc++] = "--ignore-errors";
    }

    argv[argc++] = "--progress";
    argv[argc++] = "--newline";


    if (has_bundled_ffmpeg) {
        argv[argc++] = "--ffmpeg-location";
        argv[argc++] = ffmpeg_dir;
    }

    argv[argc++] = "--";
    argv[argc++] = downloader->url;
    argv[argc] = NULL;

    append_log(downloader, "Launching yt-dlp...\n");
    int result = run_process(downloader, argv);
    if (encode_mp4) {
        // Convert successful playlist entries even if another entry failed.
        FILE *manifest = fopen(manifest_path, "r");
        bool converted = false;
        if (manifest) {
            char *path = NULL;
            size_t capacity = 0;
            ssize_t length;
            while (!atomic_load(&downloader->cancelled) && (length = getline(&path, &capacity, manifest)) >= 0) {
                if (length && path[length - 1] == '\n') path[--length] = '\0';
                if (!length) continue;
                if (convert_mp4(downloader, path, ffmpeg_dir) != 0) {
                    result = -1;
                    break;
                }
                converted = true;
            }
            free(path);
            fclose(manifest);
        }
        unlink(manifest_path);
        if (result == 0 && !converted && !atomic_load(&downloader->cancelled)) {
            append_log(downloader, "ERROR: Conversion failed: no completed video files were reported.\n");
            result = -1;
        }
    }
    return result;
}

static int run_process(Downloader *downloader, const char *const *argv) {
    if (atomic_load(&downloader->cancelled)) return -1;
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        append_log(downloader, "Failed to create output pipe.\n");
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        execv(argv[0], (char *const *)argv);
        perror("execv");
        _exit(127);
    }

    close(pipe_fds[1]);

    if (pid < 0) {
        close(pipe_fds[0]);
        append_log(downloader, "Failed to launch yt-dlp.\n");
        return -1;
    }

    pthread_mutex_lock(&downloader->process_mutex);
    setpgid(pid, pid);
    downloader->active_pid = pid;
    if (atomic_load(&downloader->cancelled)) kill(-pid, SIGTERM);
    pthread_mutex_unlock(&downloader->process_mutex);

    int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);

    char buffer[4096];
    bool pipe_open = true;
    int status = 0;
    int cancel_ticks = 0;
    bool reaped = false;

    while (pipe_open) {
        if (atomic_load(&downloader->cancelled)) {
            kill(-pid, ++cancel_ticks > 2000 ? SIGKILL : SIGTERM);
        }

        ssize_t count = read(pipe_fds[0], buffer, sizeof(buffer));
        if (count > 0) {
            append_log_bytes(downloader, buffer, (size_t)count);
        } else if (count == 0) {
            pipe_open = false;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            pipe_open = false;
        }

        pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            reaped = true;
            while ((count = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
                append_log_bytes(downloader, buffer, (size_t)count);
            }
            break;
        }

        usleep(1000);
    }

    close(pipe_fds[0]);

    while (!reaped) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) break;
        if (result < 0 && errno != EINTR) return -1;
        if (atomic_load(&downloader->cancelled))
            kill(-pid, ++cancel_ticks > 2000 ? SIGKILL : SIGTERM);
        usleep(1000);
    }

    pthread_mutex_lock(&downloader->process_mutex);
    downloader->active_pid = 0;
    pthread_mutex_unlock(&downloader->process_mutex);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int convert_mp4(Downloader *downloader, const char *source, const char *ffmpeg_dir) {
    if (access(source, R_OK) != 0) {
        append_log(downloader, "ERROR: Conversion failed: the downloaded video could not be opened.\n");
        return -1;
    }
    char *stem = strdup(source);
    size_t output_size = strlen(source) + 64;
    char *output = malloc(output_size);
    if (!stem || !output) {
        free(stem);
        free(output);
        append_log(downloader, "ERROR: Conversion failed: out of memory.\n");
        return -1;
    }
    char *extension = strrchr(stem, '.');
    char *slash = strrchr(stem, '/');
    if (extension && (!slash || extension > slash)) *extension = '\0';

    // Reserve a new filename. Keep the source and never overwrite another file.
    int fd = -1;
    for (int suffix = 0; suffix < 1000; suffix++) {
        if (suffix == 0) snprintf(output, output_size, "%s.converted.mp4", stem);
        else snprintf(output, output_size, "%s.converted-%d.mp4", stem, suffix);
        fd = open(output, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd >= 0 || errno != EEXIST) break;
    }
    free(stem);
    if (fd < 0) {
        append_log(downloader, "ERROR: Conversion failed: unable to open for writing.\n");
        free(output);
        return -1;
    }
    close(fd);
    char ffmpeg_path[4096];
    snprintf(ffmpeg_path, sizeof(ffmpeg_path), "%s/ffmpeg", ffmpeg_dir);
    const char *argv[] = {
        ffmpeg_path, "-nostdin", "-hide_banner", "-y", "-i", source,
        "-map", "0:v:0", "-map", "0:a:0?", "-map_metadata", "0",
        "-c:v", "libx264", "-preset", "fast", "-crf", "18", "-pix_fmt", "yuv420p",
        "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", "-g", "2",
        "-c:a", "aac", "-b:a", "192k", "-ac", "2", "-ar", "48000",
        "-movflags", "+faststart", output, NULL,
    };
    set_status(downloader, "Converting video to H.264 MP4...");
    int result = run_process(downloader, argv);
    if (result != 0 || atomic_load(&downloader->cancelled)) {
        unlink(output);
        append_log(downloader, "ERROR: Conversion failed. The original download has been kept.\n");
        result = -1;
    } else {
        append_log(downloader, "Converted file: ");
        append_log(downloader, output);
        append_log(downloader, "\n");
    }
    free(output);
    return result;
}

typedef enum Failure { OTHER, AUTH, COOKIES, FORMAT, NETWORK, RATE_LIMIT, UNAVAILABLE, DISK, CONVERSION } Failure;

static Failure classify_error(const char *log) {
    if (strcasestr(log, "no space left") || strcasestr(log, "unable to open for writing")) return DISK;
    if (strcasestr(log, "conversion failed") || strcasestr(log, "ERROR: Postprocessing:") ||
        strcasestr(log, "ffmpeg not found") || strcasestr(log, "ffprobe not found")) return CONVERSION;
    if (strcasestr(log, "429") || strcasestr(log, "too many requests")) return RATE_LIMIT;
    if (strcasestr(log, "private video") || strcasestr(log, "sign in") ||
        strcasestr(log, "log in") || strcasestr(log, "login required") ||
        strcasestr(log, "confirm your age") || strcasestr(log, "403 forbidden")) return AUTH;
    if (strcasestr(log, "cookie") && (strcasestr(log, "failed") ||
        strcasestr(log, "could not") || strcasestr(log, "permission") ||
        strcasestr(log, "unable") || strcasestr(log, "not find"))) return COOKIES;
    if (strcasestr(log, "requested format is not available")) return FORMAT;
    if (strcasestr(log, "timed out") || strcasestr(log, "connection reset") ||
        strcasestr(log, "network is unreachable") || strcasestr(log, "temporary failure") ||
        strcasestr(log, "502 bad gateway") || strcasestr(log, "503 service")) return NETWORK;
    if (strcasestr(log, "not available in your country") || strcasestr(log, "video unavailable") ||
        strcasestr(log, "has been removed") || strcasestr(log, "unsupported url")) return UNAVAILABLE;
    return OTHER;
}

static bool browser_available(int browser) {
    const char *paths[] = {"", "/Library/Cookies", "/Library/Application Support/Google/Chrome",
        "/Library/Application Support/Firefox/Profiles", "/Library/Application Support/Microsoft Edge",
        "/Library/Application Support/BraveSoftware/Brave-Browser"};
    const char *home = getenv("HOME");
    if (!home || browser < 1 || browser > 5) return false;
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", home, paths[browser]);
    return access(path, F_OK) == 0;
}

static void *download_thread(void *userdata) {
    Downloader *d = userdata;
    // Automatic, no cookies, or an explicitly selected browser.
    bool automatic = d->cookie_browser_index == 0;
    int browser = d->cookie_browser_index > 1 ? d->cookie_browser_index - 1 : 0;
    bool tried[6] = {true, false, false, false, false, false};
    bool fallback = false, ipv4 = false;
    Failure failure = OTHER;
    set_status(d, "Downloading with the best available settings...");
    for (int attempt = 0; attempt < 8 && !atomic_load(&d->cancelled); attempt++) {
        pthread_mutex_lock(&d->log_mutex);
        size_t offset = d->log_len;
        pthread_mutex_unlock(&d->log_mutex);
        int result = run_attempt(d, browser, fallback, ipv4);
        if (atomic_load(&d->cancelled)) break;
        char *log = downloader_copy_log(d);
        const char *current = log && strlen(log) >= offset ? log + offset : "";
        failure = classify_error(current);
        bool had_errors = strstr(current, "ERROR:") != NULL;
        free(log);
        if (result == 0 && !had_errors) {
            atomic_store(&d->succeeded, true);
            set_status(d, "Download complete. Your files are ready.");
            atomic_store(&d->done, true);
            return NULL;
        }
        if (automatic && (failure == AUTH || failure == COOKIES)) {
            // Chromium/Firefox before Safari, which often needs Full Disk Access.
            const int order[] = {2, 3, 4, 5, 1};
            int next = 0;
            for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
                if (!tried[order[i]] && browser_available(order[i])) { next = order[i]; break; }
            }
            if (!next) break;
            browser = next;
            tried[next] = true;
            const char *names[] = {"", "Safari", "Chrome", "Firefox", "Edge", "Brave"};
            char message[160];
            snprintf(message, sizeof(message), "This site needs a browser session. Trying %s...", names[next]);
            set_status(d, message);
        } else if (failure == FORMAT && !fallback) {
            fallback = true;
            set_status(d, "That stream is unavailable. Trying a combined audio/video stream...");
        } else if (failure == NETWORK && !ipv4) {
            ipv4 = true;
            set_status(d, "Connection interrupted. Retrying with an alternate network setting...");
        } else break;
    }
    if (atomic_load(&d->cancelled)) set_status(d, "Download cancelled.");
    else switch (failure) {
        case AUTH: case COOKIES:
            set_status(d, "Sign in to this site in your browser, then try again. See Details for access errors."); break;
        case RATE_LIMIT: set_status(d, "This site is limiting downloads. Wait a while before trying again."); break;
        case DISK: set_status(d, "Could not save the file. Check free space and the destination folder."); break;
        case NETWORK: set_status(d, "Could not connect. Check your internet connection and try again."); break;
        case UNAVAILABLE: set_status(d, "This link is unavailable or unsupported. Check that it plays in your browser."); break;
        case CONVERSION: set_status(d, "Conversion could not finish. Open Details for the error; original downloads may have been saved."); break;
        default: set_status(d, "Download could not finish. Open Details for the error; some files may have been saved."); break;
    }
    atomic_store(&d->done, true);
    return NULL;
}

void downloader_init(Downloader *downloader) {
    memset(downloader, 0, sizeof(*downloader));
    atomic_init(&downloader->succeeded, false);
    atomic_init(&downloader->done, true);
    atomic_init(&downloader->cancelled, false);
    pthread_mutex_init(&downloader->log_mutex, NULL);
    pthread_mutex_init(&downloader->process_mutex, NULL);
}

void downloader_destroy(Downloader *downloader) {
    downloader_cancel(downloader);
    downloader_join(downloader);
    pthread_mutex_destroy(&downloader->log_mutex);
    pthread_mutex_destroy(&downloader->process_mutex);
    free(downloader->log);
}

bool downloader_start(Downloader *downloader) {
    if (!atomic_load(&downloader->done)) return false;
    downloader_join(downloader);
    downloader_clear_log(downloader);
    atomic_store(&downloader->succeeded, false);
    // Trim paste whitespace without changing query strings or signed URLs.
    char *start = downloader->url;
    while (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t') start++;
    memmove(downloader->url, start, strlen(start) + 1);
    size_t length = strlen(downloader->url);
    while (length && strchr(" \t\r\n", downloader->url[length - 1])) downloader->url[--length] = 0;
    if ((strncmp(downloader->url, "https://", 8) != 0 && strncmp(downloader->url, "http://", 7) != 0) ||
        strpbrk(downloader->url, " \t\r\n") || !strstr(downloader->url, "://")[3]) {
        set_status(downloader, "Paste a complete http:// or https:// link to download.");
        return false;
    }
    set_status(downloader, "Starting download...");
    atomic_store(&downloader->cancelled, false);
    atomic_store(&downloader->done, false);

    if (pthread_create(&downloader->thread, NULL, download_thread, downloader) != 0) {
        atomic_store(&downloader->done, true);
        append_log(downloader, "Failed to create download thread.\n");
        return false;
    }

    downloader->thread_started = true;
    return true;
}

void downloader_cancel(Downloader *downloader) {
    atomic_store(&downloader->cancelled, true);

    pthread_mutex_lock(&downloader->process_mutex);
    pid_t pid = downloader->active_pid;
    pthread_mutex_unlock(&downloader->process_mutex);

    if (pid > 0) kill(-pid, SIGTERM);
}

void downloader_join(Downloader *downloader) {
    if (downloader->thread_started) {
        pthread_join(downloader->thread, NULL);
        downloader->thread_started = false;
    }
}

void downloader_clear_log(Downloader *downloader) {
    pthread_mutex_lock(&downloader->log_mutex);
    downloader->log_len = 0;
    if (downloader->log) downloader->log[0] = '\0';
    pthread_mutex_unlock(&downloader->log_mutex);
}

char *downloader_copy_log(Downloader *downloader) {
    pthread_mutex_lock(&downloader->log_mutex);
    size_t length = downloader->log_len;
    char *copy = malloc(length + 1);
    if (copy) {
        if (downloader->log) {
            memcpy(copy, downloader->log, length);
        }
        copy[length] = '\0';
    }
    pthread_mutex_unlock(&downloader->log_mutex);
    return copy;
}

void downloader_copy_status(Downloader *downloader, char *buffer, size_t size) {
    pthread_mutex_lock(&downloader->log_mutex);
    snprintf(buffer, size, "%s", downloader->status);
    pthread_mutex_unlock(&downloader->log_mutex);
}
