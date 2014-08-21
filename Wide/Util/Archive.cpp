#include <Wide/Util/Archive.h>
#ifdef _MSC_VER
#define LIBARCHIVE_STATIC
#endif
#include <archive.h>
#include <archive_entry.h>

using namespace Wide;
using namespace Util;

Archive Util::ReadFromFile(std::string filepath) {
    Archive a;
    auto read_archive = std::unique_ptr<archive, decltype(&archive_read_free)>(archive_read_new(), archive_read_free);
    archive_read_support_filter_all(read_archive.get());
    archive_read_support_format_all(read_archive.get());
    auto code = archive_read_open_filename(read_archive.get(), filepath.c_str(), 10240);
    if (code != ARCHIVE_OK)
        throw std::runtime_error("Failed to open archive.");
    archive_entry* entry;
    while (archive_read_next_header(read_archive.get(), &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        auto key = archive_entry_pathname(entry);
        auto size = archive_entry_size(entry);
        std::string buf;
        buf.resize(size);
        auto val = archive_read_data(read_archive.get(), &buf[0], size);
        if (val == ARCHIVE_EOF)
            break;
        if (val == ARCHIVE_FATAL)
            throw std::runtime_error("Failed to read from archive: " + std::string(archive_error_string(read_archive.get())));
        a.data[key] = buf;
    }
    return a;
}

void Util::WriteToFile(Archive a, std::string filepath) {
    auto write_archive = std::unique_ptr<archive, decltype(&archive_write_free)>(archive_write_new(), archive_write_free);
    //archive_write_add_filter_gzip(write_archive.get());
    archive_write_set_format_gnutar(write_archive.get());
    archive_write_open_filename(write_archive.get(), filepath.c_str());

    for (auto&& file : a.data) {
        auto entry = std::unique_ptr<archive_entry, decltype(&archive_entry_free)>(archive_entry_new(), &archive_entry_free);
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_pathname(entry.get(), file.first.c_str());
        archive_entry_set_size(entry.get(), file.second.size());
        archive_write_header(write_archive.get(), entry.get());
        archive_write_data(write_archive.get(), &file.second[0], file.second.size());
    }
}