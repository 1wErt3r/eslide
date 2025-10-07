#include "media.h"

// Global media file list and current index
Eina_List* media_files = NULL;
int current_media_index = 0;

// Runtime-configurable images directory
static const char* images_dir_runtime = IMAGES_DIR;

void media_set_images_dir(const char* path)
{
    if (path && *path)
        images_dir_runtime = path;
}

// Function to check if a file has an image extension
Eina_Bool is_image_file(const char* filename)
{
    return (eina_str_has_suffix(filename, ".png") || eina_str_has_suffix(filename, ".jpg")
        || eina_str_has_suffix(filename, ".jpeg") || eina_str_has_suffix(filename, ".gif")
        || eina_str_has_suffix(filename, ".bmp"));
}

// Function to check if a file has a video extension
Eina_Bool is_video_file(const char* filename)
{
    return (eina_str_has_suffix(filename, ".mp4") || eina_str_has_suffix(filename, ".mov")
        || eina_str_has_suffix(filename, ".avi") || eina_str_has_suffix(filename, ".mkv")
        || eina_str_has_suffix(filename, ".webm"));
}

// Function to check if a file is a supported media file
Eina_Bool is_media_file(const char* filename)
{
    return is_image_file(filename) || is_video_file(filename);
}

// Function to free a media file structure
void free_media_file(MediaFile* media_file)
{
    if (!media_file)
        return;

    if (media_file->path) {
        free(media_file->path);
        media_file->path = NULL;
    }
    free(media_file);
}

// Function to scan and store media files from the images directory (lazy loading)
void scan_media_files(void)
{
    DIR* dir;
    struct dirent* entry;
    Eina_Strbuf* filepath_buf;
    char* filepath;
    struct stat file_stat;

    // Free any existing media file list before scanning
    MediaFile* media_file;
    EINA_LIST_FREE(media_files, media_file)
    {
        free_media_file(media_file);
    }
    media_files = NULL;

    dir = opendir(images_dir_runtime);
    if (!dir) {
        ERR("Could not open images directory: %s", images_dir_runtime);
        return;
    }

    filepath_buf = eina_strbuf_new();

    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and directories
        if (entry->d_name[0] == '.')
            continue;

        if (is_media_file(entry->d_name)) {
            eina_strbuf_reset(filepath_buf);
            eina_strbuf_append(filepath_buf, images_dir_runtime);
            eina_strbuf_append(filepath_buf, entry->d_name);
            filepath = strdup(eina_strbuf_string_get(filepath_buf));

            // Check if it's a regular file
            if (stat(filepath, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
                // Create media file structure
                MediaFile* new_media = malloc(sizeof(MediaFile));
                if (new_media) {
                    new_media->path = filepath;
                    new_media->is_image = is_image_file(entry->d_name);
                    media_files = eina_list_append(media_files, new_media);

                    if (new_media->is_image)
                        INF("Added image: %s", new_media->path);
                    else
                        INF("Added video: %s", new_media->path);
                } else {
                    free(filepath);
                }
            } else {
                free(filepath);
            }
        }
    }

    eina_strbuf_free(filepath_buf);
    closedir(dir);

    if (eina_list_count(media_files) == 0) {
        WRN("No media files found in %s", images_dir_runtime);
    } else {
        INF("Loaded %d media files", eina_list_count(media_files));
    }
}

// Function to get the count of media files (scans directory each time for up-to-date count)
int get_media_file_count(void)
{
    // Scan media files to ensure we have current count
    scan_media_files();
    return eina_list_count(media_files);
}

// Function to get path of media at specific index
char* get_media_path_at_index(int index)
{
    if (index < 0)
        return NULL;

    scan_media_files(); // Refresh the list to ensure it's current
    unsigned int media_count = eina_list_count(media_files);
    if ((unsigned int) index >= media_count)
        return NULL;

    MediaFile* media_file = eina_list_nth(media_files, index);
    if (!media_file)
        return NULL;

    return strdup(media_file->path); // Return a copy to avoid double free
}

// Media file list cleanup
void media_cleanup(void)
{
    // Free media file structures
    MediaFile* media_file;
    EINA_LIST_FREE(media_files, media_file)
    {
        free_media_file(media_file);
    }
    media_files = NULL;
}