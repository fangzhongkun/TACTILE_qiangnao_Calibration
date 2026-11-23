#include "region_lut.h"

#include "internal_flash.h"
#include "sampler.h"

#include <string.h>

extern serial_t serial1;

#define REGION_LUT_PROTOCOL 0x01U

/* 请求命令与应答命令（应答 = 请求 | 0x80）。 */
#define REGION_LUT_CMD_STATUS     0x02U
#define REGION_LUT_CMD_STATUS_ALL 0x03U
#define REGION_LUT_CMD_BEGIN    0x10U
#define REGION_LUT_CMD_WRITE    0x11U
#define REGION_LUT_CMD_VERIFY   0x12U
#define REGION_LUT_CMD_APPLY    0x13U
#define REGION_LUT_CMD_COMMIT   0x14U
#define REGION_LUT_CMD_ABORT    0x16U
#define REGION_LUT_CMD_ERASE    0x17U
#define REGION_LUT_CMD_SET_MODE 0x18U

#define REGION_LUT_ST_OK              0x00U
#define REGION_LUT_ST_PARAM           0x03U
#define REGION_LUT_ST_SESSION         0x04U
#define REGION_LUT_ST_CRC             0x08U
#define REGION_LUT_ST_INCOMPLETE      0x09U
#define REGION_LUT_ST_NOT_MONOTONIC   0x0CU
#define REGION_LUT_ST_SEQUENCE        0x0EU
#define REGION_LUT_ST_FLASH           0x10U
#define REGION_LUT_ST_BUSY            0x11U
#define REGION_LUT_ST_REGION_PARTIAL  0x12U

#define REGION_LUT_HEADER_SIZE   12U
#define REGION_LUT_RESP_DATA_LEN 12U
#define REGION_LUT_RESP_SIZE     (REGION_LUT_HEADER_SIZE + REGION_LUT_RESP_DATA_LEN)

#define REGION_LUT_ERASE_SCOPE_FINGER 0x00U /* 擦整根手指（DataLength=0 时的默认行为） */
#define REGION_LUT_ERASE_SCOPE_REGION 0x01U /* 只擦请求头里指定的区域 */

#define REGION_LUT_POINT_BYTES 8U
#define REGION_LUT_IMAGE_MAGIC 0x54554C52UL /* "RLUT" */
/* V2 uses the unshifted two-byte AD axis; old eight-bit LUT images must not be reused. */
#define REGION_LUT_IMAGE_VERSION 0x0002U

typedef struct
{
    uint8_t  valid;
    uint8_t  point_count;
    uint8_t  mode;
    uint8_t  reserved;
    uint32_t table_crc32;
    uint32_t data_id;
    uint32_t reserved2;
    float    ad[REGION_LUT_MAX_POINTS];
    float    force[REGION_LUT_MAX_POINTS];
} region_lut_slot_t;

typedef struct
{
    uint32_t          magic;
    uint16_t          version;
    uint16_t          slot_count;
    uint32_t          sequence;
    uint32_t          crc32;
    region_lut_slot_t slots[REGION_LUT_SLOT_COUNT];
} region_lut_image_t;

typedef struct
{
    uint8_t  active;
    uint8_t  endpoint;
    uint8_t  region;
    uint16_t session;
    uint8_t  point_count;
    uint8_t  received_count;
    uint8_t  verified;
    uint8_t  applied;
    uint8_t  target_mode;
    uint16_t table_bytes;
    uint32_t table_crc32;
    uint32_t data_id;
    float    ad[REGION_LUT_MAX_POINTS];
    float    force[REGION_LUT_MAX_POINTS];
} region_lut_txn_t;

typedef struct
{
    uint8_t  protocol;
    uint8_t  command;
    uint8_t  endpoint;
    uint8_t  region;
    uint16_t sequence;
    uint16_t session;
    uint16_t data_length;
    uint16_t reserved;
} region_lut_header_t;

typedef struct
{
    uint8_t  status;
    uint8_t  point_count;
    uint8_t  received_count;
    uint8_t  mode;
    uint32_t table_crc32;
    uint32_t data_id;
} region_lut_result_t;

static region_lut_image_t g_image;
static region_lut_txn_t   g_txn;
static uint8_t            g_reboot_pending;

static uint32_t region_lut_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;

    while (length-- > 0UL) {
        uint8_t i;

        crc ^= (uint32_t)(*data++);
        for (i = 0U; i < 8U; ++i) {
            crc = ((crc & 1UL) != 0UL) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t region_lut_region_count(uint8_t endpoint)
{
    /* 拇指只有指尖和指根两个区域，其余四指为三个区域。 */
    return (endpoint == 1U) ? 2U : 3U;
}

static int region_lut_slot_index(uint8_t endpoint, uint8_t region)
{
    if ((endpoint < 1U) || (endpoint > REGION_LUT_ENDPOINT_COUNT)) {
        return -1;
    }
    if ((region < 1U) || (region > region_lut_region_count(endpoint))) {
        return -1;
    }
    return (int)(((endpoint - 1U) * REGION_LUT_REGION_COUNT) + (region - 1U));
}

static uint32_t region_lut_image_payload_crc(const region_lut_image_t *image)
{
    return region_lut_crc32((const uint8_t *)image->slots, (uint32_t)sizeof(image->slots));
}

static uint8_t region_lut_image_is_valid(const region_lut_image_t *image)
{
    if ((image->magic != REGION_LUT_IMAGE_MAGIC) || (image->version != REGION_LUT_IMAGE_VERSION) ||
        (image->slot_count != (uint16_t)REGION_LUT_SLOT_COUNT)) {
        return 0U;
    }
    return (image->crc32 == region_lut_image_payload_crc(image)) ? 1U : 0U;
}

static void region_lut_image_reset(region_lut_image_t *image)
{
    memset(image, 0, sizeof(*image));
    image->magic      = REGION_LUT_IMAGE_MAGIC;
    image->version    = REGION_LUT_IMAGE_VERSION;
    image->slot_count = (uint16_t)REGION_LUT_SLOT_COUNT;
    image->sequence   = 0UL;
    image->crc32      = region_lut_image_payload_crc(image);
}

static void region_lut_txn_clear(void)
{
    memset(&g_txn, 0, sizeof(g_txn));
}

/* 按 STATUS_ALL 的固定位序遍历 14 个区域（拇指 2 个、其余各 3 个），拼出掩码。 */
static uint16_t region_lut_build_mask(uint8_t force_only)
{
    uint16_t mask = 0U;
    uint8_t  bit  = 0U;
    uint8_t  endpoint;
    uint8_t  region;

    for (endpoint = 1U; endpoint <= REGION_LUT_ENDPOINT_COUNT; ++endpoint) {
        for (region = 1U; region <= region_lut_region_count(endpoint); ++region) {
            const region_lut_slot_t *slot = &g_image.slots[region_lut_slot_index(endpoint, region)];

            if ((slot->valid != 0U) && ((force_only == 0U) || (slot->mode == REGION_LUT_MODE_FORCE))) {
                mask |= (uint16_t)(1U << bit);
            }
            ++bit;
        }
    }
    return mask;
}

uint16_t region_lut_calibrated_mask(void)
{
    return region_lut_build_mask(0U);
}

uint16_t region_lut_force_mask(void)
{
    return region_lut_build_mask(1U);
}

uint8_t region_lut_reboot_pending(void)
{
    return g_reboot_pending;
}

void region_lut_init(void)
{
    /* 只读表头比较序号，避免再开一份整镜像大小的 RAM 缓冲。 */
    struct
    {
        uint32_t magic;
        uint16_t version;
        uint16_t slot_count;
        uint32_t sequence;
        uint32_t crc32;
    } header;
    uint32_t addr_by_rank[2];
    uint32_t best_sequence   = 0UL;
    uint8_t  candidate_count = 0U;
    uint8_t  i;

    region_lut_image_reset(&g_image);
    region_lut_txn_clear();

    if (flash_init() != FLASH_OK) {
        return;
    }

    for (i = 0U; i < 2U; ++i) {
        uint32_t addr = (i == 0U) ? ADDR_FLASH_SECTOR_REGION_LUT_A : ADDR_FLASH_SECTOR_REGION_LUT_B;

        if (flash_read(addr, &header, sizeof(header)) != FLASH_OK) {
            continue;
        }
        if ((header.magic != REGION_LUT_IMAGE_MAGIC) || (header.version != REGION_LUT_IMAGE_VERSION) ||
            (header.slot_count != (uint16_t)REGION_LUT_SLOT_COUNT)) {
            continue;
        }
        if ((candidate_count == 1U) && (header.sequence > best_sequence)) {
            addr_by_rank[1] = addr_by_rank[0]; /* 序号大的排前面，先试它 */
            addr_by_rank[0] = addr;
        } else {
            addr_by_rank[candidate_count] = addr;
        }
        if (header.sequence > best_sequence) {
            best_sequence = header.sequence;
        }
        ++candidate_count;
    }

    /* 序号大的那份优先，CRC 不过再退回另一份。 */
    for (i = 0U; i < candidate_count; ++i) {
        if (flash_read(addr_by_rank[i], &g_image, sizeof(g_image)) != FLASH_OK) {
            continue;
        }
        if (region_lut_image_is_valid(&g_image) != 0U) {
            (void)flash_deinit();
            return;
        }
    }

    region_lut_image_reset(&g_image);
    (void)flash_deinit();
}

/* 把内存镜像写进备用扇区，成功后该扇区成为最新有效镜像。 */
static uint8_t region_lut_image_store(void)
{
    uint32_t addr;
    uint32_t primask;
    uint8_t  status = REGION_LUT_ST_FLASH;

    g_image.sequence += 1UL;
    g_image.crc32 = region_lut_image_payload_crc(&g_image);

    /* 序号为奇数写 A，偶数写 B，保证每次写的都不是当前生效的那一份。 */
    addr = ((g_image.sequence & 1UL) != 0UL) ? ADDR_FLASH_SECTOR_REGION_LUT_A
                                             : ADDR_FLASH_SECTOR_REGION_LUT_B;

    /*
     * 擦除和连续编程期间 Flash 不可读，而中断向量表和中断服务函数都在 Flash 里，
     * 此时进中断会读 Flash 导致死机。与工程里其它写 Flash 的地方一样先关中断。
     */
    primask = __get_PRIMASK();
    __disable_irq();
    if (flash_init() == FLASH_OK) {
        if (flash_erase_and_write(addr, (const uint8_t *)&g_image, (uint32_t)sizeof(g_image)) == FLASH_OK) {
            status = REGION_LUT_ST_OK;
        }
        (void)flash_deinit();
    }
    if (primask == 0UL) {
        __enable_irq();
    }

    if (status != REGION_LUT_ST_OK) {
        g_image.sequence -= 1UL;
        g_image.crc32 = region_lut_image_payload_crc(&g_image);
    }
    return status;
}

static void region_lut_fill_result_from_slot(region_lut_result_t *result, int slot_index)
{
    const region_lut_slot_t *slot;

    if (slot_index < 0) {
        return;
    }
    slot = &g_image.slots[slot_index];
    if (slot->valid == 0U) {
        return;
    }

    result->point_count = slot->point_count;
    /* 已提交的表视为全部收齐，与协议示例中 STATUS/SET_MODE 的应答一致。 */
    result->received_count = slot->point_count;
    result->mode           = slot->mode;
    result->table_crc32    = slot->table_crc32;
    result->data_id        = slot->data_id;
}

static uint8_t region_lut_txn_matches(const region_lut_header_t *header)
{
    if (g_txn.active == 0U) {
        return 0U;
    }
    return ((g_txn.endpoint == header->endpoint) && (g_txn.region == header->region) &&
            (g_txn.session == header->session))
               ? 1U
               : 0U;
}

/*
 * 子命令处理接口。所有处理函数签名一致，公共前置条件（DataLength 下限、事务是否
 * 匹配）由派发表统一校验，处理函数只关心自己那一步的业务。
 */
static uint8_t region_lut_handle_status(package_t *package, const region_lut_header_t *header,
                                        region_lut_result_t *result)
{
    (void)package;
    region_lut_fill_result_from_slot(result, region_lut_slot_index(header->endpoint, header->region));
    return REGION_LUT_ST_OK;
}

static uint8_t region_lut_handle_begin(package_t *package, const region_lut_header_t *header,
                                       region_lut_result_t *result)
{
    uint8_t  point_count;
    uint8_t  data_format;
    uint16_t table_bytes;
    uint32_t table_crc32;
    uint32_t data_id;

    if (header->session == 0U) {
        return REGION_LUT_ST_SESSION;
    }
    /* 已有其它区域的事务在进行时拒绝，除非是同一区域同一 Session 的重发。 */
    if ((g_txn.active != 0U) && (region_lut_txn_matches(header) == 0U)) {
        return REGION_LUT_ST_BUSY;
    }

    point_count = package_read_u8(package);
    data_format = package_read_u8(package);
    table_bytes = package_read_u16(package);
    table_crc32 = package_read_u32(package);
    data_id     = package_read_u32(package);

    if ((point_count == 0U) || (point_count > REGION_LUT_MAX_POINTS) || (data_format != 0x01U) ||
        (table_bytes != (uint16_t)(point_count * REGION_LUT_POINT_BYTES))) {
        return REGION_LUT_ST_PARAM;
    }

    region_lut_txn_clear();
    g_txn.active      = 1U;
    g_txn.endpoint    = header->endpoint;
    g_txn.region      = header->region;
    g_txn.session     = header->session;
    g_txn.point_count = point_count;
    g_txn.table_bytes = table_bytes;
    g_txn.table_crc32 = table_crc32;
    g_txn.data_id     = data_id;

    result->point_count    = g_txn.point_count;
    result->received_count = 0U;
    result->table_crc32    = g_txn.table_crc32;
    result->data_id        = g_txn.data_id;
    return REGION_LUT_ST_OK;
}

static uint8_t region_lut_handle_write(package_t *package, const region_lut_header_t *header,
                                       region_lut_result_t *result)
{
    uint8_t  first;
    uint8_t  count;
    uint16_t chunk_bytes;
    uint32_t chunk_crc32;
    uint8_t  chunk[REGION_LUT_MAX_POINTS * REGION_LUT_POINT_BYTES];
    uint8_t  i;

    (void)header;

    first       = package_read_u8(package);
    count       = package_read_u8(package);
    chunk_bytes = package_read_u16(package);
    chunk_crc32 = package_read_u32(package);

    if ((count == 0U) || ((uint16_t)(first + count) > (uint16_t)g_txn.point_count) ||
        (chunk_bytes != (uint16_t)(count * REGION_LUT_POINT_BYTES)) ||
        (package_remain_size(package) < chunk_bytes)) {
        return REGION_LUT_ST_PARAM;
    }

    package_read(package, chunk, chunk_bytes);
    if (region_lut_crc32(chunk, chunk_bytes) != chunk_crc32) {
        return REGION_LUT_ST_CRC;
    }

    for (i = 0U; i < count; ++i) {
        const uint8_t *point = &chunk[(uint16_t)i * REGION_LUT_POINT_BYTES];
        float          ad;
        float          force;

        memcpy(&ad, point, sizeof(ad));
        memcpy(&force, point + 4U, sizeof(force));
        g_txn.ad[first + i]    = ad;
        g_txn.force[first + i] = force;
    }

    /* 重发同一块不重复累计。 */
    if ((uint16_t)(first + count) > (uint16_t)g_txn.received_count) {
        g_txn.received_count = (uint8_t)(first + count);
    }
    g_txn.verified = 0U;
    g_txn.applied  = 0U;

    result->point_count    = g_txn.point_count;
    result->received_count = g_txn.received_count;
    result->table_crc32    = g_txn.table_crc32;
    result->data_id        = g_txn.data_id;
    return REGION_LUT_ST_OK;
}

static uint8_t region_lut_float_is_finite(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return (((bits >> 23U) & 0xFFUL) == 0xFFUL) ? 0U : 1U;
}

static uint8_t region_lut_handle_verify(package_t *package, const region_lut_header_t *header,
                                        region_lut_result_t *result)
{
    uint8_t table[REGION_LUT_MAX_POINTS * REGION_LUT_POINT_BYTES];
    uint8_t i;

    (void)package;
    (void)header;

    result->point_count    = g_txn.point_count;
    result->received_count = g_txn.received_count;
    result->table_crc32    = g_txn.table_crc32;
    result->data_id        = g_txn.data_id;

    if (g_txn.received_count != g_txn.point_count) {
        return REGION_LUT_ST_INCOMPLETE;
    }

    for (i = 0U; i < g_txn.point_count; ++i) {
        if ((region_lut_float_is_finite(g_txn.ad[i]) == 0U) ||
            (region_lut_float_is_finite(g_txn.force[i]) == 0U)) {
            return REGION_LUT_ST_NOT_MONOTONIC;
        }
        if (i > 0U) {
            if (g_txn.ad[i] <= g_txn.ad[i - 1U]) { /* AD 严格递增 */
                return REGION_LUT_ST_NOT_MONOTONIC;
            }
            if (g_txn.force[i] < g_txn.force[i - 1U]) { /* Force 单调不减 */
                return REGION_LUT_ST_NOT_MONOTONIC;
            }
        }
    }

    for (i = 0U; i < g_txn.point_count; ++i) {
        uint8_t *point = &table[(uint16_t)i * REGION_LUT_POINT_BYTES];

        memcpy(point, &g_txn.ad[i], sizeof(float));
        memcpy(point + 4U, &g_txn.force[i], sizeof(float));
    }
    if (region_lut_crc32(table, g_txn.table_bytes) != g_txn.table_crc32) {
        return REGION_LUT_ST_CRC;
    }

    g_txn.verified = 1U;
    return REGION_LUT_ST_OK;
}

static uint8_t region_lut_handle_apply(package_t *package, const region_lut_header_t *header,
                                       region_lut_result_t *result)
{
    uint8_t apply;
    uint8_t target_mode;

    (void)header;

    if (g_txn.verified == 0U) {
        return REGION_LUT_ST_SEQUENCE;
    }

    apply       = package_read_u8(package);
    target_mode = package_read_u8(package);
    if (apply != 0x01U) {
        return REGION_LUT_ST_PARAM;
    }
    if ((target_mode != REGION_LUT_MODE_RAW_AD) && (target_mode != REGION_LUT_MODE_FORCE)) {
        return REGION_LUT_ST_PARAM;
    }

    g_txn.applied = 1U;
    /* 记录目标模式，COMMIT 成功后随表一起生效。 */
    g_txn.target_mode = target_mode;

    result->point_count    = g_txn.point_count;
    result->received_count = g_txn.received_count;
    result->table_crc32    = g_txn.table_crc32;
    result->data_id        = g_txn.data_id;
    return REGION_LUT_ST_OK;
}

static uint8_t region_lut_handle_commit(package_t *package, const region_lut_header_t *header,
                                        region_lut_result_t *result)
{
    uint32_t           table_crc32;
    uint32_t           data_id;
    uint8_t            enabled;
    int                slot_index;
    region_lut_slot_t *slot;
    region_lut_slot_t  backup;
    uint8_t            status;

    (void)header;

    if ((g_txn.verified == 0U) || (g_txn.applied == 0U)) {
        return REGION_LUT_ST_SEQUENCE;
    }

    table_crc32 = package_read_u32(package);
    data_id     = package_read_u32(package);
    enabled     = package_read_u8(package);

    if ((table_crc32 != g_txn.table_crc32) || (data_id != g_txn.data_id)) {
        return REGION_LUT_ST_CRC;
    }

    slot_index = region_lut_slot_index(g_txn.endpoint, g_txn.region);
    if (slot_index < 0) {
        return REGION_LUT_ST_PARAM;
    }

    slot = &g_image.slots[slot_index];
    memcpy(&backup, slot, sizeof(backup));

    memset(slot, 0, sizeof(*slot));
    slot->valid       = (enabled != 0U) ? 1U : 0U;
    slot->point_count = g_txn.point_count;
    slot->mode        = g_txn.target_mode;
    slot->table_crc32 = g_txn.table_crc32;
    slot->data_id     = g_txn.data_id;
    memcpy(slot->ad, g_txn.ad, sizeof(slot->ad));
    memcpy(slot->force, g_txn.force, sizeof(slot->force));

    status = region_lut_image_store();
    if (status != REGION_LUT_ST_OK) {
        memcpy(slot, &backup, sizeof(backup)); /* 写入失败则回滚，旧表继续生效 */
        return status;
    }

    result->point_count    = slot->point_count;
    result->received_count = g_txn.received_count;
    result->mode           = slot->mode;
    result->table_crc32    = slot->table_crc32;
    result->data_id        = slot->data_id;

    region_lut_txn_clear();

    /* 这次提交让 14 个区域全部标定完成：发完本条应答后复位，开机时从 Flash 重新加载。 */
    if (region_lut_calibrated_mask() == REGION_LUT_ALL_MASK) {
        g_reboot_pending = 1U;
    }
    return REGION_LUT_ST_OK;
}

static uint8_t region_lut_handle_abort(package_t *package, const region_lut_header_t *header,
                                       region_lut_result_t *result)
{
    (void)package;
    (void)result;

    if ((g_txn.active != 0U) && (g_txn.endpoint == header->endpoint) &&
        (g_txn.region == header->region)) {
        region_lut_txn_clear();
    }
    return REGION_LUT_ST_OK;
}

static uint8_t region_lut_handle_erase(package_t *package, const region_lut_header_t *header,
                                       region_lut_result_t *result)
{
    uint8_t region;
    uint8_t first   = 1U;
    uint8_t last    = region_lut_region_count(header->endpoint);
    uint8_t touched = 0U;

    /* DataLength=0 擦整根手指（兼容旧上位机）；带 Scope=0x01 时只擦请求头里的这个区域。 */
    if (header->data_length >= 1U) {
        const uint8_t scope = package_read_u8(package);

        if (scope == REGION_LUT_ERASE_SCOPE_REGION) {
            first = header->region;
            last  = header->region;
        } else if (scope != REGION_LUT_ERASE_SCOPE_FINGER) {
            return REGION_LUT_ST_PARAM;
        }
    }

    for (region = first; region <= last; ++region) {
        int slot_index = region_lut_slot_index(header->endpoint, region);

        if (slot_index < 0) {
            continue;
        }
        if (g_image.slots[slot_index].valid != 0U) {
            touched = 1U;
        }
        memset(&g_image.slots[slot_index], 0, sizeof(g_image.slots[slot_index]));
    }

    if ((g_txn.active != 0U) && (g_txn.endpoint == header->endpoint) && (g_txn.region >= first) &&
        (g_txn.region <= last)) {
        region_lut_txn_clear();
    }

    result->point_count    = 0U;
    result->received_count = 0U;
    result->mode           = REGION_LUT_MODE_RAW_AD;
    result->table_crc32    = 0UL;
    result->data_id        = 0UL;

    return (touched == 0U) ? REGION_LUT_ST_OK : region_lut_image_store();
}

static uint8_t region_lut_handle_set_mode(package_t *package, const region_lut_header_t *header,
                                          region_lut_result_t *result)
{
    uint8_t            mode;
    int                slot_index;
    region_lut_slot_t *slot;
    uint8_t            previous_mode;
    uint8_t            status;

    mode = package_read_u8(package);
    if ((mode != REGION_LUT_MODE_RAW_AD) && (mode != REGION_LUT_MODE_FORCE)) {
        return REGION_LUT_ST_PARAM;
    }

    slot_index = region_lut_slot_index(header->endpoint, header->region);
    if (slot_index < 0) {
        return REGION_LUT_ST_PARAM;
    }
    slot = &g_image.slots[slot_index];

    if ((mode == REGION_LUT_MODE_FORCE) && (slot->valid == 0U)) {
        return REGION_LUT_ST_REGION_PARTIAL; /* 没有有效 LUT 时不允许进入力值模式 */
    }

    previous_mode = slot->mode;
    slot->mode    = mode;
    status        = region_lut_image_store();
    if (status != REGION_LUT_ST_OK) {
        slot->mode = previous_mode;
        return status;
    }

    region_lut_fill_result_from_slot(result, slot_index);
    return REGION_LUT_ST_OK;
}

/*
 * STATUS_ALL：一次返回 14 个区域的状态，请求头 Endpoint/Region 填 0。
 * 应答仍是固定 12 字节，本命令的字段含义为：
 *   [0] Status  [1] RegionCount=14  [2..3] CalibratedMask  [4..5] ForceModeMask  [6..11] 保留 0
 * 复用通用应答的序列化：received_count/mode 放 CalibratedMask 的低/高字节，
 * table_crc32 的低 16 位放 ForceModeMask（小端正好落在 [4..5]）。
 */
static uint8_t region_lut_handle_status_all(package_t *package, const region_lut_header_t *header,
                                            region_lut_result_t *result)
{
    const uint16_t calibrated = region_lut_calibrated_mask();

    (void)package;
    (void)header;

    result->point_count    = REGION_LUT_REGION_TOTAL;
    result->received_count = (uint8_t)(calibrated & 0xFFU);
    result->mode           = (uint8_t)(calibrated >> 8U);
    result->table_crc32    = region_lut_force_mask();
    result->data_id        = 0UL;
    return REGION_LUT_ST_OK;
}

/* 子命令派发表：命令码、DataLength 下限、是否校验 Endpoint/Region、是否要求事务已打开、处理接口。 */
typedef uint8_t (*region_lut_handler_t)(package_t *package, const region_lut_header_t *header,
                                        region_lut_result_t *result);

typedef struct
{
    uint8_t              command;
    uint8_t              min_data_length;
    uint8_t              requires_slot;
    uint8_t              requires_txn;
    region_lut_handler_t handler;
} region_lut_command_t;

static const region_lut_command_t m_astcCommands[] = {
    { REGION_LUT_CMD_STATUS,      0U, 1U, 0U, region_lut_handle_status     },
    { REGION_LUT_CMD_STATUS_ALL,  0U, 0U, 0U, region_lut_handle_status_all },
    { REGION_LUT_CMD_BEGIN,      20U, 1U, 0U, region_lut_handle_begin      },
    { REGION_LUT_CMD_WRITE,       8U, 1U, 1U, region_lut_handle_write      },
    { REGION_LUT_CMD_VERIFY,      0U, 1U, 1U, region_lut_handle_verify     },
    { REGION_LUT_CMD_APPLY,       2U, 1U, 1U, region_lut_handle_apply      },
    { REGION_LUT_CMD_COMMIT,     12U, 1U, 1U, region_lut_handle_commit     },
    { REGION_LUT_CMD_ABORT,       0U, 1U, 0U, region_lut_handle_abort      },
    { REGION_LUT_CMD_ERASE,       0U, 1U, 0U, region_lut_handle_erase      },
    { REGION_LUT_CMD_SET_MODE,    1U, 1U, 0U, region_lut_handle_set_mode   },
};

static const region_lut_command_t *region_lut_find_command(uint8_t command)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)(sizeof(m_astcCommands) / sizeof(m_astcCommands[0])); ++i) {
        if (m_astcCommands[i].command == command) {
            return &m_astcCommands[i];
        }
    }
    return NULL;
}

static void region_lut_send_response(package_t *package, const region_lut_header_t *header,
                                     const region_lut_result_t *result)
{
    uint8_t  payload[REGION_LUT_RESP_SIZE];
    uint16_t offset = 0U;

    payload[offset++] = REGION_LUT_PROTOCOL;
    payload[offset++] = (uint8_t)(header->command | 0x80U);
    payload[offset++] = header->endpoint;
    payload[offset++] = header->region;
    payload[offset++] = (uint8_t)(header->sequence & 0xFFU);
    payload[offset++] = (uint8_t)(header->sequence >> 8U);
    payload[offset++] = (uint8_t)(header->session & 0xFFU);
    payload[offset++] = (uint8_t)(header->session >> 8U);
    payload[offset++] = (uint8_t)(REGION_LUT_RESP_DATA_LEN & 0xFFU);
    payload[offset++] = (uint8_t)(REGION_LUT_RESP_DATA_LEN >> 8U);
    payload[offset++] = 0U;
    payload[offset++] = 0U;

    payload[offset++] = result->status;
    payload[offset++] = result->point_count;
    payload[offset++] = result->received_count;
    payload[offset++] = result->mode;
    payload[offset++] = (uint8_t)(result->table_crc32 & 0xFFUL);
    payload[offset++] = (uint8_t)((result->table_crc32 >> 8U) & 0xFFUL);
    payload[offset++] = (uint8_t)((result->table_crc32 >> 16U) & 0xFFUL);
    payload[offset++] = (uint8_t)((result->table_crc32 >> 24U) & 0xFFUL);
    payload[offset++] = (uint8_t)(result->data_id & 0xFFUL);
    payload[offset++] = (uint8_t)((result->data_id >> 8U) & 0xFFUL);
    payload[offset++] = (uint8_t)((result->data_id >> 16U) & 0xFFUL);
    payload[offset++] = (uint8_t)((result->data_id >> 24U) & 0xFFUL);

    serial_async_send(&serial1, package->channel, (uint8_t)((package->flags & 0xFCU) | 0x02U), payload,
                      REGION_LUT_RESP_SIZE);
}

uint8_t region_lut_handle_request(package_t *package)
{
    region_lut_header_t         header;
    region_lut_result_t         result;
    const region_lut_command_t *entry;

    if (package->length < REGION_LUT_HEADER_SIZE) {
        return 0U;
    }

    header.protocol    = package_read_u8(package);
    header.command     = package_read_u8(package);
    header.endpoint    = package_read_u8(package);
    header.region      = package_read_u8(package);
    header.sequence    = package_read_u16(package);
    header.session     = package_read_u16(package);
    header.data_length = package_read_u16(package);
    header.reserved    = package_read_u16(package);

    if (header.protocol != REGION_LUT_PROTOCOL) {
        return 0U;
    }

    memset(&result, 0, sizeof(result));

    /*
     * Protocol 字段已经足以判定这是本模块的帧（旧命令走 flags=0x00，不会进到这里），
     * 所以未知命令要回错误码，绝不能落回旧命令解析——那会把 Command 字节当成旧
     * CONFIG_CALIBRATION 的参数误触发零点标定。
     */
    entry = region_lut_find_command(header.command);
    if (entry == NULL) {
        result.status = REGION_LUT_ST_PARAM;
        region_lut_send_response(package, &header, &result);
        return 1U;
    }

    /* 公共前置条件统一在这里判，处理接口只做自己那一步。 */
    if (((entry->requires_slot != 0U) && (region_lut_slot_index(header.endpoint, header.region) < 0)) ||
        (header.data_length < entry->min_data_length) ||
        (header.data_length > package_remain_size(package))) {
        result.status = REGION_LUT_ST_PARAM;
    } else if ((entry->requires_txn != 0U) && (region_lut_txn_matches(&header) == 0U)) {
        result.status = (g_txn.active == 0U) ? REGION_LUT_ST_SEQUENCE : REGION_LUT_ST_SESSION;
    } else {
        result.status = entry->handler(package, &header, &result);
    }

    region_lut_send_response(package, &header, &result);
    return 1U;
}

static float region_lut_interpolate(const region_lut_slot_t *slot, float ad_value)
{
    uint8_t i;

    if (slot->point_count == 0U) {
        return 0.0f;
    }
    if (ad_value <= slot->ad[0]) {
        return (ad_value < slot->ad[0]) ? 0.0f : slot->force[0];
    }
    if (ad_value >= slot->ad[slot->point_count - 1U]) {
        return slot->force[slot->point_count - 1U];
    }

    for (i = 1U; i < slot->point_count; ++i) {
        if (ad_value <= slot->ad[i]) {
            float span = slot->ad[i] - slot->ad[i - 1U];

            if (span <= 0.0f) {
                return slot->force[i - 1U];
            }
            return slot->force[i - 1U] +
                   ((ad_value - slot->ad[i - 1U]) * (slot->force[i] - slot->force[i - 1U]) / span);
        }
    }
    return slot->force[slot->point_count - 1U];
}

void region_lut_apply_to_rect(uint8_t rect_id, uint16_t *data, uint8_t stride, uint16_t output_limit)
{
    uint8_t region;
    uint8_t endpoint = rect_id;

    if ((data == NULL) || (endpoint < 1U) || (endpoint > REGION_LUT_ENDPOINT_COUNT)) {
        return;
    }

    for (region = 1U; region <= region_lut_region_count(endpoint); ++region) {
        const region_lut_slot_t *slot;
        int      slot_index = region_lut_slot_index(endpoint, region);
        uint8_t  y_start;
        uint8_t  y_count;
        uint8_t  y_offset;
        uint8_t  x;
        uint32_t sum = 0UL;
        float    force;

        if (slot_index < 0) {
            continue;
        }
        slot = &g_image.slots[slot_index];
        if ((slot->valid == 0U) || (slot->mode != REGION_LUT_MODE_FORCE)) {
            continue; /* 未标定或仍在原始 AD 模式的区域保持不变 */
        }
        if (sampler_get_region_bounds(rect_id, region, &y_start, &y_count) == 0U) {
            continue;
        }

        for (y_offset = 0U; y_offset < y_count; ++y_offset) {
            uint8_t y = (uint8_t)(y_start + y_offset);

            for (x = 0U; x < SAMPLER_X_COUNT; ++x) {
                if (sampler_rect_skip_point(rect_id, x, y) != 0U) {
                    continue;
                }
                sum += data[((uint16_t)y * stride) + x];
            }
        }

        if (sum == 0UL) {
            continue;
        }

        /*
         * 查表横轴用"当前区域的 AD 总和"，与上位机 buildWireLut() 建表口径一致。
         * 不要改成平均值：上位机下发的 AD 轴就是区域总和，除以点数会把横轴缩小
         * 几十倍，运行时几乎所有按压都会落在曲线最开头。
         */
        force = region_lut_interpolate(slot, (float)sum);
        if (force <= 0.0f) {
            force = 0.0f;
        }
        /* 总力按各点 AD 占比分摊回每个点。 */
        for (y_offset = 0U; y_offset < y_count; ++y_offset) {
            uint8_t y = (uint8_t)(y_start + y_offset);

            for (x = 0U; x < SAMPLER_X_COUNT; ++x) {
                uint16_t index;
                float    share;

                if (sampler_rect_skip_point(rect_id, x, y) != 0U) {
                    continue;
                }
                index = (uint16_t)(((uint16_t)y * stride) + x);
                share = (force * (float)data[index]) / (float)sum;

                if (share <= 0.0f) {
                    data[index] = 0U;
                } else if (share >= (float)output_limit) {
                    data[index] = output_limit;
                } else {
                    data[index] = (uint16_t)(share + 0.5f);
                }
            }
        }
    }
}
