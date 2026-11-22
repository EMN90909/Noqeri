#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

extern int64_t nqNativeMainCompile(const uint8_t* sourcePtr, size_t sourceLen,
                                   uint8_t* outPtr, size_t outCap);

// Simple bump-chunk arena for large (>4KB) local arrays in Noqeri-compiled functions.
typedef struct NqArenaChunk {
    struct NqArenaChunk* prev;
    size_t cap;
    size_t used;
    uint8_t data[];
} NqArenaChunk;

static NqArenaChunk* g_arena_head = NULL;

typedef struct NqArenaMark {
    NqArenaChunk* chunk;
    size_t used;
} NqArenaMark;

// Pack (chunk index/pointer) or use a small mark stack so mark fits in 64 bits.
#define NQ_MAX_MARKS 4096
static NqArenaMark g_marks[NQ_MAX_MARKS];
static size_t g_mark_sp = 0;

uint64_t nq_rt_arena_mark(void) {
    if (g_mark_sp >= NQ_MAX_MARKS) {
        fprintf(stderr, "noqeri native runtime: arena mark stack overflow\n");
        abort();
    }
    size_t idx = g_mark_sp++;
    g_marks[idx].chunk = g_arena_head;
    g_marks[idx].used = g_arena_head ? g_arena_head->used : 0;
    return (uint64_t)idx;
}

void* nq_rt_arena_alloc(size_t size) {
    size_t aligned = (size + 15u) & ~(size_t)15u;
    if (aligned == 0) aligned = 16;
    if (!g_arena_head || g_arena_head->used + aligned > g_arena_head->cap) {
        size_t chunk_cap = aligned > (16u * 1024u * 1024u) ? aligned : (16u * 1024u * 1024u);
        NqArenaChunk* c = (NqArenaChunk*)calloc(1, sizeof(NqArenaChunk) + chunk_cap);
        if (!c) {
            fprintf(stderr, "noqeri native runtime: out of memory allocating %zu bytes\n", size);
            abort();
        }
        c->prev = g_arena_head;
        c->cap = chunk_cap;
        c->used = 0;
        g_arena_head = c;
    }
    void* ptr = g_arena_head->data + g_arena_head->used;
    memset(ptr, 0, aligned);
    g_arena_head->used += aligned;
    return ptr;
}

void nq_rt_arena_reset(uint64_t mark_idx) {
    if (mark_idx >= g_mark_sp) return;
    NqArenaMark m = g_marks[mark_idx];
    g_mark_sp = (size_t)mark_idx;
    while (g_arena_head && g_arena_head != m.chunk) {
        NqArenaChunk* prev = g_arena_head->prev;
        free(g_arena_head);
        g_arena_head = prev;
    }
    if (g_arena_head) {
        g_arena_head->used = m.used;
    }
}

void nq_rt_bounds_trap(void) {
    fprintf(stderr, "noqeri native runtime: bounds check failed\n");
    abort();
}

void nq_rt_print_i64(int64_t val) {
    printf("%lld\n", (long long)val);
}

static char* trim_line(char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
    return s;
}

static int redundant_forward_proto(const char* clean) {
    if (strncmp(clean, "function ", 9) != 0) return 0;
    if (strchr(clean, '{') != NULL) return 0;
    return strchr(clean, '(') != NULL && strchr(clean, ')') != NULL;
}

static uint8_t* bundle_manifest(const char* root, const char* manifest_path, size_t* out_len) {
    FILE* mf = fopen(manifest_path, "rb");
    if (!mf) {
        fprintf(stderr, "cannot open manifest: %s\n", manifest_path);
        return NULL;
    }
    size_t cap = 1024 * 1024;
    size_t len = 0;
    uint8_t* buf = (uint8_t*)malloc(cap);
    const char* hdr = "module noqeri.compiler.selfhost.bundle\n";
    size_t hdr_len = strlen(hdr);
    memcpy(buf, hdr, hdr_len);
    len = hdr_len;

    char line[4096];
    while (fgets(line, sizeof(line), mf)) {
        char* rel = trim_line(line);
        if (*rel == '\0' || *rel == '#') continue;
        char full[8192];
        snprintf(full, sizeof(full), "%s/%s", root, rel);
        FILE* sf = fopen(full, "rb");
        if (!sf) {
            fprintf(stderr, "cannot open source: %s\n", full);
            free(buf);
            fclose(mf);
            return NULL;
        }
        char sline[16384];
        while (fgets(sline, sizeof(sline), sf)) {
            char copy[16384];
            size_t sl = strlen(sline);
            if (sl >= sizeof(copy)) sl = sizeof(copy) - 1;
            memcpy(copy, sline, sl);
            copy[sl] = '\0';
            char* clean = trim_line(copy);
            if (strncmp(clean, "module ", 7) == 0 || strncmp(clean, "import ", 7) == 0) continue;
            if (redundant_forward_proto(clean)) continue;
            while (len + sl + 2 > cap) {
                cap *= 2;
                buf = (uint8_t*)realloc(buf, cap);
            }
            memcpy(buf + len, sline, sl);
            len += sl;
            if (sl == 0 || sline[sl - 1] != '\n') buf[len++] = '\n';
        }
        if (len + 2 > cap) { cap *= 2; buf = (uint8_t*)realloc(buf, cap); }
        buf[len++] = '\n';
        fclose(sf);
    }
    fclose(mf);
    *out_len = len;
    return buf;
}

static uint8_t* read_single_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *out_len = n;
    return buf;
}

int main(int argc, char** argv) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0) {
        if (rl.rlim_cur < 64ULL * 1024ULL * 1024ULL) {
            rl.rlim_cur = 64ULL * 1024ULL * 1024ULL;
            if (rl.rlim_max < rl.rlim_cur) rl.rlim_cur = rl.rlim_max;
            setrlimit(RLIMIT_STACK, &rl);
        }
    }
    if (argc >= 2 && strcmp(argv[1], "--selftest") == 0) {
        return 0;
    }
    size_t src_len = 0;
    uint8_t* src = NULL;
    const char* out_path = NULL;
    if (argc == 5 && strcmp(argv[1], "compile-manifest") == 0) {
        src = bundle_manifest(argv[2], argv[3], &src_len);
        out_path = argv[4];
    } else if (argc == 4 && strcmp(argv[1], "compile") == 0) {
        src = read_single_file(argv[2], &src_len);
        out_path = argv[3];
    } else {
        fprintf(stderr, "usage:\n  %s --selftest\n  %s compile <source.noqeri> <out.o>\n  %s compile-manifest <root> <manifest> <out.o>\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }
    if (!src || src_len == 0) {
        fprintf(stderr, "noqeri native compiler: empty or unreadable input\n");
        free(src);
        return 1;
    }
    size_t out_cap = 16u * 1024u * 1024u;
    uint8_t* out = (uint8_t*)malloc(out_cap);
    if (!out) return 1;
    int64_t written = nqNativeMainCompile(src, src_len, out, out_cap);
    if (written < 0) {
        fprintf(stderr, "noqeri native compiler: compilation failed with status %lld\n", (long long)written);
        free(out);
        free(src);
        return 1;
    }
    FILE* of = fopen(out_path, "wb");
    if (!of) {
        fprintf(stderr, "noqeri native compiler: cannot write %s\n", out_path);
        free(out);
        free(src);
        return 1;
    }
    fwrite(out, 1, (size_t)written, of);
    fclose(of);
    fprintf(stderr, "Wrote native ELF64 object: %s (%lld bytes)\n", out_path, (long long)written);
    free(out);
    free(src);
    return 0;
}
