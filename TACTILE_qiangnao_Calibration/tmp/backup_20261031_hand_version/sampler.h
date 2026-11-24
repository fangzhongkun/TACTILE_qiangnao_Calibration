#ifndef APPLICATION_SAMPLER_H
#define APPLICATION_SAMPLER_H

#include <stdint.h>

/*
 * Strong-brain glove topology, taken from O02.eprj2:
 *   - five fingers, each with X0..X7 through an analogue mux;
 *   - each finger has an independent Y drive bank (Y0..Y10; pinky Y0..Y8);
 *   - one ADC and one active-low mux enable per finger;
 *   - there is no palm sensor on this board.
 */
#define SAMPLER_X_COUNT       8U
#define SAMPLER_MAX_Y_COUNT   11U
#define SAMPLER_Y_COUNT       SAMPLER_MAX_Y_COUNT
#define SAMPLER_FINGER_COUNT  5U

/* This project currently builds the right glove. 0 remains available for a future left build. */
#ifdef HAND_RIGHT
	#define STRONG_GLOVE_HAND_RIGHT 1U
#else
	#define STRONG_GLOVE_HAND_RIGHT 0U
#endif

#define FRAME_DATA_BYTES 2U

/*
 * Finger upload payload:
 *   [endpoint/region-count]
 *   repeated [region-id][valid-point-count][bytes-per-point][samples...]
 *   Each two-byte sample is transmitted low byte first.
 */
#define SAMPLER_REGION_HEADER_SIZE       3U
#define SAMPLER_FINGER_MAX_REGION_COUNT  3U
#define SAMPLER_RECT_MAX_POINTS          (SAMPLER_X_COUNT * SAMPLER_Y_COUNT)
#define SAMPLER_RECT_MAX_PAYLOAD_SIZE    (1U + \
    (SAMPLER_FINGER_MAX_REGION_COUNT * SAMPLER_REGION_HEADER_SIZE) + \
    (SAMPLER_RECT_MAX_POINTS * FRAME_DATA_BYTES))

typedef enum {
    SAMPLER_RECT_THUMB = 1,
    SAMPLER_RECT_INDEX = 2,
    SAMPLER_RECT_MIDDLE = 3,
    SAMPLER_RECT_RING = 4,
    SAMPLER_RECT_PINKY = 5,
    SAMPLER_RECT_COUNT = 5
} sampler_rect_id_t;

typedef enum {
    SAMPLER_REGION_TIP = 1,
    SAMPLER_REGION_MIDDLE = 2,
    SAMPLER_REGION_ROOT = 3
} sampler_region_id_t;

typedef struct {
    uint8_t id;
    uint8_t rows;
    uint8_t cols;
    const uint16_t *data;
    uint8_t stride;
} sampler_rect_view_t;

typedef struct {
    uint16_t finger[SAMPLER_FINGER_COUNT][SAMPLER_MAX_Y_COUNT][SAMPLER_X_COUNT];
} sampler_rectangles_t;

void sampler_hardware_init(void);
void sampler_switch_rows(uint8_t N);
void sampler_switch_cols(uint8_t N);
uint16_t sampler_get_value(void);

void sampler_capture_rectangles(sampler_rectangles_t *rects);
uint8_t sampler_rect_skip_point(uint8_t rect_id, uint8_t x, uint8_t y);
uint8_t sampler_get_rect_view(const sampler_rectangles_t *rects, uint8_t rect_id,
                              sampler_rect_view_t *view);
uint8_t sampler_get_region_bounds(uint8_t rect_id, uint8_t region_id, uint8_t *y_start, uint8_t *y_count);
uint16_t sampler_pack_finger_payload(const sampler_rectangles_t *rects, uint8_t rect_id,
                                     uint8_t *payload, uint16_t payload_size);

#endif
