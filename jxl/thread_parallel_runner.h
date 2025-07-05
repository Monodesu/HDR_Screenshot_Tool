#ifndef JXL_THREAD_PARALLEL_RUNNER_H
#define JXL_THREAD_PARALLEL_RUNNER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JxlThreadParallelRunner JxlThreadParallelRunner;

static inline JxlThreadParallelRunner* JxlThreadParallelRunnerCreate(void*, size_t) { return nullptr; }
static inline void JxlThreadParallelRunnerDestroy(JxlThreadParallelRunner*) {}

#ifdef __cplusplus
}
#endif

#endif // JXL_THREAD_PARALLEL_RUNNER_H
