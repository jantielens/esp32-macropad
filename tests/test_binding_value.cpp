// Production-helper parity and unsafe printf regression coverage.
#include "binding_value.h"
#include "external_data.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void expect_extract(const char* payload, const char* path, bool success, const char* expected) {
    char out[128] = "unchanged";
    assert(binding_value_extract_json(payload, path, out, sizeof(out)) == success);
    assert(std::strcmp(out, expected) == 0);
}

static void test_extract() {
    const char* payload = R"({"name":"living room","temp":21.5,"count":7,"on":true,"off":false,"child":{"v":"ok"},"list":[1,2],"empty":null})";
    expect_extract(payload, "name", true, "living room");
    expect_extract(payload, "temp", true, "21.5");
    expect_extract(payload, "count", true, "7");
    expect_extract(payload, "on", true, "true");
    expect_extract(payload, "off", true, "false");
    expect_extract(payload, "child", true, R"({"v":"ok"})");
    expect_extract(payload, "child.v", true, "ok");
    expect_extract(payload, ".child..v.", true, "ok");
    expect_extract(payload, "list", true, "[1,2]");
    expect_extract(payload, "list.0", false, "");
    expect_extract(payload, "temp.missing", false, "");
    expect_extract(payload, "missing", false, "");
    expect_extract(payload, "empty", false, "");
    expect_extract("not JSON", "temp", false, "not JSON");
    expect_extract("not JSON", ".", true, "not JSON");
    expect_extract(R"({"x": 2})", ".", true, R"({"x": 2})");
    expect_extract("\"root\"", "", true, "root");
    const std::string prefix(36, 'x');
    const std::string long_path_payload =
        "{\"attributes\":{\"" + prefix + "\":\"wrong\",\"" + prefix + "_actual\":\"right\"}}";
    expect_extract(long_path_payload.c_str(), ("attributes." + prefix + "_actual").c_str(), true, "right");
    expect_extract(long_path_payload.c_str(), ("attributes." + prefix + "_missing").c_str(), false, "");
    char out[4];
    assert(!binding_value_extract_json(payload, "name", out, sizeof(out)));
    assert(std::strcmp(out, "liv") == 0);
    assert(!binding_value_extract_json(payload, "child", out, sizeof(out)));
    assert(std::strcmp(out, "{\"v") == 0);
    assert(binding_value_extract_json("abc", ".", out, sizeof(out)));
    assert(!binding_value_extract_json("abcd", ".", out, sizeof(out)));
    assert(binding_value_extract_json("{\"v\":\"abc\"}", "v", out, sizeof(out)));
    assert(!binding_value_extract_json("{\"v\":\"abcd\"}", "v", out, sizeof(out)));
    assert(!binding_value_extract_json(nullptr, ".", out, sizeof(out)));
    assert(!binding_value_extract_json(payload, ".", out, 0));
}

static void test_select_json() {
    JsonDocument doc;
    assert(!deserializeJson(doc, R"({"daily":{"values":[12.5,23.5]},"0":"numeric key","null":null,"text":"a\nb","object":{"x":1},"@duration":42,"large":123456789,"precise":12.3456789,"float":0.1,"scientific":1.23456789e8,"unsigned":18446744073709551615})"));
    char out[64];
    auto select = [&](const char* path, const char* expected) {
        assert(binding_value_select_json(doc.as<JsonVariantConst>(), path, out, sizeof(out)));
        if (std::strcmp(out, expected) != 0)
            std::fprintf(stderr, "select %s: expected <%s>, got <%s>\n", path, expected, out);
        assert(std::strcmp(out, expected) == 0);
    };
    auto missing = [&](const char* path) {
        assert(!binding_value_select_json(doc.as<JsonVariantConst>(), path, out, sizeof(out)));
    };
    select("daily.values.0", "12.5");
    select("daily.values.1", "23.5");
    select("0", "numeric key");
    select("text", "a\nb");
    select("object", "{\"x\":1}");
    select("@duration", "42");
    select("large", "123456789");
    assert(binding_value_select_json(doc.as<JsonVariantConst>(), "precise", out, sizeof(out)));
    assert(std::strtod(out, nullptr) == doc["precise"].as<double>());
    select("float", "0.1");
    select("unsigned", "18446744073709551615");
    select("scientific", "123456789");
    char formatted[64];
    binding_value_format_external(out, "%d", formatted, sizeof(formatted));
    assert(std::strcmp(formatted, "123456789") == 0);
    assert(binding_value_select_json(doc.as<JsonVariantConst>(), "precise", out, sizeof(out)));
    binding_value_format_external(out, "%.7f", formatted, sizeof(formatted));
    assert(std::strcmp(formatted, "12.3456789") == 0);
    select("unsigned", "18446744073709551615");
    binding_value_format_external(out, "%llu", formatted, sizeof(formatted));
    assert(std::strcmp(formatted, "18446744073709551615") == 0);
    binding_value_format_external("1.23456789e+08", "%d", formatted, sizeof(formatted));
    assert(std::strcmp(formatted, "123456789") == 0);
    binding_value_format_external("-1.25e+02", "%d", formatted, sizeof(formatted));
    assert(std::strcmp(formatted, "-125") == 0);
    binding_value_format_external("1e19", "%llu", formatted, sizeof(formatted));
    assert(std::strcmp(formatted, "10000000000000000000") == 0);
    binding_value_format_external("18446744073709551616", "%llu", formatted, sizeof(formatted));
    assert(std::strcmp(formatted, "18446744073709551616") == 0);
    binding_value_format("1.23456789e+08", "%d", formatted, sizeof(formatted));
    assert(std::strcmp(formatted, "1") == 0);
    missing(".@duration");
    missing("daily.values.-1");
    missing("daily.values.1x");
    missing("daily.values.184467440737095516160");
    missing("daily.values.2");
    missing("daily..values");
    missing("object.x.");
    missing("null");
    missing("missing");
    char small[4];
    assert(!binding_value_select_json(doc.as<JsonVariantConst>(), "object", small, sizeof(small)));
    assert(!binding_value_select_json(doc.as<JsonVariantConst>(), "0", small, sizeof(small)));
}

static void test_external_config() {
    ExternalDataConfig next{};
    JsonDocument doc;
    auto parse = [&](const char* json) {
        doc.clear();
        assert(!deserializeJson(doc, json));
        return external_data_parse_config(doc.as<JsonVariantConst>(), next);
    };
    assert(parse(R"([{"name":"weather","url":"https://example.org/data?location=pelt","refresh_seconds":1}])"));
    assert(std::strcmp(next.sources[0].url, "https://example.org/data?location=pelt") == 0);
    assert(next.sources[0].refresh_seconds == 1);
    assert(parse(R"([{"name":"renamed","url":"https://example.org/data?location=pelt"}])"));
    assert(next.sources[0].refresh_seconds == 1800);
    assert(!parse(R"([{"name":"weather","url":""}])"));
    assert(!parse(R"([{"name":"weather"}])"));
    assert(!parse(R"([{"name":"renamed"}])"));
    assert(!parse(R"([{"name":"weather","url":null}])"));
    assert(!parse(R"([{"name":"weather","url":"https://example.org","refresh_seconds":null}])"));
    assert(!parse(R"([{"name":"weather","url":"https://example.org","refresh_seconds":0}])"));
    assert(!parse(R"([{"name":"weather","url":"https://example.org","refresh_seconds":86401}])"));
    assert(!parse(R"([{"name":"weather","url":"https://example.org"},{"name":"weather","url":"https://example.org"}])"));
    assert(!parse(R"([{"name":"bad.name","url":"https://example.org"}])"));
    assert(!parse(R"([{"name":"new","url":"https://example.org/hello world"}])"));
    assert(!parse(R"([{"name":"new","url":"https://user:token@example.org/"}])"));
    assert(!parse(R"([{}, {}, {}, {}, {}])"));
    assert(parse("[]") && next.count == 0);

    JsonDocument request;
    JsonArray sources = request["external_data"].to<JsonArray>();
    const std::string url = "https://example.org/" + std::string(511 - 20, 'a');
    assert(url.size() == 511);
    for (unsigned i = 0; i < EXTERNAL_DATA_MAX_SOURCES; ++i) {
        JsonObject source = sources.add<JsonObject>();
        source["name"] = std::string(31, 'n') + std::to_string(i);
        source["url"] = url;
        source["refresh_seconds"] = 86400;
    }
    assert(measureJson(request) <= 4096);
    assert(external_data_parse_config(sources, next));
    assert(next.count == EXTERNAL_DATA_MAX_SOURCES);
}

static void test_external_source_update() {
    JsonDocument input;
    ExternalDataSourceUpdate update{};
    ExternalDataConfig config{};
    auto parse = [&](const char* json) {
        input.clear();
        assert(!deserializeJson(input, json));
        return external_data_parse_update(input.as<JsonObjectConst>(), update);
    };
    assert(parse(R"({"source":"weather","url":"https://example.org/weather"})"));
    assert(external_data_apply_update(config, update));
    assert(config.count == 1 && config.sources[0].refresh_seconds == 1800);
    assert(parse(R"({"source":"weather","refresh_seconds":1})"));
    assert(external_data_apply_update(config, update));
    assert(std::strcmp(config.sources[0].url, "https://example.org/weather") == 0);
    assert(config.sources[0].refresh_seconds == 1);
    assert(parse(R"({"source":"weather","url":"https://example.org/new"})"));
    assert(external_data_apply_update(config, update));
    assert(config.sources[0].refresh_seconds == 1);
    assert(parse(R"({"source":"other","refresh_seconds":60})"));
    assert(!external_data_apply_update(config, update));
    assert(config.count == 1);
    assert(!parse(R"({"source":"weather"})"));
    assert(!parse(R"({"source":"bad.name","url":"https://example.org"})"));
    assert(!parse(R"({"source":"weather","url":"https://example.org","refresh_seconds":0})"));
    assert(!parse(R"({"source":"weather","url":"https://example.org\u0000hidden"})"));
    assert(!parse(R"({"source":"weather\u0000hidden","url":"https://example.org"})"));
    for (unsigned i = 1; i < EXTERNAL_DATA_MAX_SOURCES; ++i) {
        input.clear();
        input["source"] = std::string("source") + std::to_string(i);
        input["url"] = "https://example.org";
        assert(external_data_parse_update(input.as<JsonObjectConst>(), update));
        assert(external_data_apply_update(config, update));
    }
    assert(parse(R"({"source":"weather","refresh_seconds":300})"));
    assert(external_data_apply_update(config, update));
    assert(config.count == EXTERNAL_DATA_MAX_SOURCES);
    assert(std::strcmp(config.sources[1].name, "source1") == 0);
    assert(parse(R"({"source":"overflow","url":"https://example.org"})"));
    assert(!external_data_apply_update(config, update));
}

static void test_external_inspection() {
    JsonDocument source;
    assert(!deserializeJson(source, R"({"current":{"temperature":21.5},"daily":[10,20,30,40,50,60,70,80,90],"bad.key":1,"null":null})"));
    JsonDocument output;
    auto inspect = [&](const char* path, size_t offset) {
        output.clear();
        return binding_value_inspect_json(source.as<JsonVariantConst>(), path, offset,
                                          output.to<JsonObject>());
    };
    assert(inspect(".", 0));
    assert(std::strcmp(output["type"], "object") == 0);
    assert(std::strcmp(output["children"][0]["path"], "current") == 0);
    assert(!output["children"][2]["bindable"].as<bool>());
    assert(!output["children"][3]["bindable"].as<bool>());
    assert(inspect("current", 0));
    assert(std::strcmp(output["children"][0]["path"], "current.temperature") == 0);
    assert(output["children"][0]["value"].as<double>() == 21.5);
    assert(inspect("daily", 0));
    assert(output["children"].size() == 8 && output["next_offset"].as<int>() == 8);
    assert(inspect("daily", 8));
    assert(output["children"].size() == 1 && !output.containsKey("next_offset"));
    assert(std::strcmp(output["children"][0]["path"], "daily.8") == 0);
    assert(output["children"][0]["value"].as<int>() == 90);
    assert(inspect("daily.0", 0));
    assert(output["value"].as<int>() == 10);
    assert(!inspect("daily.9", 0));
    assert(!inspect("bad;key", 0));
    assert(!inspect("null", 0));

    source.clear();
    source["long"] = std::string(120, 'x');
    assert(inspect(".", 0));
    assert(output["children"][0]["value_truncated"].as<bool>());
    source.clear();
    assert(std::strlen(output["children"][0]["value"].as<const char*>()) == 96);
    source["long"] = std::string(95, 'x') + "\xc3\xa9" + std::string(5, 'x');
    assert(inspect(".", 0));
    assert(std::strlen(output["children"][0]["value"].as<const char*>()) == 95);
    source.clear();
    source[std::string(48, 'k')] = 1;
    assert(inspect("", 0));
    assert(!output["children"][0]["bindable"].as<bool>());
    assert(!output["children"][0].containsKey("path"));
}

static void test_legacy_ca_bundle() {
    static const unsigned char bundle[] = {
#include "external_data_ca_bundle.inc"
    };
    const size_t count = (bundle[0] << 8) | bundle[1];
    assert(count > 0);
    size_t position = 2;
    const unsigned char* previous = nullptr;
    size_t previous_length = 0;
    for (size_t i = 0; i < count; ++i) {
        assert(position + 4 <= sizeof(bundle));
        const size_t name_length = (bundle[position] << 8) | bundle[position + 1];
        const size_t key_length = (bundle[position + 2] << 8) | bundle[position + 3];
        position += 4;
        assert(name_length && key_length && position + name_length + key_length <= sizeof(bundle));
        if (previous) {
            const size_t common = previous_length < name_length ? previous_length : name_length;
            const int order = std::memcmp(previous, bundle + position, common);
            assert(order < 0 || (order == 0 && previous_length <= name_length));
        }
        previous = bundle + position;
        previous_length = name_length;
        position += name_length + key_length;
    }
    assert(position == sizeof(bundle));
}

static void expect_format(const char* raw, const char* format, const char* expected) {
    char out[128] = "unchanged";
    binding_value_format(raw, format, out, sizeof(out));
    if (std::strcmp(out, expected) != 0) {
        std::fprintf(stderr, "format %s, raw %s: expected <%s>, got <%s>\n",
                     format ? format : "(null)", raw, expected, out);
        assert(false);
    }
}

static void test_format() {
    expect_format("21.56", "%.1f °C", "21.6 °C");
    expect_format("21.56 trailing", "%.1f", "21.6");
    expect_format("21.56", "%d", "21");
    expect_format("-.75", "%d", "0");
    expect_format("21.56", "%+05i", "+0021");
    expect_format("-1", "%u", "4294967295");
    expect_format("255", "%#x", "0xff");
    expect_format("255", "%X", "FF");
    expect_format("9", "%o", "11");
    expect_format("2", "%.2e", "2.00e+00");
    expect_format("2", "%g%%", "2%");
    expect_format("2", "%% %g %%", "% 2 %");
    expect_format("2", "%a", "0x1p+1");
    expect_format("hello", "[%8.3s]", "[     hel]");
    expect_format("hello", nullptr, "hello");
    expect_format("hello", "", "hello");
    expect_format("invalid", "%.1f", "invalid");
    expect_format("nan", "%f", "nan");
    expect_format("inf", "%f", "inf");
    expect_format("nan", "%d", "nan");
    expect_format("inf", "%d", "inf");
    expect_format("256", "%hhu", "0");
    expect_format("-12", "%hd", "-12");
    expect_format("-12", "%hhd", "-12");
    expect_format("65536", "%hu", "0");
    expect_format("42", "%ld", "42");
    expect_format("42", "%lu", "42");
    expect_format("4294967296", "%lld", "4294967296");
    expect_format("4294967296", "%llu", "4294967296");
    expect_format("42", "%jd", "42");
    expect_format("42", "%ju", "42");
    expect_format("42", "%zd", "42");
    expect_format("42", "%zu", "42");
    expect_format("42", "%td", "42");
    expect_format("42", "%tu", "42");
    expect_format("1.25", "%.2lf", "1.25");
    expect_format("1.25", "%.2Lf", "1.25");
    const char* rejected[] = {
        "%n", "%ln", "%p", "%c", "%ls", "%lls", "%zs", "%Ls", "%hs",
        "%llf", "%hf", "%Ld", "%llld", "%d %s", "%*s", "%.*f", "%1$s",
        "%", "%d %", "%%", "%999999999999d", "%#.1s", "%+u", "%#d",
    };
    for (const char* format : rejected) expect_format("42", format, "42");
    char out[4] = {};
    binding_value_format("12.5", "%.1f C", out, sizeof(out));
    assert(std::strcmp(out, "12.") == 0);
    binding_value_format("hello", "%s", out, 1);
    assert(out[0] == '\0');
    binding_value_format("hello", "%s", nullptr, 0);
}

static void test_params() {
    char entity[16], path[12], format[16];
    auto parse = [&](const char* params) {
        return binding_value_parse_params(params, entity, sizeof(entity), path, sizeof(path), format, sizeof(format));
    };
    assert(parse("sensor.room"));
    assert(std::strcmp(entity, "sensor.room") == 0 && !path[0] && !format[0]);
    assert(parse("topic;child.v"));
    assert(std::strcmp(path, "child.v") == 0 && !format[0]);
    assert(parse("topic;;%s;unit"));
    assert(!path[0] && std::strcmp(format, "%s;unit") == 0);
    assert(parse("topic;.;%.1f"));
    assert(std::strcmp(entity, "topic") == 0 && std::strcmp(path, ".") == 0);
    assert(std::strcmp(format, "%.1f") == 0);
    assert(parse(";;"));
    assert(!entity[0] && !path[0] && !format[0]);
    assert(parse(""));
    assert(!parse(nullptr));
    assert(!parse("1234567890123456;path;fmt"));
    assert(std::strcmp(entity, "123456789012345") == 0);
    assert(std::strcmp(path, "path") == 0 && std::strcmp(format, "fmt") == 0);
    assert(!parse("topic;123456789012;fmt"));
    assert(std::strcmp(path, "12345678901") == 0 && std::strcmp(format, "fmt") == 0);
    assert(!parse("topic;path;1234567890123456"));
    assert(std::strcmp(format, "123456789012345") == 0);
    assert(parse("123456789012345;12345678901;123456789012345"));
    assert(!binding_value_parse_params("topic", entity, 0, path, sizeof(path), format, sizeof(format)));
}

int main() {
    test_extract();
    test_select_json();
    test_external_config();
    test_external_source_update();
    test_external_inspection();
    test_legacy_ca_bundle();
    test_format();
    test_params();
    std::puts("binding value tests passed");
}
