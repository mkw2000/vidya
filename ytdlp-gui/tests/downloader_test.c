#include "../src/downloader.c"
#include <assert.h>

int main(int argc, char **argv) {
    assert(classify_error("ERROR: HTTP Error 429: Too Many Requests") == RATE_LIMIT);
    assert(classify_error("ERROR: Requested format is not available") == FORMAT);
    assert(classify_error("ERROR: Sign in to confirm your age") == AUTH);
    assert(classify_error("ERROR: Could not copy cookie database") == COOKIES);
    assert(classify_error("ERROR: No space left on device") == DISK);
    assert(classify_error("ERROR: Connection reset") == NETWORK);
    assert(classify_error("ERROR: Conversion failed") == CONVERSION);
    assert(classify_error("ERROR: Postprocessing: ffmpeg not found") == CONVERSION);
    Downloader d;
    downloader_init(&d);
    snprintf(d.url, sizeof(d.url), "  https://example.com/watch?v=abc&list=keep&signature=xyz\n");
    if (argc > 1 && strcmp(argv[1], "manual") == 0) d.cookie_browser_index = 3;
    if (argc > 1 && strcmp(argv[1], "never") == 0) d.cookie_browser_index = 1;
    if (argc > 1 && strcmp(argv[1], "wav") == 0) {
        d.audio_only = true;
        d.convert_output = true;
        d.audio_format_index = 6;
        d.audio_quality_index = 1;
        d.embed_thumbnail = true;
    }
    if (argc > 1 && strcmp(argv[1], "mp3") == 0) {
        d.audio_only = true;
        d.convert_output = true;
        d.audio_format_index = 2;
        d.audio_quality_index = 1;
    }
    if (argc > 1 && strcmp(argv[1], "original") == 0) {
        d.audio_only = true;
        d.audio_format_index = 6;
        d.audio_quality_index = 1;
    }
    if (argc > 1 && strcmp(argv[1], "mkv") == 0) {
        d.convert_output = true;
        d.video_container_index = 2;
        d.embed_thumbnail = true;
    }
    assert(downloader_start(&d));
    if (argc > 1 && strcmp(argv[1], "cancel") == 0) {
        usleep(100000);
        downloader_cancel(&d);
    }
    downloader_join(&d);
    char *log = downloader_copy_log(&d);
    puts(log);
    free(log);
    assert(atomic_load(&d.done));
    if (argc > 1 && (!strcmp(argv[1], "manual") || !strcmp(argv[1], "never") || !strcmp(argv[1], "cancel") || !strcmp(argv[1], "terminal"))) {
        assert(!atomic_load(&d.succeeded));
    } else assert(atomic_load(&d.succeeded));
    snprintf(d.url, sizeof(d.url), "--exec bad");
    assert(!downloader_start(&d));
    downloader_destroy(&d);
}
