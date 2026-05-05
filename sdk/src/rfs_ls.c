#include "rfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// --- Funções Auxiliares Básicas ---

uint16_t get_map_entry(FILE *img, uint16_t map_start, uint16_t block)
{
    uint16_t ptr;
    fseek(img, (long)map_start * RFS_BLOCK_SIZE + (block * sizeof(uint16_t)), SEEK_SET);
    fread(&ptr, sizeof(uint16_t), 1, img);
    return ptr;
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

// Lê um arquivo inteiro do disco para uma string na RAM (usado para cache)
bool read_file_to_buf(FILE *img, rfs_index_block_t *idx, const char *path, char **out_buf)
{
    rfs_directory_entry_t ent;
    if (!find_path(img, idx, path, &ent))
        return false;
    if (ent.mode & RFS_MODE_DIRECTORY)
        return false;
    if (ent.file_size == 0)
        return false;

    char *buf = malloc(ent.file_size + 1);
    uint16_t curr = ent.starting_block;
    uint32_t read = 0;
    while (curr != RFS_LAST_BLOCK_POINTER && curr != RFS_FREE_POINTER && curr != RFS_RESERVED_POINTER)
    {
        fseek(img, (long)curr * RFS_BLOCK_SIZE, SEEK_SET);
        uint32_t chunk = (ent.file_size - read > RFS_BLOCK_SIZE) ? RFS_BLOCK_SIZE : (ent.file_size - read);
        fread(buf + read, 1, chunk, img);
        read += chunk;
        curr = get_map_entry(img, idx->block_map_start, curr);
    }
    buf[read] = '\0';
    *out_buf = buf;
    return true;
}

// --- Tradução de ID para Nome (BLINDADO COM strtok_r) ---

void lookup_name(const char *db_buf, uint16_t target_id, char *out_name)
{
    // Fallback padrão: string do número
    sprintf(out_name, "%u", target_id);
    if (!db_buf)
        return;

    char *buf_copy = strdup(db_buf);
    char *saveptr_line;
    char *line = strtok_r(buf_copy, "\n", &saveptr_line);

    while (line)
    {
        char l[256];
        strncpy(l, line, 255);
        l[255] = '\0';

        char *saveptr_field;
        char *name = strtok_r(l, ":", &saveptr_field);

        if (name)
        {
            strtok_r(NULL, ":", &saveptr_field); // Pula a senha
            char *id_str = strtok_r(NULL, ":", &saveptr_field);

            if (id_str && (uint16_t)atoi(id_str) == target_id)
            {
                strncpy(out_name, name, 8); // Limite visual de 8 chars
                out_name[8] = '\0';
                break;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr_line);
    }
    free(buf_copy);
}

// --- Listagem do Diretório ---

void list_directory(FILE *img, rfs_index_block_t *idx, uint16_t dir_start_block, const char *passwd_buf, const char *group_buf)
{
    uint16_t curr_block = dir_start_block;
    rfs_directory_block_t dir_block;

    printf("%-11s %-8s %-8s %-12s %-16s %s\n", "Permissões", "Usuário", "Grupo", "Tamanho/Dev", "Modificado", "Nome");
    printf("----------------------------------------------------------------------------------\n");

    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER && curr_block != RFS_FREE_POINTER)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&dir_block, sizeof(rfs_directory_block_t), 1, img);

        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            rfs_directory_entry_t *entry = &dir_block.entries[i];

            if (entry->name[0] != '\0')
            {
                char perms[12];

                // 1. Identifica o Tipo do Item (Incluindo os novos Devices!)
                if (entry->mode & RFS_MODE_DIRECTORY)
                    perms[0] = 'd';
                else if (entry->mode & RFS_MODE_SYMLINK)
                    perms[0] = 'l';
                else if (entry->mode & RFS_MODE_CHAR_DEV)
                    perms[0] = 'c';
                else if (entry->mode & RFS_MODE_BLOCK_DEV)
                    perms[0] = 'b';
                else
                    perms[0] = '-';

                // Permissões
                perms[1] = (entry->mode & 0400) ? 'r' : '-';
                perms[2] = (entry->mode & 0200) ? 'w' : '-';
                perms[3] = (entry->mode & 0100) ? 'x' : '-';
                perms[4] = (entry->mode & 0040) ? 'r' : '-';
                perms[5] = (entry->mode & 0020) ? 'w' : '-';
                perms[6] = (entry->mode & 0010) ? 'x' : '-';
                perms[7] = (entry->mode & 0004) ? 'r' : '-';
                perms[8] = (entry->mode & 0002) ? 'w' : '-';
                perms[9] = (entry->mode & 0001) ? 'x' : '-';
                perms[10] = '\0';

                // 2. Tradução de Nomes usando Cache
                char user_str[16];
                char group_str[16];
                lookup_name(passwd_buf, entry->user_id, user_str);
                lookup_name(group_buf, entry->group_id, group_str);

                // 3. Formatação de Tamanho x Dispositivo
                char size_str[32];
                if (perms[0] == 'c' || perms[0] == 'b')
                {
                    // Extrai Major e Minor do espaço reservado
                    uint16_t major = (entry->reserved[0] << 8) | entry->reserved[1];
                    uint16_t minor = (entry->reserved[2] << 8) | entry->reserved[3];
                    snprintf(size_str, sizeof(size_str), "%u, %u", major, minor);
                }
                else
                {
                    // Arquivo ou Diretório normal
                    snprintf(size_str, sizeof(size_str), "%u", entry->file_size);
                }

                // 4. Formatação de Tempo
                char time_str[20];
                snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d",
                         entry->modification_time.year, entry->modification_time.month, entry->modification_time.day,
                         entry->modification_time.hour, entry->modification_time.minute);

                // 5. Impressão Final (Com Link Simbólico, se houver)
                if (perms[0] == 'l')
                {
                    char target_path[512] = {0};
                    long current_pos = ftell(img);
                    fseek(img, (long)entry->starting_block * RFS_BLOCK_SIZE, SEEK_SET);
                    size_t to_read = (entry->file_size < 511) ? entry->file_size : 511;
                    fread(target_path, 1, to_read, img);
                    target_path[to_read] = '\0';
                    fseek(img, current_pos, SEEK_SET);

                    printf("%s %-8s %-8s %-12s %-16s %s -> %s\n",
                           perms, user_str, group_str, size_str, time_str, entry->name, target_path);
                }
                else
                {
                    printf("%s %-8s %-8s %-12s %-16s %s\n",
                           perms, user_str, group_str, size_str, time_str, entry->name);
                }
            }
        }
        curr_block = get_map_entry(img, idx->block_map_start, curr_block);
    }
}

// --- MAIN ---

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Uso: %s <imagem.img> <caminho_diretorio>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *img = fopen(argv[1], "rb");
    if (!img)
    {
        perror("Erro ao abrir a imagem");
        return EXIT_FAILURE;
    }

    rfs_index_block_t idx;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&idx, sizeof(rfs_index_block_t), 1, img);

    // Carregamento do Cache de Usuários e Grupos
    char *passwd_buf = NULL;
    char *group_buf = NULL;
    read_file_to_buf(img, &idx, "/etc/passwd", &passwd_buf);
    read_file_to_buf(img, &idx, "/etc/group", &group_buf);

    // Busca do Diretório Alvo
    uint16_t target_dir_block;
    if (strcmp(argv[2], "/") == 0)
    {
        target_dir_block = idx.root_dir_start;
    }
    else
    {
        rfs_directory_entry_t entry;
        if (!find_path(img, &idx, argv[2], &entry))
        {
            fprintf(stderr, "Erro: Caminho '%s' não encontrado.\n", argv[2]);
            free(passwd_buf);
            free(group_buf);
            fclose(img);
            return EXIT_FAILURE;
        }
        if (!(entry.mode & RFS_MODE_DIRECTORY))
        {
            fprintf(stderr, "Erro: '%s' não é um diretório.\n", argv[2]);
            free(passwd_buf);
            free(group_buf);
            fclose(img);
            return EXIT_FAILURE;
        }
        target_dir_block = entry.starting_block;
    }

    printf("Listando diretório: %s\n\n", argv[2]);
    list_directory(img, &idx, target_dir_block, passwd_buf, group_buf);

    free(passwd_buf);
    free(group_buf);
    fclose(img);
    return EXIT_SUCCESS;
}