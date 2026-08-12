# Plano de Implementação — romm-nx como sincronizador RomM ⇄ Tico

Documento de execução para um agente implementar. Baseado no PRD `tasks/prd-romm-tico-sync.md` (ler antes). Segue cada etapa com arquivos, funções e comportamentos exatos.

---

## 0. Contexto & estado atual

- **App**: homebrew Switch (libnx + Borealis dos vendedores Plutonium), C++20, `-fno-rtti -fno-exceptions`. Build: `make` na raiz (devkitpro).
- **Branch atual**: `feature/multi-system-support` (HEAD `36e7509`). Existem worktrees:
  - `wt-saves/` (branch `feature/saves`) — com **mudanças não-commitadas** que adicionam download de saves: `SaveEntry`, `fetchSavesAsync`, `jsonParseSaveItems`, `DownloadManager::DownloadSave`, tela/URL de saves. **Portar essas peças** (ver §2).
  - `wt-internal-fb/` (branch `feature/internal-filebrowser`) — file browser, não relevante aqui.
- **Rede**: `HttpClient` (source/navigation/HttpClient.cpp) com pool de curl (4 workers, 3 prioridades). Só GET e POST JSON. **Não faz multipart** — precisa adicionar (ver §6).
- **Caminhos hoje**: `ConfigManager::GetRomPath()` → `<roms_base_dir>roms/<slug>/`, default `sdmc:/romm-nx/`. Covers/cache em `sdmc:/switch/romm-nx/cache/covers/`.
- **API RomM**: já usados `GET /api/platforms`, `GET /api/roms?platform_ids=`, `GET /api/roms/{id}`, `GET /api/roms/{file_id}/files/content/{name}`. Saves: `GET /api/saves?rom_id=`, `GET /api/saves/{id}/content`, `POST /api/saves` (multipart).

---

## 1. Decisões de produto (fixas, não reabrir)

- **Plataformas v1 (20, diretório do Tico)**: `3ds`, `atomiswave`, `dc`, `fbneo`, `game-gear`, `gb`, `gba`, `gbc`, `gc`, `genesis`, `master-system`, `n64`, `naomi`, `nes`, `psp`, `psx`, `saturn`, `sega-cd`, `snes`, `wii`.
- **SlugMap RomM→Tico**: `sms→master-system`, `gamegear→game-gear`, `ngc→gc`, `segacd→sega-cd`; demais iguais.
- **Emulador (nome do core) para upload de save** — tabela fixa:

| Plataforma Tico | core `emulator` |
|---|---|
| gb, gbc | `gambatte` |
| gba | `mgba` |
| snes | `snes9x` |
| n64 | `mupen64plus` |
| genesis, master-system | `genesis_plus_gx` |
| psp | `ppsspp` |
| psx | `pcsx_rearmed` |

  Plataformas **sem core** (3ds, atomiswave, dc, fbneo, game-gear, gc, naomi, nes, saturn, sega-cd, wii) → etapa de save marcada **"não suportada"**.
- **Extensão de save no Tico** → tabela (default `.sav`). Confirmar `.fla`/`.mpk` do n64 na prática (ver §12).

| Plataforma | ext local |
|---|---|
| gb, gbc, gba, snes, genesis, master-system, psp, psx | `.sav` |
| n64 | `.fla` (fallback) — ver §12 |

- **Conflito de ROM**: compara **tamanho** — igual → skip; diferente → sobrescrever.
- **Conflito de save**: via `sync_state.json`. Fingerprint local = **tamanho + hash curto**. Primeira sincronização (sem registro) **nunca sobrescreve sem prompt**.
- **Assets**: só **covers** em `assets/covers/<plataforma>/`.
- **Multi-disco**: jogos com >1 arquivo → etapa ROM "não suportado", **pula todo o sync**.
- **UI**: botão "Sync" no detail view abre um **modal** com as 3 etapas (ROM, saves, cover) e estado por etapa. Prompt de conflito de save = modal com 3 opções.
- **screenshotFile**: fora de escopo.

---

## 2. Portar peças do `wt-saves` (não-commitado)

Copiar para o branch atual, adaptando nomes de caminho para `/tico`:

1. `source/model/DataModel.hpp` — struct `SaveEntry` (id, rom_id, file_name, file_size_bytes, emulator, slot, missing_from_fs, created_at, updated_at).
2. `source/model/RommApi.hpp/.cpp` — `SaveFetchResult` + `fetchSavesAsync(int romId)` (`GET /api/saves?rom_id=`).
3. `source/model/JsonUtil.hpp/.cpp` — `jsonParseSaveItems(json, vector<SaveEntry>&)`.
4. `source/model/DownloadManager.hpp/.cpp` — `SaveDownloadResult` + `DownloadSave(const SaveEntry&, platform_slug)` (via `GET /api/saves/{id}/content`). **Ajustar**: o destino deixa de ser `GetSavePath()` e passa a ir para o `SyncManager`/`ConfigManager::GetTicoSavePath()`; o nome precisa remover o sufixo `[timestamp]` e trocar a extensão (ver §9).

> O diff exato está em `git -C wt-saves diff` (worktree). Não commitar nada de lá; apenas portar.

---

## 3. Novo arquivo `source/model/TicoCatalog.hpp/.cpp`

Hub central de mapeamento. Adicionar ao Makefile (`SOURCES` já varre `source/model` — sem mudança necessária).

```cpp
namespace romm::model {
    // Slug RomM -> pasta do Tico. Fallback: slug intacto.
    std::string ResolveTicoPlatformSlug(const std::string& romm_slug);

    // True se a plataforma está na lista das 20 suportadas na v1.
    bool IsTicoPlatformSupported(const std::string& tico_slug);

    // Nome do core do Tico para upload de save, ou "" se sem core.
    std::string ResolveTicoCore(const std::string& tico_slug);

    // Extensão local esperada pelo save no Tico (com ponto), ex.: ".sav".
    std::string ResolveTicoSaveExtension(const std::string& tico_slug);

    // Lista das 20 plataformas, para testes/UI.
    const std::vector<std::string>& GetTicoPlatformList();
}
```

Internamente:
- SlugMap: `unordered_map<string,string>`.
- Emulador: `unordered_map<string,string>`.
- Extensão: `unordered_map<string,string>` (default `.sav`).
- Todas as 3 tabelas usam **macros/constantes** no mesmo arquivo para ficarem lado a lado.

Nota: `ResolveTicoPlatformSlug` é o **único** ponto que converte slug RomM→pasta Tico. ROM, save e cover usam ele.

---

## 4. `ConfigManager` — caminhos do Tico

Em `source/model/ConfigManager.hpp/.cpp`:

```cpp
// Novo membro: std::string tico_base_dir = "sdmc:/tico/";
const std::string& GetTicoBaseDir() const { return tico_base_dir; }
void SetTicoBaseDir(const std::string& dir);   // normaliza '/' final, valida com RomPathManager::ValidatePath

std::string GetTicoRomPath(const std::string& romm_slug) const;   // <base>roms/<ResolveTicoPlatformSlug>/
std::string GetTicoSavePath(const std::string& romm_slug) const;  // <base>saves/<ResolveTicoPlatformSlug>/
std::string GetTicoCoverPath(const std::string& romm_slug) const; // <base>assets/covers/<ResolveTicoPlatformSlug>/
```

- **Load()**: ler `"tico_base_dir"` do config.json; se ausente/vazio → default `sdmc:/tico/`; se `ValidatePath` falhar → default.
- **Save()**: gravar `"tico_base_dir": "..."` no JSON.

---

## 5. `SyncManager` — orquestrador (novo, `source/model/SyncManager.hpp/.cpp`)

Singleton `SyncManager::Instance()`. Responsável por: rodar o sync de um jogo em background, expor snapshot para a UI, persistir `sync_state.json`, e resolver conflitos via callback.

### 5.1 Estruturas

```cpp
enum class SyncStage { Rom, Saves, Cover };
enum class SyncStageState { Pending, Running, Ok, Skipped, Unsupported, Failed, WaitingConflict };

struct SyncStageResult {
    SyncStage stage;
    SyncStageState state = SyncStageState::Pending;
    std::string message;   // legível, já traduzida ou chave i18n
};

enum class SaveConflictKind { ServerOnly, LocalOnly, BothNew, FirstSyncBoth };

struct SaveConflict {
    bool active = false;
    int rom_id = 0;
    SaveConflictKind kind;
    std::string local_path;
    std::string local_info;   // tamanho + hash curto
    std::string server_info;  // updated_at + file_name
    std::string server_updated_at;
    int server_save_id = 0;
    std::string target_rel_path; // relativo à pasta de saves do Tico
};

struct SyncSnapshot {
    bool running = false;
    int rom_id = 0;
    std::string platform_slug;   // RomM
    std::string title;
    std::vector<SyncStageResult> stages; // ordem: Rom, Saves, Cover
    SaveConflict conflict;
};
```

### 5.2 API pública

```cpp
static SyncManager& Instance();

// Inicia sync do jogo. Cenário: copia do current GameDetail (rom_id, files,
// path_cover_large, file_name). Não faz nada se já está rodando esse rom_id.
void StartSync(const romm::model::GameDetail& detail, const std::string& platform_slug, const std::string& title);

SyncSnapshot GetSnapshot() const;          // cópia thread-safe
bool IsRunning() const;

// Resolução de conflito (chamada pela UI após o prompt).
void ResolveConflict(bool overwrite_local); // true = local wins (sobe), false = server wins (baixa)
void CancelSync();                           // cancela entre etapas

// Testes/independente: hash curto do arquivo.
static std::string ShortHash(const std::string& path);
```

### 5.3 `sync_state.json`

- Caminho: `sdmc:/switch/romm-nx/sync_state.json` (mesma pasta de `installed_index.json`).
- Formato (array):

```json
[
  {
    "rom_id": 123,
    "platform": "gba",
    "rom_path": "sdmc:/tico/roms/gba/Invader (Europe).gba",
    "rom_size": 8388608,
    "save_local_fingerprint": "<size>-<hash>",
    "server_save_id": 42,
    "server_save_updated_at": "2026-08-01T16:00:34"
  }
]
```

- `LoadSyncState()` / `SaveSyncState()` — parse/manual write no estilo do `installed_index.json` (JsonUtil). Chave: rom_id.
- Atualizar ao final de cada sync bem-sucedido (ROM ok, save ok, cover ok) **e** após conflito resolvido.

### 5.4 Fluxo do worker (thread própria — NÃO usar a fila de ROMs)

```
StartSync():
  - checa config válido; se files.size() > 1 → etapa ROM = Unsupported, fim (multi-disco).
  - marca stages Pending; roda em std::thread único (0x100000 stack, padrão do app).

Worker:
  1) ROM (SyncStage::Rom)
     - resolved = ConfigManager::GetTicoRomPath(slug) + sanitized filename
     - se stat existe && tamanho == detail.file_size_bytes → Skip "já presente"
     - se existe e tamanho diferente → sobrescrever (baixa de novo)
     - baixa via GET /api/roms/{file_id}/files/content/{nome} direto para o caminho do Tico
       (reutilizar DownloadWriter/curl de DownloadManager.cpp; escrever .part → validar → rename)
     - falha → Failed; tamanho igual=Ok "instalado"
  2) Saves (SyncStage::Saves)
     - core = ResolveTicoCore(plat); se vazio → Unsupported "plataforma sem core"
     - saves = RommApi::fetchSavesAsync(rom_id) (síncrono dentro do worker)
     - filtra missing_from_fs; escolhe mais recente por updated_at (se houver)
     - target = GetTicoSavePath(slug) + basename(ROM sem ext) + ResolveTicoSaveExtension(slug)
     - lê registro do sync_state (se existe)
     - decisão (ver §5.5) → baixa / sobe / promp / skip
  3) Cover (SyncStage::Cover)
     - fonte: detail.path_cover_large (ou small); constrói URL = host + path
     - target = GetTicoCoverPath(slug) + basename(ROM sem ext) + ".jpg"
     - se existe com mesmo tamanho → Skip; senão baixa (part → rename)
Fin:
  - grava sync_state
  - snapshot.running = false
```

### 5.5 Decisão de save (regra exata)

Estado local lido de `sync_state.json` (fingerprint = `size-shortHash`), servidor = save mais recente.

| caso | ação |
|---|---|
| sem registro (primeira vez) e há save no servidor e arquivo local existe | **prompt** (FirstSyncBoth) |
| sem registro e só servidor tem | **baixar** |
| sem registro e só local tem | **subir** |
| registro existe, servidor mudou (`updated_at` > gravado) e local mudou (fingerprint != gravado) | **prompt** (BothNew) |
| registro existe, só servidor mudou | **baixar** (sobrescreve local) |
| registro existe, só local mudou | **subir** (sobrescreve servidor) |
| nada mudou | skip |

- **Baixar**: `DownloadSave` (portado) → grava em target com `.part`→rename. Nome: basename do ROM + ext da tabela (remove `[timestamp]` do servidor).
- **Subir**: `POST /api/saves?rom_id=&emulator=<core>&overwrite=true` multipart (ver §6), campo `saveFile` = arquivo local.
- **Prompt**: seta `snapshot.conflict.active`. Fica parado até `ResolveConflict`. `ResolveConflict(true)` = sobre-local (faz upload); `false` = sobre-servidor (faz download).

---

## 6. `HttpClient` — upload multipart

Em `source/navigation/HttpClient.hpp/.cpp`, adicionar:

```cpp
// POST multipart/form-data com um campo de arquivo. fields: pares nome->valor.
static std::shared_ptr<HttpResult> uploadFileAsync(
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    const std::map<std::string, std::string>& fields,
    const std::string& file_field_name,   // ex.: "saveFile"
    const std::string& file_path,         // arquivo local a subir
    const std::string& file_name_on_server,
    HttpPriority priority = HttpPriority::Normal);
```

Implementação com `curl_mime`:
- `curl_mime_init(curl)`; `curl_mime_addpart()` para cada field textual (`name` = key, `data` = value) e uma part para o arquivo (`name` = file_field_name, `filename` = file_name_on_server, `data`/`filedata` = file_path, `type` = `application/octet-stream`).
- Mesmo fluxo de `performRequest`: `CURLOPT_MIMEPOST`, `CURLOPT_HTTPHEADER`, manter `Expect:` vazio (já no buildHeaderList).
- Rodar dentro do pool (`queue()->enqueue`), retornando `shared_ptr<HttpResult>`.

---

## 7. API de upload/save no `RommApi`

Adicionar (sync, usado dentro do worker do SyncManager):

```cpp
// POST /api/saves?rom_id=..&emulator=..&overwrite=..  (multipart saveFile)
static std::shared_ptr<HttpResult> uploadSaveAsync(
    int rom_id, const std::string& emulator, bool overwrite,
    const std::string& local_path, const std::string& file_name_on_server);
```

Monta URL com query-params, chama `HttpClient::uploadFileAsync`.

---

## 8. UI — botão Sync no DetailView

Em `source/ui/DetailLayout.{hpp,cpp}` e `DetailCard`:

- Adicionar um segundo botão de ação "Sync" ao lado do botão "Download" no `DetailCard` (render na área de ações). Texto: chave i18n `detail.btn.sync`.
- `NavigationManager` (`DetailFocus::Actions` faz `selected_detail_action_idx`; hoje 0=Download). Adicionar índice 1 = Sync. A no índice 1 → `SyncManager::Instance().StartSync(detail, ctx.platform_slug, ctx.title)` e abre o modal de sync.
- Render do botão Sync análogo ao do Download (texturas pré-renderizadas, estado focado destacado).

---

## 9. Modal de Sync (`source/ui/SyncModal.{hpp,cpp}` — novo)

Seguir o padrão do `UninstallConfirmModal` (Element desenhado por cima, controlado por `NavigationManager`):
- **Modo progresso**: 3 linhas (ROM / Saves / Cover) com estado (pendente/ok/pulado/não suportado/erro) + mensagem. Poll `SyncManager::GetSnapshot()` a cada frame (padrão do app: `PollDetailSaves`/`PollUpdateNotification`).
- **Modo conflito**: mostra caminho local + info local vs info servidor, e 3 opções: "Sobrescrever local" / "Sobrescrever servidor" / "Pular". A/B/X seleciona; B no progresso = fecha modal (não cancela worker? — melhor: B fecha o modal, o worker continua; o snapshot fica disponível).
- Integração: `NavigationManager` guarda `bool sync_modal_active` + `SyncModalState`; `HandleInput` desvia para o modal quando ativo (como `HandleUninstallModalInput`). Render do modal no layout do Detail (ou como Element no DetailLayout).

Textos i18n (en+fr):
- `sync.title`, `sync.stage.rom`, `sync.stage.saves`, `sync.stage.cover`
- `sync.state.pending|ok|skipped|unsupported|failed|running`
- `sync.conflict.title`, `sync.conflict.overwrite_local`, `sync.conflict.overwrite_server`, `sync.conflict.skip`
- `sync.rom.already`, `sync.rom.downloading`, `sync.rom.overwriting`, `sync.cover.ok`, etc. (mensagens de etapa)

---

## 10. `NavigationManager` — input/poll

Em `source/navigation/NavigationManager.{hpp,cpp}`:
- Novo estado `sync_modal_active`.
- `HandleInput`: se `sync_modal_active` → `HandleSyncModalInput(keys_down)` e retorna.
- `HandleSyncModalInput`: progresso → B fecha; conflito → up/down escolhe opção, A confirma (`ResolveConflict`), B = pular.
- Novo `PollSync()` chamado a cada frame (ver `MainApplication`), que atualiza o modal com o snapshot.
- `MainApplication::Poll...()` — adicionar chamada a `SyncManager::Instance()`/`nav.PollSync()` no render loop (junto de `PollDetailPrefetch` etc.).

---

## 11. i18n

Adicionar chaves em `romfs/lang/en.json` e `romfs/lang/fr.json` (442 linhas hoje; manter contagem igual entre os dois). Prefixos `sync.*` e `detail.btn.sync`.

---

## 12. Pontos de validação em hardware (deixar claro no código)

- Extensão de save do **n64** no Tico: estrutura mostra `.fla` e `.mpk`. Decidir se `ResolveTicoSaveExtension("n64")` deve retornar `.fla` sempre, ou derivar do tipo do save do servidor (slot/emulator). **Default implementado: `.fla` com TODO**.
- Nome de save baixado: basename do ROM **sem extensão** + ext da tabela. Confirmar que o Tico aceita nome igual ao da ROM (ex.: `Invader (Europe).sav`).
- ROMs `.7z` no servidor (ex.: genesis): o Tico espera `.md`. **v1 trata o arquivo como servido** (sem extração) — documentar que pode não abrir no Tico.

---

## 13. Build & verificação

```
make   # devkitpro obrigatório; zero novos -Wall warnings
```
- Compilar cedo e com frequência (each new file).
- Nada de testes automatizados (não existe framework); validar logicamente: SlugMap, tabela de cores, extensões, decisão de save (tabela §5.5) — testável como funções puras em um pequeno main de host se desejado.
- Não commitar sem pedido.

---

## 14. Ordem de implementação sugerida

1. `TicoCatalog` (§3) + testes mentais do SlugMap.
2. `ConfigManager` tico paths (§4).
3. Portar `SaveEntry`/`fetchSavesAsync`/`jsonParseSaveItems`/`DownloadSave` (§2).
4. `HttpClient::uploadFileAsync` + `RommApi::uploadSaveAsync` (§6, §7).
5. `SyncManager` completo (§5) — primeira versão sem UI (logs).
6. Botão Sync + modal + poll (§8, §9, §10).
7. i18n (§11).
8. Build final, revisão dos TODOs de hardware (§12).