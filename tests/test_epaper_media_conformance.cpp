#include "device_classes/epaper/epaper_media_validation.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <zlib.h>

namespace {

constexpr uint16_t kPanelWidth = 1872;
constexpr uint16_t kPanelHeight = 1404;
constexpr size_t kG16pSize = 18 + (size_t)kPanelWidth * kPanelHeight / 2;
const char* kVectorRoot =
		"docs/dev/photoframe-next-image/conformance/photoframe-next-image-v1/"
		"profiles/e1003-landscape/vectors/";

std::vector<uint8_t> read_vector(const char* relative_path) {
		char path[512];
		snprintf(path, sizeof(path), "%s%s", kVectorRoot, relative_path);
		FILE* file = fopen(path, "rb");
		assert(file);
		assert(fseek(file, 0, SEEK_END) == 0);
		const long size = ftell(file);
		assert(size >= 0);
		rewind(file);
		std::vector<uint8_t> data((size_t)size);
		assert(data.empty() || fread(data.data(), 1, data.size(), file) == data.size());
		fclose(file);
		return data;
}

EpaperMediaValidation validate_g16z(const std::vector<uint8_t>& data) {
		if (data.size() <= 4 || memcmp(data.data(), "G16Z", 4) != 0) {
				return EpaperMediaValidation::Malformed;
		}
		std::vector<uint8_t> output(kG16pSize);
		z_stream stream = {};
		assert(inflateInit2(&stream, -15) == Z_OK);
		stream.next_in = const_cast<Bytef*>(data.data() + 4);
		stream.avail_in = (uInt)(data.size() - 4);
		stream.next_out = output.data();
		stream.avail_out = (uInt)output.size();
		const int status = inflate(&stream, Z_FINISH);
		const size_t consumed = (data.size() - 4) - stream.avail_in;
		const size_t produced = output.size() - stream.avail_out;
		inflateEnd(&stream);
		const EpaperMediaValidation completion = epaper_validate_g16z_completion(
				status, Z_STREAM_END, consumed, data.size() - 4, produced, kG16pSize);
		if (completion != EpaperMediaValidation::Valid) return completion;
		return epaper_validate_g16p(output.data(), output.size(), kPanelWidth, kPanelHeight);
}

} // namespace

int main() {
		const auto valid_g16p = read_vector("valid/frame.g16p");
		assert(epaper_validate_g16p(valid_g16p.data(), valid_g16p.size(),
				kPanelWidth, kPanelHeight) == EpaperMediaValidation::Valid);
		const auto bad_crc = read_vector("invalid/bad-payload-crc.g16p");
		assert(epaper_validate_g16p(bad_crc.data(), bad_crc.size(),
				kPanelWidth, kPanelHeight) == EpaperMediaValidation::PayloadCrcMismatch);
		const auto odd_width = read_vector("invalid/odd-width.g16p");
		assert(epaper_validate_g16p(odd_width.data(), odd_width.size(),
				kPanelWidth, kPanelHeight) != EpaperMediaValidation::Valid);
		const auto wrong_dimensions_g16p = read_vector("invalid/wrong-dimensions.g16p");
		assert(epaper_validate_g16p(wrong_dimensions_g16p.data(), wrong_dimensions_g16p.size(),
				kPanelWidth, kPanelHeight) == EpaperMediaValidation::WrongGeometry);

		const auto baseline = read_vector("valid/baseline.jpg");
		assert(epaper_validate_jpeg(baseline.data(), baseline.size(),
				kPanelWidth, kPanelHeight) == EpaperMediaValidation::Valid);
		const auto progressive = read_vector("invalid/progressive.jpg");
		assert(epaper_validate_jpeg(progressive.data(), progressive.size(),
				kPanelWidth, kPanelHeight) == EpaperMediaValidation::Unsupported);
		const auto wrong_dimensions_jpeg = read_vector("invalid/wrong-dimensions.jpg");
		assert(epaper_validate_jpeg(wrong_dimensions_jpeg.data(), wrong_dimensions_jpeg.size(),
				kPanelWidth, kPanelHeight) == EpaperMediaValidation::WrongGeometry);

		assert(validate_g16z(read_vector("valid/frame.g16z")) == EpaperMediaValidation::Valid);
		assert(validate_g16z(read_vector("invalid/trailing-bytes.g16z")) ==
				EpaperMediaValidation::TrailingBytes);
		assert(validate_g16z(read_vector("invalid/gzip-wrapper.g16z")) !=
				EpaperMediaValidation::Valid);
		assert(validate_g16z(read_vector("invalid/zlib-wrapper.g16z")) !=
				EpaperMediaValidation::Valid);
		return 0;
}