#include "rfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Preenche uma struct rfs_datetime_t com o horário atual
rfs_datetime_t get_current_rfs_time() {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    rfs_datetime_t dt = {
        .year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday,
        .hour = tm.tm_hour, .minute = tm.tm_min, .second = tm.tm_sec, .timezone_offset = 0
    };
    return dt;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <imagem.img> <cilindros> <cabecas> <setores_por_trilha>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s disco.img 80 2 18\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];
    uint32_t cylinders = atoi(argv[2]);
    uint32_t heads = atoi(argv[3]);
    uint32_t sectors = atoi(argv[4]);

    // --- 1. CÁLCULO E VALIDAÇÃO DE GEOMETRIA ---
    uint32_t sectors_per_cylinder = heads * sectors;
    uint32_t total_blocks = cylinders * heads * sectors;

    // Limite arquitetural (ponteiros de 16 bits = max 65535)
    if (total_blocks > 65534) {
        fprintf(stderr, "Erro: A geometria excede o limite arquitetural de 65534 blocos.\n");
        return EXIT_FAILURE;
    }
    if (total_blocks < 20) {
        fprintf(stderr, "Erro: Geometria mínima requerida é de 20 blocos.\n");
        return EXIT_FAILURE;
    }

    // Cálculos da Tabela de Alocação (FAT)
    uint32_t map_entries_size_bytes = total_blocks * sizeof(uint16_t);
    uint16_t map_blocks = (map_entries_size_bytes + RFS_BLOCK_SIZE - 1) / RFS_BLOCK_SIZE; 
    
    uint32_t pointers_per_block = RFS_BLOCK_SIZE / sizeof(uint16_t);
    uint32_t total_map_slots = map_blocks * pointers_per_block;

    // Alinhamento geométrico do mapa no MEIO do disco (otimização de Seek Time)
    uint32_t middle_block = total_blocks / 2;
    uint32_t map_start = middle_block - (middle_block % sectors_per_cylinder);
    
    // Evita colisão com o bootloader caso o disco seja minúsculo
    if (map_start <= RFS_LAYOUT_INDEX_BLOCK_ADDRESS) {
        map_start = sectors_per_cylinder; 
        if (map_start <= RFS_LAYOUT_INDEX_BLOCK_ADDRESS) {
            map_start = RFS_LAYOUT_INDEX_BLOCK_ADDRESS + 1;
        }
    }

    uint16_t map_end = map_start + map_blocks - 1;
    uint16_t root_dir_start = map_end + 1; // A Raiz fica imediatamente após o mapa

    // --- 2. CRIAÇÃO FÍSICA DO DISCO E ZERO-FILL ---
    //printf("Formatando disco RFS: %s\n", img_path);
    //printf("Total de Blocos: %u (%.2f MB)\n", total_blocks, (float)(total_blocks * RFS_BLOCK_SIZE) / (1024 * 1024));
    
    FILE *img = fopen(img_path, "wb"); 
    if (!img) {
        perror("Erro ao criar o arquivo de imagem");
        return EXIT_FAILURE;
    }

    // Escreve zeros em TODO o disco (Low-Level Format)
    uint8_t zero_block[RFS_BLOCK_SIZE] = {0};
    for (uint32_t i = 0; i < total_blocks; i++) {
        fwrite(zero_block, 1, RFS_BLOCK_SIZE, img);
    }

    // --- 3. CONSTRUÇÃO DO MAPA DE BLOCOS (FAT) ---
    // Criamos o mapa com o tamanho FÍSICO exato que ele ocupará
    uint16_t *map = calloc(total_map_slots, sizeof(uint16_t));
    
    // Marca do bloco 0 ao 9 como reservados (Bootloader e Superblock)
    for (uint32_t i = 0; i <= RFS_LAYOUT_INDEX_BLOCK_ADDRESS; i++) {
        map[i] = RFS_RESERVED_POINTER;
    }
    
    // Marca o espaço que o próprio mapa ocupa fisicamente no meio do disco
    for (uint32_t i = map_start; i <= map_end; i++) {
        map[i] = RFS_RESERVED_POINTER;
    }
    
    // Marca o bloco do Diretório Raiz
    map[root_dir_start] = RFS_LAST_BLOCK_POINTER;

    // SEGURANÇA: Marca os "ponteiros fantasmas" (além do tamanho do disco) como reservados
    for (uint32_t i = total_blocks; i < total_map_slots; i++) {
        map[i] = RFS_RESERVED_POINTER;
    }

    // Grava a Tabela FAT no disco
    fseek(img, (long)map_start * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(map, sizeof(uint16_t), total_map_slots, img);
    free(map);

    // --- 4. CONSTRUÇÃO DO DIRETÓRIO RAIZ (ROOT) ---
    rfs_directory_block_t root_block = {0};
    rfs_datetime_t now = get_current_rfs_time();

    // Entrada "."
    strcpy(root_block.entries[0].name, ".");
    root_block.entries[0].mode = RFS_MODE_DIRECTORY | 0755;
    root_block.entries[0].starting_block = root_dir_start;
    root_block.entries[0].creation_time = now;
    root_block.entries[0].modification_time = now;
    root_block.entries[0].user_id = 0; // Dono Root
    root_block.entries[0].group_id = 0;

    // Entrada ".."
    strcpy(root_block.entries[1].name, "..");
    root_block.entries[1].mode = RFS_MODE_DIRECTORY | 0755;
    root_block.entries[1].starting_block = root_dir_start;
    root_block.entries[1].creation_time = now;
    root_block.entries[1].modification_time = now;
    root_block.entries[1].user_id = 0;
    root_block.entries[1].group_id = 0;

    fseek(img, (long)root_dir_start * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&root_block, sizeof(rfs_directory_block_t), 1, img);

    // --- 5. CONSTRUÇÃO DO BLOCO ÍNDICE (SUPERBLOCK) ---
    rfs_index_block_t index = {0};
    strncpy(index.signature, "RFS1", 4);
    index.total_blocks = total_blocks;
    index.block_map_start = map_start;
    index.block_map_blocks = map_blocks;
    index.block_map_end = map_end;
    index.root_dir_start = root_dir_start;
    index.geometry_cylinders = cylinders;
    index.geometry_heads = heads;
    index.geometry_sectors = sectors;
    index.geometry_sectors_per_cylinder = sectors_per_cylinder;

    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&index, sizeof(rfs_index_block_t), 1, img);

    fclose(img);

    // Relatório visual final
    //printf("Formatação RFS concluída com sucesso!\n");
    //printf("--------------------------------------\n");
    //printf("Superblock: Bloco %d\n", RFS_LAYOUT_INDEX_BLOCK_ADDRESS);
    //printf("Mapa FAT  : Bloco %d a %d (%d blocos)\n", map_start, map_end, map_blocks);
    //printf("Dir Raiz  : Bloco %d\n", root_dir_start);
    //printf("Segurança : %u ponteiros fantasmas bloqueados.\n", total_map_slots - total_blocks);
    //printf("--------------------------------------\n");

    return EXIT_SUCCESS;
}