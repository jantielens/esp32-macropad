#include "web_portal_utils.h"

#include <LittleFS.h>
#include <memory>

struct FileStreamContext {
		File file;
		~FileStreamContext() { if (file) file.close(); }
};

void sendFileThrottled(AsyncWebServerRequest *request,
                       const char *path,
                       const char *content_type) {
		if (!request) return;

		auto ctx = std::make_shared<FileStreamContext>();
		ctx->file = LittleFS.open(path, "r");
		if (!ctx->file) {
				request->send(404, "application/json",
				              "{\"success\":false,\"message\":\"File read error\"}");
				return;
		}

		const size_t file_size = ctx->file.size();

		request->send(content_type, file_size,
				[ctx](uint8_t *buffer, size_t max_len, size_t index) -> size_t {
						if (!ctx->file || !ctx->file.available()) return 0;

						const size_t to_read = (max_len < HTTP_STREAM_CHUNK_SIZE)
						                       ? max_len : HTTP_STREAM_CHUNK_SIZE;
						return ctx->file.read(buffer, to_read);
				}
		);
}
