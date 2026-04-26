#include "rfs.h"

// Define o limite máximo de profundidade da árvore (evita estouro de pilha)
#define MAX_TREE_DEPTH 256

// Lê o próximo bloco do mapa de alocação (FAT)
uint16_t get_next_block(FILE *img, uint16_t map_start, uint16_t current_block)
{
    uint16_t next_block;
    long offset = (long)map_start * RFS_BLOCK_SIZE + (current_block * sizeof(uint16_t));
    fseek(img, offset, SEEK_SET);
    fread(&next_block, sizeof(uint16_t), 1, img);
    return next_block;
}

// Verifica se a entrada é válida e se não é "." ou ".."
bool is_valid_entry(rfs_directory_entry_t *entry)
{
    if (entry->name[0] == '\0')
        return false; // Entrada vazia/excluída
    if (strcmp(entry->name, ".") == 0)
        return false; // Diretório atual
    if (strcmp(entry->name, "..") == 0)
        return false; // Diretório pai
    return true;
}

// Conta quantas entradas válidas existem em um diretório (necessário para saber qual é a última)
int count_valid_entries(FILE *img, uint16_t map_start, uint16_t dir_start_block)
{
    int count = 0;
    uint16_t curr_block = dir_start_block;
    rfs_directory_block_t dir_block;

    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER && curr_block != RFS_FREE_POINTER)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&dir_block, sizeof(rfs_directory_block_t), 1, img);

        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            if (is_valid_entry(&dir_block.entries[i]))
            {
                count++;
            }
        }
        curr_block = get_next_block(img, map_start, curr_block);
    }
    return count;
}

// Função recursiva que imprime a árvore
void print_tree(FILE *img, uint16_t map_start, uint16_t dir_start_block, int depth, bool *is_last_at_depth)
{
    if (depth >= MAX_TREE_DEPTH)
        return; // Proteção contra caminhos absurdamente profundos

    int total_entries = count_valid_entries(img, map_start, dir_start_block);
    int current_entry_count = 0;

    uint16_t curr_block = dir_start_block;
    rfs_directory_block_t dir_block;

    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER && curr_block != RFS_FREE_POINTER)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&dir_block, sizeof(rfs_directory_block_t), 1, img);

        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            rfs_directory_entry_t *entry = &dir_block.entries[i];

            if (is_valid_entry(entry))
            {
                current_entry_count++;
                bool is_last_item = (current_entry_count == total_entries);

                // Imprime a indentação baseada no histórico de profundidade
                for (int d = 0; d < depth; d++)
                {
                    if (is_last_at_depth[d])
                    {
                        printf("    "); // Espaço vazio se o ancestral for o último
                    }
                    else
                    {
                        printf("│   "); // Linha reta se o ancestral não for o último
                    }
                }

                // Imprime a ramificação atual
                if (is_last_item)
                {
                    printf("└── ");
                }
                else
                {
                    printf("├── ");
                }

                printf("%s\n", entry->name);

                // Se for um diretório, chama a recursão
                if (entry->mode & RFS_MODE_DIRECTORY)
                {
                    is_last_at_depth[depth] = is_last_item; // Registra se este dir é o último na sua camada
                    print_tree(img, map_start, entry->starting_block, depth + 1, is_last_at_depth);
                }
            }
        }
        curr_block = get_next_block(img, map_start, curr_block);
    }
}

// Procura um nome específico dentro de um diretório (mesma função do ls)
bool find_in_directory(FILE *img, uint16_t map_start, uint16_t dir_start_block, const char *target_name, rfs_directory_entry_t *out_entry)
{
    uint16_t curr_block = dir_start_block;
    rfs_directory_block_t dir_block;

    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER && curr_block != RFS_FREE_POINTER)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&dir_block, sizeof(rfs_directory_block_t), 1, img);

        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            if (dir_block.entries[i].name[0] != '\0')
            {
                if (strcmp(dir_block.entries[i].name, target_name) == 0)
                {
                    *out_entry = dir_block.entries[i];
                    return true;
                }
            }
        }
        curr_block = get_next_block(img, map_start, curr_block);
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "Uso: %s <imagem_disco.img> [caminho/do/diretorio]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *img_path = argv[1];
    const char *target_path = (argc == 3) ? argv[2] : "/";

    FILE *img = fopen(img_path, "rb");
    if (!img)
    {
        perror("Erro ao abrir a imagem do disco");
        exit(EXIT_FAILURE);
    }

    // Lendo o Superblock
    rfs_index_block_t index_block;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index_block, sizeof(rfs_index_block_t), 1, img);

    if (strncmp(index_block.signature, "RFS1", 4) != 0)
    {
        fprintf(stderr, "Erro: Assinatura RFS1 inválida.\n");
        fclose(img);
        exit(EXIT_FAILURE);
    }

    uint16_t current_dir_block = index_block.root_dir_start;

    // Se o usuário passou um caminho diferente da raiz ("/"), navega até lá
    if (strcmp(target_path, "/") != 0)
    {
        char *path_copy = strdup(target_path);
        char *token = strtok(path_copy, "/");
        rfs_directory_entry_t entry;

        while (token != NULL)
        {
            if (!find_in_directory(img, index_block.block_map_start, current_dir_block, token, &entry))
            {
                fprintf(stderr, "tree: %s: Arquivo ou diretório inexistente\n", target_path);
                free(path_copy);
                fclose(img);
                exit(EXIT_FAILURE);
            }

            if (!(entry.mode & RFS_MODE_DIRECTORY))
            {
                fprintf(stderr, "tree: %s: Não é um diretório\n", target_path);
                free(path_copy);
                fclose(img);
                exit(EXIT_FAILURE);
            }

            current_dir_block = entry.starting_block;
            token = strtok(NULL, "/");
        }
        free(path_copy);
    }

    // Imprime a raiz solicitada
    printf("%s\n", target_path);

    // Inicia a impressão recursiva da árvore
    bool is_last_at_depth[MAX_TREE_DEPTH] = {false};
    print_tree(img, index_block.block_map_start, current_dir_block, 0, is_last_at_depth);

    fclose(img);
    return EXIT_SUCCESS;
}