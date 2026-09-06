#include "llmos/lmof.h"
#include "llmos/sha256.h"

static bool magic_ok(const uint8_t magic[8]) {
    const uint8_t *want = (const uint8_t *)LMOF_MAGIC_BYTES;
    for (uint32_t i = 0; i < 8u; ++i) if (magic[i] != want[i]) return false;
    return true;
}

static bool hash_ok(const uint8_t *data, size_t size, const LmofHeader *header) {
    size_t hash_offset = (size_t)((const uint8_t *)header->content_hash - data);
    Sha256Context ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, hash_offset);
    sha256_update(&ctx, data + hash_offset + 32u, size - hash_offset - 32u);
    uint8_t digest[32];
    sha256_final(&ctx, digest);
    for (uint32_t i = 0; i < 32u; ++i) if (digest[i] != header->content_hash[i]) return false;
    return true;
}

LmofResult lmof_parse(const uint8_t *data, size_t size, LmofPackage *out) {
    if (size < sizeof(LmofHeader)) return LMOF_ERR_TOO_SMALL;
    const LmofHeader *header = (const LmofHeader *)data;
    if (!magic_ok(header->magic)) return LMOF_ERR_MAGIC;
    if (header->major != LMOF_VERSION_MAJOR) return LMOF_ERR_VERSION;
    if (header->package_bytes != (uint64_t)size) return LMOF_ERR_SIZE_MISMATCH;
    if (header->graph_directory != 0u || header->tokenizer_directory != 0u ||
        header->variant_directory != 0u || header->integrity_root != 0u)
        return LMOF_ERR_RESERVED_SECTION;

    uint64_t dir_off = header->tensor_directory;
    uint64_t dir_bytes = (uint64_t)header->tensor_count * (uint64_t)sizeof(LmofTensorRecord);
    if (dir_off > (uint64_t)size || dir_bytes > (uint64_t)size - dir_off)
        return LMOF_ERR_TENSOR_DIRECTORY_BOUNDS;

    const LmofTensorRecord *tensors = (const LmofTensorRecord *)(data + dir_off);
    for (uint32_t i = 0; i < header->tensor_count; ++i) {
        uint64_t t_off = tensors[i].offset;
        uint64_t t_len = tensors[i].length;
        if (t_off > (uint64_t)size || t_len > (uint64_t)size - t_off)
            return LMOF_ERR_TENSOR_DATA_BOUNDS;
    }

    if (!hash_ok(data, size, header)) return LMOF_ERR_HASH_MISMATCH;

    out->base = data;
    out->size = size;
    out->header = header;
    out->tensors = tensors;
    return LMOF_OK;
}

const char *lmof_result_string(LmofResult r) {
    switch (r) {
        case LMOF_OK: return "ok";
        case LMOF_ERR_TOO_SMALL: return "package smaller than header";
        case LMOF_ERR_MAGIC: return "bad magic";
        case LMOF_ERR_VERSION: return "unsupported version";
        case LMOF_ERR_SIZE_MISMATCH: return "package_bytes does not match buffer size";
        case LMOF_ERR_RESERVED_SECTION: return "reserved directory offset is non-zero";
        case LMOF_ERR_TENSOR_DIRECTORY_BOUNDS: return "tensor directory out of bounds";
        case LMOF_ERR_TENSOR_DATA_BOUNDS: return "tensor data out of bounds";
        case LMOF_ERR_HASH_MISMATCH: return "content hash mismatch";
        default: return "unknown";
    }
}

static bool name_eq(const char *field, const char *name) {
    for (uint32_t i = 0; i < LMOF_NAME_CAP; ++i) {
        char a = field[i], b = name[i];
        if (a != b) return false;
        if (a == 0) return true;
    }
    return true;
}

const LmofTensorRecord *lmof_find_tensor(const LmofPackage *pkg, const char *name, uint32_t layer) {
    for (uint32_t i = 0; i < pkg->header->tensor_count; ++i) {
        const LmofTensorRecord *t = &pkg->tensors[i];
        if (t->layer != layer) continue;
        if (name_eq(t->name, name)) return t;
    }
    return NULL;
}

const int8_t *lmof_tensor_data(const LmofPackage *pkg, const LmofTensorRecord *rec) {
    if (!rec) return NULL;
    return (const int8_t *)(pkg->base + rec->offset);
}
