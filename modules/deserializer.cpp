module;

#include <bitstream/bitstream.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <print>
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

  struct kill_report_data {
    std::uint32_t fields_mask{0};
    std::optional<std::uint32_t> killer_player_idx;
    std::optional<std::string> killer_name;
    std::optional<std::uint32_t> victim_unit_proxy;
    std::optional<std::uint32_t> killer_unit_proxy;
    std::optional<std::uint32_t> victim_player_idx;
    std::optional<std::uint8_t> damage_initiator_type;
    std::optional<std::uint8_t> damage_initiator_subtype;
    std::optional<bool> is_special_kill;
    std::optional<std::uint8_t> killer_unit_type;
    std::optional<std::string> damage_initiator_weapon_name;
    std::uint32_t bits_read{0};
  };

  struct id_field_header32 {
    std::uint32_t flags{0};
    std::array<std::uint32_t, 32> sizes{};
    std::uint8_t count{0};
  };

  bool read_size_code(danet::BitStream& bs, std::uint32_t& size_bits) {
    std::uint8_t code = 0;
    if (!bs.ReadBits(&code, 3)) {
      return false;
    }

    switch (code) {
      case 1:
        size_bits = 1;
        return true;
      case 2:
        size_bits = 8;
        return true;
      case 3:
        size_bits = 16;
        return true;
      case 4:
        size_bits = 32;
        return true;
      case 5:
        size_bits = 64;
        return true;
      case 6:
        size_bits = 96;
        return true;
      case 7:
        size_bits = 128;
        return true;
      case 0:
      default:
        return bs.ReadCompressed(size_bits);
    }
  }

  bool read_id_field_header32(danet::BitStream& bs, id_field_header32& header) {
    std::uint16_t size_table_offset_bytes = 0;
    if (!bs.ReadBits(reinterpret_cast<std::uint8_t*>(&size_table_offset_bytes), 16)) {
      return false;
    }

    if (!bs.ReadCompressed(header.flags)) {
      return false;
    }

    const std::uint32_t payload_read_offset = bs.GetReadOffset();
    const std::uint32_t size_table_offset_bits =
      static_cast<std::uint32_t>(size_table_offset_bytes) * 8;
    if (size_table_offset_bits > payload_read_offset + bs.GetNumberOfUnreadBits()) {
      return false;
    }

    header.count = static_cast<std::uint8_t>(std::popcount(header.flags));
    bs.SetReadOffset(size_table_offset_bits);
    for (std::uint8_t i = 0; i < header.count; ++i) {
      if (!read_size_code(bs, header.sizes[i])) {
        return false;
      }
    }

    bs.SetReadOffset(payload_read_offset);
    return true;
  }

  bool read_proxy_id(danet::BitStream& bs, std::uint32_t& value) {
    if (bs.ReadCompressed(value)) {
      return true;
    }

    if (bs.GetNumberOfUnreadBits() >= 32) {
      return bs.Read(value);
    }

    return false;
  }

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

  std::expected<kill_report_data, std::error_code>
  deserialize_kill_report_packet(std::span<const std::byte> payload) {
    if (payload.empty()) {
      return std::unexpected(make_error_code(deserialize_error::insufficient_data));
    }

    danet::BitStream bs(
      reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), false
    );

    id_field_header32 field_header;
    if (!read_id_field_header32(bs, field_header)) {
      return std::unexpected(make_error_code(deserialize_error::bitstream_read_failure));
    }

    kill_report_data result;
    result.fields_mask = field_header.flags;

    bool read_ok = true;
    std::uint32_t pending = field_header.flags;
    std::uint8_t field_ordinal = 0;
    while (read_ok && pending != 0) {
      const std::uint32_t field_index = static_cast<std::uint32_t>(std::countr_zero(pending));
      switch (field_index) {
        case 1: {
          std::uint32_t value = 0;
          read_ok &= bs.Read(value);
          if (read_ok) {
            result.killer_player_idx = value;
          }
          break;
        }
        case 2: {
          std::string value;
          read_ok &= bs.Read(value);
          if (read_ok) {
            result.killer_name = std::move(value);
          }
          break;
        }
        case 3: {
          std::uint32_t value = 0;
          read_ok &= read_proxy_id(bs, value);
          if (read_ok) {
            result.victim_unit_proxy = value;
          }
          break;
        }
        case 4: {
          std::uint32_t value = 0;
          read_ok &= read_proxy_id(bs, value);
          if (read_ok) {
            result.killer_unit_proxy = value;
          }
          break;
        }
        case 5: {
          std::uint32_t value = 0;
          read_ok &= bs.Read(value);
          if (read_ok) {
            result.victim_player_idx = value;
          }
          break;
        }
        case 6: {
          std::uint8_t value = 0;
          read_ok &= bs.Read(value);
          if (read_ok) {
            result.damage_initiator_type = value;
          }
          break;
        }
        case 7: {
          std::uint8_t value = 0;
          read_ok &= bs.Read(value);
          if (read_ok) {
            result.damage_initiator_subtype = value;
          }
          break;
        }
        case 8: {
          bool value = false;
          read_ok &= bs.Read(value);
          if (read_ok) {
            result.is_special_kill = value;
          }
          break;
        }
        case 9: {
          std::uint8_t value = 0;
          read_ok &= bs.Read(value);
          if (read_ok) {
            result.killer_unit_type = value;
          }
          break;
        }
        case 10: {
          std::string value;
          read_ok &= bs.Read(value);
          if (read_ok) {
            result.damage_initiator_weapon_name = std::move(value);
          }
          break;
        }
        default:
          if (field_ordinal >= field_header.count) {
            read_ok = false;
            break;
          }
          bs.IgnoreBits(field_header.sizes[field_ordinal]);
          break;
      }

      pending &= ~(1u << field_index);
      ++field_ordinal;
    }

    if (!read_ok) {
      return std::unexpected(make_error_code(deserialize_error::invalid_format));
    }

    result.bits_read = bs.GetReadOffset();
    return result;
  }

  export std::expected<chat_packet_data, std::error_code>
  deserialize_chat(std::span<const std::byte> payload) {
    return deserialize_chat_packet(payload);
  }

  export void print_chat_packet(std::span<const std::byte> payload) {
    auto chat_result = deserialize_chat_packet(payload);
    if (!chat_result) {
      std::println("  Failed to deserialize chat packet: {}", chat_result.error().message());
      return;
    }

    const auto& chat = *chat_result;
    std::println(
      "  Chat Sender='{}', Message='{}', IsEnemy={}, Channel={}, UnreadBits={}", chat.sender_name,
      chat.message, chat.is_enemy ? "true" : "false", chat.channel_id,
      (payload.size() * 8) - chat.bits_read
    );
  }

  export std::expected<mpi_packet_data, std::error_code>
  deserialize_mpi(std::span<const std::byte> payload) {
    return deserialize_mpi_packet(payload);
  }

  export std::expected<generic_packet_data, std::error_code>
  deserialize_generic(std::span<const std::byte> payload) {
    return deserialize_generic_packet(payload);
  }

  export std::expected<kill_report_data, std::error_code>
  deserialize_kill_report(std::span<const std::byte> payload) {
    return deserialize_kill_report_packet(payload);
  }

  export void print_kill_report(std::span<const std::byte> payload) {
    auto tkr_result = deserialize_kill_report_packet(payload);
    if (!tkr_result) {
      std::println("  TextKillReport:  deserialize failed: {}", tkr_result.error().message());
      return;
    }

    const auto& tkr = *tkr_result;
    std::println("  TextKillReport:  mask=0x{:08X}, bits_read={}", tkr.fields_mask, tkr.bits_read);
    if (tkr.killer_player_idx) {
      std::println("    killer_player_idx:            {}", *tkr.killer_player_idx);
    }
    if (tkr.killer_name) {
      std::println("    killer_name:                  {}", *tkr.killer_name);
    }
    if (tkr.victim_unit_proxy) {
      std::println("    victim_unit_proxy:            {}", *tkr.victim_unit_proxy);
    }
    if (tkr.killer_unit_proxy) {
      std::println("    killer_unit_proxy:            {}", *tkr.killer_unit_proxy);
    }
    if (tkr.victim_player_idx) {
      std::println("    victim_player_idx:            {}", *tkr.victim_player_idx);
    }
    if (tkr.damage_initiator_type) {
      std::println("    damage_initiator_type:        {}", *tkr.damage_initiator_type);
    }
    if (tkr.damage_initiator_subtype) {
      std::println("    damage_initiator_subtype:     {}", *tkr.damage_initiator_subtype);
    }
    if (tkr.is_special_kill) {
      std::println("    is_special_kill:              {}", *tkr.is_special_kill ? "true" : "false");
    }
    if (tkr.killer_unit_type) {
      std::println("    killer_unit_type:             {}", *tkr.killer_unit_type);
    }
    if (tkr.damage_initiator_weapon_name) {
      if (tkr.damage_initiator_weapon_name->empty()) {
        std::println("    damage_initiator_weapon_name: <empty>");
      } else {
        std::println("    damage_initiator_weapon_name: {}", *tkr.damage_initiator_weapon_name);
      }
    }
  }

} // namespace wrpl

namespace std {
  template <>
  struct is_error_code_enum<wrpl::deserialize_error> : true_type {};
} // namespace std
