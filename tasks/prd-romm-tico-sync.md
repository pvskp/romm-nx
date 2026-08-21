# PRD: romm-nx como sincronizador RomM ⇄ Tico

## 1. Introdução / Visão geral

O **romm-nx** hoje é um cliente de download de ROMs do [RomM](https://github.com/rommapp/romm) para Nintendo Switch: ele navega a biblioteca do servidor, mostra covers e baixa jogos para `sdmc:/romm-nx/roms/<sistema>/`. (Nota: este documento também referencia variantes de arquitetura nos branches `feature/saves` e `feature/multi-system-support`.)

Esta proposta **muda o propósito** do app: ele deixa de ser "apenas baixar ROMs" e passa a ser um **sincronizador entre o servidor RomM e o frontend [Tico](https://tico-sw.github.io/)**, operado **por jogo**, diretamente na tela de detalhes de cada jogo.

O objetivo é que o usuário, ao abrir um jogo, possa sincronizar **na pasta correta do Tico**:
1. A **ROM** — na pasta `roms/<plataforma>/` associada ao emulador no Tico.
2. Os **saves** — ida e volta (baixar ROM saves do servidor E subir saves locais).
3. As **covers** — para que o Tico não precise de configuração manual de assets.

Os arquivos devem ir para **`sdmc:/tico/`** (a estrutura real do Tico), não mais para `sdmc:/romm-nx/`.

## 2. Objetivos

- Sincronizar um jogo por vez a partir do detail view, com 3 etapas claras: ROM, saves, cover.
- Entregar ROMs na pasta de ROMs do Tico: `sdmc:/tico/roms/<plataforma-tico>/`.
- Sincronizar saves bidirecionalmente entre `sdmc:/tico/saves/<plataforma-tico>/` (arquivos do Tico) e o RomM (`/api/saves*`).
- Colocar a cover do jogo em `sdmc:/tico/assets/covers/<plataforma-tico>/`.
- Resolver conflitos de forma segura e explícita, **sem perder dados**.
- Mapear corretamente os nomes de plataforma e extensões de save entre RomM e Tico.

## 3. User Stories

### US-001: Configurar caminho base do Tico
**Description:** Como usuário, quero que o app saiba onde o Tico está no SD, para que ele grave tudo dentro de `sdmc:/tico/`.

**Acceptance Criteria:**
- [ ] `ConfigManager` expõe caminhos resolvidos para ROOMs, saves e covers do Tico.
- [ ] Os caminhos são derivados de um único "tico base dir" (ex.: `sdmc:/tico/`).
- [ ] Os caminhos terminam em `/` e usam os nomes de plataforma do Tico (não os slugs RomM).
- [ ] Compila com `make` sem novos avisos `-Wall`.

### US-002: Mapear plataformas RomM → Tico
**Description:** Como usuário, quero que um jogo do RomM caia na pasta certa no Tico, mesmo quando o nome da plataforma difere entre os dois.

**Acceptance Criteria:**
- [ ] Existe um SlugMap central (RomM slug → pasta Tico): `sms→master-system`, `gamegear→game-gear`, `ngc→gc`, `segacd→sega-cd`.
- [ ] Plataformas com nome igual passam sem alteração. As **20 plataformas do Tico suportadas na v1**: `3ds`, `atomiswave`, `dc`, `fbneo`, `game-gear`, `gb`, `gba`, `gbc`, `gc`, `genesis`, `master-system`, `n64`, `naomi`, `nes`, `psp`, `psx`, `saturn`, `sega-cd`, `snes`, `wii`.
- [ ] O SlugMap é consultado em um único ponto (ex.: `ResolveTicoPlatformSlug()`), reutilizado por ROM/save/cover.
- [ ] Slug desconhecido cai em fallback (slug RomM usado como pasta, não trava).
- [ ] Testável em unidade manual (compilar e rodar no switch ou stub).

### US-003: Mapear extensão de save por emulador
**Description:** Como usuário, quero que saves baixados do RomM cheguem com a extensão que o emulador do Tico espera (não `.srm` de forma genérica).

**Acceptance Criteria:**
- [ ] Existe uma tabela de tradução de extensão por plataforma/emulador do Tico.
- [ ] Ex.: GBA/GBC/SNES/GENESIS/SMS `.srm`→`.sav`; N64 `.srm`→`.fla` (memory pak) e `.srm`→`.mpk` conforme o tipo.
- [ ] O nome do arquivo no Tico usa o **basename da ROM** + extensão traduzida, **sem** o sufixo `[timestamp]` do RomM.
- [ ] A tradução é centralizada (um só lugar para consulta/mock).
- [ ] Existe uma **tabela fixa de emuladores por plataforma** (nome do core do Tico) usada como `emulator` no `POST /api/saves`, confirmada com os cores reais do Tico:

| Plataforma (Tico) | Core `emulator` |
|---|---|
| gb / gbc | `gambatte` |
| gba | `mgba` |
| snes | `snes9x` |
| n64 | `mupen64plus` |
| genesis / master-system | `genesis_plus_gx` |
| psp | `ppsspp` |
| psx | `pcsx_rearmed` |

### US-003b: Usar emulator correto no upload
**Description:** Como usuário, quero que o save enviado ao RomM seja guardado na pasta do emulador/core certo no servidor.

**Acceptance Criteria:**
- [ ] O `emulator` enviado em `POST /api/saves` vem da tabela fixa plataforma→core (US-003).
- [ ] A tabela cobre as plataformas do Tico suportadas nesta versão **que têm core** (gb, gbc, gba, snes, n64, genesis, master-system, psp, psx).
- [ ] Plataformas **sem core** na v1 (3ds, atomiswave, dc, fbneo, game-gear, gc, naomi, nes, saturn, sega-cd, wii) não fazem upload de save — a etapa de saves é marcada como "não suportada" nesses casos.
- [ ] Se não houver entrada para uma plataforma, envia sem `emulator` (campo opcional) e não falha.

### US-004: Baixar ROM do jogo na pasta do Tico
**Description:** Como usuário, quero tocar em "sincronizar" num jogo e ter a ROM na pasta `roms/<plataforma>/` do Tico, sem duplicar trabalho se ela já está lá.

**Acceptance Criteria:**
- [ ] O download reutiliza o pipeline atual do `DownloadManager` (download `.part` → validação → rename).
- [ ] Se a ROM já existe no Tico com o mesmo **tamanho** → skip (sem re-download), marcado como "já presente".
- [ ] Se a ROM já existe com tamanho **diferente** → sobrescreve (baixa de novo).
- [ ] Erro de rede / espaço partido deixa a ROM em estado de falha visível.
- [ ] Continua a respeitar a whitelist de extensão e o limite de caminho do app.

### US-005: Baixar saves do servidor para o Tico
**Description:** Como usuário, quero baixar do RomM o save mais recente de um jogo para a pasta de saves do Tico.

**Acceptance Criteria:**
- [ ] Usa `GET /api/saves?rom_id=<id>` (já implementado no branch `feature/saves`).
- [ ] Escolhe a versão mais recente por `updated_at` (nem sempre a primeira da lista).
- [ ] Baixa via `GET /api/saves/{id}/content` para `sdmc:/tico/saves/<plataforma>/<basename-rom>.<ext-tico>`.
- [ ] Remove o sufixo de data/timestamp do nome do servidor (ex.: `Invader (Europe) [2026-...].srm` → `Invader (Europe).sav`).
- [ ] Não clobberia um save local mais novo sem passar pelo fluxo de conflito (US-007).

### US-006: Enviar saves locais para o servidor
**Description:** Como usuário, quero subir meus saves do Tico para o RomM para não perder progresso.

**Acceptance Criteria:**
- [ ] `HttpClient` possui suporte a upload `multipart/form-data` (via `curl_mime`).
- [ ] Chama `POST /api/saves?rom_id=<id>&emulator=<core da tabela>&overwrite=<bool>` com `saveFile`. `screenshotFile` fica de fora nesta versão.
- [ ] Após upload bem-sucedido, o save passa a existir no servidor (resposta `SaveSchema`).
- [ ] Falha de upload apresenta erro na UI e não marca falso sucesso.

### US-007: Prompt de conflito de save
**Description:** Como usuário, quando o save local e o do servidor divergirem, quero decidir o que fazer — não perder progresso.

**Acceptance Criteria:**
- [ ] A detecção de divergência **não** compara `mtime` local contra `updated_at` do servidor diretamente — em CFW o relógio pode estar errado.
- [ ] O app usa o **`sync_state.json`** (US-010) como referência: grava o `updated_at` do servidor e o fingerprint do arquivo local (tamanho **+ hash curto**) no momento de cada sync.
- [ ] Conflito = servidor mudou (`updated_at` mais novo que o gravado) **e** local mudou (tamanho **ou hash curto** diferente do gravado) desde o último sync.
- [ ] Se **só um** lado mudou desde o último sync, sincroniza nessa direção sem prompt.
- [ ] **Primeira sincronização do jogo** (sem registro em `sync_state.json`): nunca sobrescreve nada sem prompt. Se o servidor tem save → baixa; se só o local existe → sobe; se ambos existem → prompt.
- [ ] Se os dois mudaram, abre prompt: "Sobrescrever local", "Sobrescrever servidor", "Pular".
- [ ] A tela de prompt mostra as duas datas para decisão informada.
- [ ] A decisão vale só para aquele save (sem "aplicar a todos" nesta versão).
- [ ] Cancelar/fechar o prompt = pular.

### US-010: Registrar estado de sync em arquivo persistente
**Description:** Como usuário, quero que o app se lembre do que já foi sincronizado, mesmo que o relógio do Switch esteja desatualizado, para detectar conflitos de save com segurança.

**Acceptance Criteria:**
- [ ] Existe um arquivo persistente `sdmc:/switch/romm-nx/sync_state.json` (mesmo padrão de `installed_index.json`).
- [ ] Para cada jogo, grava: rom_id, plataforma, caminho da ROM, e o save sincronizado (id servidor, `updated_at`, e fingerprint do arquivo local: tamanho + hash curto).
- [ ] É o estado usado na US-007 para saber o que mudou de cada lado.
- [ ] É atualizado ao final de cada sync bem-sucedido (e no conflito resolvido).
- [ ] Não depende do relógio do sistema local para comparações (usa timestamps do servidor + fingerprint local).

### US-008: Baixar a cover do jogo para o Tico
**Description:** Como usuário, quero que a cover usada pelo RomM apareça no Tico automÁticamente.

**Acceptance Criteria:**
- [ ] Usa `path_cover_large` (ou o cache de covers já existente) como fonte.
- [ ] Salva em `sdmc:/tico/assets/covers/<plataforma-tico>/<basename-rom>.jpg`.
- [ ] Se já existe cover com o mesmo tamanho → skip.
- [ ] Não gera backgrounds, icons ou logos (fora de escopo).

### US-009: Modal de sync disparado por botão
**Description:** Como usuário, quero apertar um botão no detail view e ver o resultado de cada etapa do sync (ROM, saves, cover).

**Acceptance Criteria:**
- [ ] Um **botão** no detail view dispara um **modal de sync** (não um tab fixo).
- [ ] O modal lista as 3 etapas com estados: pendente, ok, pulado, erro.
- [ ] Cada etapa mostra detalhe útil (ex.: caminho no Tico, nome/versão do save, mensagem de erro).
- [ ] A tela não trava: o sync roda em thread, o UI atualiza via polling (padrão já usado para covers/saves).
- [ ] O modal pode ser fechado e reaberto; reusa `sync_state.json` (US-010) para não repetir trabalho.

## 4. Requisitos Funcionais

- `FR-1`: `ConfigManager` resolve caminhos do Tico: `GetTicoRomPath(slug)`, `GetTicoSavePath(slug)`, `GetTicoCoverPath(slug)` — todos sob `sdmc:/tico/`.
- `FR-1.1`: O "tico base dir" é configurável no `config.json` (default `sdmc:/tico/`), como já é feito para `romms_base_dir`.
- `FR-2`: SlugMap central RomM→Tico com fallback seguro (descrito em US-002).
- `FR-3`: Tabela de extensão de save por plataforma/emulador (descrita em US-003).
- `FR-3.1`: Tabela fixa plataforma→core do Tico para o campo `emulator` do upload (US-003b). Plataformas sem core → etapa de save marcada "não suportada".
- `FR-4`: Botão de "Sincronizar" no detail view dispara um modal que roda, em sequência: ROM → saves (baixar+subir) → cover. Jogos com mais de um arquivo (multi-disco) são marcados como "não suportado" e pulados.
- `FR-5`: Compare-por-tamanho decide skip/sobrescrever para ROM e cover.
- `FR-6`: Detecção de conflito de save usa o `sync_state.json` (US-010) — servidor `updated_at` × fingerprint local gravado (tamanho + hash curto); divergência abre prompt (US-007). Primeira sincronização: nunca sobrescrever sem prompt.
- `FR-7`: `HttpClient` ganha `uploadMultipart()` (POST com `curl_mime`).
- `FR-8`: Nomes no Tico: `<basename da ROM>` + extensão traduzida; sufixo `[timestamp]` do RomM é removido.
- `FR-9`: Erros de qualquer etapa são capturados como strings legíveis e exibidos na tela de sync sem crash (`-fno-exceptions`).
- `FR-10`: Toda escrita no SD usa o padrão `.part` → validação → rename do `DownloadManager` para evitar arquivos corrompidos em queda de energia.
- `FR-11`: `SyncManager` gerencia e persiste o estado por jogo em `sync_state.json` (carregar/salvar/atualizar no final de cada etapa).
- `FR-12`: Fingerprint do arquivo local no `sync_state` = tamanho + hash curto (custo baixo, save tem poucos KB).

## 5. Não-Objetivos (fora de escopo)

- Backgrounds, icons e logos para o Tico (apenas covers).
- Geração de arte (ex.: PNG a partir de cover).
- Sync em massa / "aplicar a todos os jogos".
- Sincronização de save states (`states/`) ou cheats.
- Launcher / abrir jogos no Tico.
- Decomprimir arquivos compactados (ex.: `.7z` → `.srm` interno) — a sincronização trata o arquivo como for servido.
- **Jogos multi-disco** (ex.: PS1 com vários `.bin`/`.chd` + `.m3u`) — fora de escopo nesta versão; se o jogo tiver mais de um arquivo, a etapa de ROM é marcada como "não suportada" e segue sem baixar.
- Upload de `screenshotFile` no `POST /api/saves` (fica para versão futura).
- Hash/checksum para comparação de ROM (uso apenas de tamanho).

## 6. Considerações de Design

- **Caminhos**: manter um único "tico base dir" no config (default `sdmc:/tico/`, editável); nunca espalhar magic strings de caminho pelo código (hoje existem vários `sdmc:/switch/...` hard-coded — usar helpers).
- **Reuso**: os branches `feature/saves` (download save) e `feature/multi-system-support` (DownloadManager v2) já têm boa parte; o trabalho é redirecionar caminhos e juntar as peças.
- **UI**: botão "Sincronizar" no detail view abre um modal de sync com progresso por etapa, reutilizando o padrão de polling assíncrono.
- **Prompt de conflito**: modal simples com 3 opções + mostra as duas datas (local gravado × servidor).

## 7. Considerações Técnicas

- **Plataforma**: libnx + Borealis (Plutonium), C++20, `-fno-rtti -fno-exceptions`, compilado com `make` (devkitpro).
- **Rede**: `HttpClient` com pool de curl e prioridades. Falta capacidade de upload multipart (`curl_mime`) — necessário para `POST /api/saves`.
- **API RomM relevante**:
  - `GET  /api/saves?rom_id=` — lista saves do usuário (`SaveSchema`, inclui `updated_at`, `emulator`, `slot`, `file_name`).
  - `GET  /api/saves/{id}/content` — baixa o arquivo.
  - `POST /api/saves?rom_id=&emulator=&overwrite=` — upload (`multipart/form-data`, campo `saveFile`).
  - `GET/POST /api/roms/.../files/content/...` — ROM (já usado).
  - Covers vêm de `path_cover_small/large` (`/assets/romm/resources/...`).
- **Persistência local**: além de `installed_index.json`, um `sync_state.json` por app guarda o que já foi sincronizado por jogo (US-010) — usado para detectar conflito de save sem depender do relógio local do CFW.
- **Desempenho**: comparação por tamanho é barata (stat); não há hash de ROM — custo zero extra.
- **Teste**: o build exige devkitpro; testes manuais em hardware. Stubs de `ConfigManager`/SlugMap/tabela de cores ajudam a validar lógica no host.

## 8. Métricas de Sucesso

- Usuário consegue, a partir de um jogo na biblioteca, ter ROM + save + cover prontos no Tico em **uma ação de sync** (um clique no botão).
- Nenhum dado é perdido em conflito de save (sempre há opção de pular / decidir).
- ROMs e covers não são re-baixadas quando já presentes (comparação de tamanho correta).
- Save do servidor chega no Tico com **extensão e nome corretos** para o emulador rodar (sem "save não aparece no Tico").
- Saves sobem para a pasta do **emulador correto** no servidor (campo `emulator` mapeado por plataforma).
- Conflito de save detectado corretamente mesmo com relógio do Switch errado (via `sync_state.json`).
- Nenhum novo `-Wall` warning ou regressão no download de ROM atual.

## 9. Perguntas em Aberto

- Nenhuma pendente de decisão de produto para a v1; restam validações técnicas durante a implementação (ex.: extensão exata de save por core — `.sav` vs `.fla`/`.mpk` no n64 — confirmada contra o Tico na prática).
