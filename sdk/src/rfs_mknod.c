#include "rfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <libgen.h>

// --- 1. FUNÇÕES DE RESOLUÇÃO DE NOMES (Com strtok_r seguro) ---

bool is_numeric(const char *str)
{
    if (!str || *str == '\0')
        return false;
    for (int i = 0; str[i] != '\0'; i++)
        if (!isdigit(str[i]))
            return false;
    return true;
}

uint16_t get_map_entry(FILE *img, uint16_t map_start, uint16_t block)
{
    uint16_t ptr;
    fseek(img, (long)map_start * RFS_BLOCK_SIZE + (block * 2), SEEK_SET);
    fread(&ptr, 2, 1, img);
    return ptr;
}

void set_map_entry(FILE *img, uint16_t map_start, uint16_t block, uint16_t val)
{
    fseek(img, (long)map_start * RFS_BLOCK_SIZE + (block * 2), SEEK_SET);
    fwrite(&val, 2, 1, img);
}

bool find_entry_location(FILE *img, uint16_t ms, uint16_t ds, const char *n, rfs_directory_entry_t *out)
{
    uint16_t c = ds;
    rfs_directory_block_t b;
    while (c != RFS_LAST_BLOCK_POINTER && c != RFS_FREE_POINTER && c != RFS_RESERVED_POINTER)
    {
        fseek(img, (long)c * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&b, 1, RFS_BLOCK_SIZE, img);
        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            if (b.entries[i].name[0] != '\0' && strcmp(b.entries[i].name, n) == 0)
            {
                if (out)
                    *out = b.entries[i];
                return true;
            }
        }
        c = get_map_entry(img, ms, c);
    }
    return false;
}

bool find_path(FILE *img, rfs_index_block_t *idx, const char *path, rfs_directory_entry_t *out_entry)
{
    if (strcmp(path, "/") == 0)
        return false;
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    uint16_t curr_dir_block = idx->root_dir_start;
    bool found = false;
    while (token != NULL)
    {
        found = find_entry_location(img, idx->block_map_start, curr_dir_block, token, out_entry);
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
        return 0; // Padrão root (0) para mknod
    char *content;
    if (!read_file_to_buf(img, idx, &ent, &content))
        return 0;

    char *saveptr_line;
    char *line = strtok_r(content, "\n", &saveptr_line);
    while (line)
    {
        char l[256];
        strncpy(l, line, 255);
        l[255] = '\0';
        char *saveptr_field;
        char *u = strtok_r(l, ":", &saveptr_field);
        if (u && strcmp(u, name) == 0)
        {
            strtok_r(NULL, ":", &saveptr_field);
            char *uid = strtok_r(NULL, ":", &saveptr_field);
            if (uid)
            {
                uint16_t res = atoi(uid);
                free(content);
                return res;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr_line);
    }
    free(content);
    return 0; // Padrão root (0)
}

uint16_t resolve_group(FILE *img, rfs_index_block_t *idx, const char *name)
{
    if (is_numeric(name))
        return (uint16_t)atoi(name);
    rfs_directory_entry_t ent;
    if (!find_path(img, idx, "/etc/group", &ent))
        return 0; // Padrão root (0) para mknod
    char *content;
    if (!read_file_to_buf(img, idx, &ent, &content))
        return 0;

    char *saveptr_line;
    char *line = strtok_r(content, "\n", &saveptr_line);
    while (line)
    {
        char l[256];
        strncpy(l, line, 255);
        l[255] = '\0';
        char *saveptr_field;
        char *g = strtok_r(l, ":", &saveptr_field);
        if (g && strcmp(g, name) == 0)
        {
            strtok_r(NULL, ":", &saveptr_field);
            char *gid = strtok_r(NULL, ":", &saveptr_field);
            if (gid)
            {
                uint16_t res = atoi(gid);
                free(content);
                return res;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr_line);
    }
    free(content);
    return 0; // Padrão root (0)
}

// --- 2. GERENCIAMENTO FÍSICO E ALOCAÇÃO ---

uint16_t allocate_free_block(FILE *img, rfs_index_block_t *index)
{
    uint16_t l = index->block_map_start - 1, r = index->block_map_end + 1;
    while (l > RFS_LAYOUT_INDEX_BLOCK_ADDRESS || r < index->total_blocks)
    {
        if (r < index->total_blocks && get_map_entry(img, index->block_map_start, r) == RFS_FREE_POINTER)
        {
            set_map_entry(img, index->block_map_start, r, RFS_LAST_BLOCK_POINTER);
            uint8_t z[RFS_BLOCK_SIZE] = {0};
            fseek(img, (long)r * RFS_BLOCK_SIZE, SEEK_SET);
            fwrite(z, 1, RFS_BLOCK_SIZE, img);
            return r;
        }
        if (l > RFS_LAYOUT_INDEX_BLOCK_ADDRESS && get_map_entry(img, index->block_map_start, l) == RFS_FREE_POINTER)
        {
            set_map_entry(img, index->block_map_start, l, RFS_LAST_BLOCK_POINTER);
            uint8_t z[RFS_BLOCK_SIZE] = {0};
            fseek(img, (long)l * RFS_BLOCK_SIZE, SEEK_SET);
            fwrite(z, 1, RFS_BLOCK_SIZE, img);
            return l;
        }
        l--;
        r++;
    }
    return 0;
}

bool add_entry_to_directory(FILE *img, rfs_index_block_t *index, uint16_t dir_start_block, rfs_directory_entry_t *new_entry)
{
    uint16_t curr = dir_start_block, last = curr;
    rfs_directory_block_t db;
    while (curr != RFS_LAST_BLOCK_POINTER && curr != RFS_FREE_POINTER && curr != RFS_RESERVED_POINTER)
    {
        fseek(img, (long)curr * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&db, 1, RFS_BLOCK_SIZE, img);
        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            if (db.entries[i].name[0] == '\0')
            {
                memset(&db.entries[i], 0, sizeof(rfs_directory_entry_t));
                db.entries[i] = *new_entry;
                fseek(img, (long)curr * RFS_BLOCK_SIZE, SEEK_SET);
                fwrite(&db, 1, RFS_BLOCK_SIZE, img);
                return true;
            }
        }
        last = curr;
        curr = get_map_entry(img, index->block_map_start, curr);
    }
    uint16_t new_dir = allocate_free_block(img, index);
    if (!new_dir)
        return false;
    set_map_entry(img, index->block_map_start, last, new_dir);
    memset(&db, 0, RFS_BLOCK_SIZE);
    db.entries[0] = *new_entry;
    fseek(img, (long)new_dir * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&db, 1, RFS_BLOCK_SIZE, img);
    return true;
}

rfs_datetime_t get_current_rfs_time()
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    return (rfs_datetime_t){.year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday, .hour = tm.tm_hour, .minute = tm.tm_min, .second = tm.tm_sec};
}

// --- 3. MAIN ---

int main(int argc, char **argv)
{
    if (argc < 6 || argc > 9)
    {
        fprintf(stderr, "Uso: %s <imagem.img> <caminho> <tipo: c/b> <major> <minor> [modo] [usuario] [grupo]\n", argv[0]);
        fprintf(stderr, "Exemplo CHAR : %s disco.img /dev/tty0 c 4 0 620 root root\n", argv[0]);
        fprintf(stderr, "Exemplo BLOCK: %s disco.img /dev/sda b 8 0 660 root disk\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];
    char *target_path = argv[2];
    char type = argv[3][0];
    uint16_t major = (uint16_t)atoi(argv[4]);
    uint16_t minor = (uint16_t)atoi(argv[5]);

    FILE *img = fopen(img_path, "r+b");
    if (!img)
    {
        perror("Erro ao abrir a imagem");
        return EXIT_FAILURE;
    }

    rfs_index_block_t idx;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&idx, 1, sizeof(idx), img);

    // Resolução dos parâmetros opcionais
    uint16_t mode = (argc >= 7) ? strtol(argv[6], NULL, 8) : 0660;
    uint16_t uid = (argc >= 8) ? resolve_user(img, &idx, argv[7]) : 0;
    uint16_t gid = (argc >= 9) ? resolve_group(img, &idx, argv[8]) : 0;

    uint16_t dev_flag = 0;
    if (type == 'c' || type == 'C')
        dev_flag = RFS_MODE_CHAR_DEV;
    else if (type == 'b' || type == 'B')
        dev_flag = RFS_MODE_BLOCK_DEV;
    else
    {
        fprintf(stderr, "Erro: Tipo de dispositivo inválido. Use 'c' (Character) ou 'b' (Block).\n");
        fclose(img);
        return EXIT_FAILURE;
    }

    char *path_copy1 = strdup(target_path);
    char *path_copy2 = strdup(target_path);
    char *parent_dir = dirname(path_copy1);
    char *target_name = basename(path_copy2);

    uint16_t current_dir_block = idx.root_dir_start;

    if (strcmp(parent_dir, "/") != 0 && strcmp(parent_dir, ".") != 0)
    {
        char *token = strtok(parent_dir, "/");
        rfs_directory_entry_t entry;
        while (token != NULL)
        {
            if (!find_entry_location(img, idx.block_map_start, current_dir_block, token, &entry))
            {
                fprintf(stderr, "Erro: Diretório pai '%s' não existe.\n", token);
                free(path_copy1);
                free(path_copy2);
                fclose(img);
                return EXIT_FAILURE;
            }
            if (!(entry.mode & RFS_MODE_DIRECTORY))
            {
                fprintf(stderr, "Erro: O caminho '%s' não é um diretório.\n", token);
                free(path_copy1);
                free(path_copy2);
                fclose(img);
                return EXIT_FAILURE;
            }
            current_dir_block = entry.starting_block;
            token = strtok(NULL, "/");
        }
    }

    if (find_entry_location(img, idx.block_map_start, current_dir_block, target_name, NULL))
    {
        fprintf(stderr, "Erro: Já existe um item com o nome '%s' neste diretório.\n", target_name);
        free(path_copy1);
        free(path_copy2);
        fclose(img);
        return EXIT_FAILURE;
    }

    rfs_directory_entry_t dev_entry;
    memset(&dev_entry, 0, sizeof(rfs_directory_entry_t));
    strncpy(dev_entry.name, target_name, RFS_MAX_NAME_LENGTH);

    dev_entry.mode = dev_flag | (mode & 0777);
    dev_entry.user_id = uid;
    dev_entry.group_id = gid;
    dev_entry.creation_time = get_current_rfs_time();
    dev_entry.modification_time = dev_entry.creation_time;

    // Sem alocar blocos
    dev_entry.file_size = 0;
    dev_entry.starting_block = RFS_LAST_BLOCK_POINTER;

    // Salvando os IDs de Controlador/Dispositivo
    dev_entry.reserved[0] = (major >> 8) & 0xFF;
    dev_entry.reserved[1] = major & 0xFF;
    dev_entry.reserved[2] = (minor >> 8) & 0xFF;
    dev_entry.reserved[3] = minor & 0xFF;

    if (!add_entry_to_directory(img, &idx, current_dir_block, &dev_entry))
    {
        fprintf(stderr, "Erro: Falha ao gravar a entrada do dispositivo. Disco cheio?\n");
        free(path_copy1);
        free(path_copy2);
        fclose(img);
        return EXIT_FAILURE;
    }

    //printf("Nó criado: %s\n", target_path);
    //printf("Tipo : %s\n", (type == 'c' || type == 'C') ? "Character Device" : "Block Device");
    //printf("Major: %u | Minor: %u\n", major, minor);
    //printf("Dono : %u | Grupo: %u\n", uid, gid);

    free(path_copy1);
    free(path_copy2);
    fclose(img);
    return EXIT_SUCCESS;
}