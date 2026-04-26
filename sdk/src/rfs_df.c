#include "rfs.h"

// Lê o valor de um ponteiro específico no mapa de alocação
uint16_t get_map_entry(FILE *img, uint16_t map_start, uint16_t block_index)
{
    uint16_t ptr;
    long offset = (long)map_start * RFS_BLOCK_SIZE + (block_index * sizeof(uint16_t));
    fseek(img, offset, SEEK_SET);
    fread(&ptr, sizeof(uint16_t), 1, img);
    return ptr;
}

// Formata bytes para uma string legível (KB, MB)
void format_size(uint32_t bytes, char *buffer)
{
    if (bytes >= 1024 * 1024)
    {
        sprintf(buffer, "%.1f MB", (float)bytes / (1024 * 1024));
    }
    else if (bytes >= 1024)
    {
        sprintf(buffer, "%.1f KB", (float)bytes / 1024);
    }
    else
    {
        sprintf(buffer, "%u B", bytes);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <imagem>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *img_path = argv[1];

    FILE *img = fopen(img_path, "rb");
    if (!img)
    {
        perror("Erro ao abrir a imagem do disco");
        exit(EXIT_FAILURE);
    }

    // Lendo o Bloco Índice (Superblock)
    rfs_index_block_t index_block;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index_block, sizeof(rfs_index_block_t), 1, img);

    // Valida a Assinatura
    if (strncmp(index_block.signature, "RFS1", 4) != 0)
    {
        fprintf(stderr, "Erro: Assinatura RFS1 inválida.\n");
        fclose(img);
        exit(EXIT_FAILURE);
    }

    uint16_t total_blocks = index_block.total_blocks;
    uint16_t free_blocks = 0;
    uint16_t used_blocks = 0;

    // Percorre todo o mapa de blocos (do bloco 0 até o total_blocks - 1)
    for (uint16_t i = 0; i < total_blocks; i++)
    {
        uint16_t map_val = get_map_entry(img, index_block.block_map_start, i);

        // Se o valor for 0x0000, o bloco está livre. Caso contrário, está em uso.
        if (map_val == RFS_FREE_POINTER)
        {
            free_blocks++;
        }
        else
        {
            used_blocks++;
        }
    }

    // Calcula os valores em bytes
    uint32_t total_bytes = (uint32_t)total_blocks * RFS_BLOCK_SIZE;
    uint32_t free_bytes = (uint32_t)free_blocks * RFS_BLOCK_SIZE;
    uint32_t used_bytes = (uint32_t)used_blocks * RFS_BLOCK_SIZE;

    // Calcula a porcentagem de uso
    float use_percentage = 0.0;
    if (total_blocks > 0)
    {
        use_percentage = ((float)used_blocks / total_blocks) * 100.0;
    }

    // Prepara strings formatadas
    char str_total[32], str_used[32], str_free[32];
    format_size(total_bytes, str_total);
    format_size(used_bytes, str_used);
    format_size(free_bytes, str_free);

    // Exibe o resultado no estilo do comando "df" do Unix
    printf("%-20s %-15s %-15s %-15s %-5s\n", "Sistema", "Total", "Usado", "Livre", "Uso%");
    printf("%-20s %-15s %-15s %-15s %-5.1f%%\n",
           img_path,
           str_total,
           str_used,
           str_free,
           use_percentage);

    // Exibe também informações técnicas de baixo nível
    printf("\n--- Detalhes Físicos (Blocos de %d bytes) ---\n", RFS_BLOCK_SIZE);
    printf("Total de Blocos: %u\n", total_blocks);
    printf("Blocos Usados:   %u\n", used_blocks);
    printf("Blocos Livres:   %u\n", free_blocks);

    fclose(img);
    return EXIT_SUCCESS;
}