#include "native_extension.h"

#if HAS_NATIVE_EXTENSIONS

#include "log_manager.h"
#include "message_bubble.h"
#include "storage.h"

#include <esp_heap_caps.h>
#include <esp_mmu_map.h>
#include <esp_partition.h>
#include <lvgl.h>
#include <string.h>

#define TAG "Extensions"

namespace {

constexpr uint32_t SLOT_MAGIC = 0x31545845u;
constexpr uint32_t STAGE_MAGIC = 0x31544753u;
constexpr uint32_t ELF_OFFSET = 0x2000;
constexpr uint32_t SLOT_OFFSET[NATIVE_EXTENSION_SLOT_COUNT] = {0x00000, 0x10000, 0x20000};
constexpr uint32_t SLOT_SIZE[NATIVE_EXTENSION_SLOT_COUNT] = {0x10000, 0x10000, 0x20000};
constexpr uint32_t SLOT_CAPACITY[NATIVE_EXTENSION_SLOT_COUNT] = {0xE000, 0xE000, 0x1E000};
constexpr uint16_t ELF_TYPE_DYN = 3;
constexpr uint16_t ELF_MACHINE_RISCV = 243;
constexpr uint32_t ELF_SHT_DYNSYM = 11;
constexpr uint32_t ELF_SHT_RELA = 4;

struct ElfHeader { uint8_t ident[16]; uint16_t type, machine; uint32_t version, entry, phoff, shoff, flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; };
struct ElfSection { uint32_t name, type, flags, addr, offset, size, link, info, align, entsize; };
struct ElfSymbol { uint32_t name, value, size; uint8_t info, other; uint16_t shndx; };
struct StageHeader { uint32_t magic; NativeExtensionSlotHeader slot; };
static_assert(sizeof(ElfHeader) == 52 && sizeof(ElfSection) == 40 && sizeof(ElfSymbol) == 16, "Unexpected ELF layout");

struct LoadedSlot {
    void* mapping;
    NativeExtensionSlotInfo info;
    NativeExtensionCreateFn create;
    NativeExtensionDestroyFn destroy;
    NativeExtensionEventFn tap;
    NativeExtensionEventFn long_press;
    NativeExtensionTickFn tick;
};
LoadedSlot s_slots[NATIVE_EXTENSION_SLOT_COUNT] = {};
bool s_pending_delete[NATIVE_EXTENSION_SLOT_COUNT] = {};

bool valid_slot(uint8_t slot) { return slot < NATIVE_EXTENSION_SLOT_COUNT; }
bool range_valid(size_t offset, size_t size, size_t total) { return offset <= total && size <= total - offset; }
void stage_path(uint8_t slot, char* path, size_t len) { snprintf(path, len, "/extensions/slot%u.stage", slot); }

void host_set_text(char* out, size_t len, const char* text) { if (out && len) strlcpy(out, text ? text : "", len); }
void* host_label_create(void* parent) { return parent ? lv_label_create(static_cast<lv_obj_t*>(parent)) : nullptr; }
void host_label_set_text(void* label, const char* text) { if (label) lv_label_set_text(static_cast<lv_obj_t*>(label), text ? text : ""); }
void host_obj_center(void* obj) { if (obj) lv_obj_center(static_cast<lv_obj_t*>(obj)); }
void host_log_info(const char* message) { LOGI(TAG, "Extension: %s", message ? message : ""); }
void host_notify(const char* message) {
    if (!message || !message[0]) return;
    MessageBubbleParams params = {"", 1500, 0xFFFFFF, 0x00695C, 0, false, 100, 0, NOTIFY_LOC_CENTER};
    strlcpy(params.text, message, sizeof(params.text));
    message_bubble_show(&params);
}
const NativeExtensionHostApi HOST_API = {NATIVE_EXTENSION_ABI_VERSION, host_set_text, host_label_create, host_label_set_text, host_obj_center, host_log_info, host_notify};

const esp_partition_t* extension_partition() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(NATIVE_EXTENSION_PARTITION_SUBTYPE), NATIVE_EXTENSION_PARTITION_LABEL);
}

bool read_header(const esp_partition_t* partition, uint8_t slot, NativeExtensionSlotHeader* header) {
    if (!partition || !valid_slot(slot) || !header || esp_partition_read(partition, SLOT_OFFSET[slot], header, sizeof(*header)) != ESP_OK) return false;
    return header->magic == SLOT_MAGIC && header->abi_version == NATIVE_EXTENSION_ABI_VERSION && header->elf_size > 0 && header->elf_size <= SLOT_CAPACITY[slot] && header->enabled <= 1 && header->id[0];
}

bool read_raw_header(const esp_partition_t* partition, uint8_t slot, NativeExtensionSlotHeader* header) {
    return partition && valid_slot(slot) && header &&
           esp_partition_read(partition, SLOT_OFFSET[slot], header, sizeof(*header)) == ESP_OK &&
           header->magic == SLOT_MAGIC && header->elf_size > 0 &&
           header->elf_size <= SLOT_CAPACITY[slot] && header->enabled <= 1 && header->id[0];
}

bool read_stage(uint8_t slot, StageHeader* stage) {
    char path[32]; stage_path(slot, path, sizeof(path));
    File file = Storage.open(path, "r");
    const bool valid = file && file.read(reinterpret_cast<uint8_t*>(stage), sizeof(*stage)) == sizeof(*stage) &&
                       stage->magic == STAGE_MAGIC && stage->slot.elf_size <= SLOT_CAPACITY[slot];
    if (file) file.close();
    return valid;
}

bool parse_filename(const char* filename, NativeExtensionSlotHeader* header) {
    const char* at = filename ? strrchr(filename, '@') : nullptr;
    const char* dot = filename ? strrchr(filename, '.') : nullptr;
    if (!at || !dot || at == filename || at >= dot || strcmp(dot, ".elf") != 0 ||
        static_cast<size_t>(at - filename) >= sizeof(header->id) ||
        static_cast<size_t>(dot - at - 1) >= sizeof(header->version)) return false;
    memset(header, 0, sizeof(*header));
    memcpy(header->id, filename, at - filename);
    memcpy(header->version, at + 1, dot - at - 1);
    for (size_t index = 0; header->id[index]; ++index) {
        const char c = header->id[index];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    header->abi_version = NATIVE_EXTENSION_ABI_VERSION;
    header->enabled = 1;
    return header->version[0];
}

bool find_symbol(const uint8_t* file, size_t len, const ElfHeader* elf, const ElfSection* sections, uintptr_t bias, const char* target, uintptr_t* result) {
    for (uint16_t section_index = 0; section_index < elf->shnum; ++section_index) {
        const ElfSection& section = sections[section_index];
        if (section.type != ELF_SHT_DYNSYM || section.entsize != sizeof(ElfSymbol) || section.link >= elf->shnum || !range_valid(section.offset, section.size, len)) continue;
        const ElfSection& strings = sections[section.link];
        if (!range_valid(strings.offset, strings.size, len)) continue;
        const ElfSymbol* symbols = reinterpret_cast<const ElfSymbol*>(file + section.offset);
        const char* names = reinterpret_cast<const char*>(file + strings.offset);
        for (size_t index = 0; index < section.size / sizeof(ElfSymbol); ++index) {
            if (symbols[index].shndx && symbols[index].name < strings.size && strcmp(names + symbols[index].name, target) == 0) { *result = bias + symbols[index].value; return true; }
        }
    }
    return false;
}

bool valid_elf(const uint8_t* elf_data, size_t elf_size) {
    if (!elf_data || elf_size < sizeof(ElfHeader)) return false;
    const ElfHeader* elf = reinterpret_cast<const ElfHeader*>(elf_data);
    if (memcmp(elf->ident, "\x7f" "ELF", 4) || elf->type != ELF_TYPE_DYN ||
        elf->machine != ELF_MACHINE_RISCV || elf->shentsize != sizeof(ElfSection) ||
        !range_valid(elf->shoff, static_cast<size_t>(elf->shnum) * elf->shentsize, elf_size)) return false;
    const ElfSection* sections = reinterpret_cast<const ElfSection*>(elf_data + elf->shoff);
    for (uint16_t index = 0; index < elf->shnum; ++index) {
        if (sections[index].type == ELF_SHT_RELA && sections[index].size) return false;
    }
    return true;
}

bool load_slot(const esp_partition_t* partition, uint8_t slot, const NativeExtensionSlotHeader& header) {
    uint8_t* metadata = static_cast<uint8_t*>(heap_caps_malloc(header.elf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!metadata) metadata = static_cast<uint8_t*>(malloc(header.elf_size));
    if (!metadata || esp_partition_read(partition, SLOT_OFFSET[slot] + ELF_OFFSET, metadata, header.elf_size) != ESP_OK) { if (metadata) heap_caps_free(metadata); return false; }
    const ElfHeader* elf = reinterpret_cast<const ElfHeader*>(metadata);
    if (header.elf_size < sizeof(*elf) || memcmp(elf->ident, "\x7f" "ELF", 4) || elf->type != ELF_TYPE_DYN || elf->machine != ELF_MACHINE_RISCV || elf->shentsize != sizeof(ElfSection) || !range_valid(elf->shoff, (size_t)elf->shnum * elf->shentsize, header.elf_size)) { heap_caps_free(metadata); return false; }
    const ElfSection* sections = reinterpret_cast<const ElfSection*>(metadata + elf->shoff);
    for (uint16_t index = 0; index < elf->shnum; ++index) if (sections[index].type == ELF_SHT_RELA && sections[index].size) { heap_caps_free(metadata); return false; }
    void* mapping = nullptr;
    if (esp_mmu_map(partition->address + SLOT_OFFSET[slot], ELF_OFFSET + header.elf_size, MMU_TARGET_FLASH0, static_cast<mmu_mem_caps_t>(MMU_MEM_CAP_EXEC | MMU_MEM_CAP_READ), 0, &mapping) != ESP_OK || !mapping) { heap_caps_free(metadata); return false; }
    uintptr_t create = 0, destroy = 0, tap = 0, long_press = 0, tick = 0;
    const uintptr_t bias = reinterpret_cast<uintptr_t>(mapping) + ELF_OFFSET;
    const bool required = find_symbol(metadata, header.elf_size, elf, sections, bias, "native_extension_create_instance", &create) && find_symbol(metadata, header.elf_size, elf, sections, bias, "native_extension_destroy_instance", &destroy);
    find_symbol(metadata, header.elf_size, elf, sections, bias, "native_extension_on_tap", &tap);
    find_symbol(metadata, header.elf_size, elf, sections, bias, "native_extension_on_long_press", &long_press);
    find_symbol(metadata, header.elf_size, elf, sections, bias, "native_extension_tick", &tick);
    heap_caps_free(metadata);
    if (!required) { esp_mmu_unmap(mapping); return false; }
    LoadedSlot& loaded = s_slots[slot];
    loaded.mapping = mapping; loaded.create = reinterpret_cast<NativeExtensionCreateFn>(create); loaded.destroy = reinterpret_cast<NativeExtensionDestroyFn>(destroy); loaded.tap = reinterpret_cast<NativeExtensionEventFn>(tap); loaded.long_press = reinterpret_cast<NativeExtensionEventFn>(long_press); loaded.tick = reinterpret_cast<NativeExtensionTickFn>(tick);
    loaded.info = {slot, true, false, false, false, true, true, SLOT_CAPACITY[slot], header.elf_size, 0, header.abi_version, {}, {}};
    strlcpy(loaded.info.id, header.id, sizeof(loaded.info.id)); strlcpy(loaded.info.version, header.version, sizeof(loaded.info.version));
    LOGI(TAG, "Loaded slot %u: %s@%s", slot, header.id, header.version);
    return true;
}

void install_stage(const esp_partition_t* partition, uint8_t slot) {
    char path[32]; stage_path(slot, path, sizeof(path));
    if (!Storage.exists(path)) return;
    File file = Storage.open(path, "r"); StageHeader stage = {};
    if (!file || file.read(reinterpret_cast<uint8_t*>(&stage), sizeof(stage)) != sizeof(stage) || stage.magic != STAGE_MAGIC || stage.slot.elf_size > SLOT_CAPACITY[slot]) { if (file) file.close(); return; }
    if (esp_partition_erase_range(partition, SLOT_OFFSET[slot], SLOT_SIZE[slot]) != ESP_OK) { file.close(); return; }
    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer) buffer = static_cast<uint8_t*>(malloc(4096));
    if (!buffer) { file.close(); return; }
    size_t offset = 0;
    bool written = true;
    while (offset < stage.slot.elf_size) {
        const size_t count = (stage.slot.elf_size - offset < 4096) ? stage.slot.elf_size - offset : 4096;
        if (file.read(buffer, count) != count || esp_partition_write(partition, SLOT_OFFSET[slot] + ELF_OFFSET + offset, buffer, count) != ESP_OK) { written = false; break; }
        offset += count;
    }
    heap_caps_free(buffer);
    file.close(); stage.slot.magic = SLOT_MAGIC;
    if (written && esp_partition_write(partition, SLOT_OFFSET[slot], &stage.slot, sizeof(stage.slot)) == ESP_OK) { Storage.remove(path); LOGI(TAG, "Installed slot %u: %s", slot, stage.slot.id); }
}

LoadedSlot* loaded_by_id(const char* id) { for (auto& slot : s_slots) if (slot.mapping && strcmp(slot.info.id, id) == 0) return &slot; return nullptr; }

} // namespace

bool native_extension_init() {
    const esp_partition_t* partition = extension_partition();
    if (!partition) return false;
    for (uint8_t slot = 0; slot < NATIVE_EXTENSION_SLOT_COUNT; ++slot) install_stage(partition, slot);
    for (uint8_t slot = 0; slot < NATIVE_EXTENSION_SLOT_COUNT; ++slot) { NativeExtensionSlotHeader header = {}; if (read_header(partition, slot, &header) && header.enabled) load_slot(partition, slot, header); }
    return true;
}

uint8_t native_extension_slot_count() { return NATIVE_EXTENSION_SLOT_COUNT; }
bool native_extension_get_slot(uint8_t slot, NativeExtensionSlotInfo* out) {
    if (!out || !valid_slot(slot)) return false;
    *out = s_slots[slot].info; out->slot = slot; out->capacity = SLOT_CAPACITY[slot];
    out->pending_delete = s_pending_delete[slot];
    NativeExtensionSlotHeader header = {};
    const esp_partition_t* partition = extension_partition();
    if (read_raw_header(partition, slot, &header)) {
        out->installed = true; out->enabled = header.enabled; out->elf_size = header.elf_size;
        out->abi_version = header.abi_version;
        out->incompatible_abi = header.abi_version != NATIVE_EXTENSION_ABI_VERSION;
        strlcpy(out->id, header.id, sizeof(out->id)); strlcpy(out->version, header.version, sizeof(out->version));
    }
    StageHeader stage = {};
    if (read_stage(slot, &stage)) {
        out->staged = true;
        out->staged_size = stage.slot.elf_size;
        if (!out->installed) {
            strlcpy(out->id, stage.slot.id, sizeof(out->id));
            strlcpy(out->version, stage.slot.version, sizeof(out->version));
        }
    }
    return true;
}
bool native_extension_stage(uint8_t slot, const char* filename, const uint8_t* elf, size_t elf_size) {
    if (!valid_slot(slot) || elf_size == 0 || elf_size > SLOT_CAPACITY[slot] || !valid_elf(elf, elf_size)) return false;
    NativeExtensionSlotHeader slot_header = {};
    if (!parse_filename(filename, &slot_header)) return false;
    slot_header.elf_size = elf_size;
    if (!Storage.exists("/extensions") && !Storage.mkdir("/extensions")) return false;
    char path[32]; stage_path(slot, path, sizeof(path));
    File file = Storage.open(path, "w");
    const StageHeader stage = {STAGE_MAGIC, slot_header};
    const bool written = file && file.write(reinterpret_cast<const uint8_t*>(&stage), sizeof(stage)) == sizeof(stage) &&
                         file.write(elf, elf_size) == elf_size;
    if (file) file.close();
    if (!written) Storage.remove(path);
    if (written) LOGI(TAG, "Staged slot %u: %s@%s (%u bytes)", slot, slot_header.id,
                      slot_header.version, static_cast<unsigned>(elf_size));
    return written;
}
bool native_extension_set_enabled(uint8_t slot, bool enabled) {
    const esp_partition_t* partition = extension_partition();
    NativeExtensionSlotHeader header = {};
    if (!read_header(partition, slot, &header)) return false;
    header.enabled = enabled ? 1 : 0;
    if (esp_partition_erase_range(partition, SLOT_OFFSET[slot], ELF_OFFSET) != ESP_OK) return false;
    return esp_partition_write(partition, SLOT_OFFSET[slot], &header, sizeof(header)) == ESP_OK;
}
bool native_extension_delete(uint8_t slot) {
    const esp_partition_t* partition = extension_partition();
    if (!valid_slot(slot) || !partition || esp_partition_erase_range(partition, SLOT_OFFSET[slot], SLOT_SIZE[slot]) != ESP_OK) return false;
    s_pending_delete[slot] = s_slots[slot].mapping != nullptr;
    LOGI(TAG, "Deleted slot %u%s", slot, s_pending_delete[slot] ? "; reboot required to unload" : "");
    return true;
}
bool native_extension_create_instance(const char* id, uint32_t instance_id, void* root, const char* config) {
    LoadedSlot* slot = loaded_by_id(id);
    if (!slot || !root) { LOGW(TAG, "Create unavailable: %s", id ? id : ""); return false; }
    LOGI(TAG, "Create %s instance=%08lx", id, static_cast<unsigned long>(instance_id));
    slot->create(&HOST_API, instance_id, root, config ? config : "");
    return true;
}
void native_extension_destroy_instance(const char* id, uint32_t instance_id) { if (LoadedSlot* slot = loaded_by_id(id)) slot->destroy(&HOST_API, instance_id); }
NativeExtensionEventResult native_extension_on_tap(const char* id, uint32_t instance_id) {
    LoadedSlot* slot = loaded_by_id(id);
    const NativeExtensionEventResult result = (slot && slot->tap) ? slot->tap(&HOST_API, instance_id) : NATIVE_EXTENSION_PASS_THROUGH;
    LOGI(TAG, "Tap %s instance=%08lx: %s", id ? id : "", static_cast<unsigned long>(instance_id), result == NATIVE_EXTENSION_HANDLED ? "handled" : "pass-through");
    return result;
}
NativeExtensionEventResult native_extension_on_long_press(const char* id, uint32_t instance_id) {
    LoadedSlot* slot = loaded_by_id(id);
    const NativeExtensionEventResult result = (slot && slot->long_press) ? slot->long_press(&HOST_API, instance_id) : NATIVE_EXTENSION_PASS_THROUGH;
    LOGI(TAG, "Long press %s instance=%08lx: %s", id ? id : "", static_cast<unsigned long>(instance_id), result == NATIVE_EXTENSION_HANDLED ? "handled" : "pass-through");
    return result;
}
void native_extension_tick_instance(const char* id, uint32_t instance_id) { if (LoadedSlot* slot = loaded_by_id(id); slot && slot->tick) slot->tick(&HOST_API, instance_id); }

#endif