#ifndef MEDIA_H
#define MEDIA_H

#include "common.h"
#include <dirent.h>
#include <sys/stat.h>

// Function declarations for media file handling

// File type detection functions
Eina_Bool is_image_file(const char *filename);
Eina_Bool is_video_file(const char *filename);
Eina_Bool is_media_file(const char *filename);

// Media file management functions
void free_media_file(MediaFile *media_file);
void scan_media_files(void);
int get_media_file_count(void);
char* get_media_path_at_index(int index);

// Media file list cleanup
void media_cleanup(void);

// Global media file list (to be accessed by other modules)
extern Eina_List *media_files;
extern int current_media_index;

#endif /* MEDIA_H */