module;

#include <print>
#include <zlib.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <format>
#include <istream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

export module parser;

namespace wrpl {

  enum class packet_type : std::uint8_t {
    end_marker = 0,
    start_marker = 1,
    aircraft_small = 2,
    chat = 3,
    mpi = 4,
    next_segment = 5,
    ecs = 6,
    snapshot = 7,
    replay_header_info = 8,
  };

  std::string get_packet_type_name(std::uint8_t type_val) {
    switch (static_cast<packet_type>(type_val)) {
      case packet_type::end_marker:
        return "end_marker";
      case packet_type::start_marker:
        return "start_marker";
      case packet_type::aircraft_small:
        return "aircraft_small";
      case packet_type::chat:
        return "chat";
      case packet_type::mpi:
        return "mpi";
      case packet_type::next_segment:
        return "next_segment";
      case packet_type::ecs:
        return "ecs";
      case packet_type::snapshot:
        return "snapshot";
      case packet_type::replay_header_info:
        return "replay_header_info";
      default:
        return std::format("unknown ({})", type_val);
    }
  }

  class byte_stream_reader {
public:
    byte_stream_reader(std::span<const std::byte> data) : data_{data} {
    }

    std::span<const std::byte> read(std::size_t size) {
      if (position_ + size > data_.size()) {
        size = data_.size() - position_;
      }
      std::span<const std::byte> result = data_.subspan(position_, size);
      position_ += size;
      return result;
    }

    std::span<const std::byte> remaining_bytes() {
      return data_.subspan(position_);
    }

private:
    std::span<const std::byte> data_;
    std::size_t position_ = 0;
  };

  class decompressed_stream_reader {
public:
    explicit decompressed_stream_reader(std::istream& compressed_stream) :
        compressed_stream_{compressed_stream} {
      z_stream_.zalloc = Z_NULL;
      z_stream_.zfree = Z_NULL;
      z_stream_.opaque = Z_NULL;
      z_stream_.avail_in = 0;
      z_stream_.next_in = Z_NULL;
      if (inflateInit(&z_stream_) != Z_OK) {
        throw std::runtime_error("zlib inflateInit failed");
      }
    }

    ~decompressed_stream_reader() {
      inflateEnd(&z_stream_);
    }

    decompressed_stream_reader(const decompressed_stream_reader&) = delete;
    decompressed_stream_reader& operator=(const decompressed_stream_reader&) = delete;
    decompressed_stream_reader(decompressed_stream_reader&&) = delete;
    decompressed_stream_reader& operator=(decompressed_stream_reader&&) = delete;

    std::vector<std::byte> read(std::size_t size) {
      fill_buffer(size);
      std::size_t bytes_to_read = std::min(size, buffer_.size());
      std::vector<std::byte> result(buffer_.begin(), buffer_.begin() + bytes_to_read);
      buffer_.erase(buffer_.begin(), buffer_.begin() + bytes_to_read);
      return result;
    }

    void prepend_to_buffer(std::span<const std::byte> data) {
      buffer_.insert(buffer_.begin(), data.begin(), data.end());
    }

    std::streampos tell() {
      std::streampos pos = compressed_stream_.tellg();
      if (pos != -1) {
        pos -= z_stream_.avail_in;
      }
      return pos;
    }

    bool is_eof() {
      fill_buffer(1);
      return eof_compressed_ && buffer_.empty();
    }

private:
    static constexpr std::size_t CHUNK_SIZE = 16 * 1024;
    std::istream& compressed_stream_;
    z_stream z_stream_{};
    std::deque<std::byte> buffer_;
    bool eof_compressed_ = false;
    std::size_t compressed_bytes_fed_ = 0;
    std::vector<std::byte> input_chunk_buffer_{CHUNK_SIZE};

    void fill_buffer(std::size_t min_bytes) {
      while (buffer_.size() < min_bytes && !eof_compressed_) {
        if (z_stream_.avail_in == 0 && !compressed_stream_.eof()) {
          compressed_stream_.read(reinterpret_cast<char*>(input_chunk_buffer_.data()), CHUNK_SIZE);
          std::streamsize bytes_read = compressed_stream_.gcount();
          compressed_bytes_fed_ += static_cast<std::size_t>(bytes_read);
          z_stream_.avail_in = static_cast<uInt>(bytes_read);
          z_stream_.next_in = reinterpret_cast<Bytef*>(input_chunk_buffer_.data());
        }

        if (z_stream_.avail_in == 0 && compressed_stream_.eof()) {
          eof_compressed_ = true;
        }

        std::vector<std::byte> output_chunk(CHUNK_SIZE);
        z_stream_.avail_out = CHUNK_SIZE;
        z_stream_.next_out = reinterpret_cast<Bytef*>(output_chunk.data());

        int ret = inflate(&z_stream_, eof_compressed_ ? Z_FINISH : Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
          throw std::runtime_error(
            std::format(
              "zlib inflate error (fed ~{} bytes): {}", compressed_bytes_fed_,
              z_stream_.msg ? z_stream_.msg : "unknown"
            )
          );
        }

        std::size_t have = CHUNK_SIZE - z_stream_.avail_out;
        if (have > 0) {
          buffer_.insert(buffer_.end(), output_chunk.begin(), output_chunk.begin() + have);
        }

        if (ret == Z_STREAM_END) {
          eof_compressed_ = true;
        }
      }
    }
  };

  struct variable_length_result {
    std::int64_t payload_size;
    std::size_t prefix_bytes_read;
  };

  std::optional<variable_length_result> read_variable_length_size(byte_stream_reader& stream) {
    std::span<const std::byte> first_byte_data = stream.read(1);
    if (first_byte_data.empty()) {
      return std::nullopt;
    }
    std::uint8_t first_byte = static_cast<std::uint8_t>(first_byte_data[0]);
    std::size_t prefix_bytes_read = 1;
    std::int64_t payload_size = -1;

    if ((first_byte & 0x80) != 0) {
      if ((first_byte & 0x40) == 0) {
        payload_size = first_byte & 0x7F;
      } else {
        std::println("Warning: Invalid first size prefix byte: {:#02x}", first_byte);
        return variable_length_result{-1, prefix_bytes_read};
      }
    } else {
      if ((first_byte & 0x40) != 0) {
        std::span<const std::byte> b1_data = stream.read(1);
        if (b1_data.size() < 1) {
          return std::nullopt;
        }
        prefix_bytes_read += 1;
        payload_size =
          ((static_cast<std::uint32_t>(first_byte) << 8) | static_cast<std::uint8_t>(b1_data[0])) ^
          0x4000;
      } else if ((first_byte & 0x20) != 0) {
        std::span<const std::byte> b1_b2_data = stream.read(2);
        if (b1_b2_data.size() < 2) {
          return std::nullopt;
        }
        prefix_bytes_read += 2;
        payload_size = ((static_cast<std::uint32_t>(first_byte) << 16) |
                        (static_cast<std::uint8_t>(b1_b2_data[0]) << 8) |
                        static_cast<std::uint8_t>(b1_b2_data[1])) ^
                       0x200000;
      } else if ((first_byte & 0x10) != 0) {
        std::span<const std::byte> b1_b3_data = stream.read(3);
        if (b1_b3_data.size() < 3) {
          return std::nullopt;
        }
        prefix_bytes_read += 3;
        payload_size = ((static_cast<std::uint32_t>(first_byte) << 24) |
                        (static_cast<std::uint8_t>(b1_b3_data[0]) << 16) |
                        (static_cast<std::uint8_t>(b1_b3_data[1]) << 8) |
                        static_cast<std::uint8_t>(b1_b3_data[2])) ^
                       0x10000000;
      } else {
        std::span<const std::byte> b1_b4_data = stream.read(4);
        if (b1_b4_data.size() < 4) {
          return std::nullopt;
        }
        prefix_bytes_read += 4;
        std::uint32_t raw_value;
        std::memcpy(&raw_value, b1_b4_data.data(), sizeof(raw_value));
        if constexpr (std::endian::native == std::endian::big) {
          raw_value = std::byteswap(raw_value);
        }
        payload_size = raw_value;
      }
    }

    if (payload_size < 0) {
      std::println(
        "Warning: Calculated negative payload size ({}). This might "
        "indicate an issue.",
        payload_size
      );
      return variable_length_result{-1, prefix_bytes_read};
    }

    return variable_length_result{payload_size, prefix_bytes_read};
  }

  struct packet_header_result {
    std::uint8_t packet_type_val;
    std::uint32_t timestamp_ms;
    std::size_t bytes_read_for_header;
  };

  std::optional<packet_header_result>
  read_packet_header_from_stream(byte_stream_reader& data_stream, std::uint32_t last_timestamp_ms) {
    std::span<const std::byte> first_byte_data = data_stream.read(1);
    if (first_byte_data.empty()) {
      return std::nullopt;
    }
    std::uint8_t first_byte = static_cast<std::uint8_t>(first_byte_data[0]);
    std::size_t bytes_read_for_header = 1;
    std::uint32_t timestamp_ms = last_timestamp_ms;
    std::uint8_t packet_type_val = 0;

    if ((first_byte & 0x10) != 0) {
      packet_type_val = first_byte ^ 0x10;
    } else {
      packet_type_val = first_byte;
      std::span<const std::byte> ts_bytes = data_stream.read(4);
      if (ts_bytes.size() < 4) {
        std::println("Warning: Unexpected EOF reading timestamp after type byte.");
        return packet_header_result{packet_type_val, timestamp_ms, bytes_read_for_header};
      }
      std::memcpy(&timestamp_ms, ts_bytes.data(), sizeof(timestamp_ms));
      if constexpr (std::endian::native == std::endian::big) {
        timestamp_ms = std::byteswap(timestamp_ms);
      }
      bytes_read_for_header += 4;
    }

    return packet_header_result{packet_type_val, timestamp_ms, bytes_read_for_header};
  }

  export void process_stream(std::istream& compressed_stream) {
    decompressed_stream_reader stream(compressed_stream);
    int packet_index = 0;
    std::uint32_t last_timestamp_ms = 0;
    std::uint64_t total_decompressed_bytes_processed = 0;

    while (!stream.is_eof()) {
      std::streampos approx_compressed_pos_start_packet = stream.tell();
      std::println(
        "\n== Packet {} (Comp. offset ~{:#0x}) ==", packet_index,
        static_cast<std::uint64_t>(approx_compressed_pos_start_packet)
      );
      try {
        std::vector<std::byte> size_prefix_bytes = stream.read(5);
        if (size_prefix_bytes.empty()) {
          if (stream.is_eof()) {
            std::println("Clean EOF reached before next packet size prefix.");
          } else {
            std::println("Could not read packet size prefix despite not being at EOF.");
          }
          break;
        }

        byte_stream_reader prefix_stream(size_prefix_bytes);
        std::optional<variable_length_result> size_result =
          read_variable_length_size(prefix_stream);

        if (!size_result || size_result->payload_size < 0) {
          std::print("Error reading/interpreting size prefix. Bytes: ");
          for (const std::byte b : size_prefix_bytes) {
            std::print("{:02x}", static_cast<std::uint8_t>(b));
          }
          std::println(". Stopping.");
          break;
        }

        std::span<const std::byte> unused_prefix_bytes = prefix_stream.remaining_bytes();
        stream.prepend_to_buffer(unused_prefix_bytes);

        std::int64_t payload_size = size_result->payload_size;
        std::size_t prefix_bytes_read = size_result->prefix_bytes_read;

        std::println(
          "  Read size prefix ({} decomp. bytes): Expected payload "
          "size = {} bytes",
          prefix_bytes_read, payload_size
        );

        std::vector<std::byte> packet_data = stream.read(payload_size);

        if (packet_data.size() != static_cast<std::size_t>(payload_size)) {
          std::println(
            "  Warning: Incomplete packet! Expected {}, got {}.", payload_size, packet_data.size()
          );
          if (packet_data.empty()) {
            std::println("  No payload data read. Stopping.");
            break;
          }
        }
        total_decompressed_bytes_processed += packet_data.size();
        byte_stream_reader payload_stream(packet_data);
        std::optional<packet_header_result> header_result =
          read_packet_header_from_stream(payload_stream, last_timestamp_ms);
        if (header_result) {
          std::println(
            "  Parsed Header ({} bytes): Type={}, Timestamp={}ms",
            header_result->bytes_read_for_header,
            get_packet_type_name(header_result->packet_type_val), header_result->timestamp_ms
          );
          last_timestamp_ms = header_result->timestamp_ms;
          std::span<const std::byte> payload_bytes = payload_stream.remaining_bytes();
          std::size_t payload_size_actual = payload_bytes.size();
          std::println("  Actual Payload Size: {} bytes", payload_size_actual);
          if (static_cast<packet_type>(header_result->packet_type_val) == packet_type::mpi &&
              payload_size_actual >= 4) {
            std::uint16_t obj_id, msg_id;
            std::memcpy(&obj_id, payload_bytes.data(), sizeof(obj_id));
            std::memcpy(&msg_id, payload_bytes.data() + 2, sizeof(msg_id));
            if constexpr (std::endian::native == std::endian::big) {
              obj_id = std::byteswap(obj_id);
              msg_id = std::byteswap(msg_id);
            }
            std::println(
              "  MPI Header:      ObjectID=0x{:04X}, MessageID=0x{:04X}", obj_id, msg_id
            );
            payload_bytes = payload_bytes.subspan(4);
            payload_size_actual -= 4;
          }
          if (payload_size_actual > 0) {
            std::print("  Payload Hex: ");
            std::size_t bytes_to_print =
              std::min(payload_size_actual, static_cast<std::size_t>(64));
            for (std::size_t i = 0; i < bytes_to_print; ++i) {
              std::print("{:02X} ", static_cast<std::uint8_t>(payload_bytes[i]));
            }
            if (payload_size_actual > bytes_to_print) {
              std::print("...");
            }
            std::println("");
          } else {
            std::println("  Payload Hex: (empty)");
          }
        }
      } catch (const std::exception& e) {
        std::println(stderr, "  Error during packet processing loop: {}", e.what());
        break;
      }
      packet_index++;
    }
    std::streampos approx_compressed_pos_end = stream.tell();
    std::println(
      "\n== End of stream processing (Comp. offset ~{:#0x}) ==",
      static_cast<std::uint64_t>(approx_compressed_pos_end)
    );
    std::println("Total decompressed bytes processed: {}", total_decompressed_bytes_processed);
  }
} // namespace wrpl
