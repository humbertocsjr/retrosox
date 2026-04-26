#include "rfs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Funções auxiliares de navegação (mesma lógica do chown)
uint16_t get_map_entry(FILE *img, uint16_t map_start, uint16_t block)
{
    uint16_t ptr;
    fseek(img, (long)map_start * RFS_BLOCK_SIZE + (block * sizeof(uint16_t)), SEEK_SET);
    fread(&ptr, sizeof(uint16_t), 1, img);
    return ptr;
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

bool find_path(FILE *img, rfs_index_block_t *idx, const char *path, uint16_t *out_block, int *out_index, rfs_directory_entry_t *out_entry)
{
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    uint16_t curr_dir_block = idx->root_dir_start;
    bool found = false;
    while (token != NULL)
    {
        found = find_entry_location(img, idx->block_map_start, curr_dir_block, token, out_block, out_index, out_entry);
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

bool read_file_content(FILE *img, rfs_index_block_t *idx, rfs_directory_entry_t *entry, char **out_buf)
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

// Resolução de Grupo via /etc/group
bool resolve_groupname(FILE *img, rfs_index_block_t *idx, const char *groupname, uint16_t *out_gid)
{
    rfs_directory_entry_t group_file;
    if (!find_path(img, idx, "/etc/group", NULL, NULL, &group_file))
    {
        fprintf(stderr, "Erro: Arquivo '/etc/group' não encontrado.\n");
        return false;
    }
    char *content;
    if (!read_file_content(img, idx, &group_file, &content))
        return false;

    char *line = strtok(content, "\n");
    while (line != NULL)
    {
        char line_copy[256];
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        char *gname = strtok(line_copy, ":");
        if (gname && strcmp(gname, groupname) == 0)
        {
            strtok(NULL, ":"); // Pula senha
            char *gid_str = strtok(NULL, ":");
            if (gid_str)
            {
                *out_gid = (uint16_t)atoi(gid_str);
                free(content);
                return true;
            }
        }
        line = strtok(NULL, "\n");
    }
    free(content);
    return false;
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Uso: %s <imagem.img> <grupo_ou_gid> <caminho_alvo>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *img = fopen(argv[1], "r+b");
    if (!img)
        return EXIT_FAILURE;

    rfs_index_block_t idx;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&idx, sizeof(rfs_index_block_t), 1, img);

    uint16_t final_gid;
    if (isdigit(argv[2][0]))
    {
        final_gid = (uint16_t)atoi(argv[2]);
    }
    else
    {
        if (!resolve_groupname(img, &idx, argv[2], &final_gid))
        {
            fprintf(stderr, "Erro: Grupo '%s' não encontrado.\n", argv[2]);
            fclose(img);
            return EXIT_FAILURE;
        }
    }

    uint16_t e_block;
    int e_idx;
    rfs_directory_entry_t entry;
    if (!find_path(img, &idx, argv[3], &e_block, &e_idx, &entry))
    {
        fprintf(stderr, "Erro: Alvo não encontrado.\n");
        fclose(img);
        return EXIT_FAILURE;
    }

    entry.group_id = final_gid;
    rfs_directory_block_t d_block;
    fseek(img, (long)e_block * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&d_block, sizeof(rfs_directory_block_t), 1, img);
    d_block.entries[e_idx] = entry;
    fseek(img, (long)e_block * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&d_block, sizeof(rfs_directory_block_t), 1, img);

    printf("Grupo de '%s' alterado para %u.\n", argv[3], final_gid);
    fclose(img);
    return EXIT_SUCCESS;
}