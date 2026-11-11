#include "db_cross.h"

#include <openssl/evp.h>
#include <openssl/aes.h>
#include <lzma.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Zalos::Core::Crypto {

namespace {

static uint32_t read_u32_be(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct FileEntry {
    std::string name;
    uint32_t size;
};

struct TlvBoxLite {
    std::unordered_map<uint32_t, std::vector<std::vector<uint8_t>>> values;
    bool little_endian_used = true;

    bool ParseCore(const uint8_t* buf, size_t n, bool little_endian, std::string& err) {
        values.clear();
        if (!buf || n == 0) { err = "Empty buffer"; return false; }
        size_t off = 0;
        while (off + 8 <= n) {
            uint32_t key = little_endian ? read_u32_le(buf + off) : read_u32_be(buf + off);
            uint32_t len = little_endian ? read_u32_le(buf + off + 4) : read_u32_be(buf + off + 4);
            off += 8;
            if (len > (n - off)) { err = "TLV length out of bounds"; return false; }
            std::vector<uint8_t> v(buf + off, buf + off + len);
            off += len;
            values[key].push_back(std::move(v));
        }
        if (off != n && (n - off) > 8) { err = "Trailing bytes"; return false; }
        return true;
    }

    int ScoreForBinNetShape() const {
        int score = 0;
        static const uint32_t expected_top[] = {0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b};
        static const uint32_t expected_attach[] = {
            0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
            0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
            0x40, 0x41, 0x42, 0x43, 0x44
        };
        for (auto k : expected_top) if (values.find(k) != values.end()) score += 6;
        for (auto k : expected_attach) if (values.find(k) != values.end()) score += 4;
        for (const auto& kv : values) {
            if (kv.first <= 0x80) score += 1;
            if (kv.first > 0x10000) score -= 3;
        }
        return score;
    }

    bool Parse(const uint8_t* buf, size_t n, std::string& err) {
        TlvBoxLite le_try;
        std::string le_err;
        bool le_ok = le_try.ParseCore(buf, n, true, le_err);
        int le_score = le_ok ? le_try.ScoreForBinNetShape() : -1000000;

        TlvBoxLite be_try;
        std::string be_err;
        bool be_ok = be_try.ParseCore(buf, n, false, be_err);
        int be_score = be_ok ? be_try.ScoreForBinNetShape() : -1000000;

        if (!le_ok && !be_ok) {
            err = !le_err.empty() ? le_err : be_err;
            return false;
        }

        if (be_score > le_score) values = std::move(be_try.values);
        else values = std::move(le_try.values);
        little_endian_used = !(be_score > le_score);
        return true;
    }

    std::vector<uint32_t> GetAllKeys() const {
        std::vector<uint32_t> ks;
        ks.reserve(values.size());
        for (const auto& it : values) ks.push_back(it.first);
        std::sort(ks.begin(), ks.end());
        return ks;
    }

    bool GetIntValue(uint32_t key, int& out) const {
        auto it = values.find(key);
        if (it == values.end() || it->second.empty()) return false;
        const auto& v = it->second[0];
        if (v.size() < 4) return false;
        out = (int)(little_endian_used ? read_u32_le(v.data()) : read_u32_be(v.data()));
        return true;
    }

    bool GetInt64Value(uint32_t key, int64_t& out) const {
        auto it = values.find(key);
        if (it == values.end() || it->second.empty()) return false;
        const auto& v = it->second[0];
        if (v.size() >= 8) {
            if (little_endian_used) {
                out = (int64_t)(
                    ((uint64_t)v[0]) |
                    ((uint64_t)v[1] << 8) |
                    ((uint64_t)v[2] << 16) |
                    ((uint64_t)v[3] << 24) |
                    ((uint64_t)v[4] << 32) |
                    ((uint64_t)v[5] << 40) |
                    ((uint64_t)v[6] << 48) |
                    ((uint64_t)v[7] << 56)
                );
            } else {
                out = (int64_t)(
                    ((uint64_t)v[7]) |
                    ((uint64_t)v[6] << 8) |
                    ((uint64_t)v[5] << 16) |
                    ((uint64_t)v[4] << 24) |
                    ((uint64_t)v[3] << 32) |
                    ((uint64_t)v[2] << 40) |
                    ((uint64_t)v[1] << 48) |
                    ((uint64_t)v[0] << 56)
                );
            }
            return true;
        }
        if (v.size() >= 4) {
            out = (int64_t)(little_endian_used ? read_u32_le(v.data()) : read_u32_be(v.data()));
            return true;
        }
        return false;
    }

    bool GetBytesValue(uint32_t key, std::vector<uint8_t>& out) const {
        auto it = values.find(key);
        if (it == values.end() || it->second.empty()) return false;
        out = it->second[0];
        return true;
    }
};

static bool IsLikelyUtf8Printable(const std::vector<uint8_t>& v) {
    if (v.empty()) return true;
    size_t good = 0;
    size_t bad = 0;
    for (size_t i = 0; i < v.size(); ) {
        uint8_t b = v[i];
        if (b == 0) return false;
        if (b == 9 || b == 10 || b == 13) { good++; i++; continue; }
        if (b < 0x20) { bad++; i++; continue; }
        if (b <= 0x7E) { good++; i++; continue; }

        auto need = [&](int n) -> bool { return i + (size_t)n <= v.size(); };
        if (b >= 0xC2 && b <= 0xDF) {
            if (!need(2)) return false;
            uint8_t b1 = v[i+1];
            if ((b1 & 0xC0) != 0x80) { bad++; i++; continue; }
            good += 2; i += 2; continue;
        }
        if (b >= 0xE0 && b <= 0xEF) {
            if (!need(3)) return false;
            uint8_t b1 = v[i+1], b2 = v[i+2];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { bad++; i++; continue; }
            good += 3; i += 3; continue;
        }
        if (b >= 0xF0 && b <= 0xF4) {
            if (!need(4)) return false;
            uint8_t b1 = v[i+1], b2 = v[i+2], b3 = v[i+3];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) { bad++; i++; continue; }
            good += 4; i += 4; continue;
        }
        bad++; i++;
    }
    return bad == 0 && good * 20 >= v.size() * 19;
}

static bool TryDecodeTextSmart(const std::vector<uint8_t>& v, std::string& out) {
    if (IsLikelyUtf8Printable(v)) {
        out.assign((const char*)v.data(), v.size());
        return true;
    }
    out.clear();
    if (v.size() < 4 || (v.size() % 2) != 0) return false;
    size_t zero_hi = 0;
    size_t units = v.size() / 2;
    out.reserve(units);
    for (size_t i = 0; i + 1 < v.size(); i += 2) {
        uint8_t lo = v[i];
        uint8_t hi = v[i + 1];
        if (hi == 0) zero_hi++;
        if (hi == 0 && lo >= 0x20 && lo <= 0x7e) {
            out.push_back((char)lo);
        } else if (hi == 0 && (lo == 9 || lo == 10 || lo == 13)) {
            out.push_back((char)lo);
        }
    }
    return !out.empty() && zero_hi * 100 >= units * 60;
}

static void ParseAttachChatMsgLite(const uint8_t* buf, size_t n, AttachmentData& out) {
    std::string err;
    TlvBoxLite tlv;
    if (!tlv.Parse(buf, n, err)) return;

    for (uint32_t k : tlv.GetAllKeys()) {
        std::vector<uint8_t> v;
        if (!tlv.GetBytesValue(k, v)) continue;

        auto setStr = [&](std::string& field) {
            std::string sv;
            if (TryDecodeTextSmart(v, sv)) field = sv;
        };
        auto setInt = [&](int& field) {
            int iv = 0;
            if (tlv.GetIntValue(k, iv)) field = iv;
        };

        switch (k) {
            case 0x28: setStr(out.type); break;
            case 0x29: setInt(out.catId); break;
            case 0x2a: setInt(out.id); break;
            case 0x2b: setStr(out.extInfo); break;
            case 0x2d: setStr(out.action); break;
            case 0x2e: setStr(out.params); break;
            case 0x2f: setStr(out.title); break;
            case 0x30: setStr(out.href); break;
            case 0x31: setStr(out.thumb); break;
            case 0x32: setStr(out.description); break;
            case 0x33: setStr(out.size); break;
            case 0x34: setStr(out.ext); break;
            default: break;
        }
    }
}

static void ParseReferenceOrQuoteLite(const uint8_t* buf, size_t n, QuoteData& out) {
    std::string err;
    TlvBoxLite tlv;
    if (!tlv.Parse(buf, n, err)) return;

    int64_t i64 = 0;
    int i32 = 0;
    if (tlv.GetInt64Value(0x50, i64)) out.ownerId = i64;
    else if (tlv.GetIntValue(0x50, i32)) out.ownerId = static_cast<uint32_t>(i32);
    if (tlv.GetIntValue(0x53, i32)) out.cliMsgType = i32;
    if (tlv.GetInt64Value(0x51, i64)) out.cliMsgId = i64;
    if (tlv.GetInt64Value(0x52, i64)) out.globalMsgId = i64;
    if (tlv.GetInt64Value(0x54, i64)) out.ts = i64;

    std::vector<uint8_t> v;
    std::string sv;
    if (tlv.GetBytesValue(0x56, v) && TryDecodeTextSmart(v, sv)) out.msg = sv;
    if (tlv.GetBytesValue(0x57, v) && TryDecodeTextSmart(v, sv)) out.attach = sv;
    if (tlv.GetBytesValue(0x5a, v) && TryDecodeTextSmart(v, sv)) out.fromD = sv;
}

static void ParseMentionLite(const uint8_t* buf, size_t n, MentionData& out) {
    std::string err;
    TlvBoxLite tlv;
    if (!tlv.Parse(buf, n, err)) return;
    int64_t i64 = 0;
    int i = 0;
    if (tlv.GetInt64Value(0x65, i64)) out.uid = i64;
    else if (tlv.GetIntValue(0x65, i)) out.uid = i;
    if (tlv.GetIntValue(0x66, i)) out.pos = i;
    if (tlv.GetIntValue(0x67, i)) out.len = i;
    if (tlv.GetIntValue(0x68, i)) out.type = i;
}

static bool aes256cbc_decrypt_nopad(
    const std::vector<uint8_t>& ct,
    const std::vector<uint8_t>& key32,
    const uint8_t iv[16],
    std::vector<uint8_t>& pt_out
) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key32.data(), iv)) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    std::vector<uint8_t> pt(ct.size() + AES_BLOCK_SIZE);
    int len1 = 0, len2 = 0;
    if (1 != EVP_DecryptUpdate(ctx, pt.data(), &len1, ct.data(), (int)ct.size())) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    if (1 != EVP_DecryptFinal_ex(ctx, pt.data() + len1, &len2)) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    EVP_CIPHER_CTX_free(ctx);
    pt.resize((size_t)len1 + (size_t)len2);
    pt_out = std::move(pt);
    return true;
}

}

std::vector<uint8_t> DbCross::deriveAes256Key(const std::string& privateKeyStr) {
    std::vector<uint8_t> key(32, 0);
    for (size_t i = 0; i < 32 && i < privateKeyStr.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(privateKeyStr[i]);
        if (c >= 'a' && c <= 'z') {
            c = static_cast<unsigned char>(c - 'a' + 'A');
        }
        key[i] = c;
    }
    return key;
}

std::vector<uint8_t> DbCross::decryptAes256Cbc(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv
) {
    if (key.size() != 32 || iv.size() != 16) {
        throw std::invalid_argument("Invalid key (must be 32 bytes) or IV size (must be 16 bytes)");
    }
    if (ciphertext.empty()) return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to allocate EVP_CIPHER_CTX");

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize AES-256-CBC decryption");
    }

    std::vector<uint8_t> plaintext(ciphertext.size() + AES_BLOCK_SIZE);
    int outLen1 = 0;
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &outLen1, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptUpdate failed");
    }

    int outLen2 = 0;
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptFinal_ex failed (bad padding or corrupted key)");
    }

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(outLen1 + outLen2);
    return plaintext;
}

bool DbCross::decryptAes256CbcChunkedResetIv(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& key32,
    std::vector<uint8_t>& plaintextOut
) {
    const size_t chunk = 0x10000;
    if ((ciphertext.size() % 16) != 0 || key32.size() != 32) return false;

    plaintextOut.clear();
    plaintextOut.reserve(ciphertext.size());
    uint8_t iv0[16] = {0};

    for (size_t off = 0; off < ciphertext.size(); off += chunk) {
        size_t len = std::min(chunk, ciphertext.size() - off);
        std::vector<uint8_t> part_ct(ciphertext.begin() + off, ciphertext.begin() + off + len);
        std::vector<uint8_t> part_pt;
        if (!aes256cbc_decrypt_nopad(part_ct, key32, iv0, part_pt)) return false;
        plaintextOut.insert(plaintextOut.end(), part_pt.begin(), part_pt.end());
    }
    return true;
}

std::vector<uint8_t> DbCross::decompressLzma(const std::vector<uint8_t>& compressedData) {
    if (compressedData.empty()) return {};

    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_auto_decoder(&strm, UINT64_MAX, 0);
    if (ret != LZMA_OK) throw std::runtime_error("Failed to init LZMA auto decoder");

    std::vector<uint8_t> decompressed;
    const size_t chunkSize = 64 * 1024;
    std::vector<uint8_t> outBuf(chunkSize);

    strm.next_in = compressedData.data();
    strm.avail_in = compressedData.size();

    do {
        strm.next_out = outBuf.data();
        strm.avail_out = outBuf.size();

        ret = lzma_code(&strm, LZMA_FINISH);
        if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
            lzma_end(&strm);
            throw std::runtime_error("LZMA decode failed");
        }

        size_t written = outBuf.size() - strm.avail_out;
        decompressed.insert(decompressed.end(), outBuf.data(), outBuf.data() + written);
    } while (ret != LZMA_STREAM_END);

    lzma_end(&strm);
    return decompressed;
}

bool DbCross::decompressAndDecryptZdb4(
    const std::string& inputPath,
    const std::string& outputDir,
    const std::string& privateKeyStr,
    std::string* error
) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    std::ifstream f(inputPath, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return fail("cannot open backup file");
    const size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> full(size);
    if (!f.read(reinterpret_cast<char*>(full.data()), static_cast<std::streamsize>(size))) return fail("cannot read backup file");
    if (size < 32) return fail("backup file too small");

    struct Span { size_t off; size_t len; };
    std::vector<Span> spans;
    if (size % 16 == 0) spans.push_back({0, size});
    if (size > 4 && (size - 4) % 16 == 0) {
        spans.push_back({4, size - 4});
        spans.push_back({0, size - 4});
    }
    if (spans.empty()) return fail("ciphertext is not AES block aligned");

    const auto key = deriveAes256Key(privateKeyStr);
    std::vector<uint8_t> pt;
    bool magic = false;
    for (const Span& sp : spans) {
        std::vector<uint8_t> ct(full.begin() + static_cast<ptrdiff_t>(sp.off),
                                full.begin() + static_cast<ptrdiff_t>(sp.off + sp.len));
        std::vector<uint8_t> tmp;
        if (!decryptAes256CbcChunkedResetIv(ct, key, tmp)) {
            const uint8_t iv0[16] = {0};
            aes256cbc_decrypt_nopad(ct, key, iv0, tmp);
        }
        if (tmp.size() >= 18 && std::memcmp(tmp.data(), "ZDB4.0", 6) == 0) {
            pt = std::move(tmp);
            magic = true;
            break;
        }
    }
    if (!magic) return fail("wrong key or not a ZDB4.0 backup");

    size_t off = 14;
    const uint32_t fileCount = read_u32_be(pt.data() + off);
    off += 4;
    std::vector<FileEntry> files;
    for (uint32_t i = 0; i < fileCount; i++) {
        if (off + 4 > pt.size()) return fail("truncated file table");
        const uint32_t nlen = read_u32_be(pt.data() + off);
        off += 4;
        if (off + nlen + 4 > pt.size()) return fail("truncated file table");
        std::string name(reinterpret_cast<char*>(pt.data() + off), nlen);
        off += nlen;
        const uint32_t fsize = read_u32_be(pt.data() + off);
        off += 4;
        if (name.empty() || name.find("..") != std::string::npos || name.front() == '/') return fail("unsafe file name");
        files.push_back({name, fsize});
    }

    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_auto_decoder(&strm, UINT64_MAX, 0) != LZMA_OK) return fail("cannot init LZMA decoder");
    strm.next_in = pt.data() + off;
    strm.avail_in = pt.size() - off;

    std::error_code ec;
    fs::create_directories(outputDir, ec);
    std::vector<uint8_t> buf(65536);
    size_t idx = 0;
    size_t written = 0;
    std::ofstream out;
    auto openNext = [&]() -> bool {
        const fs::path p = fs::path(outputDir) / files[idx].name;
        fs::create_directories(p.parent_path(), ec);
        out.open(p, std::ios::binary | std::ios::trunc);
        return out.is_open();
    };
    if (!files.empty() && !openNext()) { lzma_end(&strm); return fail("cannot write extracted file"); }
    auto skipEmpty = [&]() -> bool {
        while (idx < files.size() && files[idx].size == 0) {
            out.close();
            idx++;
            if (idx < files.size() && !openNext()) return false;
        }
        return true;
    };
    if (!skipEmpty()) { lzma_end(&strm); return fail("cannot write extracted file"); }

    lzma_ret ret = LZMA_OK;
    do {
        strm.next_out = buf.data();
        strm.avail_out = buf.size();
        ret = lzma_code(&strm, strm.avail_in == 0 ? LZMA_FINISH : LZMA_RUN);
        const size_t produced = buf.size() - strm.avail_out;
        size_t pos = 0;
        while (pos < produced && idx < files.size()) {
            const size_t take = std::min<size_t>(files[idx].size - written, produced - pos);
            out.write(reinterpret_cast<const char*>(buf.data() + pos), static_cast<std::streamsize>(take));
            written += take;
            pos += take;
            if (written == files[idx].size) {
                out.close();
                idx++;
                written = 0;
                if (idx < files.size() && !openNext()) { lzma_end(&strm); return fail("cannot write extracted file"); }
                if (!skipEmpty()) { lzma_end(&strm); return fail("cannot write extracted file"); }
            }
        }
    } while (ret == LZMA_OK);
    lzma_end(&strm);
    if (out.is_open()) out.close();
    if (ret != LZMA_STREAM_END) return fail("LZMA stream is damaged");
    if (idx != files.size()) return fail("backup ended before all files were extracted");

    bool anyDb = false;
    for (const FileEntry& fe : files) {
        if (fe.name.size() < 3 || fe.name.compare(fe.name.size() - 3, 3, ".db") != 0) continue;
        std::ifstream db(fs::path(outputDir) / fe.name, std::ios::binary);
        char hdr[16] = {0};
        db.read(hdr, 16);
        if (std::memcmp(hdr, "SQLite format 3", 16) == 0) { anyDb = true; break; }
    }
    if (!anyDb) return fail("no SQLite database inside the backup");
    return true;
}

BinNetMessage DbCross::parseBinNet(const uint8_t* buf, size_t len) {
    BinNetMessage result;
    if (!buf || len == 0) return result;

    TlvBoxLite tlv;
    std::string err;
    if (!tlv.Parse(buf, len, err)) return result;

    for (uint32_t k : tlv.GetAllKeys()) {
        auto it = tlv.values.find(k);
        if (it == tlv.values.end() || it->second.empty()) continue;

        switch (k) {
            case 0x06: {
                for (const auto& v : it->second) {
                    if (v.size() < 8) continue;
                    AttachmentData att;
                    ParseAttachChatMsgLite(v.data(), v.size(), att);
                    result.attachs.push_back(att);
                }
                break;
            }
            case 0x07: {
                const auto& v = it->second[0];
                ParseReferenceOrQuoteLite(v.data(), v.size(), result.quote);
                break;
            }
            case 0x08: {
                for (const auto& v : it->second) {
                    MentionData m;
                    ParseMentionLite(v.data(), v.size(), m);
                    result.mentions.push_back(m);
                }
                break;
            }
            case 0x09: {
                int qst = 0;
                if (tlv.GetIntValue(0x09, qst)) result.quoteStatus = qst;
                break;
            }
            case 0x0a: {
                const auto& v = it->second[0];
                std::string s;
                if (TryDecodeTextSmart(v, s)) result.dataSource = s;
                break;
            }
            case 0x0b: {
                const auto& v = it->second[0];
                std::string s;
                if (TryDecodeTextSmart(v, s)) result.text = s;
                break;
            }
            default: break;
        }
    }
    return result;
}

bool DbCross::verifyCryptoSelfTest() {
    try {
        std::string testKeyStr = "0123456789ABCDEF0123456789ABCDEF";
        auto key = deriveAes256Key(testKeyStr);
        std::vector<uint8_t> iv = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        std::string secret = "Zalos Qt6 E2EE Self-Test Passed!";
        std::vector<uint8_t> plaintext(secret.begin(), secret.end());

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
        std::vector<uint8_t> ciphertext(plaintext.size() + AES_BLOCK_SIZE);
        int len1 = 0, len2 = 0;
        EVP_EncryptUpdate(ctx, ciphertext.data(), &len1, plaintext.data(), static_cast<int>(plaintext.size()));
        EVP_EncryptFinal_ex(ctx, ciphertext.data() + len1, &len2);
        ciphertext.resize(len1 + len2);
        EVP_CIPHER_CTX_free(ctx);

        auto decrypted = decryptAes256Cbc(ciphertext, key, iv);
        std::string result(decrypted.begin(), decrypted.end());
        return result == secret;
    } catch (...) {
        return false;
    }
}

}
