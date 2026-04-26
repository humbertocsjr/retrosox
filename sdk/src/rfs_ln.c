#include "rfs.h"
#include <sys/stat.h>
#include <time.h>
#include <libgen.h>

// --- Funções Auxiliares de Gerenciamento da FAT ---

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

// Busca um bloco livre em todo o disco e o zera fisicamente
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

rfs_datetime_t get_current_rfs_time()
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    rfs_datetime_t dt = {
        .year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday, .hour = tm.tm_hour, .minute = tm.tm_min, .second = tm.tm_sec, .timezone_offset = 0};
    return dt;
}

// Adiciona uma nova entrada limpando o lixo residual (Slack Space)
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
                memset(&dir_block.entries[i], 0, sizeof(rfs_directory_entry_t)); // Limpeza!
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

// Procura uma entrada (arquivo ou pasta) no diretório
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

int main(int argc, char **argv)
{
    // Parâmetros mínimos: programa, imagem, flag -s, alvo, destino
    if (argc != 5)
    {
        fprintf(stderr, "Uso: %s <imagem.img> -s <caminho_alvo> <caminho_do_link>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s disco.img -s /docs/relatorio.txt /atalho.txt\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];
    const char *flag = argv[2];
    const char *target_path = argv[3];
    const char *link_path = argv[4];

    if (strcmp(flag, "-s") != 0)
    {
        fprintf(stderr, "Erro: Hard Links nativos não são suportados na arquitetura atual do RFS (sem Inodes).\n");
        fprintf(stderr, "Por favor, use a flag '-s' para criar um Link Simbólico (Soft Link).\n");
        return EXIT_FAILURE;
    }

    if (strlen(target_path) >= RFS_BLOCK_SIZE)
    {
        fprintf(stderr, "Erro: O caminho de destino excede o limite de %d caracteres para um Link Simbólico.\n", RFS_BLOCK_SIZE - 1);
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

    // Manipulação de strings para o nome do novo atalho
    char *link_path_copy1 = strdup(link_path);
    char *link_path_copy2 = strdup(link_path);
    char *parent_dir_path = dirname(link_path_copy1);
    char *new_link_name = basename(link_path_copy2);

    // Navega até a pasta pai onde o link será criado
    uint16_t current_dir_block = index_block.root_dir_start;
    if (strcmp(parent_dir_path, "/") != 0 && strcmp(parent_dir_path, ".") != 0)
    {
        char *token = strtok(parent_dir_path, "/");
        rfs_directory_entry_t entry;

        while (token != NULL)
        {
            if (!find_entry_location(img, index_block.block_map_start, current_dir_block, token, NULL, NULL, &entry))
            {
                fprintf(stderr, "Erro: O diretório pai '%s' para o link não existe.\n", token);
                free(link_path_copy1);
                free(link_path_copy2);
                fclose(img);
                return EXIT_FAILURE;
            }
            if (!(entry.mode & RFS_MODE_DIRECTORY))
            {
                fprintf(stderr, "Erro: O caminho '%s' não é um diretório.\n", token);
                free(link_path_copy1);
                free(link_path_copy2);
                fclose(img);
                return EXIT_FAILURE;
            }
            current_dir_block = entry.starting_block;
            token = strtok(NULL, "/");
        }
    }

    // Verifica se já existe algo com o mesmo nome na pasta
    if (find_entry_location(img, index_block.block_map_start, current_dir_block, new_link_name, NULL, NULL, NULL))
    {
        fprintf(stderr, "Erro: O item '%s' já existe neste diretório.\n", new_link_name);
        free(link_path_copy1);
        free(link_path_copy2);
        fclose(img);
        return EXIT_FAILURE;
    }

    // 1. Aloca 1 bloco de dados para guardar o caminho (string)
    uint16_t new_block = allocate_free_block(img, &index_block);
    if (!new_block)
    {
        fprintf(stderr, "Erro: Disco RFS cheio!\n");
        free(link_path_copy1);
        free(link_path_copy2);
        fclose(img);
        return EXIT_FAILURE;
    }

    // 2. Grava a string do caminho dentro deste novo bloco
    fseek(img, (long)new_block * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(target_path, 1, strlen(target_path), img);

    // 3. Cria a entrada estrutural com a flag RFS_MODE_SYMLINK
    rfs_directory_entry_t symlink_entry;
    memset(&symlink_entry, 0, sizeof(rfs_directory_entry_t)); // Limpa lixo de memória
    strncpy(symlink_entry.name, new_link_name, RFS_MAX_NAME_LENGTH);

    // Links simbólicos geralmente exibem permissão 0777 (o alvo que decide o acesso final)
    symlink_entry.mode = RFS_MODE_SYMLINK | 0777;
    symlink_entry.starting_block = new_block;
    symlink_entry.file_size = strlen(target_path); // O tamanho do arquivo = tamanho da string do path!
    symlink_entry.creation_time = get_current_rfs_time();
    symlink_entry.modification_time = symlink_entry.creation_time;
    symlink_entry.user_id = 1000;
    symlink_entry.group_id = 1000;

    add_entry_to_directory(img, &index_block, current_dir_block, &symlink_entry);

    printf("Link simbólico criado: '%s' -> '%s'\n", link_path, target_path);

    free(link_path_copy1);
    free(link_path_copy2);
    fclose(img);
    return EXIT_SUCCESS;
}