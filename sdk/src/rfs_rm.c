#include "rfs.h"
#include <sys/stat.h>
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

// Devolve toda a cadeia de blocos de um arquivo/diretório para a FAT
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

// Procura uma entrada no diretório e retorna o bloco e índice exatos para podermos apagá-la depois
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

// --- Deleção Recursiva (O Coração do rm -rf) ---
void delete_recursive(FILE *img, rfs_index_block_t *idx, uint16_t start_block, uint16_t mode)
{
    if (start_block == RFS_FREE_POINTER || start_block == RFS_RESERVED_POINTER)
        return;

    // Se for um diretório, precisamos entrar nele e destruir os filhos primeiro
    if (mode & RFS_MODE_DIRECTORY)
    {
        uint16_t curr = start_block;
        rfs_directory_block_t d_block;

        while (curr != RFS_LAST_BLOCK_POINTER && curr != RFS_FREE_POINTER && curr != RFS_RESERVED_POINTER)
        {
            fseek(img, (long)curr * RFS_BLOCK_SIZE, SEEK_SET);
            fread(&d_block, sizeof(rfs_directory_block_t), 1, img);

            for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
            {
                if (d_block.entries[i].name[0] != '\0')
                {
                    // Proteção de sanidade: Ignora o self (.) e o parent (..) para não entrar em loop infinito!
                    if (strcmp(d_block.entries[i].name, ".") == 0 || strcmp(d_block.entries[i].name, "..") == 0)
                        continue;

                    // Chama a destruição para o filho
                    delete_recursive(img, idx, d_block.entries[i].starting_block, d_block.entries[i].mode);
                }
            }
            curr = get_map_entry(img, idx->block_map_start, curr);
        }
    }

    // Após limpar os filhos (ou se for apenas um arquivo/link), libera seus blocos
    free_block_chain(img, idx->block_map_start, start_block);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Uso: %s <imagem.img> <caminho_alvo>\n", argv[0]);
        fprintf(stderr, "Aviso: Esta ferramenta age como 'rm -rf'. Pastas serão deletadas recursivamente.\n");
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];
    char *target_path = argv[2];

    if (strcmp(target_path, "/") == 0)
    {
        fprintf(stderr, "Erro Fatal: Você não pode deletar o diretório raiz (/).\n");
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

    // Divisão do caminho para encontrar o pai e o filho
    char *path_copy1 = strdup(target_path);
    char *path_copy2 = strdup(target_path);
    char *parent_dir_path = dirname(path_copy1);
    char *target_name = basename(path_copy2);

    uint16_t current_dir_block = index_block.root_dir_start;

    // Navega até o diretório pai
    if (strcmp(parent_dir_path, "/") != 0 && strcmp(parent_dir_path, ".") != 0)
    {
        char *token = strtok(parent_dir_path, "/");
        rfs_directory_entry_t entry;

        while (token != NULL)
        {
            if (!find_entry_location(img, index_block.block_map_start, current_dir_block, token, NULL, NULL, &entry))
            {
                fprintf(stderr, "Erro: Caminho pai '%s' não encontrado.\n", token);
                free(path_copy1);
                free(path_copy2);
                fclose(img);
                return EXIT_FAILURE;
            }
            if (!(entry.mode & RFS_MODE_DIRECTORY))
            {
                fprintf(stderr, "Erro: Caminho pai é um arquivo, não um diretório.\n");
                free(path_copy1);
                free(path_copy2);
                fclose(img);
                return EXIT_FAILURE;
            }
            current_dir_block = entry.starting_block;
            token = strtok(NULL, "/");
        }
    }

    // Tenta encontrar o item a ser deletado dentro do pai
    uint16_t entry_block;
    int entry_index;
    rfs_directory_entry_t target_entry;

    if (!find_entry_location(img, index_block.block_map_start, current_dir_block, target_name, &entry_block, &entry_index, &target_entry))
    {
        fprintf(stderr, "Erro: O arquivo ou diretório '%s' não foi encontrado.\n", target_name);
        free(path_copy1);
        free(path_copy2);
        fclose(img);
        return EXIT_FAILURE;
    }

    // 1. Invoca a destruição recursiva dos dados no disco
    printf("Deletando alvo: %s...\n", target_path);
    delete_recursive(img, &index_block, target_entry.starting_block, target_entry.mode);

    // 2. Apaga definitivamente a entrada no diretório pai (Limpeza de Slack Space)
    rfs_directory_block_t p_block;
    fseek(img, (long)entry_block * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&p_block, sizeof(rfs_directory_block_t), 1, img);

    // Zera os 64 bytes da entrada para não deixar lixo residual!
    memset(&p_block.entries[entry_index], 0, sizeof(rfs_directory_entry_t));

    fseek(img, (long)entry_block * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&p_block, sizeof(rfs_directory_block_t), 1, img);

    printf("Item '%s' removido com sucesso.\n", target_name);

    free(path_copy1);
    free(path_copy2);
    fclose(img);
    return EXIT_SUCCESS;
}