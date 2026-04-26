# Retro Sistema Operacional X

Sistema operacional para Computadores Z80 inspirado na implementação brasileira do Unix chamada SOX.

**Projeto no inicio de desenvolvimento**

## Objetivos atuais

- Implementer Write no dispositivo de Video
- Implementar minimo do IPC suficiente para enviar Write pro dispositivo de video pra imprimir na tela

## Objetivos do Projeto de Longo Prazo

- Onde for possível implementar conforme a documentação do SOX para manter um certo nível de compatibilidade
- Implantar pelo menos o nivel de API do UNIX v4
- Suportar Sistema de Arquivos do Minix pelo menos como somente leitura para um disco secundário

## Estado do Projeto

- Boot Loader [**Feito**]
- Sistema de Arquivos [**Feito**]
- Estrutura basica do projeto [**Feito**]
- Estrutura de controladores de dispositivos [**Em Planejamento**]
- Estrutura de multitarefa [**Em Desenvolvimento**]
- Controlador de expansão de memoria MegaRAM [**Em Planejamento**]
- Controlador de Video [**Em Desenvolvimento**]
- Controlador de Disco Microsol/DDX [**Em Planejamento**]
- Comunicação entre processos (Inspirado no MINIX) [**Em Planejamento**]
- Syscall Fork [**Em Planejamento**]
- Syscall Write [**Em Planejamento**]
- Syscall Read [**Em Planejamento**]

# Computadores Suportados

- MSX 1 com 64 KiB de RAM
    - Requer Drive de Disquete DDX 3.0 ou Microsol
    - Requer MegaRAM

## Implementação para MSX 1

Testado em um Sharp Hotbit com MegaRAM e uma unidade de disquete DDX 3.0.

### Mapeamento da Memória

- 0x0000 - 0x3fff: Kernel
- 0x4000 - 0x7fff: Aplicação
- 0x8000 - 0xbfff: Extensões mapeaveis pelo aplicativo
- 0xc000 - 0xffff: Buffers e dados do Kernel

# Ferramentas de Desenvolvimento

Ferramentas Externas Necessárias:

- [HC SDK para Retro Computação](https://github.com/humbertocsjr/hcsdkretro) (Requer versão v1.9R17 ou superior)

Ferramentas embutidas:

- mkrfs - Cria uma imagem formatada como Sistema de Arquivos Retro
- rfs_add - Importa um arquivo/diretorno em uma imagem
- rfs_extract - Exporta um arquivo/diretorio de uma imagem
- rfs_ls - Lista um diretorio em uma imagem
- rfs_df - Exibe o espaço disponível em uma imagem
- rfs_chmod - Altera permissão de um item em uma imagem
- rfs_mkdir - Cria um diretorio em uma imagem
- rfs_tree - Exibe a estrutura de uma imagem em forma de arvore
- rfs_boot - Injeta o bootloader com estagio 1 e 2
- rfs_chgrp - Altera o grupo de um item
- rfs_chown - Altera o usuario de um item
- rfs_fsck - Verifica o estado do disco
- rfs_info - Exibe informações do Sistema de Arquivos
- rfs_ln - Cria um link simbolico
- rfs_rm - Remove um arquivo/diretorio(recursivamente)
- rfs_mount - Mounta uma imagem usando o FUSE (Opcional)

## Instalando as ferramentas Embutidas

Para instalar no sistema atual (compatível com POSIX) as ferramentas de desenvolvimento embutidas nesse projeto basta executar os comando abaixo:

```sh
make sdk # compila as ferramnetas principais
make fuse # [OPCIONAL] compila o rfs_mount (REQUER FUSE/MACFUSE INSTALADO)
sudo make install # instala no sistema atual
```