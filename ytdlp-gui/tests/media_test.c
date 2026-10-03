#include "../src/downloader.c"
#include <assert.h>

typedef struct Conversion {
    Downloader *downloader;
    const char *source;
    const char *tools;
    int result;
} Conversion;

static void *cancel_conversion(void *context) {
    Conversion *conversion = context;
    conversion->result = convert_mp4(conversion->downloader, conversion->source, conversion->tools);
    return NULL;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    Downloader d;
    downloader_init(&d);
    if (strcmp(argv[3], "cancel") == 0) {
        Conversion conversion = {&d, argv[1], argv[2], 0};
        pthread_t thread;
        assert(pthread_create(&thread, NULL, cancel_conversion, &conversion) == 0);
        bool running = false;
        for (int tick = 0; tick < 5000 && !running; tick++) {
            pthread_mutex_lock(&d.process_mutex);
            running = d.active_pid > 0;
            pthread_mutex_unlock(&d.process_mutex);
            if (!running) usleep(1000);
        }
        assert(running);
        downloader_cancel(&d);
        pthread_join(thread, NULL);
        assert(conversion.result != 0);
        assert(access(argv[1], R_OK) == 0);
        downloader_destroy(&d);
        return 0;
    }
    snprintf(d.url, sizeof(d.url), "%s", argv[1]);
    snprintf(d.output_dir, sizeof(d.output_dir), "%s", argv[2]);
    d.cookie_browser_index = 1;
    d.convert_output = true;
    d.audio_only = strcmp(argv[3], "wav") == 0;
    d.audio_format_index = 6;
    d.video_container_index = 1;
    assert(downloader_start(&d));
    downloader_join(&d);
    char *log = downloader_copy_log(&d);
    puts(log);
    free(log);
    bool success = atomic_load(&d.succeeded);
    downloader_destroy(&d);
    return success ? 0 : 1;
}
