#ifndef JXL_ENCODE_H
#define JXL_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JxlEncoder JxlEncoder;
typedef struct JxlEncoderFrameSettings JxlEncoderFrameSettings;

enum JxlEncoderStatus {
    JXL_ENC_SUCCESS = 0,
    JXL_ENC_ERROR = 1,
    JXL_ENC_NEED_MORE_OUTPUT = 2
};

struct JxlBasicInfo {
    uint32_t xsize;
    uint32_t ysize;
    uint32_t bits_per_sample;
    uint32_t exponent_bits_per_sample;
};

struct JxlColorEncoding { int dummy; };

struct JxlPixelFormat {
    uint32_t num_channels;
    int data_type;
    int endianness;
    uint32_t align;
};

#define JXL_TYPE_FLOAT 1
#define JXL_LITTLE_ENDIAN 0

static inline JxlEncoder* JxlEncoderCreate(void*) { return new JxlEncoder(); }
static inline void JxlEncoderDestroy(JxlEncoder* enc) { delete enc; }
static inline void JxlEncoderInitBasicInfo(JxlBasicInfo* info) { if (info) *info = {}; }
static inline int JxlEncoderSetBasicInfo(JxlEncoder*, const JxlBasicInfo*) { return 0; }
static inline void JxlColorEncodingSetToLinearSRGB(JxlColorEncoding*, int) {}
static inline int JxlEncoderSetColorEncoding(JxlEncoder*, const JxlColorEncoding*) { return 0; }
static inline JxlEncoderFrameSettings* JxlEncoderFrameSettingsCreate(JxlEncoder*, const void*) { return new JxlEncoderFrameSettings(); }
static inline int JxlEncoderAddImageFrame(const JxlEncoderFrameSettings*, const JxlPixelFormat*, const void*, size_t) { return 0; }
static inline void JxlEncoderCloseInput(JxlEncoder*) {}
static inline JxlEncoderStatus JxlEncoderProcessOutput(JxlEncoder*, uint8_t** next_out, size_t* avail_out) { (void)next_out; (void)avail_out; return JXL_ENC_SUCCESS; }

#ifdef __cplusplus
}
#endif

#endif // JXL_ENCODE_H
