#ifndef LLMOS_LMOF_H
#define LLMOS_LMOF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * LMOF: LLMOS Model Object Format (see docs/MODEL_FORMAT.md).
 *
 * This is a deliberately reduced first parse of the planned container: a
 * fixed header carrying the manifest fields inline, one tensor directory,
 * and a whole-package SHA-256 content hash. The string table, execution
 * graph directory, packing-variant directory, adapter extents and the
 * integrity Merkle tree from the full design are not implemented yet --
 * tensor names are inline fixed-size fields instead of a string table, and
 * integrity is a single hash rather than a tree. Every reserved directory
 * offset must be 0 so a package cannot silently claim unimplemented
 * sections are present.
 */

#define LMOF_MAGIC_BYTES "LMOSLMOF"
#define LMOF_VERSION_MAJOR 1u
#define LMOF_VERSION_MINOR 0u
#define LMOF_QUANT_INT8 1u
#define LMOF_NAME_CAP 32u
#define LMOF_TENSOR_DIMS 4u
#define LMOF_LAYER_GLOBAL 0xffffffffu

typedef struct {
    uint8_t  magic[8];
    uint16_t major;
    uint16_t minor;
    uint32_t header_bytes;
    uint64_t package_bytes;
    uint8_t  content_hash[32];
    uint32_t model_family;
    uint32_t default_quant;
    uint64_t tensor_directory;    /* byte offset of LmofTensorRecord[tensor_count] */
    uint64_t graph_directory;     /* reserved: must be 0 */
    uint64_t tokenizer_directory; /* reserved: must be 0 (byte tokenizer is implicit) */
    uint64_t variant_directory;   /* reserved: must be 0 */
    uint64_t integrity_root;      /* reserved: must be 0 (no Merkle tree yet) */
    uint32_t tensor_count;
    uint32_t dimension;
    uint32_t layers;
    uint32_t heads;
    uint32_t kv_heads;
    uint32_t head_dim;
    uint32_t ffn_hidden;
    uint32_t context_limit;
    uint32_t vocab_size;
} __attribute__((packed)) LmofHeader;

typedef struct {
    char     name[LMOF_NAME_CAP];
    uint32_t rank;
    uint32_t dims[LMOF_TENSOR_DIMS];
    uint64_t offset;   /* byte offset from package start */
    uint64_t length;   /* stored byte length */
    int32_t  scale_fx; /* Q16.16 per-tensor dequantization scale */
    uint32_t layer;    /* layer index, or LMOF_LAYER_GLOBAL */
} __attribute__((packed)) LmofTensorRecord;

typedef struct {
    const uint8_t *base;
    size_t size;
    const LmofHeader *header;
    const LmofTensorRecord *tensors;
} LmofPackage;

typedef enum {
    LMOF_OK = 0,
    LMOF_ERR_TOO_SMALL,
    LMOF_ERR_MAGIC,
    LMOF_ERR_VERSION,
    LMOF_ERR_SIZE_MISMATCH,
    LMOF_ERR_RESERVED_SECTION,
    LMOF_ERR_TENSOR_DIRECTORY_BOUNDS,
    LMOF_ERR_TENSOR_DATA_BOUNDS,
    LMOF_ERR_HASH_MISMATCH,
} LmofResult;

LmofResult lmof_parse(const uint8_t *data, size_t size, LmofPackage *out);
const char *lmof_result_string(LmofResult r);
const LmofTensorRecord *lmof_find_tensor(const LmofPackage *pkg, const char *name, uint32_t layer);
const int8_t *lmof_tensor_data(const LmofPackage *pkg, const LmofTensorRecord *rec);

#endif
