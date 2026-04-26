#include "rfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <libgen.h>

// --- 1. FUNÇÕES DE RESOLUÇÃO DE NOMES (/etc/passwd e /etc/group) ---

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

bool find_entry_location(FILE *img, uint16_t map_start, uint16_t dir_start_block, const char *target_name, rfs_directory_entry_t *out_entry)
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
            strtok(NULL, ":"); // Pula senha
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
    return 1000; // Fallback seguro se não achar o nome
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
            strtok(NULL, ":"); // Pula senha
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
    return 1000; // Fallback seguro
}

// --- 2. GERENCIAMENTO FÍSICO E ALOCAÇÃO ---

// Alocação Radial: Procura o bloco livre mais próximo do Mapa de Blocos
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
    return 0; // Disco cheio
}

rfs_datetime_t get_current_rfs_time()
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    return (rfs_datetime_t){.year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday, .hour = tm.tm_hour, .minute = tm.tm_min, .second = tm.tm_sec, .timezone_offset = 0};
}

// --- 3. CRIAÇÃO DE DIRETÓRIOS ---

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
                // LIMPEZA DE SLACK SPACE
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

// Search Bit Inteligente: Transforma Leitura em Leitura+Execução para pastas
uint16_t calculate_dir_mode(uint16_t input_mode)
{
    uint16_t mode = input_mode & 0777;
    mode |= 0500; // Dono sempre acessa
    if (mode & 0040)
        mode |= 0010; // Se Grupo lê, Grupo executa
    if (mode & 0004)
        mode |= 0001; // Se Outros lê, Outros executa
    return mode;
}

uint16_t create_rfs_directory(FILE *img, rfs_index_block_t *index, uint16_t parent_dir_block, const char *dir_name, uint16_t mode, uint16_t uid, uint16_t gid)
{
    uint16_t new_dir_block = allocate_free_block(img, index);
    if (!new_dir_block)
        return 0;

    rfs_directory_entry_t dir_entry = {0};
    strncpy(dir_entry.name, dir_name, RFS_MAX_NAME_LENGTH);
    dir_entry.starting_block = new_dir_block;
    dir_entry.mode = RFS_MODE_DIRECTORY | calculate_dir_mode(mode);
    dir_entry.creation_time = get_current_rfs_time();
    dir_entry.modification_time = dir_entry.creation_time;
    dir_entry.user_id = uid;
    dir_entry.group_id = gid;

    if (!add_entry_to_directory(img, index, parent_dir_block, &dir_entry))
        return 0;

    // Entradas obrigatórias "." e ".." com limpeza total dos nomes residuais
    rfs_directory_entry_t dot = dir_entry;
    memset(dot.name, 0, sizeof(dot.name));
    strcpy(dot.name, ".");
    add_entry_to_directory(img, index, new_dir_block, &dot);

    rfs_directory_entry_t dotdot = dir_entry;
    memset(dotdot.name, 0, sizeof(dotdot.name));
    strcpy(dotdot.name, "..");
    dotdot.starting_block = parent_dir_block;
    add_entry_to_directory(img, index, new_dir_block, &dotdot);

    //printf("Diretório criado: %s (UID: %u, GID: %u)\n", dir_name, uid, gid);
    return new_dir_block;
}

// --- MAIN ---

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 6)
    {
        fprintf(stderr, "Uso: %s <imagem.img> <caminho_diretorio> [permissão_octal] [usuario/uid] [grupo/gid]\n", argv[0]);
        fprintf(stderr, "Exemplo: %s disco.img /sistema/var/logs 755 root admin\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];
    char *target_path = argv[2];

    if (strcmp(target_path, "/") == 0)
    {
        fprintf(stderr, "Erro: O diretório raiz (/) já existe por padrão.\n");
        return EXIT_FAILURE;
    }

    FILE *img = fopen(img_path, "r+b");
    if (!img)
    {
        perror("Erro ao abrir a imagem");
        return EXIT_FAILURE;
    }

    rfs_index_block_t index_block;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index_block, sizeof(rfs_index_block_t), 1, img);

    // Resolução dos parâmetros opcionais
    uint16_t opt_mode = (argc >= 4) ? strtol(argv[3], NULL, 8) : 0777;
    uint16_t opt_uid = (argc >= 5) ? resolve_user(img, &index_block, argv[4]) : 1000;
    uint16_t opt_gid = (argc >= 6) ? resolve_group(img, &index_block, argv[5]) : 1000;

    // Processamento do caminho (Comportamento mkdir -p)
    char *path_copy = strdup(target_path);
    char *token = strtok(path_copy, "/");
    uint16_t current_dir_block = index_block.root_dir_start;
    bool something_created = false;

    while (token != NULL)
    {
        rfs_directory_entry_t entry;

        // Verifica se a pasta atual já existe
        if (!find_entry_location(img, index_block.block_map_start, current_dir_block, token, &entry))
        {
            // Se não existe, cria herdando as permissões e IDs fornecidos
            uint16_t new_dir_block = create_rfs_directory(img, &index_block, current_dir_block, token, opt_mode, opt_uid, opt_gid);
            if (!new_dir_block)
            {
                fprintf(stderr, "Erro: Falha ao criar a pasta '%s' (Disco cheio?).\n", token);
                free(path_copy);
                fclose(img);
                return EXIT_FAILURE;
            }
            current_dir_block = new_dir_block;
            something_created = true;
        }
        else
        {
            // Se já existe, garante que é uma pasta e apenas entra nela
            if (!(entry.mode & RFS_MODE_DIRECTORY))
            {
                fprintf(stderr, "Erro: Não é possível criar em '%s', pois já existe um arquivo com esse nome.\n", token);
                free(path_copy);
                fclose(img);
                return EXIT_FAILURE;
            }
            current_dir_block = entry.starting_block;
        }
        token = strtok(NULL, "/");
    }

    if (!something_created)
    {
        printf("Aviso: O caminho de diretórios já existia completamente.\n");
    }

    free(path_copy);
    fclose(img);
    return EXIT_SUCCESS;
}