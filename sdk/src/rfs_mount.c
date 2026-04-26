#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include "rfs.h"

// --- 1. FUNÇÕES AUXILIARES DE CONTEXTO E TEMPO ---

const char *get_img_path()
{
    return (const char *)fuse_get_context()->private_data;
}

time_t rfs_time_to_time_t(rfs_datetime_t dt)
{
    struct tm t = {0};
    t.tm_year = dt.year - 1900;
    t.tm_mon = dt.month - 1;
    t.tm_mday = dt.day;
    t.tm_hour = dt.hour;
    t.tm_min = dt.minute;
    t.tm_sec = dt.second;
    return mktime(&t);
}

rfs_datetime_t time_t_to_rfs_time(time_t t)
{
    struct tm *tm_info = localtime(&t);
    rfs_datetime_t dt = {
        .year = tm_info->tm_year + 1900, .month = tm_info->tm_mon + 1, .day = tm_info->tm_mday, .hour = tm_info->tm_hour, .minute = tm_info->tm_min, .second = tm_info->tm_sec, .timezone_offset = 0};
    return dt;
}

// --- 2. GERENCIAMENTO FÍSICO DO MAPA FAT (ALOCAÇÃO) ---

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

// Busca um bloco livre por TODO O DISCO (do Bloco 10 em diante)
// Busca um bloco livre usando Expansão Radial (Otimização de Seek Time)
uint16_t allocate_free_block(FILE *img, rfs_index_block_t *index)
{

    // Configura os ponteiros de busca para as bordas imediatas do mapa
    uint16_t left_ptr = index->block_map_start - 1;
    uint16_t right_ptr = index->block_map_end + 1;

    // Enquanto houver disco para a esquerda OU para a direita
    while (left_ptr > RFS_LAYOUT_INDEX_BLOCK_ADDRESS || right_ptr < index->total_blocks)
    {

        // 1. Prioriza tentar alocar à DIREITA do mapa
        if (right_ptr < index->total_blocks)
        {
            if (get_map_entry(img, index->block_map_start, right_ptr) == RFS_FREE_POINTER)
            {

                // Aloca o bloco
                set_map_entry(img, index->block_map_start, right_ptr, RFS_LAST_BLOCK_POINTER);

                // Zera fisicamente
                uint8_t zeros[RFS_BLOCK_SIZE] = {0};
                fseek(img, (long)right_ptr * RFS_BLOCK_SIZE, SEEK_SET);
                fwrite(zeros, 1, RFS_BLOCK_SIZE, img);

                return right_ptr;
            }
            right_ptr++; // Se estava ocupado, afasta 1 bloco para a direita na próxima rodada
        }

        // 2. Em seguida, tenta alocar à ESQUERDA do mapa
        if (left_ptr > RFS_LAYOUT_INDEX_BLOCK_ADDRESS)
        {
            if (get_map_entry(img, index->block_map_start, left_ptr) == RFS_FREE_POINTER)
            {

                // Aloca o bloco
                set_map_entry(img, index->block_map_start, left_ptr, RFS_LAST_BLOCK_POINTER);

                // Zera fisicamente
                uint8_t zeros[RFS_BLOCK_SIZE] = {0};
                fseek(img, (long)left_ptr * RFS_BLOCK_SIZE, SEEK_SET);
                fwrite(zeros, 1, RFS_BLOCK_SIZE, img);

                return left_ptr;
            }
            left_ptr--; // Se estava ocupado, afasta 1 bloco para a esquerda na próxima rodada
        }
    }

    return 0; // Se os dois ponteiros bateram nos limites do disco, ele está 100% cheio
}

void free_block_chain(FILE *img, uint16_t map_start, uint16_t start_block)
{
    uint16_t curr_block = start_block;
    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER && curr_block != RFS_FREE_POINTER)
    {
        uint16_t next_block = get_map_entry(img, map_start, curr_block);
        set_map_entry(img, map_start, curr_block, RFS_FREE_POINTER);
        curr_block = next_block;
    }
}

// --- 3. NAVEGAÇÃO E ATUALIZAÇÃO DE DIRETÓRIOS ---

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

int find_entry_by_path(FILE *img, rfs_index_block_t *index, const char *path, uint16_t *out_parent_block, rfs_directory_entry_t *out_entry, uint16_t *out_entry_block, int *out_entry_index)
{
    if (strcmp(path, "/") == 0)
    {
        memset(out_entry, 0, sizeof(rfs_directory_entry_t));
        out_entry->mode = RFS_MODE_DIRECTORY | RFS_MODE_ALL_RWX;
        out_entry->starting_block = index->root_dir_start;
        out_entry->file_size = RFS_BLOCK_SIZE;
        return 0;
    }

    char *path_copy = strdup(path);
    char *saveptr;
    char *token = strtok_r(path_copy, "/", &saveptr);

    uint16_t current_dir_block = index->root_dir_start;
    bool found = false;

    while (token != NULL)
    {
        uint16_t e_block;
        int e_idx;
        found = find_entry_location(img, index->block_map_start, current_dir_block, token, &e_block, &e_idx, out_entry);

        if (!found)
        {
            free(path_copy);
            return -ENOENT;
        }

        token = strtok_r(NULL, "/", &saveptr);
        if (token != NULL)
        {
            current_dir_block = out_entry->starting_block;
        }
        else
        {
            if (out_parent_block)
                *out_parent_block = current_dir_block;
            if (out_entry_block)
                *out_entry_block = e_block;
            if (out_entry_index)
                *out_entry_index = e_idx;
        }
    }
    free(path_copy);
    return 0;
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
            // Se o primeiro byte do nome for \0, o slot está livre
            if (dir_block.entries[i].name[0] == '\0')
            {

                // --- A CORREÇÃO ESTÁ AQUI ---
                // Zera completamente os 64 bytes da entrada antes de preencher
                memset(&dir_block.entries[i], 0, sizeof(rfs_directory_entry_t));

                // Agora copia os dados novos
                dir_block.entries[i] = *new_entry;

                // Grava o bloco inteiro de volta com o slot limpo
                fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
                fwrite(&dir_block, sizeof(rfs_directory_block_t), 1, img);
                return true;
            }
        }
        last_block = curr_block;
        curr_block = get_map_entry(img, index->block_map_start, curr_block);
    }

    // Se precisar alocar um novo bloco, o allocate_free_block já o entrega zerado.
    uint16_t new_dir_block = allocate_free_block(img, index);
    if (!new_dir_block)
        return false;

    set_map_entry(img, index->block_map_start, last_block, new_dir_block);

    // Como o bloco novo já vem zerado do allocate_free_block,
    // não precisamos de memset adicional aqui, basta gravar no índice 0.
    memset(&dir_block, 0, sizeof(rfs_directory_block_t));
    dir_block.entries[0] = *new_entry;
    fseek(img, (long)new_dir_block * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&dir_block, sizeof(rfs_directory_block_t), 1, img);

    return true;
}

void update_entry_on_disk(FILE *img, uint16_t block, int index, rfs_directory_entry_t *updated_entry)
{
    rfs_directory_block_t dir_block;
    fseek(img, (long)block * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&dir_block, sizeof(rfs_directory_block_t), 1, img);
    dir_block.entries[index] = *updated_entry;
    fseek(img, (long)block * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&dir_block, sizeof(rfs_directory_block_t), 1, img);
}

// --- 4. CALLBACKS DO FUSE ---

static int fuse_rfs_getattr(const char *path, struct stat *stbuf)
{
    FILE *img = fopen(get_img_path(), "rb");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    rfs_directory_entry_t entry;
    int res = find_entry_by_path(img, &index, path, NULL, &entry, NULL, NULL);
    fclose(img);
    if (res != 0)
        return res;

    memset(stbuf, 0, sizeof(struct stat));
    // Traduz o Mode do RFS para o Mode do Unix
    stbuf->st_mode = 0;
    if (entry.mode & RFS_MODE_DIRECTORY)
    {
        stbuf->st_mode |= S_IFDIR;
    }
    else if (entry.mode & RFS_MODE_SYMLINK)
    {
        stbuf->st_mode |= S_IFLNK; // <-- FUSE agora sabe que é um link!
    }
    else
    {
        stbuf->st_mode |= S_IFREG;
    }

    // Pega as permissões (os últimos 9 bits)
    stbuf->st_mode |= (entry.mode & 0777);
    stbuf->st_nlink = (entry.mode & RFS_MODE_DIRECTORY) ? 2 : 1;
    stbuf->st_size = entry.file_size;
    stbuf->st_uid = entry.user_id;
    stbuf->st_gid = entry.group_id;
    stbuf->st_mtime = rfs_time_to_time_t(entry.modification_time);
    stbuf->st_ctime = rfs_time_to_time_t(entry.creation_time);

    return 0;
}

static int fuse_rfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    FILE *img = fopen(get_img_path(), "rb");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    rfs_directory_entry_t dir_entry;
    int res = find_entry_by_path(img, &index, path, NULL, &dir_entry, NULL, NULL);
    if (res != 0)
    {
        fclose(img);
        return res;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    uint16_t curr_block = dir_entry.starting_block;
    rfs_directory_block_t d_block;

    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER && curr_block != RFS_FREE_POINTER)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&d_block, sizeof(rfs_directory_block_t), 1, img);

        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            if (d_block.entries[i].name[0] != '\0' && strcmp(d_block.entries[i].name, ".") != 0 && strcmp(d_block.entries[i].name, "..") != 0)
            {
                filler(buf, d_block.entries[i].name, NULL, 0);
            }
        }
        curr_block = get_map_entry(img, index.block_map_start, curr_block);
    }

    fclose(img);
    return 0;
}

// Cria um arquivo (Touch)
static int fuse_rfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    FILE *img = fopen(get_img_path(), "r+b");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    char *path_copy1 = strdup(path);
    char *path_copy2 = strdup(path);
    char *parent_path = dirname(path_copy1);
    char *new_name = basename(path_copy2);

    rfs_directory_entry_t parent_entry;
    int res = find_entry_by_path(img, &index, parent_path, NULL, &parent_entry, NULL, NULL);
    if (res != 0)
    {
        free(path_copy1);
        free(path_copy2);
        fclose(img);
        return res;
    }

    uint16_t new_block = allocate_free_block(img, &index);
    if (!new_block)
    {
        free(path_copy1);
        free(path_copy2);
        fclose(img);
        return -ENOSPC;
    }

    rfs_directory_entry_t entry = {0};
    strncpy(entry.name, new_name, RFS_MAX_NAME_LENGTH);
    entry.mode = RFS_MODE_FILE | (mode & 0777);
    entry.starting_block = new_block;
    entry.file_size = 0;
    entry.creation_time = time_t_to_rfs_time(time(NULL));
    entry.modification_time = entry.creation_time;
    entry.user_id = fuse_get_context()->uid;
    entry.group_id = fuse_get_context()->gid;

    add_entry_to_directory(img, &index, parent_entry.starting_block, &entry);

    free(path_copy1);
    free(path_copy2);
    fclose(img);
    return 0;
}

// Criação de Diretório
static int fuse_rfs_mkdir(const char *path, mode_t mode)
{
    FILE *img = fopen(get_img_path(), "r+b");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    char *path_copy1 = strdup(path);
    char *path_copy2 = strdup(path);
    char *parent_path = dirname(path_copy1);
    char *new_name = basename(path_copy2);

    rfs_directory_entry_t parent_entry;
    int res = find_entry_by_path(img, &index, parent_path, NULL, &parent_entry, NULL, NULL);
    if (res != 0)
    {
        free(path_copy1);
        free(path_copy2);
        fclose(img);
        return res;
    }

    uint16_t new_dir_block = allocate_free_block(img, &index);
    if (!new_dir_block)
    {
        free(path_copy1);
        free(path_copy2);
        fclose(img);
        return -ENOSPC;
    }

    rfs_directory_entry_t entry = {0};
    strncpy(entry.name, new_name, RFS_MAX_NAME_LENGTH);
    entry.mode = RFS_MODE_DIRECTORY | (mode & 0777);
    entry.starting_block = new_dir_block;
    entry.file_size = RFS_BLOCK_SIZE;
    entry.creation_time = time_t_to_rfs_time(time(NULL));
    entry.modification_time = entry.creation_time;
    entry.user_id = fuse_get_context()->uid;
    entry.group_id = fuse_get_context()->gid;

    add_entry_to_directory(img, &index, parent_entry.starting_block, &entry);

    rfs_directory_entry_t dot = entry;
    strcpy(dot.name, ".");
    add_entry_to_directory(img, &index, new_dir_block, &dot);
    rfs_directory_entry_t dotdot = entry;
    strcpy(dotdot.name, "..");
    dotdot.starting_block = parent_entry.starting_block;
    add_entry_to_directory(img, &index, new_dir_block, &dotdot);

    free(path_copy1);
    free(path_copy2);
    fclose(img);
    return 0;
}

static int fuse_rfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    FILE *img = fopen(get_img_path(), "rb");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    rfs_directory_entry_t entry;
    int res = find_entry_by_path(img, &index, path, NULL, &entry, NULL, NULL);
    if (res != 0)
    {
        fclose(img);
        return res;
    }

    if (offset >= entry.file_size)
    {
        fclose(img);
        return 0;
    }
    if (offset + size > entry.file_size)
        size = entry.file_size - offset;

    uint32_t blocks_to_skip = offset / RFS_BLOCK_SIZE;
    uint32_t offset_in_first = offset % RFS_BLOCK_SIZE;
    uint16_t curr_block = entry.starting_block;

    for (uint32_t i = 0; i < blocks_to_skip; i++)
    {
        curr_block = get_map_entry(img, index.block_map_start, curr_block);
    }

    size_t bytes_read = 0;
    while (bytes_read < size && curr_block != RFS_LAST_BLOCK_POINTER)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE + offset_in_first, SEEK_SET);
        size_t to_read = RFS_BLOCK_SIZE - offset_in_first;
        if (size - bytes_read < to_read)
            to_read = size - bytes_read;

        fread(buf + bytes_read, 1, to_read, img);
        bytes_read += to_read;
        offset_in_first = 0;
        curr_block = get_map_entry(img, index.block_map_start, curr_block);
    }

    fclose(img);
    return bytes_read;
}

// Escreve dados e aloca blocos se precisar crescer
static int fuse_rfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    FILE *img = fopen(get_img_path(), "r+b");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    rfs_directory_entry_t entry;
    uint16_t e_block;
    int e_idx;
    int res = find_entry_by_path(img, &index, path, NULL, &entry, &e_block, &e_idx);
    if (res != 0)
    {
        fclose(img);
        return res;
    }

    uint32_t blocks_to_skip = offset / RFS_BLOCK_SIZE;
    uint32_t offset_in_first = offset % RFS_BLOCK_SIZE;
    uint16_t curr_block = entry.starting_block;
    // uint16_t prev_block = curr_block;

    // Navega pela FAT. Se o offset for muito além (ex: lseek), aloca blocos vazios no caminho.
    for (uint32_t i = 0; i < blocks_to_skip; i++)
    {
        uint16_t next = get_map_entry(img, index.block_map_start, curr_block);
        if (next == RFS_LAST_BLOCK_POINTER)
        {
            next = allocate_free_block(img, &index);
            if (!next)
            {
                fclose(img);
                return -ENOSPC;
            }
            set_map_entry(img, index.block_map_start, curr_block, next);
        }
        // prev_block = curr_block;
        curr_block = next;
    }

    size_t bytes_written = 0;
    while (bytes_written < size)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE + offset_in_first, SEEK_SET);
        size_t to_write = RFS_BLOCK_SIZE - offset_in_first;
        if (size - bytes_written < to_write)
            to_write = size - bytes_written;

        fwrite(buf + bytes_written, 1, to_write, img);
        bytes_written += to_write;
        offset_in_first = 0;

        if (bytes_written < size)
        {
            uint16_t next = get_map_entry(img, index.block_map_start, curr_block);
            if (next == RFS_LAST_BLOCK_POINTER)
            {
                next = allocate_free_block(img, &index);
                if (!next)
                    break; // Retorna o que conseguiu escrever se lotou
                set_map_entry(img, index.block_map_start, curr_block, next);
            }
            curr_block = next;
        }
    }

    // Atualiza metadados (tamanho e hora)
    if (offset + bytes_written > entry.file_size)
    {
        entry.file_size = offset + bytes_written;
    }
    entry.modification_time = time_t_to_rfs_time(time(NULL));
    update_entry_on_disk(img, e_block, e_idx, &entry);

    fclose(img);
    return bytes_written;
}

// Lê o caminho alvo gravado dentro do arquivo de Link Simbólico
static int fuse_rfs_readlink(const char *path, char *buf, size_t size)
{
    FILE *img = fopen(get_img_path(), "rb");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    rfs_directory_entry_t entry;
    int res = find_entry_by_path(img, &index, path, NULL, &entry, NULL, NULL);
    if (res != 0)
    {
        fclose(img);
        return res;
    }

    // Segurança: se não for um link, recusa a operação
    if (!(entry.mode & RFS_MODE_SYMLINK))
    {
        fclose(img);
        return -EINVAL;
    }

    // Lê a string que está gravada no bloco de dados inicial do link
    fseek(img, (long)entry.starting_block * RFS_BLOCK_SIZE, SEEK_SET);

    // O FUSE exige que a string seja terminada em \0
    size_t to_read = entry.file_size;
    if (to_read >= size)
        to_read = size - 1;

    fread(buf, 1, to_read, img);
    buf[to_read] = '\0';

    fclose(img);
    return 0;
}

// Cria um novo Link Simbólico no disco
static int fuse_rfs_symlink(const char *target, const char *linkpath)
{
    FILE *img = fopen(get_img_path(), "r+b");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    char *path_copy1 = strdup(linkpath);
    char *path_copy2 = strdup(linkpath);
    char *parent_path = dirname(path_copy1);
    char *new_name = basename(path_copy2);

    rfs_directory_entry_t parent_entry;
    int res = find_entry_by_path(img, &index, parent_path, NULL, &parent_entry, NULL, NULL);
    if (res != 0)
    {
        free(path_copy1);
        free(path_copy2);
        fclose(img);
        return res;
    }

    // Aloca 1 bloco para guardar o caminho (Links no Linux costumam ter permissão visual 777)
    uint16_t new_block = allocate_free_block(img, &index);
    if (!new_block)
    {
        free(path_copy1);
        free(path_copy2);
        fclose(img);
        return -ENOSPC;
    }

    rfs_directory_entry_t entry = {0};
    strncpy(entry.name, new_name, RFS_MAX_NAME_LENGTH);
    entry.mode = RFS_MODE_SYMLINK | 0777;
    entry.starting_block = new_block;
    entry.file_size = strlen(target); // O tamanho do arquivo é literalmente o tamanho da string
    entry.creation_time = time_t_to_rfs_time(time(NULL));
    entry.modification_time = entry.creation_time;
    entry.user_id = fuse_get_context()->uid;
    entry.group_id = fuse_get_context()->gid;

    add_entry_to_directory(img, &index, parent_entry.starting_block, &entry);

    // Grava o caminho alvo (texto) dentro do bloco recém alocado
    fseek(img, (long)new_block * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(target, 1, entry.file_size, img);

    free(path_copy1);
    free(path_copy2);
    fclose(img);
    return 0;
}

// Deleção genérica para arquivos e diretórios
static int fuse_rfs_unlink(const char *path)
{
    FILE *img = fopen(get_img_path(), "r+b");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    rfs_directory_entry_t entry;
    uint16_t e_block;
    int e_idx;
    int res = find_entry_by_path(img, &index, path, NULL, &entry, &e_block, &e_idx);
    if (res != 0)
    {
        fclose(img);
        return res;
    }

    // Libera os blocos de dados
    free_block_chain(img, index.block_map_start, entry.starting_block);

    // Marca a entrada como vazia no pai
    entry.name[0] = '\0';
    update_entry_on_disk(img, e_block, e_idx, &entry);

    fclose(img);
    return 0;
}

// chmod
static int fuse_rfs_chmod(const char *path, mode_t mode)
{
    FILE *img = fopen(get_img_path(), "r+b");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    rfs_directory_entry_t entry;
    uint16_t e_block;
    int e_idx;
    int res = find_entry_by_path(img, &index, path, NULL, &entry, &e_block, &e_idx);
    if (res != 0)
    {
        fclose(img);
        return res;
    }

    entry.mode = (entry.mode & ~00777) | (mode & 00777);
    update_entry_on_disk(img, e_block, e_idx, &entry);

    fclose(img);
    return 0;
}

// chown
static int fuse_rfs_chown(const char *path, uid_t uid, gid_t gid)
{
    FILE *img = fopen(get_img_path(), "r+b");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    rfs_directory_entry_t entry;
    uint16_t e_block;
    int e_idx;
    int res = find_entry_by_path(img, &index, path, NULL, &entry, &e_block, &e_idx);
    if (res != 0)
    {
        fclose(img);
        return res;
    }

    if (uid != (uid_t)-1)
        entry.user_id = uid;
    if (gid != (gid_t)-1)
        entry.group_id = gid;
    update_entry_on_disk(img, e_block, e_idx, &entry);

    fclose(img);
    return 0;
}

// truncate (ex: quando roda "echo '' > arquivo")
static int fuse_rfs_truncate(const char *path, off_t size)
{
    FILE *img = fopen(get_img_path(), "r+b");
    if (!img)
        return -EIO;

    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    rfs_directory_entry_t entry;
    uint16_t e_block;
    int e_idx;
    int res = find_entry_by_path(img, &index, path, NULL, &entry, &e_block, &e_idx);
    if (res != 0)
    {
        fclose(img);
        return res;
    }

    if (size < entry.file_size)
    {
        // Se diminuir pra zero, preservamos o primeiro bloco e limpamos o resto
        if (size == 0)
        {
            uint16_t second_block = get_map_entry(img, index.block_map_start, entry.starting_block);
            set_map_entry(img, index.block_map_start, entry.starting_block, RFS_LAST_BLOCK_POINTER);
            if (second_block != RFS_LAST_BLOCK_POINTER)
            {
                free_block_chain(img, index.block_map_start, second_block);
            }
        }
    }

    entry.file_size = size;
    update_entry_on_disk(img, e_block, e_idx, &entry);

    fclose(img);
    return 0;
}

static struct fuse_operations rfs_oper = {
    .getattr = fuse_rfs_getattr,
    .readdir = fuse_rfs_readdir,
    .read = fuse_rfs_read,
    .mkdir = fuse_rfs_mkdir,
    .create = fuse_rfs_create,
    .write = fuse_rfs_write,
    .unlink = fuse_rfs_unlink,
    .rmdir = fuse_rfs_unlink, // Compartilha lógica, já que apaga a entrada e os blocos
    .chmod = fuse_rfs_chmod,
    .chown = fuse_rfs_chown,
    .truncate = fuse_rfs_truncate,
    .symlink = fuse_rfs_symlink,
    .readlink = fuse_rfs_readlink,
};

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s <imagem.img> <ponto_de_montagem> [opcoes do FUSE...]\n", argv[0]);
        return 1;
    }

    char *img_path = realpath(argv[1], NULL);
    char *mount_point = argv[2];

    int fuse_argc = argc - 1;
    char **fuse_argv = calloc(fuse_argc, sizeof(char *));
    fuse_argv[0] = argv[0];
    fuse_argv[1] = mount_point;

    for (int i = 3; i < argc; i++)
    {
        fuse_argv[i - 1] = argv[i];
    }

    printf("Montando RFS (R/W Single-Thread) '%s' em '%s'\n", img_path, mount_point);

    // Inicia o FUSE passando oper e o img_path
    int ret = fuse_main(fuse_argc, fuse_argv, &rfs_oper, img_path);

    free(img_path);
    free(fuse_argv);
    return ret;
}