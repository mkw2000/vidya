#include "downloader.h"

#include <raylib.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct AppState {
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
    bool advanced;
    bool details;
    bool url_edit;
    bool output_edit;
    bool template_edit;
    Downloader downloader;
} AppState;

static void set_default_output_dir(char *buffer, size_t buffer_len) {
    const char *home = getenv("HOME");
    if (home) {
        snprintf(buffer, buffer_len, "%s/Downloads", home);
    }
}

static bool choose_folder(char *buffer, size_t buffer_len) {
    FILE *pipe = popen("osascript -e 'POSIX path of (choose folder with prompt \"Choose output folder\")'", "r");
    if (!pipe) return false;

    char result[2048] = {0};
    bool ok = fgets(result, sizeof(result), pipe) != NULL;
    int status = pclose(pipe);
    if (!ok || status != 0) return false;

    result[strcspn(result, "\r\n")] = '\0';
    if (result[0] == '\0') return false;

    snprintf(buffer, buffer_len, "%s", result);
    return true;
}

static void draw_log(Rectangle bounds, const char *log_text) {
    GuiGroupBox(bounds, "Download details");

    Rectangle inner = {
        bounds.x + 8.0f,
        bounds.y + 22.0f,
        bounds.width - 16.0f,
        bounds.height - 30.0f,
    };

    BeginScissorMode((int)inner.x, (int)inner.y, (int)inner.width, (int)inner.height);
    ClearBackground((Color){245, 245, 245, 255});

    const int font_size = 14;
    const int line_height = 18;
    int max_lines = (int)(inner.height / line_height);
    if (max_lines < 1) max_lines = 1;

    const char *start = log_text ? log_text : "";
    int lines = 1;
    for (const char *cursor = start; *cursor; cursor++) {
        if (*cursor == '\n') lines++;
    }

    int skip = lines > max_lines ? lines - max_lines : 0;
    while (skip > 0 && *start) {
        if (*start++ == '\n') skip--;
    }

    float y = inner.y + 4.0f;
    const char *line_start = start;
    while (*line_start && y < inner.y + inner.height) {
        const char *line_end = strchr(line_start, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        if (line_len > 0) {
            char line[1024];
            if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
            memcpy(line, line_start, line_len);
            line[line_len] = '\0';
            DrawText(line, (int)inner.x + 4, (int)y, font_size, DARKGRAY);
        }
        y += line_height;
        if (!line_end) break;
        line_start = line_end + 1;
    }

    EndScissorMode();
    DrawRectangleLinesEx(inner, 1.0f, (Color){200, 200, 200, 255});
}

static void start_download(AppState *app) {
    snprintf(app->downloader.url, sizeof(app->downloader.url), "%s", app->url);
    snprintf(app->downloader.output_dir, sizeof(app->downloader.output_dir), "%s", app->output_dir);
    snprintf(app->downloader.filename_template, sizeof(app->downloader.filename_template), "%s", app->filename_template);
    app->downloader.audio_only = app->audio_only;
    app->downloader.quality_index = app->quality_index;
    app->downloader.audio_format_index = app->audio_format_index;
    app->downloader.audio_quality_index = app->audio_quality_index;
    app->downloader.video_container_index = app->video_container_index;
    app->downloader.cookie_browser_index = app->cookie_browser_index;
    app->downloader.download_subs = app->download_subs;
    app->downloader.embed_metadata = app->embed_metadata;
    app->downloader.embed_thumbnail = app->embed_thumbnail;
    app->downloader.download_playlist = app->download_playlist;
    app->downloader.ignore_errors = app->ignore_errors;
    downloader_start(&app->downloader);
}

int main(void) {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(900, 700, "vidya");
    SetTargetFPS(60);

    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
    GuiSetStyle(TEXTBOX, TEXT_PADDING, 8);
    GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);

    AppState app = {0};
    set_default_output_dir(app.output_dir, sizeof(app.output_dir));
    app.embed_metadata = true;
    app.url_edit = true;
    downloader_init(&app.downloader);

    while (!WindowShouldClose()) {
        if (atomic_load(&app.downloader.done)) {
            downloader_join(&app.downloader);
        }

        int width = GetScreenWidth();
        int height = GetScreenHeight();
        float margin = 24.0f;
        float content_width = (float)width - margin * 2.0f;
        bool is_downloading = !atomic_load(&app.downloader.done);

        BeginDrawing();
        ClearBackground((Color){238, 240, 242, 255});

        DrawText("vidya", (int)margin, 20, 26, (Color){30, 34, 38, 255});

        GuiLabel((Rectangle){margin, 54, content_width, 24}, "Paste a link. Vidya handles the download settings for you.");
        Rectangle url_bounds = {margin, 90, content_width - 96, 36};
        Rectangle output_bounds = {margin, 196, content_width - 130, 34};
        Rectangle template_bounds = {margin, 424, content_width, 30};
        if (!is_downloading && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mouse = GetMousePosition();
            app.url_edit = CheckCollisionPointRec(mouse, url_bounds);
            app.output_edit = CheckCollisionPointRec(mouse, output_bounds);
            app.template_edit = app.advanced && CheckCollisionPointRec(mouse, template_bounds);
        }
        bool submit = !is_downloading && app.url_edit && IsKeyPressed(KEY_ENTER);
        if (is_downloading) GuiDisable();
        if (GuiTextBox(url_bounds, app.url, sizeof(app.url), app.url_edit && !is_downloading)) app.url_edit = false;
        if (GuiButton((Rectangle){margin + content_width - 84, 90, 84, 36}, "Paste")) {
            const char *clipboard = GetClipboardText();
            if (clipboard) snprintf(app.url, sizeof(app.url), "%s", clipboard);
            app.url_edit = true;
        }
        GuiCheckBox((Rectangle){margin, 143, 22, 22}, "Audio only", &app.audio_only);
        GuiLabel((Rectangle){margin + 170, 141, 450, 24}, "Best available quality, automatically");
        GuiLabel((Rectangle){margin, 170, 250, 24}, "Save to");
        if (GuiTextBox(output_bounds, app.output_dir, sizeof(app.output_dir), app.output_edit && !is_downloading)) app.output_edit = false;
        if (GuiButton((Rectangle){margin + content_width - 114, 196, 114, 34}, "Choose...")) choose_folder(app.output_dir, sizeof(app.output_dir));
        GuiEnable();
        if (GuiButton((Rectangle){margin, 246, 180, 30}, app.advanced ? "Hide advanced" : "Advanced options")) {
            app.advanced = !app.advanced;
            app.template_edit = false;
        }
        if (app.advanced) {
            if (is_downloading) GuiDisable();
            float y = 296;
            if (app.audio_only) {
                GuiLabel((Rectangle){margin, y, 70, 24}, "Format");
                GuiComboBox((Rectangle){margin + 80, y, 145, 30}, "best;m4a;mp3;opus;aac;flac;wav", &app.audio_format_index);
                GuiLabel((Rectangle){margin + 250, y, 80, 24}, "Bitrate");
                GuiComboBox((Rectangle){margin + 330, y, 130, 30}, "best;320K;192K;128K;64K", &app.audio_quality_index);
            } else {
                GuiLabel((Rectangle){margin, y, 70, 24}, "Quality");
                GuiComboBox((Rectangle){margin + 80, y, 145, 30}, "best;1080p;720p;480p;worst", &app.quality_index);
                GuiLabel((Rectangle){margin + 250, y, 100, 24}, "Container");
                GuiComboBox((Rectangle){margin + 350, y, 125, 30}, "auto;mp4;mkv;webm", &app.video_container_index);
                GuiCheckBox((Rectangle){margin + 520, y + 4, 22, 22}, "Subtitles", &app.download_subs);
            }
            y += 44;
            GuiCheckBox((Rectangle){margin, y, 22, 22}, "Metadata", &app.embed_metadata);
            GuiCheckBox((Rectangle){margin + 145, y, 22, 22}, app.audio_only ? "Cover art" : "Thumbnail", &app.embed_thumbnail);
            GuiCheckBox((Rectangle){margin + 290, y, 22, 22}, "Playlist", &app.download_playlist);
            GuiCheckBox((Rectangle){margin + 420, y, 22, 22}, "Continue on errors", &app.ignore_errors);
            y += 40;
            GuiLabel((Rectangle){margin, y, 140, 24}, "Browser session");
            GuiComboBox((Rectangle){margin + 150, y - 4, 200, 30}, "Automatic;Never use cookies;Safari;Chrome;Firefox;Edge;Brave", &app.cookie_browser_index);
            GuiLabel((Rectangle){margin + 370, y, 470, 24}, "Automatic uses browser cookies only if needed.");
            GuiLabel((Rectangle){margin, 401, 250, 22}, "Filename template (optional)");
            if (GuiTextBox(template_bounds, app.filename_template, sizeof(app.filename_template), app.template_edit && !is_downloading)) app.template_edit = false;
            GuiEnable();
        }
        float action_y = app.advanced ? 470 : 296;
        if (is_downloading) {
            if (GuiButton((Rectangle){margin, action_y, content_width, 38}, atomic_load(&app.downloader.cancelled) ? "Cancelling..." : "Cancel download")) downloader_cancel(&app.downloader);
        } else {
            if (!app.url[0]) GuiDisable();
            if (GuiButton((Rectangle){margin, action_y, content_width, 38}, "Download") || (submit && app.url[0])) start_download(&app);
            GuiEnable();
        }
        char status[512];
        downloader_copy_status(&app.downloader, status, sizeof(status));
        if (!status[0]) snprintf(status, sizeof(status), "Ready when you are.");
        // Keep guidance readable even when a failure needs a longer explanation.
        char *split = NULL;
        if (MeasureText(status, 15) > (int)content_width) {
            for (char *c = status; *c; c++) {
                if (*c != ' ') continue;
                *c = 0;
                int measured = MeasureText(status, 15);
                *c = ' ';
                if (measured > (int)content_width) break;
                split = c;
            }
        }
        if (split) *split = 0;
        DrawText(status, (int)margin, (int)action_y + 47, 15, DARKGRAY);
        if (split) DrawText(split + 1, (int)margin, (int)action_y + 64, 15, DARKGRAY);
        if (GuiButton((Rectangle){margin, action_y + 78, 150, 28}, app.details ? "Hide details" : "Show details")) app.details = !app.details;
        if (atomic_load(&app.downloader.succeeded) && GuiButton((Rectangle){margin + 164, action_y + 78, 170, 28}, "Open folder")) {
            pid_t pid = fork();
            if (pid == 0) {
                execl("/usr/bin/open", "open", "--", app.downloader.output_dir, (char *)NULL);
                _exit(127);
            }
            if (pid > 0) waitpid(pid, NULL, 0);
        }
        if (app.details) {
            char *log_copy = downloader_copy_log(&app.downloader);
            if (GuiButton((Rectangle){margin + content_width - 150, action_y + 78, 150, 28}, "Copy details")) SetClipboardText(log_copy ? log_copy : "");
            float log_y = action_y + 120;
            draw_log((Rectangle){margin, log_y, content_width, (float)height - log_y - margin}, log_copy ? log_copy : "");
            free(log_copy);
        }

        EndDrawing();
    }

    downloader_destroy(&app.downloader);
    CloseWindow();
    return 0;
}
