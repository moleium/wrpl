module;

#include <bitstream/bitstream.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <system_error>
#include <vector>

export module deserializer;

namespace wrpl {

  enum class deserialize_error {
    insufficient_data = 1,
    invalid_format,
    bitstream_read_failure,
    unsupported_packet_type,
  };

  class deserialize_error_category : public std::error_category {
public:
    const char* name() const noexcept override {
      return "wrpl_deserialize";
    }

    std::string message(int ev) const override {
      switch (static_cast<deserialize_error>(ev)) {
        case deserialize_error::insufficient_data:
          return "insufficient data in packet payload";
        case deserialize_error::invalid_format:
          return "invalid packet format";
        case deserialize_error::bitstream_read_failure:
          return "bitstream read operation failed";
        case deserialize_error::unsupported_packet_type:
          return "unsupported packet type for deserialization";
        default:
          return "unknown deserialize error";
      }
    }
  };

  inline const std::error_category& get_deserialize_error_category() {
    static deserialize_error_category instance;
    return instance;
  }

  inline std::error_code make_error_code(deserialize_error e) {
    return {static_cast<int>(e), get_deserialize_error_category()};
  }

  struct chat_packet_data {
    std::string sender_name;
    std::string message;
    bool is_enemy{false};
    std::uint8_t channel_id{0};
    std::uint32_t bits_read{0};
  };

  struct mpi_packet_data {
    std::uint16_t object_id{0};
    std::uint16_t message_id{0};
    std::vector<std::byte> payload;
  };

  struct generic_packet_data {
    std::vector<std::byte> raw_payload;
  };

  std::expected<chat_packet_data, std::error_code>
  deserialize_chat_packet(std::span<const std::byte> payload) {
    if (payload.empty()) {
      return std::unexpected(make_error_code(deserialize_error::insufficient_data));
    }

    danet::BitStream bs(
      reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), false
    );

    chat_packet_data result;
    bool read_ok = true;

    // ignored
    std::uint16_t prefix_len = 0;
    read_ok &= bs.ReadCompressed(prefix_len);
    if (read_ok && prefix_len > 0) {
      bs.IgnoreBytes(prefix_len);
    }

    std::uint16_t sender_len = 0;
    if (read_ok) {
      read_ok &= bs.ReadCompressed(sender_len);
    }

    if (read_ok && sender_len > 0) {
      std::vector<char> sender_buf(sender_len + 1, '\0');
      read_ok &= bs.Read(sender_buf.data(), sender_len);
      if (read_ok) {
        result.sender_name.assign(sender_buf.data(), sender_len);
      }
    }

    std::uint16_t message_len = 0;
    if (read_ok) {
      read_ok &= bs.ReadCompressed(message_len);
    }

    if (read_ok && message_len > 0) {
      std::vector<char> message_buf(message_len + 1, '\0');
      read_ok &= bs.Read(message_buf.data(), message_len);
      if (read_ok) {
        result.message.assign(message_buf.data(), message_len);
      }
    }

    if (read_ok && bs.GetNumberOfUnreadBits() >= 8) {
      read_ok &= bs.Read(result.channel_id);
    }

    if (read_ok && bs.GetNumberOfUnreadBits() >= 1) {
      read_ok &= bs.Read(result.is_enemy);
    }

    if (!read_ok) {
      return std::unexpected(make_error_code(deserialize_error::bitstream_read_failure));
    }

    result.bits_read = bs.GetReadOffset();
    return result;
  }

  std::expected<mpi_packet_data, std::error_code>
  deserialize_mpi_packet(std::span<const std::byte> payload) {
    if (payload.size() < 4) {
      return std::unexpected(make_error_code(deserialize_error::insufficient_data));
    }

    mpi_packet_data result;
    std::memcpy(&result.object_id, payload.data(), sizeof(result.object_id));
    std::memcpy(&result.message_id, payload.data() + 2, sizeof(result.message_id));

    if (payload.size() > 4) {
      result.payload.resize(payload.size() - 4);
      std::memcpy(result.payload.data(), payload.data() + 4, payload.size() - 4);
    }

    return result;
  }

  std::expected<generic_packet_data, std::error_code>
  deserialize_generic_packet(std::span<const std::byte> payload) {
    generic_packet_data result;
    result.raw_payload.assign(payload.begin(), payload.end());
    return result;
  }

  export std::expected<chat_packet_data, std::error_code>
  deserialize_chat(std::span<const std::byte> payload) {
    return deserialize_chat_packet(payload);
  }

  export std::expected<mpi_packet_data, std::error_code>
  deserialize_mpi(std::span<const std::byte> payload) {
    return deserialize_mpi_packet(payload);
  }

  export std::expected<generic_packet_data, std::error_code>
  deserialize_generic(std::span<const std::byte> payload) {
    return deserialize_generic_packet(payload);
  }

} // namespace wrpl

namespace std {
  template <>
  struct is_error_code_enum<wrpl::deserialize_error> : true_type {};
} // namespace std
