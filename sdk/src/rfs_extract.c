#include "rfs.h"
#include <sys/stat.h> // Para a função mkdir()
#include <errno.h>
#include <unistd.h> // Para a função symlink()

// Macro para criar diretórios de forma compatível (Linux/macOS e Windows)
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#else
#define MAKE_DIR(path) mkdir(path, 0777)
#endif

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
const char *get_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

// Extrai um único arquivo
void extract_file(FILE *img, uint16_t map_start, rfs_directory_entry_t *entry, const char *dest_path)
{
    FILE *out = fopen(dest_path, "wb");
    if (!out)
    {
        fprintf(stderr, "Erro ao criar arquivo '%s': %s\n", dest_path, strerror(errno));
        return;
    }

    uint32_t remaining_bytes = entry->file_size;
    uint16_t curr_block = entry->starting_block;
    uint8_t buffer[RFS_BLOCK_SIZE];

    printf("Extraindo arquivo: %s (%u bytes)\n", dest_path, remaining_bytes);

    // Percorre os blocos de dados do arquivo
    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER &&
           curr_block != RFS_FREE_POINTER && remaining_bytes > 0)
    {

        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);

        // Só lemos e gravamos a quantidade exata de bytes que faltam (evita lixo no final)
        uint32_t to_read = (remaining_bytes < RFS_BLOCK_SIZE) ? remaining_bytes : RFS_BLOCK_SIZE;
        fread(buffer, 1, to_read, img);
        fwrite(buffer, 1, to_read, out);

        remaining_bytes -= to_read;
        curr_block = get_next_block(img, map_start, curr_block);
    }

    fclose(out);
}

// Extrai e recria um Link Simbólico no sistema hospedeiro
void extract_symlink(FILE *img, rfs_directory_entry_t *entry, const char *dest_path)
{
    char target_path[512] = {0};

    // Lê o caminho alvo gravado no bloco do RFS
    fseek(img, (long)entry->starting_block * RFS_BLOCK_SIZE, SEEK_SET);
    size_t to_read = (entry->file_size < 511) ? entry->file_size : 511;
    fread(target_path, 1, to_read, img);
    target_path[to_read] = '\0';

    printf("Criando atalho: %s -> %s\n", dest_path, target_path);

    // Usa a chamada de sistema do Unix para criar um symlink real
    if (symlink(target_path, dest_path) != 0)
    {
        fprintf(stderr, "Aviso: Falha ao recriar o link simbólico '%s': %s\n", dest_path, strerror(errno));
    }
}

// Extrai um diretório inteiro de forma recursiva
void extract_directory(FILE *img, uint16_t map_start, rfs_directory_entry_t *dir_entry, const char *dest_path)
{
    // Cria o diretório no host (se falhar e o erro não for "já existe", exibe aviso)
    if (MAKE_DIR(dest_path) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "Erro ao criar diretório '%s': %s\n", dest_path, strerror(errno));
        return;
    }

    printf("Criando diretório: %s\n", dest_path);

    uint16_t curr_block = dir_entry->starting_block;
    rfs_directory_block_t dir_block;

    while (curr_block != RFS_LAST_BLOCK_POINTER && curr_block != RFS_RESERVED_POINTER && curr_block != RFS_FREE_POINTER)
    {
        fseek(img, (long)curr_block * RFS_BLOCK_SIZE, SEEK_SET);
        fread(&dir_block, sizeof(rfs_directory_block_t), 1, img);

        for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
        {
            rfs_directory_entry_t *entry = &dir_block.entries[i];

            // Ignora entradas inválidas e ponteiros de navegação (., ..)
            if (entry->name[0] == '\0' || strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0)
            {
                continue;
            }

            // Constrói o caminho de destino concatenado
            char new_dest[1024];
            snprintf(new_dest, sizeof(new_dest), "%s/%s", dest_path, entry->name);

            // Chama a extração correta dependendo do modo
            if (entry->mode & RFS_MODE_DIRECTORY)
            {
                extract_directory(img, map_start, entry, new_dest);
            }
            else if (entry->mode & RFS_MODE_SYMLINK)
            {
                extract_symlink(img, entry, new_dest);
            }
            else
            {
                extract_file(img, map_start, entry, new_dest);
            }
        }
        curr_block = get_next_block(img, map_start, curr_block);
    }
}

// Busca uma entrada em um diretório do disco
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
    if (argc < 3 || argc > 4)
    {
        fprintf(stderr, "Uso: %s <imagem> <caminho in RFS> [caminho destino]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *img_path = argv[1];
    const char *rfs_path = argv[2];

    // Define o destino. Se omitido, usa o nome final do arquivo/diretório solicitado
    char dest_path[512];
    if (argc == 4)
    {
        strncpy(dest_path, argv[3], sizeof(dest_path) - 1);
    }
    else
    {
        if (strcmp(rfs_path, "/") == 0)
        {
            strcpy(dest_path, "./rfs_root_extracted");
        }
        else
        {
            snprintf(dest_path, sizeof(dest_path), "./%s", get_basename(rfs_path));
        }
    }

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

    rfs_directory_entry_t target_entry;

    // Trata o caso especial: usuário quer extrair o diretório raiz inteiro "/"
    if (strcmp(rfs_path, "/") == 0)
    {
        target_entry.mode = RFS_MODE_DIRECTORY;
        target_entry.starting_block = index_block.root_dir_start;
        strcpy(target_entry.name, "root");
    }
    else
    {
        // Percorre o caminho para encontrar a entrada do arquivo ou diretório
        char *path_copy = strdup(rfs_path);
        char *token = strtok(path_copy, "/");
        uint16_t current_dir_block = index_block.root_dir_start;

        while (token != NULL)
        {
            if (!find_in_directory(img, index_block.block_map_start, current_dir_block, token, &target_entry))
            {
                fprintf(stderr, "Erro: '%s' não encontrado no disco.\n", rfs_path);
                free(path_copy);
                fclose(img);
                exit(EXIT_FAILURE);
            }
            current_dir_block = target_entry.starting_block;
            token = strtok(NULL, "/");
        }
        free(path_copy);
    }
    
    // Inicia o processo de extração com base no tipo da entrada
    if (target_entry.mode & RFS_MODE_DIRECTORY)
    {
        extract_directory(img, index_block.block_map_start, &target_entry, dest_path);
    }
    else if (target_entry.mode & RFS_MODE_SYMLINK)
    {
        extract_symlink(img, &target_entry, dest_path);
    }
    else
    {
        extract_file(img, index_block.block_map_start, &target_entry, dest_path);
    }

    printf("\nExtração concluída com sucesso em: %s\n", dest_path);

    fclose(img);
    return EXIT_SUCCESS;
}