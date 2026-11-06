#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace Zalos::Core::Crypto {

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
    int64_t uid{0};
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

    static std::vector<uint8_t> deriveAes256Key(const std::string& privateKeyStr);

    static std::vector<uint8_t> decryptAes256Cbc(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& iv
    );

    static bool decryptAes256CbcChunkedResetIv(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& key32,
        std::vector<uint8_t>& plaintextOut
    );

    static std::vector<uint8_t> decompressLzma(const std::vector<uint8_t>& compressedData);

    static bool decompressAndDecryptZdb4(
        const std::string& inputPath,
        const std::string& outputDir,
        const std::string& privateKeyStr,
        std::string* error = nullptr
    );

    static BinNetMessage parseBinNet(const uint8_t* buf, size_t len);

    static bool verifyCryptoSelfTest();
};

}
