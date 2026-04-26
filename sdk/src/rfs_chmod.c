#include "rfs.h"

// Lê o próximo bloco do mapa de alocação (FAT)
uint16_t get_next_block(FILE *img, uint16_t map_start, uint16_t current_block)
{
    uint16_t next_block;
    long offset = (long)map_start * RFS_BLOCK_SIZE + (current_block * sizeof(uint16_t));
    fseek(img, offset, SEEK_SET);
    fread(&next_block, sizeof(uint16_t), 1, img);
    return next_block;
}

// Retorna apenas o nome do arquivo no final do caminho (ex: "/usr/bin/teste" -> "teste")
const char* get_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

// Navega no diretório procurando um nome e retorna sua posição exata no disco
bool find_entry_location(FILE *img, uint16_t map_start, uint16_t dir_start_block, const char *target_name, 
                         uint16_t *out_block, int *out_index, rfs_directory_entry_t *out_entry)
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
                *out_block = curr_block;
                *out_index = i;
                if (out_entry) *out_entry = dir_block.entries[i];
                return true;
            }
        }
        curr_block = get_next_block(img, map_start, curr_block);
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc != 4)
{
        fprintf(stderr, "Uso: %s <imagem> <permissao em octal> <caminho no rfs>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s disk.img 755 /dir/arquivo.txt\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *img_path = argv[1];
    const char *octal_str = argv[2];
    const char *target_path = argv[3];

    // Converte a string de permissão octal (ex: "755") para inteiro
    char *endptr;
    long new_mode = strtol(octal_str, &endptr, 8);
    if (*endptr != '\0' || new_mode < 0 || new_mode > 0777)
{
        fprintf(stderr, "Erro: Modo octal inválido '%s'. Use valores entre 000 e 777.\n", octal_str);
        exit(EXIT_FAILURE);
    }

    FILE *img = fopen(img_path, "r+b"); // r+b para permitir leitura e alteração
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

    // Não podemos dar chmod na própria raiz "/" facilmente porque ela não tem uma entrada de diretório pai padrão
    if (strcmp(target_path, "/") == 0)
{
        fprintf(stderr, "Erro: Alteração de permissão do diretório raiz (/) não suportada por este utilitário.\n");
        fclose(img);
        exit(EXIT_FAILURE);
    }

    // Copiando strings para manipulação
    char *path_copy1 = strdup(target_path);
    char *path_copy2 = strdup(target_path);
    
    // Simula a função dirname()
    char *last_slash = strrchr(path_copy1, '/');
    if (last_slash != NULL)
{
        if (last_slash == path_copy1)
{
            *(last_slash + 1) = '\0'; // Se for "/arquivo.txt", o dirname é "/"
        } else {
            *last_slash = '\0'; // Corta a string no último '/'
        }
    } else {
        strcpy(path_copy1, "."); 
    }
    char *parent_dir_path = path_copy1;
    const char *target_name = get_basename(path_copy2);

    // Navega até a pasta pai do alvo
    uint16_t current_dir_block = index_block.root_dir_start;
    if (strcmp(parent_dir_path, "/") != 0 && strcmp(parent_dir_path, ".") != 0)
{
        char *token = strtok(parent_dir_path, "/");
        rfs_directory_entry_t entry;
        
        while (token != NULL)
{
            uint16_t trash_block;
            int trash_index;
            if (!find_entry_location(img, index_block.block_map_start, current_dir_block, token, &trash_block, &trash_index, &entry))
{
                fprintf(stderr, "Erro: O caminho '%s' não existe.\n", argv[3]);
                free(path_copy1); free(path_copy2); fclose(img);
                exit(EXIT_FAILURE);
            }
            if (!(entry.mode & RFS_MODE_DIRECTORY))
{
                fprintf(stderr, "Erro: Parte do caminho fornecido não é um diretório.\n");
                free(path_copy1); free(path_copy2); fclose(img);
                exit(EXIT_FAILURE);
            }
            current_dir_block = entry.starting_block;
            token = strtok(NULL, "/");
        }
    }

    // Localiza a entrada final para alterar
    uint16_t target_block;
    int target_index;
    
    if (!find_entry_location(img, index_block.block_map_start, current_dir_block, target_name, &target_block, &target_index, NULL))
{
        fprintf(stderr, "Erro: Arquivo ou diretório '%s' não encontrado.\n", target_name);
        free(path_copy1); free(path_copy2); fclose(img);
        exit(EXIT_FAILURE);
    }

    // Lê o bloco do diretório pai onde o arquivo alvo está armazenado
    rfs_directory_block_t block_data;
    fseek(img, (long)target_block * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&block_data, sizeof(rfs_directory_block_t), 1, img);

    // Salva o tipo do arquivo (limpando as antigas permissões com ~00777)
    // 00777 em octal = 0x01FF. A inversão matemática (~00777) aplicada via bitwise AND mantém os flags de Arquivo/Dir intactos.
    uint16_t current_mode = block_data.entries[target_index].mode;
    uint16_t new_full_mode = (current_mode & ~00777) | (new_mode & 00777);
    
    block_data.entries[target_index].mode = new_full_mode;

    // Sobrescreve o bloco alterado de volta no disco
    fseek(img, (long)target_block * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&block_data, sizeof(rfs_directory_block_t), 1, img);

    printf("Permissões de '%s' alteradas para %03lo.\n", target_path, new_mode);

    free(path_copy1);
    free(path_copy2);
    fclose(img);
    return EXIT_SUCCESS;
}