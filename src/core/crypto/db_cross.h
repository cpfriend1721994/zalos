#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace Zalos::Core::Crypto {

// Structures from reverse-engineered Zalo BinNet / TLV protocol
struct AttachmentData {
    std::string type;
    int id{0};
    int catId{0};
    std::string action;
    std::string params;
    std::string extInfo;
    std::string title;
    std::string href;
    std::string thumb;
    std::string ext;
    std::string size;
    std::string description;
};

struct MentionData {
    int uid{0};
    int pos{0};
    int len{0};
    int type{0};
};

struct QuoteData {
    int64_t ownerId{0};
    int cliMsgType{0};
    std::string fromD;
    int64_t cliMsgId{0};
    int64_t globalMsgId{0};
    int64_t ts{0};
    std::string msg;
    std::string attach;
};

struct BinNetMessage {
    std::string text;
    std::string dataSource;
    int quoteStatus{0};
    QuoteData quote;
    std::vector<AttachmentData> attachs;
    std::vector<MentionData> mentions;
};

class DbCross {
public:
    DbCross() = default;
    ~DbCross() = default;

    // Derives 32-byte AES key from Zalo private key string (from macOS/Linux key format)
    static std::vector<uint8_t> deriveAes256Key(const std::string& privateKeyStr);

    // Decrypts AES-256-CBC payload with PKCS#7 unpadding
    static std::vector<uint8_t> decryptAes256Cbc(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& iv
    );

    // Decrypts AES-256-CBC without padding using fixed 64KB chunk reset-IV
    static bool decryptAes256CbcChunkedResetIv(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& key32,
        std::vector<uint8_t>& plaintextOut
    );

    // Decompresses LZMA/XZ payload
    static std::vector<uint8_t> decompressLzma(const std::vector<uint8_t>& compressedData);

    // Decrypts a ZDB4.0 mobile backup (format 1) and extracts its files (one SQLite DB per conversation)
    // into `outputDir`. `privateKeyStr` is the RSA-decrypted backup key as text; only its first 32
    // characters, upper-cased, are the AES-256 key. Returns false with a reason on any failure.
    static bool decompressAndDecryptZdb4(
        const std::string& inputPath,
        const std::string& outputDir,
        const std::string& privateKeyStr,
        std::string* error = nullptr
    );

    // Parses Zalo BinNet proprietary TLV binary network payload
    static BinNetMessage parseBinNet(const uint8_t* buf, size_t len);

    // Self-test helper to verify encryption/decryption round-trip
    static bool verifyCryptoSelfTest();
};

} // namespace Zalos::Core::Crypto

