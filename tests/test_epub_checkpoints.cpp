#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>

#include "epub/XmlSax.h"
#include "css/Css.h"

using namespace freeink::book;

class MemorySource : public BookSource {
 public:
  explicit MemorySource(const std::string& data) : data_(data) {}
  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override {
    if (fail_read) return -1;
    if (offset >= data_.size()) return 0;
    const size_t count = std::min<size_t>(len, data_.size() - offset);
    std::memcpy(dst, data_.data() + offset, count);
    return static_cast<int32_t>(count);
  }
  uint64_t size() const override { return data_.size(); }
  bool shouldStop() override { return stopped; }
  bool stopped = false;
  bool fail_read = false;
 private:
  const std::string& data_;
};

class TextHandler : public XmlHandler {
 public:
  explicit TextHandler(MemorySource& source, bool interrupt) : source_(source), interrupt_(interrupt) {}
  void onText(const char*, int len) override {
    bytes += len;
    if (interrupt_) source_.stopped = true;
  }
  int bytes = 0;
 private:
  MemorySource& source_;
  bool interrupt_;
};

int main() {
  char css_memory[32768];
  Arena css_arena(css_memory, sizeof(css_memory));
  CssStylesheetBuilder builder;
  assert(builder.begin(css_arena));
  const char css[] =
      "section.title p, section.title h1 { display:none; font-style:italic; }"
      ".hidden { display:none; }";
  builder.addText(css, sizeof(css) - 1);
  const CssStylesheet sheet = builder.finish();
  const CssDecl paragraph = cascadeFor(sheet, "p", nullptr, nullptr);
  assert(paragraph.displayNone == -1 && paragraph.styleItalic == 1);
  assert(cascadeFor(sheet, "h1", nullptr, nullptr).displayNone == -1);
  assert(cascadeFor(sheet, "p", "hidden", nullptr).displayNone == 1);
  assert(parseInlineStyle("display:none").displayNone == 1);

  const std::string xml = "<body>" + std::string(12000, 'a') + "</body>";
  const ZipEntry entry = ZipEntryReader::rawEntry(static_cast<uint32_t>(xml.size()));
  char memory[4096];
  Arena scratch(memory, sizeof(memory));

  MemorySource complete(xml);
  TextHandler complete_handler(complete, false);
  assert(XmlSax::parseEntry(complete, entry, scratch, complete_handler) == BookStatus::Ok);
  assert(complete_handler.bytes == 12000);
  assert(scratch.used() == 0);

  MemorySource interrupted(xml);
  TextHandler interrupted_handler(interrupted, true);
  assert(XmlSax::parseEntry(interrupted, entry, scratch, interrupted_handler) == BookStatus::IoError);
  assert(interrupted_handler.bytes > 0 && interrupted_handler.bytes < 12000);
  assert(scratch.used() == 0);

  MemorySource stepped(xml);
  TextHandler stepped_handler(stepped, true);
  XmlSaxSession session;
  assert(session.open(stepped, entry, scratch, stepped_handler) == BookStatus::Ok);
  bool end = false;
  assert(session.feedChunk(&end) == BookStatus::Ok);
  assert(!end && stepped_handler.bytes > 0);
  const int bytes = stepped_handler.bytes;
  assert(session.feedChunk(&end) == BookStatus::IoError);
  assert(stepped_handler.bytes == bytes);

  std::string zip(100, '\0');
  zip.append("PK\x05\x06", 4);
  zip.append(18, '\0');
  zip.append(4076, '\0');
  MemorySource zip_source(zip);
  char scan_memory[4100];
  Arena scan_scratch(scan_memory, sizeof(scan_memory));
  uint32_t directory_offset = 1, directory_size = 1;
  uint16_t entries = 1;
  assert(ZipCatalog::locateCentralDirectory(zip_source, scan_scratch, &directory_offset,
                                            &directory_size, &entries) == BookStatus::Ok);
  assert(directory_offset == 0 && directory_size == 0 && entries == 0);
  assert(scan_scratch.used() == 0);

  Arena too_small(scan_memory, 4098);
  assert(ZipCatalog::locateCentralDirectory(zip_source, too_small, &directory_offset,
                                            &directory_size, &entries) == BookStatus::OutOfMemory);
  assert(too_small.used() == 0);

  zip_source.fail_read = true;
  assert(ZipCatalog::locateCentralDirectory(zip_source, scan_scratch, &directory_offset,
                                            &directory_size, &entries) == BookStatus::IoError);
  assert(scan_scratch.used() == 0);
}
