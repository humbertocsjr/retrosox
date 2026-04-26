#include "rfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    // Exige o setor de boot (obrigatório) e permite o stage 2 (opcional)
    if (argc < 3 || argc > 4)
    {
        fprintf(stderr, "Uso: %s <imagem_rfs.img> <boot_sector.bin> [stage2.bin]\n", argv[0]);
        fprintf(stderr, "Exemplo: %s disco.img boot.bin loader.bin\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];
    const char *boot_path = argv[2];
    const char *stage2_path = (argc == 4) ? argv[3] : NULL;

    FILE *img = fopen(img_path, "r+b");
    if (!img)
    {
        perror("Erro ao abrir a imagem do disco");
        return EXIT_FAILURE;
    }

    // --- 1. VALIDAÇÃO DE SEGURANÇA (Garante que é um disco RFS) ---
    rfs_index_block_t index;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&index, sizeof(rfs_index_block_t), 1, img);

    if (strncmp(index.signature, "RFS1", 4) != 0)
    {
        fprintf(stderr, "Erro de Segurança: A imagem fornecida não possui a assinatura 'RFS1' no Bloco %d.\n", RFS_LAYOUT_INDEX_BLOCK_ADDRESS);
        fprintf(stderr, "A injeção foi cancelada para evitar corrupção de dados.\n");
        fclose(img);
        return EXIT_FAILURE;
    }

    // --- 2. INJEÇÃO DO SETOR DE BOOT (BLOCO 0) ---
    FILE *f_boot = fopen(boot_path, "rb");
    if (!f_boot)
    {
        perror("Erro ao abrir o arquivo do Setor de Boot");
        fclose(img);
        return EXIT_FAILURE;
    }

    fseek(f_boot, 0, SEEK_END);
    long boot_size = ftell(f_boot);
    fseek(f_boot, 0, SEEK_SET);

    if (boot_size > RFS_BLOCK_SIZE)
    {
        fprintf(stderr, "Erro: O Setor de Boot (%ld bytes) excede o limite de %d bytes (1 bloco).\n", boot_size, RFS_BLOCK_SIZE);
        fclose(f_boot);
        fclose(img);
        return EXIT_FAILURE;
    }

    // Cria um buffer zerado, lê o bootloader e grava no bloco 0
    uint8_t boot_buffer[RFS_BLOCK_SIZE] = {0};
    fread(boot_buffer, 1, boot_size, f_boot);
    fclose(f_boot);

    fseek(img, 0, SEEK_SET); // Offset 0 = Bloco 0
    fwrite(boot_buffer, 1, RFS_BLOCK_SIZE, img);
    //printf(">> Setor de Boot injetado com sucesso (Bloco 0): %ld bytes gravados.\n", boot_size);

    // --- 3. INJEÇÃO DO STAGE 2 (BLOCOS 1 a 8) - Opcional ---
    if (stage2_path)
    {
        FILE *f_stage2 = fopen(stage2_path, "rb");
        if (!f_stage2)
        {
            perror("Erro ao abrir o arquivo do Stage 2");
            fclose(img);
            return EXIT_FAILURE;
        }

        fseek(f_stage2, 0, SEEK_END);
        long stage2_size = ftell(f_stage2);
        fseek(f_stage2, 0, SEEK_SET);

        long max_stage2_size = (RFS_LAYOUT_INDEX_BLOCK_ADDRESS - 1) * RFS_BLOCK_SIZE; // 8 * 512 = 4096 bytes

        if (stage2_size > max_stage2_size)
        {
            fprintf(stderr, "Erro: O Stage 2 (%ld bytes) excede o limite arquitetural de %ld bytes (Blocos 1 a 8).\n", stage2_size, max_stage2_size);
            fclose(f_stage2);
            fclose(img);
            return EXIT_FAILURE;
        }

        // Buffer do tamanho exato da área do stage 2
        uint8_t *stage2_buffer = calloc(max_stage2_size, 1);
        fread(stage2_buffer, 1, stage2_size, f_stage2);
        fclose(f_stage2);

        // Grava começando do Bloco 1
        fseek(img, 1 * RFS_BLOCK_SIZE, SEEK_SET);
        fwrite(stage2_buffer, 1, max_stage2_size, img);
        free(stage2_buffer);

        //printf(">> Stage 2 injetado com sucesso (Blocos 1 a %d): %ld bytes gravados.\n", RFS_LAYOUT_INDEX_BLOCK_ADDRESS - 1, stage2_size);
    }

    //printf("\nProcesso de injeção de boot concluído! O disco RFS agora é inicializável.\n");

    fclose(img);
    return EXIT_SUCCESS;
}