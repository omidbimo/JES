/**
 * Demonstrates the JES streaming serializer to produce a JSON status report
 * for a set of sensors. No tree is allocated — JSON is written directly to
 * the output buffer.
 *
 * Build using GCC
 * gcc -o streaming_serialization streaming_serialization.c ../src/*.c -I ../src
 */

#include <stdio.h>
#include <string.h>
#include "../src/jes.h"

#define OUTPUT_SIZE 512

typedef struct {
  const char* id;
  double      value;
  const char* unit;
  bool        ok;
} sensor_t;

static const sensor_t sensors[] = {
  { "temp_0", 21.5, "C", true  },
  { "temp_1", 19.2, "C", true  },
  { "hum_0",   0.0, "%", false },
};

static const size_t sensor_count = sizeof(sensors) / sizeof(sensors[0]);

int main(void)
{
  static uint8_t stack[JES_STREAMING_SERIALIZER_REQUIRED_SIZE];
  static char    output[OUTPUT_SIZE];

  struct jes_streaming_serializer_context *ctx =
      jes_init_streaming(stack, sizeof(stack), output, sizeof(output));

  if (!ctx) {
    fprintf(stderr, "Failed to initialize streaming serializer\n");
    return 1;
  }

  (void)jes_render_object_start(ctx);

    (void)jes_render_key(ctx, "device", 6);
    (void)jes_render_string(ctx, "sensor-hub-01", 13);

    (void)jes_render_key(ctx, "uptime", 6);
    (void)jes_render_uint32(ctx, 3600);

    (void)jes_render_key(ctx, "sensors", 7);
    (void)jes_render_array_start(ctx);

      for (size_t i = 0; i < sensor_count; i++) {
        const sensor_t *s = &sensors[i];

        (void)jes_render_object_start(ctx);
          (void)jes_render_key(ctx, "id",    2); jes_render_string(ctx, s->id, strlen(s->id));
          (void)jes_render_key(ctx, "value", 5); jes_render_double(ctx, s->value);
          (void)jes_render_key(ctx, "unit",  4); jes_render_string(ctx, s->unit, strlen(s->unit));
          (void)jes_render_key(ctx, "ok",    2); s->ok ? jes_render_true(ctx) : jes_render_false(ctx);
        (void)jes_render_object_end(ctx);
      }

    (void)jes_render_array_end(ctx);

  (void)jes_render_object_end(ctx);

  if (jes_take_streaming_status(ctx) != JES_NO_ERROR) {
    fprintf(stderr, "Serialization failed\n");
    return 1;
  }

  printf("%s\n", output);
  return 0;
}
