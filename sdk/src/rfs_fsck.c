#include "rfs.h"

// Estruturas globais para a auditoria
uint16_t *map_cache;
uint8_t *block_visits;
uint32_t total_blocks;
uint32_t errors_found = 0;
uint32_t cross_links = 0;

// Navega recursivamente na árvore de arquivos validando as cadeias de blocos
void traverse_item(FILE *img, rfs_index_block_t *idx, uint16_t start_block, const char *path, bool is_dir)
{
    if (start_block == 0 || start_block >= idx->total_blocks)
    {
        printf("  [ERRO] '%s' aponta para um bloco inicial inválido ou fora dos limites: %u\n", path, start_block);
        errors_found++;
        return;
    }

    // PASSO 1: Percorre a cadeia na FAT para este arquivo e marca as visitas
    uint16_t curr = start_block;
    int chain_length = 0;

    while (curr != RFS_LAST_BLOCK_POINTER && curr != RFS_RESERVED_POINTER && curr != RFS_FREE_POINTER)
    {
        if (curr >= idx->total_blocks)
        {
            printf("  [ERRO] '%s' tentou acessar bloco fora dos limites: %u\n", path, curr);
            errors_found++;
            break;
        }

        block_visits[curr]++;
        chain_length++;

        if (block_visits[curr] > 1)
        {
            printf("  [ERRO CRÍTICO] Cross-link (Colisão) detectado! '%s' compartilha o bloco %u com outro arquivo.\n", path, curr);
            cross_links++;
            errors_found++;
            break; // Previne loops infinitos em caso de ciclo na FAT
        }

        curr = map_cache[curr];
    }

    // PASSO 2: Se for um diretório, lê seu conteúdo e dispara a recursão para os filhos
    if (is_dir)
    {
        curr = start_block;
        rfs_directory_block_t d_block;

        // Usamos um contador de segurança para não ficar preso caso a cadeia do diretório seja cíclica
        int dir_safety_limit = chain_length > 0 ? chain_length : 1000;

        while (curr != RFS_LAST_BLOCK_POINTER && curr != RFS_RESERVED_POINTER && curr != RFS_FREE_POINTER && dir_safety_limit-- > 0)
        {
            fseek(img, (long)curr * RFS_BLOCK_SIZE, SEEK_SET);
            fread(&d_block, sizeof(rfs_directory_block_t), 1, img);

            for (int i = 0; i < RFS_DIRECTORY_ENTRIES_PER_BLOCK; i++)
            {
                rfs_directory_entry_t *ent = &d_block.entries[i];
                if (ent->name[0] != '\0')
                {
                    // Ignora as navegações pai/filho para não entrar em loop infinito
                    if (strcmp(ent->name, ".") == 0 || strcmp(ent->name, "..") == 0)
                        continue;

                    char sub_path[1024];
                    if (strcmp(path, "/") == 0)
                    {
                        snprintf(sub_path, sizeof(sub_path), "/%s", ent->name);
                    }
                    else
                    {
                        snprintf(sub_path, sizeof(sub_path), "%s/%s", path, ent->name);
                    }

                    bool sub_is_dir = (ent->mode & RFS_MODE_DIRECTORY) != 0;
                    traverse_item(img, idx, ent->starting_block, sub_path, sub_is_dir);
                }
            }
            curr = map_cache[curr];
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "Uso: %s <imagem.img> [--fix]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];
    bool fix_mode = (argc == 3 && strcmp(argv[2], "--fix") == 0);

    FILE *img = fopen(img_path, fix_mode ? "r+b" : "rb");
    if (!img)
    {
        perror("Erro ao abrir a imagem");
        return EXIT_FAILURE;
    }

    printf("Iniciando auditoria RFSck em '%s'...\n\n", img_path);

    // --- FASE 1: VALIDAÇÃO DO SUPERBLOCK ---
    rfs_index_block_t idx;
    fseek(img, RFS_LAYOUT_INDEX_BLOCK_ADDRESS * RFS_BLOCK_SIZE, SEEK_SET);
    fread(&idx, sizeof(rfs_index_block_t), 1, img);

    if (strncmp(idx.signature, "RFS1", 4) != 0)
    {
        printf("[FALHA FATAL] Assinatura do sistema inválida.\n");
        fclose(img);
        return EXIT_FAILURE;
    }

    fseek(img, 0, SEEK_END);
    uint32_t file_size_bytes = ftell(img);
    uint32_t expected_bytes = idx.total_blocks * RFS_BLOCK_SIZE;

    if (file_size_bytes < expected_bytes)
    {
        printf("[FALHA FATAL] A imagem truncada! Tamanho esperado: %u, Real: %u\n", expected_bytes, file_size_bytes);
        fclose(img);
        return EXIT_FAILURE;
    }

    total_blocks = idx.total_blocks;

    // --- FASE 2: CARREGAR A FAT (MAPA) E ALOCAR AUDITORIA ---
    map_cache = calloc(total_blocks, sizeof(uint16_t));
    block_visits = calloc(total_blocks, sizeof(uint8_t));

    fseek(img, (long)idx.block_map_start * RFS_BLOCK_SIZE, SEEK_SET);
    fread(map_cache, sizeof(uint16_t), total_blocks, img);

    // Marca áreas estruturais do sistema como "visitadas" corretamente
    for (uint32_t i = 0; i <= RFS_LAYOUT_INDEX_BLOCK_ADDRESS; i++)
    {
        block_visits[i] = 1;
        if (map_cache[i] != RFS_RESERVED_POINTER)
        {
            printf("  [ERRO] Bloco do Bootloader %u não está marcado como RESERVADO na FAT.\n", i);
            errors_found++;
        }
    }
    for (uint32_t i = idx.block_map_start; i <= idx.block_map_end; i++)
    {
        block_visits[i] = 1;
        if (map_cache[i] != RFS_RESERVED_POINTER)
        {
            printf("  [ERRO] Bloco da própria Tabela FAT %u não está marcado como RESERVADO.\n", i);
            errors_found++;
        }
    }

    // --- FASE 3: TRAVESSIA PROFUNDA DOS DIRETÓRIOS ---
    printf(">> Analisando integridade da árvore de arquivos...\n");
    traverse_item(img, &idx, idx.root_dir_start, "/", true);

    // --- FASE 4: CRUZAMENTO DE DADOS (FAT vs REALIDADE) ---
    printf("\n>> Cruzando dados estruturais com a Tabela de Alocação...\n");
    uint32_t orphans = 0;
    uint32_t fat_fatal = 0;

    for (uint32_t i = 0; i < total_blocks; i++)
    {
        // Bloco Órfão (Vazamento de espaço / Leak)
        if (block_visits[i] == 0 && map_cache[i] != RFS_FREE_POINTER)
        {
            orphans++;
            if (fix_mode)
            {
                map_cache[i] = RFS_FREE_POINTER;
                // Aplica a correção física na FAT imediatamente
                fseek(img, (long)idx.block_map_start * RFS_BLOCK_SIZE + (i * sizeof(uint16_t)), SEEK_SET);
                uint16_t zero = RFS_FREE_POINTER;
                fwrite(&zero, sizeof(uint16_t), 1, img);
            }
        }

        // Bloco sendo usado por um arquivo, mas a FAT diz que está LIVRE! (Isso corrompe dados fáceis)
        if (block_visits[i] >= 1 && map_cache[i] == RFS_FREE_POINTER)
        {
            printf("  [ERRO CRÍTICO] Bloco %u pertence a um arquivo, mas a FAT marca ele como LIVRE (Risco de Sobrescrita)!\n", i);
            fat_fatal++;
            errors_found++;
        }
    }

    // --- FASE 5: RELATÓRIO FINAL ---
    printf("\n==== RELATÓRIO RFSck ====\n");
    if (orphans > 0)
    {
        printf("- Blocos Órfãos (Vazamento): %u bloco(s) [%.2f KB perdidos].\n", orphans, (float)(orphans * RFS_BLOCK_SIZE) / 1024);
        if (fix_mode)
        {
            printf("  [✓] Corrigidos: Todos os blocos órfãos foram devolvidos ao sistema.\n");
        }
        else
        {
            printf("  [!] Dica: Execute novamente passando a flag '--fix' para recuperar esse espaço.\n");
            errors_found++;
        }
    }
    else
    {
        printf("- Nenhum vazamento de blocos detectado.\n");
    }

    if (cross_links > 0)
        printf("- Cross-links detectados: %u (Requer intervenção manual apagando um dos arquivos afetados).\n", cross_links);
    if (fat_fatal > 0)
        printf("- Falhas de Proteção FAT: %u\n", fat_fatal);

    if (errors_found == 0)
    {
        printf("\nO sistema de arquivos RFS está LIMPO e SAUDÁVEL.\n");
    }
    else
    {
        printf("\nAtenção: Foram encontrados %u erro(s) ou inconsistência(s) na imagem.\n", errors_found);
    }

    free(map_cache);
    free(block_visits);
    fclose(img);

    return (errors_found == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}