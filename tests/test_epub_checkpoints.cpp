#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "epub/XmlSax.h"
#include "css/Css.h"
#include "layout/ChapterLayout.h"
#include "../src/app/widgets/epub_chapter_labels.h"

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

class TestFont : public BookFont {
 public:
  int16_t advance(uint32_t, uint16_t, uint8_t) override { return 1; }
  int16_t lineHeight(uint16_t) override { return 2; }
  int16_t ascent(uint16_t) override { return 1; }
};

class CountingPages : public PageSink {
 public:
  bool onPage(const Page&) override { ++count; return true; }
  unsigned count = 0;
};

static BookStatus layout_test(const std::string& xml, int16_t width, int16_t height,
                              unsigned* pages) {
  MemorySource source(xml);
  ZipCatalog zip;
  const ZipEntry entry = ZipEntryReader::rawEntry(static_cast<uint32_t>(xml.size()));
  std::vector<char> memory(384 * 1024);
  Arena scratch(memory.data(), memory.size());
  TestFont font;
  LayoutParams params;
  params.font = &font;
  params.pageWidth = width;
  params.pageHeight = height;
  params.marginLeft = params.marginRight = params.marginTop = params.marginBottom = 0;
  params.baseSizePx = 1;
  CountingPages sink;
  const BookStatus status = ChapterLayout::layout(source, zip, entry, "chapter.xhtml", params,
                                                   scratch, sink);
  assert(scratch.used() == 0);
  *pages = sink.count;
  return status;
}

int main() {
  const std::string empty;
  MemorySource labels_source(empty);
  const TocEntry toc_entries[] = {
      {"First", "first.xhtml", nullptr, 0},
      {"Part", "section.xhtml", "anchor", 0},
      {"Second", "second.xhtml", nullptr, 0},
      {"Subsection", "second.xhtml", "sub", 1},
  };
  bool interrupted_labels = false;
  assert(!strcmp(epub_reader_impl::chapter_navigation_label(labels_source, toc_entries, 4,
                                                               "first.xhtml", interrupted_labels), "First"));
  assert(!epub_reader_impl::chapter_navigation_label(labels_source, toc_entries, 4, "section.xhtml", interrupted_labels));
  assert(!epub_reader_impl::chapter_navigation_label(labels_source, toc_entries, 4, "second.xhtml", interrupted_labels));
  assert(!epub_reader_impl::chapter_navigation_label(labels_source, toc_entries, 4, "missing.xhtml", interrupted_labels));
  labels_source.stopped = true;
  assert(!epub_reader_impl::chapter_navigation_label(labels_source, toc_entries, 4, "first.xhtml", interrupted_labels));
  assert(interrupted_labels);
  unsigned pages = 0;
  assert(layout_test("<body><p>hello</p></body>", 8, 8, &pages) == BookStatus::Ok);
  assert(pages > 0);
  assert(layout_test("<body></body>", 8, 8, &pages) == BookStatus::Ok);
  assert(pages == 0);
  assert(layout_test("<body><p>  \n </p></body>", 8, 8, &pages) == BookStatus::Ok);
  assert(pages == 0);
  assert(layout_test("<body><p>hello</p></body>", 8, 1, &pages) == BookStatus::OutOfMemory);
  const std::string narrow = "<body><p>" + std::string(1200, 'a') + "</p></body>";
  assert(layout_test(narrow, 1, 8, &pages) == BookStatus::OutOfMemory);
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
