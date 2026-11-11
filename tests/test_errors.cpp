#include <QtTest>

#include "network/error_catalog.h"

using namespace Zalos::Core::Network;

class TestErrors : public QObject {
    Q_OBJECT

private slots:
    void knownApiCodesKeepTheirWording() {
        QCOMPARE(ErrorCatalog::render(126, "x", false), QString::fromUtf8("Tài khoản đã ngừng hoạt động."));
        QCOMPARE(ErrorCatalog::render(164, "x", false), QString::fromUtf8("Bạn không còn là thành viên trong nhóm này"));
        QVERIFY(ErrorCatalog::render(301, "x", false).contains(QString::fromUtf8("24 giờ")));
        QVERIFY(ErrorCatalog::apiError(126).en.contains("no longer active"));
    }

    void unknownCodeNeverLeaksRawTextAndKeepsTheCode() {
        const QString text = ErrorCatalog::render(9999, "Some raw English transport text", false);
        QVERIFY(!text.contains("raw English"));
        QVERIFY(text.contains("9999"));
        QVERIFY(ErrorCatalog::describe(9999, "raw").traceRaw);

        const QString en = ErrorCatalog::render(9999, "raw", true);
        QVERIFY(en.contains("9999"));
        QVERIFY(en.contains("code"));
    }

    void zeroCodeWithoutTextIsUnknownError() {
        QCOMPARE(ErrorCatalog::render(0, QString(), false), QString::fromUtf8("lỗi không xác định"));
        QCOMPARE(ErrorCatalog::render(0, QString(), true), QString("unknown error"));
    }

    void transportTextIsTranslated() {
        QVERIFY(ErrorCatalog::render(0, "Host not found", false).contains(QString::fromUtf8("máy chủ Zalo")));
        QVERIFY(ErrorCatalog::render(0, "Connection refused", true).contains("refused"));
        QVERIFY(ErrorCatalog::render(0, "Connection timed out", false).contains(QString::fromUtf8("quá lâu")));
        QVERIFY(ErrorCatalog::render(0, "Operation canceled", false).contains(QString::fromUtf8("huỷ")));
        QVERIFY(ErrorCatalog::render(0, "Network is unreachable", false).contains(QString::fromUtf8("mạng")));
        QVERIFY(!ErrorCatalog::render(0, "Network is unreachable", false).contains("unreachable"));
    }

    void httpStatusesShowTheCode() {
        const QString notFound = ErrorCatalog::render(404, "HTTP 404", false);
        QVERIFY(notFound.contains(QString::fromUtf8("Không tìm thấy")));
        QVERIFY(notFound.contains("404"));
        QVERIFY(ErrorCatalog::render(503, "HTTP 503", false).contains(QString::fromUtf8("bận")));
        QVERIFY(ErrorCatalog::render(400, "HTTP 400", true).contains("rejected"));
        QVERIFY(ErrorCatalog::render(400, "HTTP 400", true).contains("400"));
    }

    void localFileTokenIsFriendly() {
        QVERIFY(ErrorCatalog::render(0, "local_file_error", false).contains(QString::fromUtf8("tệp")));
        QVERIFY(!ErrorCatalog::render(0, "local_file_error", false).contains("local_file_error"));
    }

    void phoneSyncCodesMatchZaloPc() {
        QCOMPARE(ErrorCatalog::phoneSyncName(7), QString("BUSY_RESTORING"));
        QCOMPARE(ErrorCatalog::phoneSyncName(16), QString("LOW_STORAGE"));
        QVERIFY(ErrorCatalog::pick(ErrorCatalog::phoneSyncError(7), false).contains(QString::fromUtf8("sao lưu")));
        QVERIFY(ErrorCatalog::pick(ErrorCatalog::phoneSyncError(16), false).contains(QString::fromUtf8("Dung lượng")));
        QVERIFY(ErrorCatalog::pick(ErrorCatalog::phoneSyncError(16), true).contains("free space"));
    }

    void unmappedPhoneCodeFallsBackToDefault() {
        QCOMPARE(ErrorCatalog::phoneSyncName(400), QString("UNMAPPED"));
        const QString fallback = ErrorCatalog::pick(ErrorCatalog::phoneSyncError(400), false);
        QVERIFY(fallback.contains(QString::fromUtf8("Đã xảy ra lỗi")));
        QVERIFY(!fallback.contains("400"));
        QVERIFY(ErrorCatalog::phoneSyncError(12).en.contains("error"));
    }

    void everyPhoneCodeHasWording() {
        for (int code = 1; code <= 19; ++code) {
            const ErrorCatalog::Message m = ErrorCatalog::phoneSyncError(code);
            QVERIFY(!m.vi.isEmpty());
            QVERIFY(!m.en.isEmpty());
        }
    }
};

QTEST_APPLESS_MAIN(TestErrors)
#include "test_errors.moc"
