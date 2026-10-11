#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>

#ifdef ERROR  // wingdi.h's ERROR macro would clobber CardType::ERROR
#undef ERROR
#endif

namespace card_reader {

enum class CardType {
    NONE = 0,
    MIFARE = 1,   // Aime card (MIFARE)
    FELICA = 2,   // FeliCa card
    ERROR = 3
};

struct CardInfo {
    CardType type = CardType::NONE;
    std::string card_id_hex;  // 16 hex characters (FeliCa) or 20 hex chars (MIFARE)
    uint32_t mifare_uid = 0;  // For MIFARE cards
    bool valid = false;
};

class CardReader {
public:
    CardReader();
    ~CardReader();

    // Initialize the card reader with the serial port path
    // port: e.g., "/dev/ttyUSB0" on Linux, "COM3" on Windows
    // baudrate: 38400 or 115200
    bool initialize(const std::string& port, int baudrate = 38400);

    // Start polling for cards (turns on NFC radio)
    bool start_polling();

    // Stop polling (turns off NFC radio)
    void stop_polling();

    // Non-blocking: polling runs on a background thread.
    // Returns true if a card is currently detected
    bool poll_once();

    // Get the last detected card info (snapshot copy)
    CardInfo get_card_info() const;

    // Check if currently polling
    bool is_polling() const { return is_polling_; }

    // Set LED color (r, g, b in 0-255)
    void set_led_color(uint8_t r, uint8_t g, uint8_t b);

    // Reset LEDs
    void led_reset();

private:
    // Serial communication
    bool serial_connect(const std::string& port, int baudrate);
    void serial_disconnect();
    bool serial_write(const std::vector<uint8_t>& data);
    int port_read(uint8_t* buf, size_t n);  // like read(2): bytes read, 0 on timeout, -1 on error
    void flush_input();
    bool finish_initialize(const std::string& port);
    std::optional<std::vector<uint8_t>> serial_read_response();

    // Protocol implementation
    bool send_command(uint8_t cmd, const std::vector<uint8_t>& payload = {}, bool expect_response = true);
    bool send_command_with_payload(uint8_t cmd, const std::vector<uint8_t>& payload, std::vector<uint8_t>* out_payload, bool expect_response = true);
    std::vector<uint8_t> encode_packet(const std::vector<uint8_t>& payload);
    std::optional<std::vector<uint8_t>> decode_packet(const std::vector<uint8_t>& data);

    bool poll_blocking();  // does serial I/O, worker thread only

    // Card handling
    bool handle_mifare_card(const std::vector<uint8_t>& uid_bytes);
    bool handle_felica_card(const std::vector<uint8_t>& idm_bytes);

    // Serial port handle
    int serial_fd_ = -1;
#ifdef _WIN32
    void* serial_handle_ = nullptr;
#endif
    uint8_t sequence_ = 0;

    // State
    bool is_polling_ = false;
    CardInfo card_info_;      // worker-owned
    CardInfo snapshot_;       // guarded by snap_mutex_
    mutable std::mutex snap_mutex_;
    std::recursive_mutex io_mutex_;  // serializes serial transactions
    std::thread worker_;
    std::atomic<bool> stop_{false};

    // Protocol constants
    static constexpr uint8_t SYNC_BYTE = 0xE0;
    static constexpr uint8_t ESCAPE_BYTE = 0xD0;

    // Commands
    static constexpr uint8_t CMD_RESET = 0x62;
    static constexpr uint8_t CMD_RADIO_ON = 0x40;
    static constexpr uint8_t CMD_RADIO_OFF = 0x41;
    static constexpr uint8_t CMD_POLL = 0x42;
    static constexpr uint8_t CMD_LED_SET_COLOR = 0x81;
    static constexpr uint8_t CMD_LED_RESET = 0xF5;
    static constexpr uint8_t CMD_MIFARE_READ_BLOCK = 0x52;
    static constexpr uint8_t CMD_MIFARE_SET_KEY_SEGA = 0x54;
    static constexpr uint8_t CMD_MIFARE_SELECT = 0x43;
    static constexpr uint8_t CMD_MIFARE_AUTHENTICATE = 0x55;

    // Polling modes
    static constexpr uint8_t POLL_MODE_MIFARE = 0x01;
    static constexpr uint8_t POLL_MODE_FELICA = 0x02;
    static constexpr uint8_t POLL_MODE_BOTH = 0x03;

    // Card types from reader
    static constexpr uint8_t CARD_TYPE_NONE = 0x00;
    static constexpr uint8_t CARD_TYPE_MIFARE = 0x10;
    static constexpr uint8_t CARD_TYPE_FELICA = 0x20;
};

// Opens the reader on a worker thread: scanning the COM ports and resetting the reader takes
// seconds, which froze the title and entry screens. One attempt runs at a time, so the two screens
// never probe the ports together; an attempt left running by one screen is picked up by the next.
class ReaderOpener {
public:
    // Starts an attempt unless one is running or has a result nobody has taken yet
    void request(const std::string& port, int baudrate);
    // The attempt's reader once it has finished (nullptr if none was found); nullopt while it runs
    // or when nothing was requested
    std::optional<std::unique_ptr<CardReader>> take();

private:
    std::future<std::unique_ptr<CardReader>> pending_;
};

ReaderOpener& reader_opener();

}  // namespace card_reader