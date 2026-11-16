#include "list_source.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "binding_template.h"
#include "log_manager.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <new>
#include <string.h>

void list_source_emit_metadata(JsonArray output) {
    for (uint8_t index = 0; index < list_source_count(); ++index) {
        const ListSourceDescriptor* source = list_source_at(index);
        JsonObject entry = output.add<JsonObject>();
        entry["token"] = source->token;
        entry["title"] = source->title;
        JsonArray fields = entry["fields"].to<JsonArray>();
        for (uint8_t field_index = 0; field_index < source->field_count; ++field_index) {
            const ListSourceField& field = source->fields[field_index];
            JsonObject item = fields.add<JsonObject>();
            item["name"] = field.name;
            item["description"] = field.description;
            item["type"] = field.type == ListSourceFieldType::Number ? "number" :
                           field.type == ListSourceFieldType::Boolean ? "boolean" :
                           field.type == ListSourceFieldType::Scalar ? "scalar" : "string";
            item["optional"] = field.optional;
        }
    }
}

namespace {

const ListSourceDescriptor* sources[LIST_SOURCE_MAX_SOURCES] = {};
uint8_t source_count = 0;

bool valid_utf8(const char* text, size_t length, bool reject_controls) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    for (size_t i = 0; i < length;) {
        const unsigned char c = bytes[i++];
        if (c < 0x80) {
            if (!c || (reject_controls && (c < 0x20 || c == 0x7f))) return false;
            continue;
        }
        unsigned remaining;
        uint32_t codepoint;
        uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            remaining = 1; codepoint = c & 0x1f; minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            remaining = 2; codepoint = c & 0x0f; minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            remaining = 3; codepoint = c & 0x07; minimum = 0x10000;
        } else {
            return false;
        }
        if (remaining > length - i) return false;
        while (remaining--) {
            const unsigned char next = bytes[i++];
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
            (reject_controls && codepoint >= 0x80 && codepoint <= 0x9f)) return false;
    }
    return true;
}

bool valid_field(JsonString name) {
    if (!name.c_str() || !name.size() || name.size() > LIST_SOURCE_MAX_FIELD_BYTES) return false;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name.c_str()[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

const char* validate_item(JsonObjectConst item) {
    if (item.isNull() || item.size() > LIST_SOURCE_MAX_FIELDS) return "invalid item field count";
    JsonVariantConst id_value = item["id"];
    if (!id_value.is<const char*>()) return "item id must be a string";
    JsonString id = id_value.as<JsonString>();
    if (!id.size() || id.size() > LIST_SOURCE_MAX_ID_BYTES ||
        !valid_utf8(id.c_str(), id.size(), true)) return "invalid item id";

    for (JsonPairConst field : item) {
        if (!valid_field(field.key())) return "invalid item field name";
        JsonVariantConst value = field.value();
        if (value.isNull() || value.is<bool>()) continue;
        if (value.is<const char*>()) {
            JsonString text = value.as<JsonString>();
            if (text.size() > LIST_SOURCE_MAX_TEXT_BYTES ||
                !valid_utf8(text.c_str(), text.size(), false)) return "invalid item text";
        } else if (!value.is<double>() || !isfinite(value.as<double>())) {
            return "item fields must be finite scalars";
        }
    }
    return nullptr;
}

} // namespace

bool list_source_register(const ListSourceDescriptor* source) {
    BindingTokenView token;
    if (!source || !source->token || strlen(source->token) >= BINDING_TEMPLATE_MAX_LEN ||
        !source->title || !source->title[0] ||
        !source->fields || !source->field_count || source->field_count > LIST_SOURCE_MAX_FIELDS ||
        !binding_template_single_token(source->token, &token) ||
        !token.params_len || token.params_len > LIST_SOURCE_MAX_FIELD_BYTES ||
        !valid_field(JsonString(token.params, token.params_len)) ||
        source_count >= LIST_SOURCE_MAX_SOURCES || list_source_find(source->token)) {
        LOGE("ListSource", "Invalid, duplicate, or excess source descriptor");
        return false;
    }
    bool has_id = false;
    for (uint8_t i = 0; i < source->field_count; ++i) {
        const ListSourceField& field = source->fields[i];
        if (!field.name || !valid_field(JsonString(field.name)) ||
            !field.description || !field.description[0]) {
            LOGE("ListSource", "Invalid source field descriptor");
            return false;
        }
        for (uint8_t j = 0; j < i; ++j) {
            if (!strcmp(source->fields[j].name, field.name)) {
                LOGE("ListSource", "Duplicate source field descriptor");
                return false;
            }
        }
        if (!strcmp(field.name, "id")) {
            if (field.type != ListSourceFieldType::String || field.optional) {
                LOGE("ListSource", "Source id must be a required string");
                return false;
            }
            has_id = true;
        }
    }
    if (!has_id) {
        LOGE("ListSource", "Source descriptor is missing an id field");
        return false;
    }
    sources[source_count++] = source;
    return true;
}

const ListSourceDescriptor* list_source_find(const char* token) {
    if (!token) return nullptr;
    for (uint8_t i = 0; i < source_count; ++i) {
        if (!strcmp(sources[i]->token, token)) return sources[i];
    }
    return nullptr;
}

const ListSourceField* list_source_field(const ListSourceDescriptor* source, const char* name) {
    if (!source || !name) return nullptr;
    for (uint8_t i = 0; i < source->field_count; ++i) {
        if (!strcmp(source->fields[i].name, name)) return &source->fields[i];
    }
    return nullptr;
}

uint8_t list_source_count() { return source_count; }

const ListSourceDescriptor* list_source_at(uint8_t index) {
    return index < source_count ? sources[index] : nullptr;
}

bool list_source_preview_length(const char* text, size_t length, size_t max_bytes,
                                size_t* preview_length) {
    if (!text || !preview_length || !valid_utf8(text, length, false)) return false;
    size_t result = length < max_bytes ? length : max_bytes;
    if (result < length) {
        while (result && (static_cast<unsigned char>(text[result]) & 0xc0) == 0x80) --result;
    }
    *preview_length = result;
    return true;
}

ListSourceSnapshot::ListSourceSnapshot()
    : references_(1), allocator_(LIST_SOURCE_DOCUMENT_BYTES), document_(&allocator_),
      error_(nullptr), sealed_(false) {
    document_.to<JsonArray>();
}

ListSourceSnapshot* ListSourceSnapshot::create() {
    void* memory = heap_caps_malloc(sizeof(ListSourceSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        LOGE("ListSource", "Cannot allocate snapshot in PSRAM");
        return nullptr;
    }
    return new (memory) ListSourceSnapshot();
}

void ListSourceSnapshot::retain() {
    references_.fetch_add(1, std::memory_order_relaxed);
}

void ListSourceSnapshot::release() {
    if (references_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        this->~ListSourceSnapshot();
        heap_caps_free(this);
    }
}

const char* ListSourceSnapshot::append(JsonObjectConst item) {
    if (sealed_) return "snapshot is sealed";
    if (error_) return error_;
    if (document_.overflowed()) return error_ = "snapshot allocation failed";
    if (const char* error = validate_item(item)) return error_ = error;
    JsonArray array = document_.as<JsonArray>();
    if (array.size() >= LIST_SOURCE_MAX_ITEMS) return error_ = "too many items";
    const JsonString id = item["id"].as<JsonString>();
    for (JsonObjectConst existing : array) {
        const JsonString existing_id = existing["id"].as<JsonString>();
        if (existing_id.size() == id.size() && !memcmp(existing_id.c_str(), id.c_str(), id.size()))
            return error_ = "duplicate item id";
    }

    JsonObject output = array.add<JsonObject>();
    for (JsonPairConst field : item) {
        JsonString key = field.key();
        JsonVariantConst value = field.value();
        JsonString copied_key(key.c_str(), key.size(), JsonString::Copied);
        if (value.is<const char*>()) {
            JsonString text = value.as<JsonString>();
            output[copied_key] = JsonString(text.c_str(), text.size(), JsonString::Copied);
        } else {
            output[copied_key] = value;
        }
    }
    if (document_.overflowed()) return error_ = "snapshot allocation failed";
    return nullptr;
}

const char* ListSourceSnapshot::seal() {
    if (error_) return error_;
    if (document_.overflowed()) return error_ = "snapshot allocation failed";
    sealed_ = true;
    return nullptr;
}

JsonArrayConst ListSourceSnapshot::items() const {
    return sealed_ ? document_.as<JsonArrayConst>() : JsonArrayConst();
}

size_t ListSourceSnapshot::peakBytes() const {
    return allocator_.peak;
}

uint32_t ListSourceSnapshot::references() const {
    return references_.load(std::memory_order_acquire);
}

#endif
