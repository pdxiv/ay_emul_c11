/* See engine/include/ay_engine/trace_log.h. */
#include "ay_engine/trace_log.h"

#include <stdio.h>
#include <stdlib.h>

static FILE* g_ay_log = NULL;
static int g_ay_checked = 0;
static FILE* g_irq_log = NULL;
static int g_irq_checked = 0;
static FILE* g_step_log = NULL;
static int g_step_checked = 0;
static FILE* g_mfp_log = NULL;
static int g_mfp_checked = 0;

static FILE* open_from_env(const char* var) {
  const char* path = getenv(var);
  if (path == NULL || path[0] == '\0') return NULL;
  FILE* f = fopen(path, "w");
  if (f != NULL) setvbuf(f, NULL, _IOLBF, 0);
  return f;
}

void trace_log_ay(int64_t cycle, const char* event, int reg, int value) {
  if (!g_ay_checked) {
    g_ay_log = open_from_env("AY_ENGINE_AY_TRACE");
    g_ay_checked = 1;
  }
  if (g_ay_log == NULL) return;
  fprintf(g_ay_log, "cycle=%lld event=%s reg=%d value=%d\n",
          (long long)cycle, event, reg, value);
}

void trace_log_irq(int64_t cycle, const char* event, int level, int vector) {
  if (!g_irq_checked) {
    g_irq_log = open_from_env("AY_ENGINE_IRQ_TRACE");
    g_irq_checked = 1;
  }
  if (g_irq_log == NULL) return;
  fprintf(g_irq_log, "cycle=%lld event=%s level=%d vector=%d\n",
          (long long)cycle, event, level, vector);
}

void trace_log_step(int64_t cycle, int64_t min, int used) {
  if (!g_step_checked) {
    g_step_log = open_from_env("AY_ENGINE_STEP_TRACE");
    g_step_checked = 1;
  }
  if (g_step_log == NULL) return;
  fprintf(g_step_log, "cycle=%lld min=%lld used=%d\n", (long long)cycle,
          (long long)min, used);
}

void trace_log_mfp(int64_t cycle, int reg, int value, bool released) {
  if (!g_mfp_checked) {
    g_mfp_log = open_from_env("AY_ENGINE_MFP_TRACE");
    g_mfp_checked = 1;
  }
  if (g_mfp_log == NULL) return;
  fprintf(g_mfp_log, "cycle=%lld reg=%d value=%d released=%d\n",
          (long long)cycle, reg, value, released ? 1 : 0);
}
