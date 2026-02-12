#include <print>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

import parser;

std::optional<std::string_view> find_stream(std::string_view file_data) {
  constexpr std::size_t replay_header_size = 0x4C6;
  if (file_data.size() < replay_header_size + 2)
    return std::nullopt;

  for (std::size_t i = replay_header_size; i + 1 < file_data.size(); ++i) {
    auto cmf = static_cast<std::uint8_t>(file_data[i]);
    auto flg = static_cast<std::uint8_t>(file_data[i + 1]);
    // rfc 1950 2.2: CM=8 (deflate), CINFO=7, checksum valid
    if (cmf == 0x78 && (cmf * 256u + flg) % 31 == 0) {
      return file_data.substr(i);
    }
  }
  return std::nullopt;
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::println(stderr, "Usage: {} <path_wrpl>", argv[0]);
    return 1;
  }

  try {
    const std::filesystem::path wrpl_path = argv[1];
    if (!std::filesystem::exists(wrpl_path)) {
      std::println(stderr, "File not found at {}", wrpl_path.string());
      return 1;
    }

    std::ifstream file(wrpl_path, std::ios::binary | std::ios::ate);
    if (!file) {
      std::println(stderr, "Could not open file {}", wrpl_path.string());
      return 1;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
      std::println(stderr, "Could not read file content from {}", wrpl_path.string());
      return 1;
    }

    std::println("Read {} bytes from {}", size, wrpl_path.string());
    std::string_view file_content(buffer.data(), size);
    std::optional<std::string_view> zlib_data = find_stream(file_content);

    if (!zlib_data) {
      std::println(stderr, "Zlib stream not found in file");
      return 1;
    }

    std::println(
      "Found zlib stream at offset {}. Size: {} bytes", zlib_data->data() - file_content.data(),
      zlib_data->size()
    );

    std::stringstream zlib_stream;
    zlib_stream.write(zlib_data->data(), zlib_data->size());
    zlib_stream.seekg(0);

    wrpl::process_stream(zlib_stream);

  } catch (const std::exception& e) {
    std::println(stderr, "An unexpected error: {}", e.what());
    return 1;
  }

  return 0;
}
