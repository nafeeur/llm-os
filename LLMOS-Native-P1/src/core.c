#include "llmos/core.h"
#include "llmos/runtime.h"
#include "llmos/platform.h"
#include "llmos/sha256.h"
#include "llmos/tokenizer.h"
#include "llmos/ops.h"
#include "llmos/lmof.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MIN_U32(a,b) ((uint32_t)(a) < (uint32_t)(b) ? (uint32_t)(a) : (uint32_t)(b))

/*
 * LLMOS Native P1
 *
 * The machine is modeled around model images, tensor pages, inference
 * contexts and agents. There is intentionally no POSIX process surface,
 * general-purpose package manager, host model bridge, Python runtime or GUI.
 */

/* -------------------------------------------------------------------------- */
/* Tensor-aware early memory                                                   */
/* -------------------------------------------------------------------------- */

#define LLM_PAGE_SHIFT       12u
#define LLM_PAGE_SIZE        (1u << LLM_PAGE_SHIFT)
#define LLM_ARENA_PAGES      2048u
#define LLM_ARENA_BYTES      ((size_t)LLM_ARENA_PAGES * LLM_PAGE_SIZE)
#define MAX_MODEL_PAGES      64u
#define MAX_SESSION_PAGES    64u
#define TOKENS_PER_KV_PAGE   (LLM_PAGE_SIZE / sizeof(uint32_t))

#define PAGE_FLAG_IMMUTABLE  0x01u
#define PAGE_FLAG_PINNED     0x02u
#define PAGE_FLAG_DIRTY      0x04u

/* Aligned, real backing storage used by all tensor objects in this prototype. */
static uint8_t tensor_arena[LLM_ARENA_BYTES] __attribute__((aligned(LLM_PAGE_SIZE)));

typedef enum {
    PAGE_FREE = 0,
    PAGE_WEIGHT,
    PAGE_KV,
    PAGE_PREFIX,
    PAGE_SCRATCH,
    PAGE_AGENT_STATE,
    PAGE_JOURNAL,
    PAGE_ROLE_COUNT
} PageRole;

typedef enum {
    TIER_FAST = 0,   /* future accelerator-local memory */
    TIER_RAM,
    TIER_COLD        /* future NVMe-backed extent */
} MemoryTier;

typedef struct {
    uint16_t refs;
    uint16_t owner;
    uint16_t model;
    uint8_t role;
    uint8_t tier;
    uint8_t flags;
    uint8_t heat;
    uint32_t generation;
    uint64_t last_touch;
} PageMeta;

static PageMeta page_meta[LLM_ARENA_PAGES];
static uint16_t free_pages[LLM_ARENA_PAGES];
static uint32_t free_top;
static uint32_t page_generation;
static uint64_t page_allocations;
static uint64_t page_releases;
static uint64_t page_cow_copies;
static uint64_t page_zero_bytes;

static const char *const page_role_names[PAGE_ROLE_COUNT] = {
    "free", "weight", "kv", "prefix", "scratch", "agent", "journal"
};

static void page_system_init(void) {
    k_memset(page_meta, 0, sizeof(page_meta));
    free_top = LLM_ARENA_PAGES;
    for (uint32_t i = 0; i < LLM_ARENA_PAGES; ++i)
        free_pages[i] = (uint16_t)(LLM_ARENA_PAGES - 1u - i);
    page_generation = 0;
    page_allocations = 0;
    page_releases = 0;
    page_cow_copies = 0;
    page_zero_bytes = 0;
}

static uint8_t *page_address(uint16_t page) {
    if (page >= LLM_ARENA_PAGES) return NULL;
    return &tensor_arena[(size_t)page * LLM_PAGE_SIZE];
}

static int page_allocate(PageRole role, uint16_t model, uint16_t owner, uint8_t flags) {
    if (!free_top || role == PAGE_FREE || role >= PAGE_ROLE_COUNT) return -1;
    uint16_t page = free_pages[--free_top];
    PageMeta *m = &page_meta[page];
    m->refs = 1;
    m->owner = owner;
    m->model = model;
    m->role = (uint8_t)role;
    m->tier = TIER_RAM;
    m->flags = flags;
    m->heat = 1;
    m->generation = ++page_generation;
    m->last_touch = platform_cycles();
    k_memset(page_address(page), 0, LLM_PAGE_SIZE);
    page_zero_bytes += LLM_PAGE_SIZE;
    ++page_allocations;
    return (int)page;
}

static bool page_reference(uint16_t page) {
    if (page >= LLM_ARENA_PAGES || !page_meta[page].refs || page_meta[page].refs == UINT16_MAX)
        return false;
    ++page_meta[page].refs;
    if (page_meta[page].heat != UINT8_MAX) ++page_meta[page].heat;
    page_meta[page].last_touch = platform_cycles();
    return true;
}

static void page_release(uint16_t page) {
    if (page >= LLM_ARENA_PAGES || !page_meta[page].refs) return;
    PageMeta *m = &page_meta[page];
    if (--m->refs) return;
    k_memset(m, 0, sizeof(*m));
    if (free_top < LLM_ARENA_PAGES) free_pages[free_top++] = page;
    ++page_releases;
}

static int page_clone(uint16_t source, uint16_t owner) {
    if (source >= LLM_ARENA_PAGES || !page_meta[source].refs) return -1;
    PageMeta *src = &page_meta[source];
    int copy = page_allocate((PageRole)src->role, src->model, owner,
                             (uint8_t)(src->flags & (uint8_t)~PAGE_FLAG_IMMUTABLE));
    if (copy < 0) return -1;
    k_memcpy(page_address((uint16_t)copy), page_address(source), LLM_PAGE_SIZE);
    page_meta[copy].heat = src->heat;
    ++page_cow_copies;
    return copy;
}

static uint32_t pages_in_role(PageRole role) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < LLM_ARENA_PAGES; ++i)
        if (page_meta[i].refs && page_meta[i].role == (uint8_t)role) ++count;
    return count;
}

/* -------------------------------------------------------------------------- */
/* Models and native execution descriptors                                     */
/* -------------------------------------------------------------------------- */

#define MAX_MODELS 4u

typedef struct {
    bool used;
    bool resident;
    uint16_t id;
    char name[32];
    char architecture[20];
    char quantization[12];
    uint16_t layers;
    uint16_t dimension;
    uint16_t heads;
    uint32_t context_limit;
    uint16_t weight_pages[MAX_MODEL_PAGES];
    uint16_t weight_page_count;
    uint32_t content_hash;
} Model;

static Model models[MAX_MODELS];

static Model *model_by_id(uint16_t id) {
    for (size_t i = 0; i < ARRAY_COUNT(models); ++i)
        if (models[i].used && models[i].id == id) return &models[i];
    return NULL;
}

static bool model_register_native_test(void) {
    Model *m = &models[0];
    k_memset(m, 0, sizeof(*m));
    m->used = true;
    m->id = 1;
    k_strncpy(m->name, "NativeLM-test", sizeof(m->name));
    k_strncpy(m->architecture, "decoder-test", sizeof(m->architecture));
    k_strncpy(m->quantization, "int8", sizeof(m->quantization));
    m->layers = 4;
    m->dimension = 128;
    m->heads = 8;
    m->context_limit = MAX_SESSION_PAGES * TOKENS_PER_KV_PAGE;
    m->content_hash = 0x4c4d4f53u;

    /* Immutable pages model how installed weights are mapped once and shared. */
    for (uint16_t i = 0; i < 24u; ++i) {
        int page = page_allocate(PAGE_WEIGHT, m->id, 0,
                                 PAGE_FLAG_IMMUTABLE | PAGE_FLAG_PINNED);
        if (page < 0) return false;
        m->weight_pages[m->weight_page_count++] = (uint16_t)page;
        int8_t *weights = (int8_t *)page_address((uint16_t)page);
        for (uint32_t j = 0; j < LLM_PAGE_SIZE; ++j)
            weights[j] = (int8_t)(((i * 31u + j * 17u) % 127u) - 63);
    }
    m->resident = true;
    return true;
}

/* -------------------------------------------------------------------------- */
/* Sessions, real KV page ownership and token-aware scheduling                 */
/* -------------------------------------------------------------------------- */

#define MAX_SESSIONS       16u
#define PREFILL_CHUNK      24u
#define MAX_BATCH          4u
#define SESSION_OUTPUT     384u

typedef enum {
    SESSION_FREE = 0,
    SESSION_PREFILL,
    SESSION_DECODE,
    SESSION_SUSPENDED,
    SESSION_DONE,
    SESSION_FAILED
} SessionState;

typedef struct {
    bool used;
    uint16_t id;
    uint16_t model_id;
    uint16_t agent_id;
    uint8_t priority;
    uint8_t state;
    uint16_t kv_pages[MAX_SESSION_PAGES];
    uint16_t kv_page_count;
    uint32_t context_tokens;
    uint32_t prompt_tokens;
    uint32_t prompt_done;
    uint32_t max_new_tokens;
    uint32_t generated_tokens;
    uint32_t seed;
    uint64_t submitted_at;
    uint64_t first_token_at;
    uint64_t last_run_tick;
    uint32_t wait_ticks;
    char output[SESSION_OUTPUT];
} Session;

static Session sessions[MAX_SESSIONS];
static uint16_t next_session_id;
static uint64_t scheduler_ticks;
static uint64_t scheduler_batches;
static uint64_t scheduler_batch_members;
static uint64_t scheduler_prefill_tokens;
static uint64_t scheduler_decode_tokens;
static uint64_t scheduler_weight_page_reads;
static uint64_t scheduler_weight_page_reads_avoided;
static uint64_t scheduler_admission_denials;

static const char *session_state_name(uint8_t state) {
    switch ((SessionState)state) {
        case SESSION_PREFILL: return "prefill";
        case SESSION_DECODE: return "decode";
        case SESSION_SUSPENDED: return "suspended";
        case SESSION_DONE: return "done";
        case SESSION_FAILED: return "failed";
        default: return "free";
    }
}

static Session *session_by_id(uint16_t id) {
    for (size_t i = 0; i < ARRAY_COUNT(sessions); ++i)
        if (sessions[i].used && sessions[i].id == id) return &sessions[i];
    return NULL;
}

static uint32_t estimate_prompt_tokens(const char *prompt) {
    uint32_t bytes = (uint32_t)k_strlen(prompt);
    uint32_t words = 0;
    bool in_word = false;
    for (uint32_t i = 0; i < bytes; ++i) {
        bool space = prompt[i] == ' ' || prompt[i] == '\t' || prompt[i] == '\n';
        if (!space && !in_word) { ++words; in_word = true; }
        if (space) in_word = false;
    }
    uint32_t byte_estimate = (bytes + 3u) / 4u;
    return byte_estimate > words ? byte_estimate : words;
}

static bool session_append_token_hash(Session *s, uint32_t token_hash) {
    if (!s || !s->used) return false;
    uint32_t offset = s->context_tokens % TOKENS_PER_KV_PAGE;
    if (!s->kv_page_count || offset == 0u) {
        if (s->kv_page_count >= MAX_SESSION_PAGES) return false;
        int page = page_allocate(PAGE_KV, s->model_id, s->id, 0);
        if (page < 0) return false;
        s->kv_pages[s->kv_page_count++] = (uint16_t)page;
        offset = 0;
    } else {
        uint16_t last = s->kv_pages[s->kv_page_count - 1u];
        if (page_meta[last].refs > 1u) {
            int copy = page_clone(last, s->id);
            if (copy < 0) return false;
            page_release(last);
            s->kv_pages[s->kv_page_count - 1u] = (uint16_t)copy;
        }
    }
    uint16_t page = s->kv_pages[s->kv_page_count - 1u];
    uint32_t *tokens = (uint32_t *)page_address(page);
    tokens[offset] = token_hash;
    page_meta[page].flags |= PAGE_FLAG_DIRTY;
    page_meta[page].last_touch = platform_cycles();
    ++s->context_tokens;
    return true;
}

static void session_release_pages(Session *s) {
    if (!s) return;
    for (uint16_t i = 0; i < s->kv_page_count; ++i) page_release(s->kv_pages[i]);
    s->kv_page_count = 0;
}

static void session_drop(Session *s) {
    if (!s || !s->used) return;
    session_release_pages(s);
    k_memset(s, 0, sizeof(*s));
}

static Session *session_create(uint16_t agent_id, uint16_t model_id,
                               uint8_t priority, const char *prompt,
                               uint32_t max_new_tokens) {
    Model *model = model_by_id(model_id);
    if (!model || !model->resident) return NULL;
    uint32_t prompt_tokens = estimate_prompt_tokens(prompt);
    uint32_t total = prompt_tokens + max_new_tokens;
    uint32_t required_pages = (total + TOKENS_PER_KV_PAGE - 1u) / TOKENS_PER_KV_PAGE;
    if (total > model->context_limit || required_pages + 8u > free_top) {
        ++scheduler_admission_denials;
        return NULL;
    }
    for (size_t i = 0; i < ARRAY_COUNT(sessions); ++i) {
        if (sessions[i].used) continue;
        Session *s = &sessions[i];
        k_memset(s, 0, sizeof(*s));
        s->used = true;
        s->id = next_session_id++;
        if (!next_session_id) next_session_id = 1;
        s->model_id = model_id;
        s->agent_id = agent_id;
        s->priority = priority > 7u ? 7u : priority;
        s->state = SESSION_PREFILL;
        s->prompt_tokens = prompt_tokens ? prompt_tokens : 1u;
        s->max_new_tokens = max_new_tokens ? max_new_tokens : 1u;
        s->seed = k_hash32(prompt) ^ ((uint32_t)agent_id << 16) ^ s->id;
        s->submitted_at = platform_cycles();
        return s;
    }
    ++scheduler_admission_denials;
    return NULL;
}

static Session *session_fork(Session *parent, uint16_t agent_id,
                             const char *continuation, uint8_t priority) {
    if (!parent || !parent->used || parent->state == SESSION_FAILED) return NULL;
    Session *child = session_create(agent_id, parent->model_id, priority,
                                    continuation, parent->max_new_tokens);
    if (!child) return NULL;
    child->context_tokens = parent->context_tokens;
    child->seed ^= parent->seed;
    for (uint16_t i = 0; i < parent->kv_page_count; ++i) {
        if (!page_reference(parent->kv_pages[i])) { session_drop(child); return NULL; }
        child->kv_pages[child->kv_page_count++] = parent->kv_pages[i];
    }
    return child;
}

static bool output_append(Session *s, const char *word) {
    size_t used = k_strlen(s->output);
    size_t need = k_strlen(word) + (used ? 1u : 0u) + 1u;
    if (used + need > sizeof(s->output)) return false;
    if (used) s->output[used++] = ' ';
    k_strncpy(&s->output[used], word, sizeof(s->output) - used);
    return true;
}

static const char *native_test_token(Session *s) {
    static const char *const vocab[] = {
        "model", "memory", "tokens", "agents", "share", "weights", "context", "verify",
        "capability", "tensor", "batch", "decode", "prefill", "deadline", "rollback", "native",
        "cache", "schedule", "isolate", "stream", "quantized", "resident", "prefix", "hardware",
        "efficient", "bounded", "state", "commit", "page", "runtime", "inference", "secure"
    };
    s->seed = s->seed * 1664525u + 1013904223u + s->generated_tokens;
    return vocab[(s->seed >> 24) % ARRAY_COUNT(vocab)];
}

static uint32_t session_score(const Session *s) {
    uint32_t priority = (uint32_t)s->priority * 100000u;
    uint32_t age = s->wait_ticks > 10000u ? 10000u : s->wait_ticks;
    uint32_t phase = s->state == SESSION_DECODE ? 20000u : 0u;
    return priority + age * 100u + phase;
}

static bool session_runnable(const Session *s) {
    return s && s->used && (s->state == SESSION_PREFILL || s->state == SESSION_DECODE);
}

static bool scheduler_step(void) {
    ++scheduler_ticks;
    Session *anchor = NULL;
    uint32_t best = 0;
    for (size_t i = 0; i < ARRAY_COUNT(sessions); ++i) {
        Session *s = &sessions[i];
        if (!session_runnable(s)) continue;
        if (s->wait_ticks != UINT32_MAX) ++s->wait_ticks;
        uint32_t score = session_score(s);
        if (!anchor || score > best) { anchor = s; best = score; }
    }
    if (!anchor) return false;

    Session *batch[MAX_BATCH];
    uint32_t batch_count = 0;
    batch[batch_count++] = anchor;
    for (size_t i = 0; i < ARRAY_COUNT(sessions) && batch_count < MAX_BATCH; ++i) {
        Session *s = &sessions[i];
        if (s == anchor || !session_runnable(s)) continue;
        if (s->model_id == anchor->model_id && s->state == anchor->state)
            batch[batch_count++] = s;
    }

    Model *model = model_by_id(anchor->model_id);
    ++scheduler_batches;
    scheduler_batch_members += batch_count;
    if (model) {
        scheduler_weight_page_reads += model->weight_page_count;
        if (batch_count > 1u)
            scheduler_weight_page_reads_avoided +=
                (uint64_t)model->weight_page_count * (batch_count - 1u);
        for (uint16_t i = 0; i < model->weight_page_count; ++i) {
            PageMeta *m = &page_meta[model->weight_pages[i]];
            if (m->heat != UINT8_MAX) ++m->heat;
            m->last_touch = platform_cycles();
        }
    }

    int scratch_a = page_allocate(PAGE_SCRATCH, anchor->model_id, 0, 0);
    int scratch_b = page_allocate(PAGE_SCRATCH, anchor->model_id, 0, 0);

    for (uint32_t i = 0; i < batch_count; ++i) {
        Session *s = batch[i];
        s->wait_ticks = 0;
        s->last_run_tick = scheduler_ticks;
        if (s->state == SESSION_PREFILL) {
            uint32_t left = s->prompt_tokens - s->prompt_done;
            uint32_t chunk = MIN_U32(left, PREFILL_CHUNK);
            for (uint32_t n = 0; n < chunk; ++n) {
                uint32_t token_hash = s->seed ^ (s->prompt_done * 0x9e3779b9u);
                if (!session_append_token_hash(s, token_hash)) {
                    s->state = SESSION_FAILED;
                    break;
                }
                ++s->prompt_done;
                ++scheduler_prefill_tokens;
            }
            if (s->state != SESSION_FAILED && s->prompt_done >= s->prompt_tokens)
                s->state = SESSION_DECODE;
        } else if (s->state == SESSION_DECODE) {
            const char *word = native_test_token(s);
            uint32_t token_hash = k_hash32(word) ^ s->seed;
            if (!session_append_token_hash(s, token_hash) || !output_append(s, word)) {
                s->state = SESSION_FAILED;
                continue;
            }
            if (!s->generated_tokens) s->first_token_at = platform_cycles();
            ++s->generated_tokens;
            ++scheduler_decode_tokens;
            if (s->generated_tokens >= s->max_new_tokens) s->state = SESSION_DONE;
        }
    }

    if (scratch_a >= 0) page_release((uint16_t)scratch_a);
    if (scratch_b >= 0) page_release((uint16_t)scratch_b);
    return true;
}

static void scheduler_run_until_idle(uint32_t max_steps) {
    while (max_steps-- && scheduler_step()) { }
}

/* -------------------------------------------------------------------------- */
/* Capability-scoped agents and transactional state                            */
/* -------------------------------------------------------------------------- */

#define MAX_AGENTS          8u
#define WORKSPACE_FILES     12u
#define FILE_NAME_CAP       32u
#define FILE_DATA_CAP       256u
#define JOURNAL_CAP         96u

#define CAP_INFER           (1u << 0)
#define CAP_FORK_CONTEXT    (1u << 1)
#define CAP_WORKSPACE_WRITE (1u << 2)
#define CAP_COMMIT          (1u << 3)
#define CAP_SPAWN_AGENT     (1u << 4)
#define CAP_DEVICE_CONTROL  (1u << 5)
#define CAP_INSPECT         (1u << 6)

typedef struct {
    bool used;
    uint16_t id;
    char name[24];
    uint32_t capabilities;
    uint32_t token_budget;
    uint32_t tokens_used;
    uint16_t page_budget;
    uint8_t priority;
    uint32_t committed_jobs;
    uint32_t rolled_back_jobs;
} Agent;

typedef struct {
    bool used;
    char name[FILE_NAME_CAP];
    char data[FILE_DATA_CAP];
    uint32_t version;
    uint16_t owner;
} WorkspaceFile;

typedef struct {
    bool active;
    uint16_t owner;
    WorkspaceFile snapshot[WORKSPACE_FILES];
} Transaction;

typedef struct {
    uint64_t tick;
    uint16_t actor;
    bool allowed;
    char action[48];
} JournalEntry;

static Agent agents[MAX_AGENTS];
static uint16_t next_agent_id;
static WorkspaceFile workspace[WORKSPACE_FILES];
static Transaction transactions[MAX_AGENTS];
static JournalEntry journal[JOURNAL_CAP];
static uint32_t journal_head;
static uint32_t journal_count;

static Agent *agent_by_id(uint16_t id) {
    for (size_t i = 0; i < ARRAY_COUNT(agents); ++i)
        if (agents[i].used && agents[i].id == id) return &agents[i];
    return NULL;
}

static size_t agent_slot(const Agent *agent) {
    return (size_t)(agent - agents);
}

static void journal_add(uint16_t actor, const char *action, bool allowed) {
    JournalEntry *e = &journal[journal_head];
    e->tick = scheduler_ticks;
    e->actor = actor;
    e->allowed = allowed;
    k_strncpy(e->action, action, sizeof(e->action));
    journal_head = (journal_head + 1u) % JOURNAL_CAP;
    if (journal_count < JOURNAL_CAP) ++journal_count;
}

static bool agent_has(Agent *agent, uint32_t capability, const char *action) {
    bool allowed = agent && (agent->capabilities & capability) == capability;
    journal_add(agent ? agent->id : 0, action, allowed);
    return allowed;
}

static Agent *agent_spawn(Agent *parent, const char *name, uint8_t priority,
                          uint32_t token_budget, uint16_t page_budget,
                          uint32_t requested_caps) {
    if (parent && !agent_has(parent, CAP_SPAWN_AGENT, "agent.spawn")) return NULL;
    uint32_t caps = requested_caps;
    if (parent) caps &= parent->capabilities;
    for (size_t i = 0; i < ARRAY_COUNT(agents); ++i) {
        if (agents[i].used) continue;
        Agent *a = &agents[i];
        k_memset(a, 0, sizeof(*a));
        a->used = true;
        a->id = next_agent_id++;
        k_strncpy(a->name, name, sizeof(a->name));
        a->capabilities = caps;
        a->token_budget = token_budget;
        a->page_budget = page_budget;
        a->priority = priority > 7u ? 7u : priority;
        journal_add(parent ? parent->id : 0, "agent.created", true);
        return a;
    }
    return NULL;
}

static WorkspaceFile *workspace_find(const char *name) {
    for (size_t i = 0; i < ARRAY_COUNT(workspace); ++i)
        if (workspace[i].used && k_strcmp(workspace[i].name, name) == 0) return &workspace[i];
    return NULL;
}

static bool transaction_begin(Agent *agent) {
    if (!agent_has(agent, CAP_WORKSPACE_WRITE, "txn.begin")) return false;
    Transaction *tx = &transactions[agent_slot(agent)];
    if (tx->active) return false;
    tx->active = true;
    tx->owner = agent->id;
    k_memcpy(tx->snapshot, workspace, sizeof(workspace));
    return true;
}

static bool transaction_commit(Agent *agent) {
    if (!agent_has(agent, CAP_COMMIT, "txn.commit")) return false;
    Transaction *tx = &transactions[agent_slot(agent)];
    if (!tx->active || tx->owner != agent->id) return false;
    tx->active = false;
    ++agent->committed_jobs;
    return true;
}

static bool transaction_rollback(Agent *agent) {
    if (!agent) return false;
    Transaction *tx = &transactions[agent_slot(agent)];
    if (!tx->active || tx->owner != agent->id) return false;
    k_memcpy(workspace, tx->snapshot, sizeof(workspace));
    tx->active = false;
    ++agent->rolled_back_jobs;
    journal_add(agent->id, "txn.rollback", true);
    return true;
}

static bool workspace_write(Agent *agent, const char *name, const char *data) {
    if (!agent_has(agent, CAP_WORKSPACE_WRITE, "workspace.write")) return false;
    Transaction *tx = &transactions[agent_slot(agent)];
    if (!tx->active || tx->owner != agent->id) return false;
    WorkspaceFile *file = workspace_find(name);
    if (!file) {
        for (size_t i = 0; i < ARRAY_COUNT(workspace); ++i) {
            if (!workspace[i].used) { file = &workspace[i]; break; }
        }
    }
    if (!file) return false;
    file->used = true;
    file->owner = agent->id;
    ++file->version;
    k_strncpy(file->name, name, sizeof(file->name));
    k_strncpy(file->data, data, sizeof(file->data));
    return true;
}

static bool agent_finish_session(Agent *agent, Session *s, const char *filename,
                                 bool force_denied_action) {
    if (!agent || !s) return false;
    bool generated = s->state == SESSION_DONE && s->output[0];
    bool within_tokens = agent->tokens_used + s->generated_tokens <= agent->token_budget;
    bool within_pages = s->kv_page_count <= agent->page_budget;
    bool wrote = generated && within_tokens && within_pages &&
                 workspace_write(agent, filename, s->output);
    agent->tokens_used += s->generated_tokens;

    if (force_denied_action)
        (void)agent_has(agent, CAP_DEVICE_CONTROL, "device.poweroff");

    /* An attempted irreversible action is a verification failure even when
     * the capability system correctly denies it; the whole job is rolled back. */
    bool verified = wrote && !force_denied_action && k_strlen(s->output) >= 12u;
    if (verified && transaction_commit(agent)) return true;
    (void)transaction_rollback(agent);
    return false;
}

static bool agent_run_goal(Agent *agent, const char *goal, const char *filename,
                           bool force_denied_action) {
    if (!agent_has(agent, CAP_INFER, "model.infer")) return false;
    if (!transaction_begin(agent)) return false;
    uint32_t remaining = agent->token_budget > agent->tokens_used
                       ? agent->token_budget - agent->tokens_used : 0u;
    uint32_t requested = MIN_U32(remaining, 14u);
    if (!requested) { (void)transaction_rollback(agent); return false; }
    Session *s = session_create(agent->id, 1, agent->priority, goal, requested);
    if (!s) { (void)transaction_rollback(agent); return false; }
    scheduler_run_until_idle(4096u);
    bool ok = agent_finish_session(agent, s, filename, force_denied_action);
    session_drop(s);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Native integer tensor microbenchmark                                        */
/* -------------------------------------------------------------------------- */

static int64_t int8_dot(const int8_t *a, const int8_t *b, uint32_t count) {
    int64_t sum = 0;
    for (uint32_t i = 0; i < count; ++i) sum += (int32_t)a[i] * (int32_t)b[i];
    return sum;
}

static uint64_t tensor_microbenchmark(uint32_t iterations, int64_t *checksum) {
    Model *m = model_by_id(1);
    if (!m || m->weight_page_count < 2u) return 0;
    int scratch = page_allocate(PAGE_SCRATCH, m->id, 0, 0);
    if (scratch < 0) return 0;
    int8_t *vector = (int8_t *)page_address((uint16_t)scratch);
    for (uint32_t i = 0; i < 128u; ++i) vector[i] = (int8_t)((i % 31u) - 15);
    int8_t *matrix = (int8_t *)page_address(m->weight_pages[0]);
    int64_t total = 0;
    uint64_t start = platform_cycles();
    for (uint32_t n = 0; n < iterations; ++n)
        for (uint32_t row = 0; row < 32u; ++row)
            total += int8_dot(&matrix[row * 128u], vector, 128u);
    uint64_t elapsed = platform_cycles() - start;
    page_release((uint16_t)scratch);
    if (checksum) *checksum = total;
    return elapsed;
}

/* -------------------------------------------------------------------------- */
/* Diagnostics and command surface                                             */
/* -------------------------------------------------------------------------- */

static void print_banner(void) {
    console_write(
        "\nLLMOS Native P1\n"
        "model-centric bare-metal kernel | no Linux | no POSIX | no Python\n"
        "shared weights | paged KV | token scheduler | capability agents\n"
        "NativeLM-test validates the OS path; it is not a trained language model.\n"
        "type 'help'\n\n");
}

static void command_help(void) {
    console_write(
        "System:     about, arch, selftest, halt\n"
        "Models:     models, infer <prompt>, tensorbench\n"
        "Contexts:   sessions, fork <id> <continuation>, drop <id>, step\n"
        "Memory:     mem, pages\n"
        "Scheduler:  sched, batchdemo\n"
        "Agents:     agents, agent demo, agent run <id> <goal>, agent fail <id>\n"
        "State:      workspace, cat <name>, journal\n"
        "P2 slice:   blk, modelload, infer2 <prompt>\n");
}

static void command_about(void) {
    console_write(
        "LLMOS treats model images, tensor extents, KV contexts, agents, capabilities,\n"
        "transactions and accelerator queues as native operating-system objects.\n"
        "Architecture-specific code is limited to boot, interrupts, timers, serial,\n"
        "MMU and future device queues. The model/agent core is shared by x86-64 and\n"
        "AArch64 builds. General-purpose compatibility is intentionally excluded.\n");
}

static void command_arch(void) {
    k_printf("architecture: %s\nplatform:     %s\nnominal RAM:  %llu MiB\n",
             platform_arch(), platform_name(),
             (unsigned long long)(platform_nominal_memory_bytes() / (1024u * 1024u)));
    k_printf("tensor arena: %u pages x %u bytes = %u MiB\n",
             LLM_ARENA_PAGES, LLM_PAGE_SIZE,
             (unsigned)(LLM_ARENA_BYTES / (1024u * 1024u)));
}

static void command_models(void) {
    console_write("id  name             arch          quant layers dim heads context weight-pages\n");
    for (size_t i = 0; i < ARRAY_COUNT(models); ++i) {
        Model *m = &models[i];
        if (!m->used) continue;
        k_printf("%u   %s  %s  %s  %u  %u  %u  %u  %u\n",
                 m->id, m->name, m->architecture, m->quantization,
                 m->layers, m->dimension, m->heads, m->context_limit,
                 m->weight_page_count);
    }
    console_write("installed model pages are immutable, pinned and shared by all agents\n");
}

static void command_mem(void) {
    k_printf("tensor arena: total=%u KiB used=%u KiB free=%u KiB\n",
             (unsigned)(LLM_ARENA_BYTES / 1024u),
             (unsigned)((LLM_ARENA_PAGES - free_top) * (LLM_PAGE_SIZE / 1024u)),
             (unsigned)(free_top * (LLM_PAGE_SIZE / 1024u)));
    k_printf("allocations=%llu releases=%llu cow=%llu zeroed=%llu KiB\n",
             (unsigned long long)page_allocations,
             (unsigned long long)page_releases,
             (unsigned long long)page_cow_copies,
             (unsigned long long)(page_zero_bytes / 1024u));
    for (uint32_t role = 1; role < PAGE_ROLE_COUNT; ++role)
        k_printf("  %s: %u pages\n", page_role_names[role], pages_in_role((PageRole)role));
}

static void command_pages(void) {
    console_write("page role     model owner refs flags heat generation\n");
    uint32_t shown = 0;
    for (uint32_t i = 0; i < LLM_ARENA_PAGES && shown < 48u; ++i) {
        PageMeta *m = &page_meta[i];
        if (!m->refs) continue;
        k_printf("%u  %s  %u  %u  %u  0x%x  %u  %u\n",
                 i, page_role_names[m->role], m->model, m->owner,
                 m->refs, m->flags, m->heat, m->generation);
        ++shown;
    }
    if (LLM_ARENA_PAGES - free_top > shown)
        k_printf("... %u more allocated pages\n", LLM_ARENA_PAGES - free_top - shown);
}

static void command_sessions(void) {
    console_write("id agent model state      pri ctx prompt generated kv-pages output\n");
    bool any = false;
    for (size_t i = 0; i < ARRAY_COUNT(sessions); ++i) {
        Session *s = &sessions[i];
        if (!s->used) continue;
        any = true;
        k_printf("%u  %u  %u  %s  %u  %u  %u/%u  %u/%u  %u  %s\n",
                 s->id, s->agent_id, s->model_id, session_state_name(s->state),
                 s->priority, s->context_tokens, s->prompt_done, s->prompt_tokens,
                 s->generated_tokens, s->max_new_tokens, s->kv_page_count, s->output);
    }
    if (!any) console_write("no live contexts\n");
}

static void command_sched(void) {
    k_printf("ticks=%llu batches=%llu members=%llu avg-batch-x100=%llu\n",
             (unsigned long long)scheduler_ticks,
             (unsigned long long)scheduler_batches,
             (unsigned long long)scheduler_batch_members,
             (unsigned long long)(scheduler_batches
                 ? scheduler_batch_members * 100u / scheduler_batches : 0u));
    k_printf("prefill-tokens=%llu decode-tokens=%llu admissions-denied=%llu\n",
             (unsigned long long)scheduler_prefill_tokens,
             (unsigned long long)scheduler_decode_tokens,
             (unsigned long long)scheduler_admission_denials);
    k_printf("weight-page-reads=%llu avoided-by-batching=%llu\n",
             (unsigned long long)scheduler_weight_page_reads,
             (unsigned long long)scheduler_weight_page_reads_avoided);
}

static void command_infer(const char *prompt) {
    if (!prompt || !*prompt) { console_write("usage: infer <prompt>\n"); return; }
    Session *s = session_create(0, 1, 5, prompt, 18);
    if (!s) { console_write("admission denied: context slots or tensor pages unavailable\n"); return; }
    scheduler_run_until_idle(4096u);
    k_printf("session %u [%s]: %s\n", s->id, session_state_name(s->state), s->output);
    console_write("context remains live; use sessions, fork, or drop\n");
}

static void command_fork(const char *args) {
    bool ok = false;
    uint32_t id = k_parse_u32(args, &ok);
    if (!ok) { console_write("usage: fork <session-id> <continuation>\n"); return; }
    while (*args == ' ') ++args;
    while (*args >= '0' && *args <= '9') ++args;
    while (*args == ' ') ++args;
    Session *parent = session_by_id((uint16_t)id);
    if (!parent || !*args) { console_write("session or continuation missing\n"); return; }
    uint64_t cow_before = page_cow_copies;
    Session *child = session_fork(parent, parent->agent_id, args, parent->priority);
    if (!child) { console_write("fork failed\n"); return; }
    k_printf("forked %u -> %u sharing %u KV pages; refs incremented\n",
             parent->id, child->id, child->kv_page_count);
    scheduler_run_until_idle(4096u);
    k_printf("child output: %s\ncopy-on-write pages created during append: %llu\n",
             child->output, (unsigned long long)(page_cow_copies - cow_before));
}

static void command_drop(const char *args) {
    bool ok = false;
    uint32_t id = k_parse_u32(args, &ok);
    Session *s = ok ? session_by_id((uint16_t)id) : NULL;
    if (!s) { console_write("session not found\n"); return; }
    session_drop(s);
    k_printf("session %u dropped; KV references released\n", id);
}

static void command_tensorbench(void) {
    int64_t checksum = 0;
    uint64_t cycles = tensor_microbenchmark(256u, &checksum);
    k_printf("int8 tensor kernel: 256 x 32 x 128 MACs, cycles=%llu checksum=%lld\n",
             (unsigned long long)cycles, (long long)checksum);
    console_write("This measures the in-kernel integer operator path, not language-model quality.\n");
}

static void command_batchdemo(void) {
    const char *prompts[] = {
        "agent alpha summarize memory placement",
        "agent beta inspect token deadlines",
        "agent gamma verify context sharing",
        "agent delta plan quantized inference"
    };
    Session *created[ARRAY_COUNT(prompts)];
    for (size_t i = 0; i < ARRAY_COUNT(prompts); ++i)
        created[i] = session_create((uint16_t)(i + 1u), 1, (uint8_t)(4u - i), prompts[i], 10u);
    scheduler_run_until_idle(4096u);
    for (size_t i = 0; i < ARRAY_COUNT(created); ++i) {
        if (!created[i]) continue;
        k_printf("agent %u -> %s\n", created[i]->agent_id, created[i]->output);
    }
    command_sched();
}

static void command_agents(void) {
    console_write("id name               pri caps tokens-used/budget page-budget commits rollbacks\n");
    for (size_t i = 0; i < ARRAY_COUNT(agents); ++i) {
        Agent *a = &agents[i];
        if (!a->used) continue;
        k_printf("%u  %s  %u  0x%x  %u/%u  %u  %u  %u\n",
                 a->id, a->name, a->priority, a->capabilities,
                 a->tokens_used, a->token_budget, a->page_budget,
                 a->committed_jobs, a->rolled_back_jobs);
    }
    console_write("cap bits: infer=1 fork=2 write=4 commit=8 spawn=10 device=20 inspect=40\n");
}

static void command_agent_demo(void) {
    Agent *a1 = agent_by_id(2);
    Agent *a2 = agent_by_id(3);
    Agent *a3 = agent_by_id(4);
    if (!a1 || !a2 || !a3) { console_write("demo agents unavailable\n"); return; }

    Session *s1 = NULL, *s2 = NULL, *s3 = NULL;
    if (agent_has(a1, CAP_INFER, "model.infer"))
        s1 = session_create(a1->id, 1, a1->priority, "analyze tensor residency", 10);
    if (agent_has(a2, CAP_INFER, "model.infer"))
        s2 = session_create(a2->id, 1, a2->priority, "verify shared context safety", 10);
    if (agent_has(a3, CAP_INFER, "model.infer"))
        s3 = session_create(a3->id, 1, a3->priority, "plan low power decode", 10);
    (void)transaction_begin(a1);
    (void)transaction_begin(a2);
    (void)transaction_begin(a3);
    scheduler_run_until_idle(4096u);
    bool r1 = agent_finish_session(a1, s1, "alpha.txt", false);
    bool r2 = agent_finish_session(a2, s2, "beta.txt", false);
    bool r3 = agent_finish_session(a3, s3, "gamma.txt", false);
    k_printf("multi-agent verified commits: alpha=%s beta=%s gamma=%s\n",
             r1 ? "yes" : "no", r2 ? "yes" : "no", r3 ? "yes" : "no");
    if (s1) session_drop(s1);
    if (s2) session_drop(s2);
    if (s3) session_drop(s3);
}

static void command_agent_run(const char *args, bool force_fail) {
    bool ok = false;
    uint32_t id = k_parse_u32(args, &ok);
    if (!ok) { console_write("usage: agent run <id> <goal>\n"); return; }
    while (*args == ' ') ++args;
    while (*args >= '0' && *args <= '9') ++args;
    while (*args == ' ') ++args;
    Agent *agent = agent_by_id((uint16_t)id);
    if (!agent || !*args) { console_write("agent or goal missing\n"); return; }
    char filename[FILE_NAME_CAP];
    filename[0] = 'a'; filename[1] = 'g'; filename[2] = 'e'; filename[3] = 'n'; filename[4] = 't';
    filename[5] = '-';
    uint32_t pos = 6;
    if (id >= 10u) filename[pos++] = (char)('0' + (id / 10u) % 10u);
    filename[pos++] = (char)('0' + id % 10u);
    k_strncpy(&filename[pos], ".txt", sizeof(filename) - pos);
    bool result = agent_run_goal(agent, args, filename, force_fail);
    k_printf("agent %u job: %s\n", id, result ? "verified and committed" : "rolled back");
}

static void command_workspace(void) {
    bool any = false;
    for (size_t i = 0; i < ARRAY_COUNT(workspace); ++i) {
        WorkspaceFile *f = &workspace[i];
        if (!f->used) continue;
        any = true;
        k_printf("%s owner=%u version=%u bytes=%u\n",
                 f->name, f->owner, f->version, (unsigned)k_strlen(f->data));
    }
    if (!any) console_write("workspace object store is empty\n");
}

static void command_cat(const char *name) {
    while (name && *name == ' ') ++name;
    WorkspaceFile *f = workspace_find(name);
    if (!f) { console_write("object not found\n"); return; }
    k_printf("--- %s v%u ---\n%s\n", f->name, f->version, f->data);
}

static void command_journal(void) {
    if (!journal_count) { console_write("journal empty\n"); return; }
    uint32_t start = (journal_head + JOURNAL_CAP - journal_count) % JOURNAL_CAP;
    uint32_t skip = journal_count > 32u ? journal_count - 32u : 0u;
    for (uint32_t n = skip; n < journal_count; ++n) {
        JournalEntry *e = &journal[(start + n) % JOURNAL_CAP];
        k_printf("%llu agent=%u %s %s\n", (unsigned long long)e->tick, e->actor,
                 e->allowed ? "allow" : "deny", e->action);
    }
}

/* -------------------------------------------------------------------------- */
/* P2 slice: real transformer forward pass over a block-loaded LMOF package    */
/*                                                                              */
/* This is additive to, and separate from, the simulated NativeLM-test        */
/* scheduler path above: it is a genuine RMSNorm / RoPE / grouped-query        */
/* attention / SwiGLU / sampling forward pass in Q16.16 fixed point (the       */
/* kernel build disables x87/SSE/NEON, so there is no runtime float), reading  */
/* int8-quantized weights validated by lmof_parse() out of a package fetched   */
/* over a real VirtIO block driver. Shapes are compiled in rather than fully   */
/* dynamic -- a loaded package must declare exactly this geometry -- and the   */
/* KV cache here is a flat per-command buffer, not yet unified with the        */
/* paged, copy-on-write KV system the scheduler above uses. Both of those are  */
/* documented scope cuts, not oversights; see docs/ROADMAP.md.                 */
/* -------------------------------------------------------------------------- */

#define M2_DIM         32u
#define M2_LAYERS      2u
#define M2_HEADS       4u
#define M2_KV_HEADS    2u
#define M2_HEAD_DIM    8u
#define M2_FFN_HIDDEN  64u
#define M2_VOCAB       TOKENIZER_VOCAB_SIZE
#define M2_MAX_CTX     64u
#define M2_GROUP_SIZE  (M2_HEADS / M2_KV_HEADS)
#define M2_EPS_FX      1

#define M2_PACKAGE_CAP (96u * 1024u)
static uint8_t m2_package[M2_PACKAGE_CAP] __attribute__((aligned(512)));
static LmofPackage m2_pkg;
static bool m2_loaded;

static fx_t m2_key_cache[M2_LAYERS][M2_MAX_CTX][M2_KV_HEADS][M2_HEAD_DIM];
static fx_t m2_val_cache[M2_LAYERS][M2_MAX_CTX][M2_KV_HEADS][M2_HEAD_DIM];
static uint32_t m2_seed = 0xC0FFEEu;

static const LmofTensorRecord *m2_find(const char *name, uint32_t layer) {
    return lmof_find_tensor(&m2_pkg, name, layer);
}

static bool m2_dequant_named(const char *name, uint32_t layer, uint32_t n, fx_t *out) {
    const LmofTensorRecord *rec = m2_find(name, layer);
    if (!rec || rec->length != n) return false;
    const int8_t *data = lmof_tensor_data(&m2_pkg, rec);
    for (uint32_t i = 0; i < n; ++i) out[i] = rec->scale_fx * (int32_t)data[i];
    return true;
}

static bool m2_matvec_named(const char *name, uint32_t layer, const fx_t *x,
                            uint32_t out_dim, uint32_t in_dim, fx_t *out) {
    const LmofTensorRecord *rec = m2_find(name, layer);
    if (!rec || rec->length != (uint64_t)out_dim * in_dim) return false;
    fx_matvec(x, lmof_tensor_data(&m2_pkg, rec), out_dim, in_dim, rec->scale_fx, out);
    return true;
}

static bool m2_forward_token(uint32_t token, uint32_t position, fx_t *logits) {
    fx_t hidden[M2_DIM];
    const LmofTensorRecord *emb = m2_find("token_embedding", LMOF_LAYER_GLOBAL);
    if (!emb || emb->length != (uint64_t)M2_VOCAB * M2_DIM || token >= M2_VOCAB) return false;
    const int8_t *emb_row = lmof_tensor_data(&m2_pkg, emb) + (size_t)token * M2_DIM;
    for (uint32_t i = 0; i < M2_DIM; ++i) hidden[i] = emb->scale_fx * (int32_t)emb_row[i];

    for (uint32_t layer = 0; layer < M2_LAYERS; ++layer) {
        fx_t norm_w[M2_DIM], normed[M2_DIM];
        if (!m2_dequant_named("attn_norm", layer, M2_DIM, norm_w)) return false;
        rmsnorm_fx(hidden, norm_w, M2_DIM, M2_EPS_FX, normed);

        fx_t q[M2_HEADS * M2_HEAD_DIM], k[M2_KV_HEADS * M2_HEAD_DIM], v[M2_KV_HEADS * M2_HEAD_DIM];
        if (!m2_matvec_named("wq", layer, normed, M2_HEADS * M2_HEAD_DIM, M2_DIM, q)) return false;
        if (!m2_matvec_named("wk", layer, normed, M2_KV_HEADS * M2_HEAD_DIM, M2_DIM, k)) return false;
        if (!m2_matvec_named("wv", layer, normed, M2_KV_HEADS * M2_HEAD_DIM, M2_DIM, v)) return false;

        for (uint32_t h = 0; h < M2_HEADS; ++h) rope_apply_fx(&q[h * M2_HEAD_DIM], M2_HEAD_DIM, position);
        for (uint32_t kh = 0; kh < M2_KV_HEADS; ++kh) rope_apply_fx(&k[kh * M2_HEAD_DIM], M2_HEAD_DIM, position);

        if (position >= M2_MAX_CTX) return false;
        for (uint32_t kh = 0; kh < M2_KV_HEADS; ++kh) {
            for (uint32_t d = 0; d < M2_HEAD_DIM; ++d) {
                m2_key_cache[layer][position][kh][d] = k[kh * M2_HEAD_DIM + d];
                m2_val_cache[layer][position][kh][d] = v[kh * M2_HEAD_DIM + d];
            }
        }

        fx_t attn_out[M2_HEADS * M2_HEAD_DIM];
        fx_t keys_buf[M2_MAX_CTX * M2_HEAD_DIM], vals_buf[M2_MAX_CTX * M2_HEAD_DIM];
        for (uint32_t h = 0; h < M2_HEADS; ++h) {
            uint32_t kv_head = h / M2_GROUP_SIZE;
            for (uint32_t j = 0; j <= position; ++j) {
                for (uint32_t d = 0; d < M2_HEAD_DIM; ++d) {
                    keys_buf[j * M2_HEAD_DIM + d] = m2_key_cache[layer][j][kv_head][d];
                    vals_buf[j * M2_HEAD_DIM + d] = m2_val_cache[layer][j][kv_head][d];
                }
            }
            attention_head_fx(&q[h * M2_HEAD_DIM], keys_buf, vals_buf, position + 1u,
                              M2_HEAD_DIM, &attn_out[h * M2_HEAD_DIM]);
        }

        fx_t attn_proj[M2_DIM];
        if (!m2_matvec_named("wo", layer, attn_out, M2_DIM, M2_HEADS * M2_HEAD_DIM, attn_proj)) return false;
        for (uint32_t i = 0; i < M2_DIM; ++i) hidden[i] += attn_proj[i];

        fx_t ffn_norm_w[M2_DIM], normed2[M2_DIM];
        if (!m2_dequant_named("ffn_norm", layer, M2_DIM, ffn_norm_w)) return false;
        rmsnorm_fx(hidden, ffn_norm_w, M2_DIM, M2_EPS_FX, normed2);

        fx_t h1[M2_FFN_HIDDEN], h3[M2_FFN_HIDDEN], gated[M2_FFN_HIDDEN], ffn_out[M2_DIM];
        if (!m2_matvec_named("w1", layer, normed2, M2_FFN_HIDDEN, M2_DIM, h1)) return false;
        if (!m2_matvec_named("w3", layer, normed2, M2_FFN_HIDDEN, M2_DIM, h3)) return false;
        for (uint32_t i = 0; i < M2_FFN_HIDDEN; ++i) gated[i] = fx_mul(silu_fx(h1[i]), h3[i]);
        if (!m2_matvec_named("w2", layer, gated, M2_DIM, M2_FFN_HIDDEN, ffn_out)) return false;
        for (uint32_t i = 0; i < M2_DIM; ++i) hidden[i] += ffn_out[i];
    }

    fx_t final_norm_w[M2_DIM], final_hidden[M2_DIM];
    if (!m2_dequant_named("final_norm", LMOF_LAYER_GLOBAL, M2_DIM, final_norm_w)) return false;
    rmsnorm_fx(hidden, final_norm_w, M2_DIM, M2_EPS_FX, final_hidden);

    return m2_matvec_named("token_embedding", LMOF_LAYER_GLOBAL, final_hidden, M2_VOCAB, M2_DIM, logits);
}

static void command_blk(void) {
    if (!platform_block_present()) {
        console_write("no VirtIO block device found\n");
        return;
    }
    k_printf("virtio-blk: %llu sectors (%llu KiB)\n",
             (unsigned long long)platform_block_sector_count(),
             (unsigned long long)(platform_block_sector_count() / 2u));
}

static void command_modelload(void) {
    if (!platform_block_present()) {
        console_write("no block device: cannot load an LMOF package on this platform\n");
        return;
    }
    if (!platform_block_read(0, m2_package, 1u)) {
        console_write("block read failed (sector 0)\n");
        return;
    }
    const LmofHeader *probe = (const LmofHeader *)m2_package;
    uint64_t package_bytes = probe->package_bytes;
    if (package_bytes < sizeof(LmofHeader) || package_bytes > M2_PACKAGE_CAP) {
        k_printf("implausible package_bytes=%llu (cap=%u)\n",
                 (unsigned long long)package_bytes, M2_PACKAGE_CAP);
        return;
    }
    uint32_t sectors = (uint32_t)((package_bytes + 511u) / 512u);
    if (!platform_block_read(0, m2_package, sectors)) {
        console_write("block read failed (full package)\n");
        return;
    }

    LmofResult r = lmof_parse(m2_package, (size_t)package_bytes, &m2_pkg);
    if (r != LMOF_OK) {
        k_printf("lmof parse failed: %s\n", lmof_result_string(r));
        m2_loaded = false;
        return;
    }
    const LmofHeader *h = m2_pkg.header;
    if (h->dimension != M2_DIM || h->layers != M2_LAYERS || h->heads != M2_HEADS ||
        h->kv_heads != M2_KV_HEADS || h->head_dim != M2_HEAD_DIM ||
        h->ffn_hidden != M2_FFN_HIDDEN || h->vocab_size != M2_VOCAB) {
        console_write("package geometry does not match this kernel's compiled-in shape\n");
        k_printf("expected dim=%u layers=%u heads=%u kv_heads=%u head_dim=%u ffn=%u vocab=%u\n",
                 M2_DIM, M2_LAYERS, M2_HEADS, M2_KV_HEADS, M2_HEAD_DIM, M2_FFN_HIDDEN, M2_VOCAB);
        m2_loaded = false;
        return;
    }
    k_memset(m2_key_cache, 0, sizeof(m2_key_cache));
    k_memset(m2_val_cache, 0, sizeof(m2_val_cache));
    m2_loaded = true;
    k_printf("loaded: dim=%u layers=%u heads=%u kv_heads=%u head_dim=%u ffn=%u vocab=%u tensors=%u\n",
             h->dimension, h->layers, h->heads, h->kv_heads, h->head_dim, h->ffn_hidden,
             h->vocab_size, h->tensor_count);
    console_write("content hash verified; weights are pinned for this session\n");
}

static void command_infer2(const char *prompt) {
    if (!m2_loaded) { console_write("no model loaded; run 'modelload' first\n"); return; }
    if (!prompt || !*prompt) { console_write("usage: infer2 <prompt>\n"); return; }

    uint32_t tokens[M2_MAX_CTX];
    uint32_t prompt_count = tokenizer_encode(prompt, tokens, M2_MAX_CTX / 2u);
    if (!prompt_count) { console_write("prompt too long for compiled-in context\n"); return; }

    uint64_t start_cycles = platform_cycles();
    fx_t logits[M2_VOCAB];
    uint32_t position = 0;
    for (; position < prompt_count; ++position) {
        if (!m2_forward_token(tokens[position], position, logits)) {
            console_write("forward pass failed (tensor shape mismatch)\n");
            return;
        }
    }

    uint32_t generated[M2_MAX_CTX];
    uint32_t generated_count = 0;
    uint32_t next = tokens[prompt_count - 1u];
    while (position < M2_MAX_CTX && generated_count < 24u) {
        next = sample_fx(logits, M2_VOCAB, FX_ONE, &m2_seed);
        if (next == TOKEN_EOS) break;
        generated[generated_count++] = next;
        if (!m2_forward_token(next, position, logits)) break;
        ++position;
    }
    uint64_t elapsed = platform_cycles() - start_cycles;

    char text[M2_MAX_CTX];
    uint32_t len = tokenizer_decode(generated, generated_count, text, sizeof(text));
    (void)len;
    k_printf("infer2: %u prompt tokens, %u generated tokens, cycles=%llu\n",
             prompt_count, generated_count, (unsigned long long)elapsed);
    k_printf("output: %s\n", text);
    console_write("int8/fixed-point forward pass over synthetic, untrained test weights;\n"
                  "not a measure of language-model quality.\n");
}

/* -------------------------------------------------------------------------- */
/* Self-tests                                                                  */
/* -------------------------------------------------------------------------- */

static bool test_page_lifecycle(void) {
    uint32_t before = free_top;
    int p = page_allocate(PAGE_SCRATCH, 1, 99, 0);
    if (p < 0 || free_top + 1u != before) return false;
    if (!page_reference((uint16_t)p) || page_meta[p].refs != 2u) return false;
    page_release((uint16_t)p);
    page_release((uint16_t)p);
    return free_top == before;
}

static bool test_kv_cow(void) {
    Session *a = session_create(1, 1, 1, "base", 2);
    if (!a) return false;
    scheduler_run_until_idle(128);
    Session *b = session_fork(a, 2, "branch", 1);
    if (!b || !a->kv_page_count || a->kv_pages[0] != b->kv_pages[0]) {
        if (a) session_drop(a);
        if (b) session_drop(b);
        return false;
    }
    uint16_t shared = a->kv_pages[b->kv_page_count - 1u];
    uint64_t before = page_cow_copies;
    bool appended = session_append_token_hash(b, 0xa5a5u);
    bool ok = appended && page_cow_copies == before + 1u &&
              a->kv_pages[a->kv_page_count - 1u] == shared &&
              b->kv_pages[b->kv_page_count - 1u] != shared;
    session_drop(a);
    session_drop(b);
    return ok;
}

static bool test_batching(void) {
    uint64_t before_avoided = scheduler_weight_page_reads_avoided;
    Session *a = session_create(1, 1, 2, "one", 2);
    Session *b = session_create(2, 1, 2, "two", 2);
    if (!a || !b) { if (a) session_drop(a); if (b) session_drop(b); return false; }
    scheduler_run_until_idle(128);
    bool ok = a->state == SESSION_DONE && b->state == SESSION_DONE &&
              scheduler_weight_page_reads_avoided > before_avoided;
    session_drop(a);
    session_drop(b);
    return ok;
}

static bool test_capability_denial(void) {
    Agent *restricted = agent_by_id(4);
    return restricted && !agent_has(restricted, CAP_DEVICE_CONTROL, "selftest.device");
}

static bool test_transaction_rollback(void) {
    Agent *agent = agent_by_id(2);
    if (!agent) return false;
    WorkspaceFile before[WORKSPACE_FILES];
    k_memcpy(before, workspace, sizeof(before));
    bool ok = transaction_begin(agent) &&
              workspace_write(agent, "temporary", "must disappear") &&
              transaction_rollback(agent);
    return ok && k_memcmp(before, workspace, sizeof(before)) == 0;
}

static bool test_denied_job_rolls_back(void) {
    Agent *agent = agent_by_id(4);
    if (!agent) return false;
    WorkspaceFile before[WORKSPACE_FILES];
    k_memcpy(before, workspace, sizeof(before));
    uint32_t rollbacks = agent->rolled_back_jobs;
    bool committed = agent_run_goal(agent, "attempt irreversible device action",
                                    "denied-job", true);
    return !committed && agent->rolled_back_jobs == rollbacks + 1u &&
           k_memcmp(before, workspace, sizeof(before)) == 0;
}

static bool test_weight_sharing(void) {
    Model *m = model_by_id(1);
    if (!m || !m->resident || !m->weight_page_count) return false;
    for (uint16_t i = 0; i < m->weight_page_count; ++i) {
        PageMeta *p = &page_meta[m->weight_pages[i]];
        if (p->role != PAGE_WEIGHT || !(p->flags & PAGE_FLAG_IMMUTABLE) || p->refs != 1u)
            return false;
    }
    return true;
}

static bool test_sha256_known_vector(void) {
    static const uint8_t expect[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    uint8_t digest[32];
    sha256_digest("abc", 3u, digest);
    return k_memcmp(digest, expect, sizeof(expect)) == 0;
}

static bool test_tokenizer_roundtrip(void) {
    uint32_t tokens[64];
    uint32_t n = tokenizer_encode("llmos native", tokens, ARRAY_COUNT(tokens));
    if (!n) return false;
    char text[64];
    uint32_t len = tokenizer_decode(tokens, n, text, sizeof(text));
    return len == k_strlen("llmos native") && k_strcmp(text, "llmos native") == 0;
}

static bool test_rope_preserves_norm(void) {
    fx_t v[8];
    int64_t before = 0, after = 0;
    for (uint32_t i = 0; i < 8u; ++i) {
        v[i] = fx_from_int((int32_t)i) - fx_from_int(4);
        before += (int64_t)fx_mul(v[i], v[i]);
    }
    if (!rope_apply_fx(v, 8u, 3u)) return false;
    for (uint32_t i = 0; i < 8u; ++i) after += (int64_t)fx_mul(v[i], v[i]);
    int64_t diff = before > after ? before - after : after - before;
    return diff < (before / 1000 + 4); /* rotation preserves L2 norm within rounding */
}

static bool test_attention_single_position_returns_value(void) {
    fx_t q[4], k[4], v[4], out[4];
    for (uint32_t i = 0; i < 4u; ++i) {
        q[i] = fx_from_int(1);
        k[i] = fx_from_int(1);
        v[i] = fx_from_int((int32_t)(i + 1u));
    }
    attention_head_fx(q, k, v, 1u, 4u, out);
    for (uint32_t i = 0; i < 4u; ++i) {
        int32_t diff = out[i] - v[i];
        if (diff < 0) diff = -diff;
        if (diff > 64) return false; /* softmax over one entry is exactly 1.0 */
    }
    return true;
}

static bool test_sigmoid_midpoint(void) {
    fx_t mid = sigmoid_fx(0);
    int32_t diff = mid - FX_ONE / 2;
    if (diff < 0) diff = -diff;
    return diff <= 64;
}

static bool test_lmof_detects_corruption(void) {
    static uint8_t buf[512];
    k_memset(buf, 0, sizeof(buf));
    LmofHeader *h = (LmofHeader *)buf;
    k_memcpy(h->magic, "LMOSLMOF", 8u);
    h->major = LMOF_VERSION_MAJOR;
    h->minor = LMOF_VERSION_MINOR;
    h->header_bytes = (uint32_t)sizeof(LmofHeader);
    h->default_quant = LMOF_QUANT_INT8;
    h->tensor_directory = sizeof(LmofHeader);
    h->tensor_count = 1;
    h->dimension = 8; h->layers = 1; h->heads = 1; h->kv_heads = 1;
    h->head_dim = 8; h->ffn_hidden = 16; h->context_limit = 64; h->vocab_size = 259;

    LmofTensorRecord *rec = (LmofTensorRecord *)(buf + h->tensor_directory);
    k_strncpy(rec->name, "t", LMOF_NAME_CAP);
    rec->rank = 1; rec->dims[0] = 16;
    rec->offset = h->tensor_directory + sizeof(LmofTensorRecord);
    rec->length = 16;
    rec->scale_fx = FX_ONE;
    rec->layer = 0;
    for (uint32_t i = 0; i < 16u; ++i) buf[rec->offset + i] = (uint8_t)i;

    uint64_t package_bytes = rec->offset + rec->length;
    h->package_bytes = package_bytes;

    Sha256Context ctx;
    sha256_init(&ctx);
    size_t hash_off = (size_t)((uint8_t *)h->content_hash - buf);
    sha256_update(&ctx, buf, hash_off);
    sha256_update(&ctx, buf + hash_off + 32u, (size_t)package_bytes - hash_off - 32u);
    sha256_final(&ctx, h->content_hash);

    LmofPackage pkg;
    if (lmof_parse(buf, (size_t)package_bytes, &pkg) != LMOF_OK) return false;
    if (!lmof_find_tensor(&pkg, "t", 0)) return false;

    buf[rec->offset] ^= 0xffu;
    LmofPackage pkg2;
    return lmof_parse(buf, (size_t)package_bytes, &pkg2) == LMOF_ERR_HASH_MISMATCH;
}

int llmos_run_selftests(bool verbose) {
    unsigned passed = 0;
    unsigned total = 0;
#define RUN(label, expr) do { ++total; bool r_ = (expr); if (r_) ++passed; if (verbose) k_printf("  %s: %s\n", label, r_ ? "PASS" : "FAIL"); } while (0)
    RUN("tensor page lifecycle", test_page_lifecycle());
    RUN("immutable shared model weights", test_weight_sharing());
    RUN("KV context copy-on-write", test_kv_cow());
    RUN("model-affine dynamic batching", test_batching());
    RUN("agent capability denial", test_capability_denial());
    RUN("transaction rollback", test_transaction_rollback());
    RUN("denied agent job rolls back", test_denied_job_rolls_back());
    RUN("sha256 known vector", test_sha256_known_vector());
    RUN("tokenizer roundtrip", test_tokenizer_roundtrip());
    RUN("fixed-point rope preserves norm", test_rope_preserves_norm());
    RUN("fixed-point attention single position", test_attention_single_position_returns_value());
    RUN("fixed-point sigmoid midpoint", test_sigmoid_midpoint());
    RUN("lmof detects content corruption", test_lmof_detects_corruption());
#undef RUN
    if (verbose) k_printf("selftest: %u/%u passed\n", passed, total);
    return passed == total ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Initialization and shell                                                    */
/* -------------------------------------------------------------------------- */

static char *skip_spaces(char *s) { while (s && *s == ' ') ++s; return s; }

static char *split_command(char *line) {
    while (*line && *line != ' ') ++line;
    if (*line) *line++ = 0;
    return skip_spaces(line);
}

void llmos_dispatch(char *line) {
    line = skip_spaces(line);
    if (!line || !*line) return;
    char *args = split_command(line);

    if (k_strcmp(line, "help") == 0) command_help();
    else if (k_strcmp(line, "about") == 0) command_about();
    else if (k_strcmp(line, "arch") == 0) command_arch();
    else if (k_strcmp(line, "models") == 0) command_models();
    else if (k_strcmp(line, "mem") == 0) command_mem();
    else if (k_strcmp(line, "pages") == 0) command_pages();
    else if (k_strcmp(line, "sessions") == 0) command_sessions();
    else if (k_strcmp(line, "sched") == 0) command_sched();
    else if (k_strcmp(line, "infer") == 0) command_infer(args);
    else if (k_strcmp(line, "fork") == 0) command_fork(args);
    else if (k_strcmp(line, "drop") == 0) command_drop(args);
    else if (k_strcmp(line, "step") == 0) k_printf("scheduler: %s\n", scheduler_step() ? "ran batch" : "idle");
    else if (k_strcmp(line, "tensorbench") == 0) command_tensorbench();
    else if (k_strcmp(line, "batchdemo") == 0) command_batchdemo();
    else if (k_strcmp(line, "agents") == 0) command_agents();
    else if (k_strcmp(line, "agent") == 0) {
        if (k_strcmp(args, "demo") == 0 || !*args) command_agent_demo();
        else if (k_starts_with(args, "run ")) command_agent_run(args + 4, false);
        else if (k_starts_with(args, "fail ")) command_agent_run(args + 5, true);
        else console_write("usage: agent demo | agent run <id> <goal> | agent fail <id> <goal>\n");
    }
    else if (k_strcmp(line, "workspace") == 0) command_workspace();
    else if (k_strcmp(line, "cat") == 0) command_cat(args);
    else if (k_strcmp(line, "journal") == 0) command_journal();
    else if (k_strcmp(line, "blk") == 0) command_blk();
    else if (k_strcmp(line, "modelload") == 0) command_modelload();
    else if (k_strcmp(line, "infer2") == 0) command_infer2(args);
    else if (k_strcmp(line, "selftest") == 0) (void)llmos_run_selftests(true);
    else if (k_strcmp(line, "halt") == 0) platform_halt();
    else k_printf("unknown command: %s\n", line);
}

void llmos_initialize(bool interactive) {
    page_system_init();
    k_memset(models, 0, sizeof(models));
    k_memset(sessions, 0, sizeof(sessions));
    k_memset(agents, 0, sizeof(agents));
    k_memset(workspace, 0, sizeof(workspace));
    k_memset(transactions, 0, sizeof(transactions));
    k_memset(journal, 0, sizeof(journal));
    next_session_id = 1;
    next_agent_id = 1;
    scheduler_ticks = 0;
    scheduler_batches = 0;
    scheduler_batch_members = 0;
    scheduler_prefill_tokens = 0;
    scheduler_decode_tokens = 0;
    scheduler_weight_page_reads = 0;
    scheduler_weight_page_reads_avoided = 0;
    scheduler_admission_denials = 0;
    journal_head = 0;
    journal_count = 0;

    bool model_ok = model_register_native_test();
    Agent *root = agent_spawn(NULL, "kernel-supervisor", 7, 4096, 64,
        CAP_INFER | CAP_FORK_CONTEXT | CAP_WORKSPACE_WRITE | CAP_COMMIT |
        CAP_SPAWN_AGENT | CAP_DEVICE_CONTROL | CAP_INSPECT);
    if (root) {
        (void)agent_spawn(root, "interactive", 6, 512, 16,
            CAP_INFER | CAP_FORK_CONTEXT | CAP_WORKSPACE_WRITE | CAP_COMMIT | CAP_INSPECT);
        (void)agent_spawn(root, "background", 2, 512, 16,
            CAP_INFER | CAP_FORK_CONTEXT | CAP_WORKSPACE_WRITE | CAP_COMMIT | CAP_INSPECT);
        (void)agent_spawn(root, "restricted", 4, 256, 8,
            CAP_INFER | CAP_WORKSPACE_WRITE | CAP_COMMIT | CAP_INSPECT);
    }
    journal_add(0, model_ok ? "boot.model.ready" : "boot.model.failed", model_ok);
    if (interactive) print_banner();
}

void kernel_main(uint64_t boot_arg) {
    platform_early_init(boot_arg);
    llmos_initialize(true);
    char line[256];
    for (;;) {
        console_write("llmos> ");
        (void)console_readline(line, sizeof(line));
        llmos_dispatch(line);
    }
}
