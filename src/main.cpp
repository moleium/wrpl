#include <print>

#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

import parser;

std::optional<std::string_view> find_stream(std::string_view file_data) {
  constexpr std::array<std::string_view, 3> possible_headers = {
    std::string_view{"\x78\x01"}, std::string_view{"\x78\x9C"}, std::string_view{"\x78\xDA"}
  };

  for (const auto& header : possible_headers) {
    if (size_t pos = file_data.find(header); pos != std::string_view::npos) {
      return file_data.substr(pos);
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
