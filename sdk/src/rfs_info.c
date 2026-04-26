#include "rfs.h"
#include <stdlib.h>

// Formata bytes para uma string legível (KB, MB) com 2 casas decimais
void format_size(uint32_t bytes, char *buffer)
{
    if (bytes >= 1024 * 1024)
    {
        sprintf(buffer, "%.2f MB", (float)bytes / (1024 * 1024));
    }
    else if (bytes >= 1024)
    {
        sprintf(buffer, "%.2f KB", (float)bytes / 1024);
    }
    else
    {
        sprintf(buffer, "%u Bytes", bytes);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <imagem.img>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];

    FILE *img = fopen(img_path, "rb");
    if (!img)
    {
        perror("Erro ao abrir a imagem do disco");
        return EXIT_FAILURE;
    }

    // 1. Lê o Bloco Índice (Superblock)
    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    // 2. Valida a Assinatura
    if (strncmp(index.signature, "RFS1", 4) != 0)
    {
        fprintf(stderr, "Erro: O arquivo '%s' não é uma imagem RFS válida (Assinatura Ausente).\n", img_path);
        fclose(img);
        return EXIT_FAILURE;
    }

    // 3. Auditoria de Espaço Lendo o Mapa de Blocos (FAT)
    uint32_t free_blocks = 0;
    uint32_t struct_blocks = 0;
    uint32_t content_blocks = 0;

    uint16_t *map_cache = malloc(index.total_blocks * sizeof(uint16_t));
    if (!map_cache)
    {
        fprintf(stderr, "Erro de memória ao alocar buffer para o mapa.\n");
        fclose(img);
        return EXIT_FAILURE;
    }

    fseek(img, (long)index.block_map_start * RFS_BLOCK_SIZE, SEEK_SET);
    fread(map_cache, sizeof(uint16_t), index.total_blocks, img);

    for (uint32_t i = 0; i < index.total_blocks; i++)
    {
        if (map_cache[i] == RFS_FREE_POINTER)
        {
            free_blocks++;
        }
        else
        {
            // Se o bloco está na área do Boot/Superblock OU na área da Tabela FAT
            if (i <= RFS_LAYOUT_INDEX_BLOCK_ADDRESS || (i >= index.block_map_start && i <= index.block_map_end))
            {
                struct_blocks++;
            }
            else
            {
                // Qualquer outro bloco em uso é conteúdo (Arquivos ou Diretórios)
                content_blocks++;
            }
        }
    }
    free(map_cache);

    uint32_t used_blocks = struct_blocks + content_blocks;

    // 4. Cálculos e Strings Base
    char str_total[32], str_free[32], str_used[32], str_struct[32], str_content[32], str_map_size[32];
    format_size((uint32_t)index.total_blocks * RFS_BLOCK_SIZE, str_total);
    format_size(free_blocks * RFS_BLOCK_SIZE, str_free);
    format_size(used_blocks * RFS_BLOCK_SIZE, str_used);
    format_size(struct_blocks * RFS_BLOCK_SIZE, str_struct);
    format_size(content_blocks * RFS_BLOCK_SIZE, str_content);
    format_size((uint32_t)index.block_map_blocks * RFS_BLOCK_SIZE, str_map_size);

    float pct_used = ((float)used_blocks / index.total_blocks) * 100.0;
    float pct_struct = ((float)struct_blocks / index.total_blocks) * 100.0;
    float pct_content = ((float)content_blocks / index.total_blocks) * 100.0;

    // 5. Impressão do Relatório
    printf("==================================================\n");
    printf("        INFORMAÇÕES DO SISTEMA RFS (RFS_INFO)       \n");
    printf("==================================================\n");

    printf("\n[ Arquitetura Física ]\n");
    printf("  Caminho da Imagem : %s\n", img_path);
    printf("  Assinatura        : %.4s\n", index.signature);
    printf("  Tamanho do Bloco  : %d Bytes\n", RFS_BLOCK_SIZE);
    printf("  Tamanho Total     : %s\n", str_total);
    printf("  Total de Blocos   : %u\n", index.total_blocks);

    printf("\n[ Geometria do Disco (CHS) ]\n");
    printf("  Cilindros         : %u\n", index.geometry_cylinders);
    printf("  Cabeças (Heads)   : %u\n", index.geometry_heads);
    printf("  Setores p/ Trilha : %u\n", index.geometry_sectors);
    printf("  Setores p/ Cilind.: %u\n", index.geometry_sectors_per_cylinder);

    printf("\n[ Estrutura Lógica ]\n");
    printf("  Bootloader        : Blocos 0 a %d\n", RFS_LAYOUT_INDEX_BLOCK_ADDRESS - 1);
    printf("  Superblock (Index): Bloco %d\n", RFS_LAYOUT_INDEX_BLOCK_ADDRESS);
    printf("  Início do Mapa    : Bloco %u\n", index.block_map_start);
    printf("  Fim do Mapa       : Bloco %u\n", index.block_map_end);
    printf("  Diretório Raiz (/): Bloco %u\n", index.root_dir_start);

    printf("\n[ Utilização de Espaço ]\n");
    printf("  Espaço Livre      : %s (%u blocos)\n", str_free, free_blocks);
    printf("  Espaço Total Usado: %s (%u blocos) [%.1f%%]\n", str_used, used_blocks, pct_used);
    printf("    ├─ Estruturas   : %s (%u blocos) [%.1f%%]\n", str_struct, struct_blocks, pct_struct);
    printf("    └─ Conteúdo     : %s (%u blocos) [%.1f%%]\n", str_content, content_blocks, pct_content);

    printf("==================================================\n");

    fclose(img);
    return EXIT_SUCCESS;
}