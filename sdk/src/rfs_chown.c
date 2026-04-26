#include "rfs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Lê uma entrada na FAT
uint16_t get_map_entry(FILE *img, uint16_t map_start, uint16_t block)
{
    uint16_t ptr;
    fseek(img, (long)map_start * RFS_BLOCK_SIZE + (block * sizeof(uint16_t)), SEEK_SET);
    fread(&ptr, sizeof(uint16_t), 1, img);
    return ptr;
}

// Procura um item dentro de um bloco de diretório específico
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

// Navega a partir da raiz (/) e encontra a entrada de um caminho completo
bool find_path(FILE *img, rfs_index_block_t *idx, const char *path, uint16_t *out_block, int *out_index, rfs_directory_entry_t *out_entry)
{
    if (strcmp(path, "/") == 0)
        return false;

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
                break; // Se tem algo depois, isso precisava ser um diretório
            }
            curr_dir_block = out_entry->starting_block;
        }
    }
    free(path_copy);
    return found;
}

// Lê todo o conteúdo de um arquivo do RFS para a memória (String)
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
    buf[read] = '\0'; // Garante que é uma string válida
    *out_buf = buf;
    return true;
}

// Procura o usuário no arquivo /etc/passwd da imagem e retorna seu UID
bool resolve_username(FILE *img, rfs_index_block_t *idx, const char *username, uint16_t *out_uid)
{
    rfs_directory_entry_t passwd_entry;

    // 1. Acha o arquivo /etc/passwd no RFS
    if (!find_path(img, idx, "/etc/passwd", NULL, NULL, &passwd_entry))
    {
        fprintf(stderr, "Erro: Arquivo '/etc/passwd' não encontrado na imagem.\n");
        return false;
    }

    if (passwd_entry.mode & RFS_MODE_DIRECTORY)
    {
        fprintf(stderr, "Erro: '/etc/passwd' é um diretório, não um arquivo.\n");
        return false;
    }

    // 2. Extrai o conteúdo
    char *content;
    if (!read_file_content(img, idx, &passwd_entry, &content))
    {
        fprintf(stderr, "Erro: '/etc/passwd' está vazio ou ilegível.\n");
        return false;
    }

    // 3. Faz o parser (Formato: usuario:senha:UID:GID:info:home:shell)
    char *line = strtok(content, "\n");
    while (line != NULL)
    {
        char line_copy[256];
        strncpy(line_copy, line, sizeof(line_copy) - 1);

        char *user = strtok(line_copy, ":");
        if (user && strcmp(user, username) == 0)
        {
            strtok(NULL, ":"); // Pula o campo da senha
            char *uid_str = strtok(NULL, ":");
            if (uid_str)
            {
                *out_uid = (uint16_t)atoi(uid_str);
                free(content);
                return true;
            }
        }
        line = strtok(NULL, "\n");
    }

    free(content);
    fprintf(stderr, "Erro: Usuário '%s' não encontrado em /etc/passwd.\n", username);
    return false;
}

// Verifica se uma string contém apenas números
bool is_numeric(const char *str)
{
    for (int i = 0; str[i] != '\0'; i++)
    {
        if (!isdigit(str[i]))
            return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Uso: %s <imagem.img> <usuario_ou_uid> <caminho_alvo>\n", argv[0]);
        fprintf(stderr, "Exemplo 1: %s disco.img root /bin/bash\n", argv[0]);
        fprintf(stderr, "Exemplo 2: %s disco.img 1000 /home/user/nota.txt\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];
    const char *user_input = argv[2];
    const char *target_path = argv[3];

    FILE *img = fopen(img_path, "r+b");
    if (!img)
    {
        perror("Erro ao abrir a imagem");
        return EXIT_FAILURE;
    }

    rfs_index_block_t index_block;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index_block, sizeof(rfs_index_block_t), 1, img);

    // --- 1. RESOLUÇÃO DE USUÁRIO ---
    uint16_t final_uid;

    if (is_numeric(user_input))
    {
        final_uid = (uint16_t)atoi(user_input);
        printf("UID %u recebido diretamente.\n", final_uid);
    }
    else
    {
        printf("Resolvendo usuário '%s' via /etc/passwd...\n", user_input);
        if (!resolve_username(img, &index_block, user_input, &final_uid))
        {
            fclose(img);
            return EXIT_FAILURE;
        }
        printf("Usuário '%s' resolvido para o UID %u.\n", user_input, final_uid);
    }

    // --- 2. LOCALIZANDO O ALVO ---
    uint16_t entry_block;
    int entry_index;
    rfs_directory_entry_t entry;

    if (!find_path(img, &index_block, target_path, &entry_block, &entry_index, &entry))
    {
        fprintf(stderr, "Erro: O arquivo ou diretório '%s' não foi encontrado.\n", target_path);
        fclose(img);
        return EXIT_FAILURE;
    }

    // --- 3. ATUALIZANDO O DONO NO DISCO ---
    // Atualiza o UID e também a hora de modificação do status (ctime/mtime na nossa arquitetura)
    entry.user_id = final_uid;
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    entry.modification_time = (rfs_datetime_t){
        .year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday, .hour = tm.tm_hour, .minute = tm.tm_min, .second = tm.tm_sec, .timezone_offset = 0};

    // Lê o bloco do diretório pai, atualiza a entrada específica e grava de volta
    rfs_directory_block_t d_block;
    fseek(img, (long)entry_block * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&d_block, sizeof(rfs_directory_block_t), 1, img);

    d_block.entries[entry_index] = entry;

    fseek(img, (long)entry_block * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&d_block, sizeof(rfs_directory_block_t), 1, img);

    printf("Sucesso! Dono de '%s' alterado para %u.\n", target_path, final_uid);

    fclose(img);
    return EXIT_SUCCESS;
}