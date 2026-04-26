#include "rfs.h"
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <libgen.h>
#include <ctype.h>

// --- FUNÇÕES DE RESOLUÇÃO DE NOMES (INTERNAS AO RFS) ---

bool is_numeric(const char *str)
{
    if (!str || *str == '\0')
        return false;
    for (int i = 0; str[i] != '\0'; i++)
    {
        if (!isdigit(str[i]))
            return false;
    }
    return true;
}

uint16_t get_map_entry(FILE *img, uint16_t map_start, uint16_t block)
{
    uint16_t ptr;
    fseek(img, (long)map_start * RFS_BLOCK_SIZE + (block * sizeof(uint16_t)), SEEK_SET);
    fread(&ptr, sizeof(uint16_t), 1, img);
    return ptr;
}

void set_map_entry(FILE *img, uint16_t map_start, uint16_t block, uint16_t value)
{
    fseek(img, (long)map_start * RFS_BLOCK_SIZE + (block * sizeof(uint16_t)), SEEK_SET);
    fwrite(&value, sizeof(uint16_t), 1, img);
}

bool find_entry_location(FILE *img, uint16_t map_start, uint16_t dir_start_block, const char *target_name, uint16_t *out_block, int *out_index, rfs_directory_entry_t *out_entry)
{
    uint16_t curr_block = dir_start_block;
    rfs_directory_block_t dir_block;
    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER && curr_block != RFS_FREE_POINTER)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&dir_block, sizeof(rfs_directory_block_t), 1, img);
        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            if (dir_block.entries[i].name[0] != '\0' && strcmp(dir_block.entries[i].name, target_name) == 0)
            {
                if (out_block)
                    *out_block = curr_block;
                if (out_index)
                    *out_index = i;
                if (out_entry)
                    *out_entry = dir_block.entries[i];
                return true;
            }
        }
        curr_block = get_map_entry(img, map_start, curr_block);
    }
    return false;
}

bool find_path(FILE *img, rfs_index_block_t *idx, const char *path, rfs_directory_entry_t *out_entry)
{
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    uint16_t curr_dir_block = idx->root_dir_start;
    bool found = false;
    while (token != NULL)
    {
        found = find_entry_location(img, idx->block_map_start, curr_dir_block, token, NULL, NULL, out_entry);
        if (!found)
            break;
        token = strtok(NULL, "/");
        if (token != NULL)
        {
            if (!(out_entry->mode & RFS_MODE_DIRECTORY))
            {
                found = false;
                break;
            }
            curr_dir_block = out_entry->starting_block;
        }
    }
    free(path_copy);
    return found;
}

bool read_file_to_buf(FILE *img, rfs_index_block_t *idx, rfs_directory_entry_t *entry, char **out_buf)
{
    if (entry->file_size == 0)
        return false;
    char *buf = malloc(entry->file_size + 1);
    uint16_t curr = entry->starting_block;
    uint32_t read = 0;
    while (curr != RFS_LAST_BLOCK_POINTER && curr != RFS_FREE_POINTER && curr != RFS_RESERVED_POINTER)
    {
        fseek(img, (long)curr * RFS_BLOCK_SIZE, SEEK_SET);
        uint32_t chunk = (entry->file_size - read > RFS_BLOCK_SIZE) ? RFS_BLOCK_SIZE : (entry->file_size - read);
        fread(buf + read, 1, chunk, img);
        read += chunk;
        curr = get_map_entry(img, idx->block_map_start, curr);
    }
    buf[read] = '\0';
    *out_buf = buf;
    return true;
}

uint16_t resolve_user(FILE *img, rfs_index_block_t *idx, const char *name)
{
    if (is_numeric(name))
        return (uint16_t)atoi(name);
    rfs_directory_entry_t ent;
    if (!find_path(img, idx, "/etc/passwd", &ent))
        return 1000;
    char *content;
    if (!read_file_to_buf(img, idx, &ent, &content))
        return 1000;
    char *line = strtok(content, "\n");
    while (line)
    {
        char l[256];
        strncpy(l, line, 255);
        char *u = strtok(l, ":");
        if (u && strcmp(u, name) == 0)
        {
            strtok(NULL, ":");
            char *uid = strtok(NULL, ":");
            if (uid)
            {
                uint16_t res = atoi(uid);
                free(content);
                return res;
            }
        }
        line = strtok(NULL, "\n");
    }
    free(content);
    return 1000;
}

uint16_t resolve_group(FILE *img, rfs_index_block_t *idx, const char *name)
{
    if (is_numeric(name))
        return (uint16_t)atoi(name);
    rfs_directory_entry_t ent;
    if (!find_path(img, idx, "/etc/group", &ent))
        return 1000;
    char *content;
    if (!read_file_to_buf(img, idx, &ent, &content))
        return 1000;
    char *line = strtok(content, "\n");
    while (line)
    {
        char l[256];
        strncpy(l, line, 255);
        char *g = strtok(l, ":");
        if (g && strcmp(g, name) == 0)
        {
            strtok(NULL, ":");
            char *gid = strtok(NULL, ":");
            if (gid)
            {
                uint16_t res = atoi(gid);
                free(content);
                return res;
            }
        }
        line = strtok(NULL, "\n");
    }
    free(content);
    return 1000;
}

// --- LÓGICA DE ALOCAÇÃO E INJEÇÃO ---

uint16_t allocate_free_block(FILE *img, rfs_index_block_t *index)
{
    uint16_t left_ptr = index->block_map_start - 1;
    uint16_t right_ptr = index->block_map_end + 1;
    while (left_ptr > RFS_LAYOUT_INDEX_BLOCK_ADDRESS || right_ptr < index->total_blocks)
    {
        if (right_ptr < index->total_blocks)
        {
            if (get_map_entry(img, index->block_map_start, right_ptr) == RFS_FREE_POINTER)
            {
                set_map_entry(img, index->block_map_start, right_ptr, RFS_LAST_BLOCK_POINTER);
                uint8_t zeros[RFS_BLOCK_SIZE] = {0};
                fseek(img, (long)right_ptr * RFS_BLOCK_SIZE, SEEK_SET);
                fwrite(zeros, 1, RFS_BLOCK_SIZE, img);
                return right_ptr;
            }
            right_ptr++;
        }
        if (left_ptr > RFS_LAYOUT_INDEX_BLOCK_ADDRESS)
        {
            if (get_map_entry(img, index->block_map_start, left_ptr) == RFS_FREE_POINTER)
            {
                set_map_entry(img, index->block_map_start, left_ptr, RFS_LAST_BLOCK_POINTER);
                uint8_t zeros[RFS_BLOCK_SIZE] = {0};
                fseek(img, (long)left_ptr * RFS_BLOCK_SIZE, SEEK_SET);
                fwrite(zeros, 1, RFS_BLOCK_SIZE, img);
                return left_ptr;
            }
            left_ptr--;
        }
    }
    return 0;
}

void free_block_chain(FILE *img, uint16_t map_start, uint16_t start_block)
{
    uint16_t curr = start_block;
    while (curr != RFS_LAST_BLOCK_POINTER && curr != RFS_RESERVED_POINTER && curr != RFS_FREE_POINTER)
    {
        uint16_t next = get_map_entry(img, map_start, curr);
        set_map_entry(img, map_start, curr, RFS_FREE_POINTER);
        curr = next;
    }
}

rfs_datetime_t get_current_rfs_time()
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    return (rfs_datetime_t){.year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday, .hour = tm.tm_hour, .minute = tm.tm_min, .second = tm.tm_sec};
}

bool add_entry_to_directory(FILE *img, rfs_index_block_t *index, uint16_t dir_start_block, rfs_directory_entry_t *new_entry)
{
    uint16_t curr_block = dir_start_block, last_block = curr_block;
    rfs_directory_block_t dir_block;
    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER && curr_block != RFS_FREE_POINTER)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&dir_block, sizeof(rfs_directory_block_t), 1, img);
        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            if (dir_block.entries[i].name[0] == '\0')
            {
                memset(&dir_block.entries[i], 0, sizeof(rfs_directory_entry_t));
                dir_block.entries[i] = *new_entry;
                fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
                fwrite(&dir_block, sizeof(rfs_directory_block_t), 1, img);
                return true;
            }
        }
        last_block = curr_block;
        curr_block = get_map_entry(img, index->block_map_start, curr_block);
    }
    uint16_t new_dir_block = allocate_free_block(img, index);
    if (!new_dir_block)
        return false;
    set_map_entry(img, index->block_map_start, last_block, new_dir_block);
    memset(&dir_block, 0, sizeof(rfs_directory_block_t));
    dir_block.entries[0] = *new_entry;
    fseek(img, (long)new_dir_block * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&dir_block, sizeof(rfs_directory_block_t), 1, img);
    return true;
}

uint16_t calculate_dir_mode(uint16_t input_mode)
{
    uint16_t mode = input_mode & 0777;
    mode |= 0500;
    if (mode & 0040)
        mode |= 0010;
    if (mode & 0004)
        mode |= 0001;
    return mode;
}

uint16_t create_rfs_directory(FILE *img, rfs_index_block_t *index, uint16_t parent_dir_block, const char *dir_name, uint16_t mode, uint16_t uid, uint16_t gid)
{
    uint16_t new_dir_block = allocate_free_block(img, index);
    if (!new_dir_block)
        return 0;
    rfs_directory_entry_t dir_entry = {.starting_block = new_dir_block, .mode = RFS_MODE_DIRECTORY | calculate_dir_mode(mode), .creation_time = get_current_rfs_time(), .modification_time = get_current_rfs_time(), .user_id = uid, .group_id = gid};
    strncpy(dir_entry.name, dir_name, RFS_MAX_NAME_LENGTH);
    if (!add_entry_to_directory(img, index, parent_dir_block, &dir_entry))
        return 0;
    rfs_directory_entry_t dot = dir_entry;
    memset(dot.name, 0, 32);
    strcpy(dot.name, ".");
    add_entry_to_directory(img, index, new_dir_block, &dot);
    rfs_directory_entry_t dotdot = dir_entry;
    memset(dotdot.name, 0, 32);
    strcpy(dotdot.name, "..");
    dotdot.starting_block = parent_dir_block;
    add_entry_to_directory(img, index, new_dir_block, &dotdot);
    return new_dir_block;
}

void inject_file(FILE *img, rfs_index_block_t *index, uint16_t parent_dir_block, const char *local_path, const char *rfs_name, uint16_t mode, uint16_t uid, uint16_t gid)
{
    FILE *local = fopen(local_path, "rb");
    if (!local)
        return;
    fseek(local, 0, SEEK_END);
    uint32_t size = ftell(local);
    fseek(local, 0, SEEK_SET);
    uint16_t e_block;
    int e_idx;
    rfs_directory_entry_t entry;
    bool exists = find_entry_location(img, index->block_map_start, parent_dir_block, rfs_name, &e_block, &e_idx, &entry);
    uint16_t curr_block = exists ? entry.starting_block : allocate_free_block(img, index);
    uint16_t start_block = curr_block;
    uint32_t left = size;
    uint8_t buf[RFS_BLOCK_SIZE];
    while (true)
    {
        size_t r = (left < RFS_BLOCK_SIZE) ? left : RFS_BLOCK_SIZE;
        memset(buf, 0, RFS_BLOCK_SIZE);
        if (r > 0)
            fread(buf, 1, r, local);
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
        fwrite(buf, 1, RFS_BLOCK_SIZE, img);
        if (left > r)
            left -= r;
        else
            left = 0;
        if (left > 0)
        {
            uint16_t n = get_map_entry(img, index->block_map_start, curr_block);
            if (n >= RFS_LAST_BLOCK_POINTER)
            {
                n = allocate_free_block(img, index);
                set_map_entry(img, index->block_map_start, curr_block, n);
            }
            curr_block = n;
        }
        else
            break;
    }
    uint16_t excess = get_map_entry(img, index->block_map_start, curr_block);
    if (excess < RFS_LAST_BLOCK_POINTER)
    {
        set_map_entry(img, index->block_map_start, curr_block, RFS_LAST_BLOCK_POINTER);
        free_block_chain(img, index->block_map_start, excess);
    }
    else
        set_map_entry(img, index->block_map_start, curr_block, RFS_LAST_BLOCK_POINTER);
    if (exists)
    {
        entry.file_size = size;
        entry.modification_time = get_current_rfs_time();
        entry.mode = RFS_MODE_FILE | (mode & 0777);
        entry.user_id = uid;
        entry.group_id = gid;
        rfs_directory_block_t db;
        fseek(img, (long)e_block * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&db, 1, RFS_BLOCK_SIZE, img);
        db.entries[e_idx] = entry;
        fseek(img, (long)e_block * RFS_BLOCK_SIZE, SEEK_SET);
        fwrite(&db, 1, RFS_BLOCK_SIZE, img);
    }
    else
    {
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.name, rfs_name, 32);
        entry.mode = RFS_MODE_FILE | (mode & 0777);
        entry.starting_block = start_block;
        entry.file_size = size;
        entry.creation_time = get_current_rfs_time();
        entry.modification_time = entry.creation_time;
        entry.user_id = uid;
        entry.group_id = gid;
        add_entry_to_directory(img, index, parent_dir_block, &entry);
    }
    fclose(local);
}

void inject_directory(FILE *img, rfs_index_block_t *index, uint16_t parent_dir_block, const char *local_path, const char *rfs_name, uint16_t mode, uint16_t uid, uint16_t gid)
{
    DIR *dir = opendir(local_path);
    if (!dir)
        return;
    rfs_directory_entry_t ex;
    bool exists = find_entry_location(img, index->block_map_start, parent_dir_block, rfs_name, NULL, NULL, &ex);
    uint16_t target = exists ? ex.starting_block : create_rfs_directory(img, index, parent_dir_block, rfs_name, mode, uid, gid);
    struct dirent *ent;
    while ((ent = readdir(dir)))
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char sub[1024];
        snprintf(sub, 1024, "%s/%s", local_path, ent->d_name);
        struct stat st;
        stat(sub, &st);
        if (S_ISDIR(st.st_mode))
            inject_directory(img, index, target, sub, ent->d_name, mode, uid, gid);
        else
            inject_file(img, index, target, sub, ent->d_name, mode, uid, gid);
    }
    closedir(dir);
}

int main(int argc, char **argv)
{
    if (argc < 4)
        return 1;
    FILE *img = fopen(argv[1], "r+b");
    rfs_index_block_t idx;
    fseek(img, 9 * 512, SEEK_SET);
    fread(&idx, 1, sizeof(idx), img);
    uint16_t mode = (argc >= 5) ? strtol(argv[4], NULL, 8) : 0777;
    uint16_t uid = (argc >= 6) ? resolve_user(img, &idx, argv[5]) : 1000;
    uint16_t gid = (argc >= 7) ? resolve_group(img, &idx, argv[6]) : 1000;
    char *p1 = strdup(argv[3]), *p2 = strdup(argv[3]), *parent = dirname(p1), *target = basename(p2);
    uint16_t curr = idx.root_dir_start;
    if (strcmp(parent, "/") != 0 && strcmp(parent, ".") != 0)
    {
        char *tok = strtok(parent, "/");
        rfs_directory_entry_t e;
        while (tok)
        {
            if (!find_entry_location(img, idx.block_map_start, curr, tok, NULL, NULL, &e))
                curr = create_rfs_directory(img, &idx, curr, tok, mode, uid, gid);
            else
                curr = e.starting_block;
            tok = strtok(NULL, "/");
        }
    }
    struct stat st;
    stat(argv[2], &st);
    if (S_ISDIR(st.st_mode))
        inject_directory(img, &idx, curr, argv[2], target, mode, uid, gid);
    else
        inject_file(img, &idx, curr, argv[2], target, mode, uid, gid);
    fclose(img);
    return 0;
}