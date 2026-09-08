#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct Downloader {
    char url[2048];
    char output_dir[1024];
    char filename_template[512];
    bool audio_only;
    int quality_index;
    int audio_format_index;
    int audio_quality_index;
    int video_container_index;
    int cookie_browser_index;
    bool download_subs;
    bool embed_metadata;
    bool embed_thumbnail;
    bool download_playlist;
    bool ignore_errors;

    char status[512]; // Protected by log_mutex.
    atomic_bool succeeded;
    atomic_bool done;
    atomic_bool cancelled;
    pthread_t thread;
    bool thread_started;

    pthread_mutex_t log_mutex;
    char *log;
    size_t log_len;
    size_t log_cap;

    pthread_mutex_t process_mutex;
    pid_t active_pid;
} Downloader;

void downloader_init(Downloader *downloader);
void downloader_destroy(Downloader *downloader);
bool downloader_start(Downloader *downloader);
void downloader_cancel(Downloader *downloader);
void downloader_join(Downloader *downloader);
void downloader_clear_log(Downloader *downloader);
void downloader_copy_status(Downloader *downloader, char *buffer, size_t size);
char *downloader_copy_log(Downloader *downloader);
