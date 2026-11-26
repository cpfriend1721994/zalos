// SPDX-License-Identifier: BSD-3-Clause
#include "diag.h"
#include "playout.h"
#include "serial_worker.h"

#include <QElapsedTimer>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace zcall::audio;

namespace {

class Latch {
public:
    void set() {
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_set = true;
        }
        m_cv.notify_all();
    }
    bool waitFor(int ms) {
        std::unique_lock<std::mutex> lock(m_lock);
        return m_cv.wait_for(lock, std::chrono::milliseconds(ms), [this] { return m_set; });
    }

private:
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_set{false};
};

class BlockingSink : public Sink {
public:
    bool write(const int16_t*, int, std::string*) override {
        entered.set();
        release.waitFor(5000);
        return true;
    }
    void close() override {}

    Latch entered;
    Latch release;
};

class NullSink : public Sink {
public:
    bool write(const int16_t*, int, std::string*) override { return true; }
    void close() override {}
};

}

class TestCallAudioWorker : public QObject {
    Q_OBJECT

private slots:
    void runsTasksInPostOrder() {
        std::vector<int> ran;
        Latch done;
        {
            SerialWorker w;
            w.post([&ran] { ran.push_back(1); });
            w.post([&ran] { ran.push_back(2); });
            w.post([&ran, &done] {
                ran.push_back(3);
                done.set();
            });
            QVERIFY(done.waitFor(5000));
        }
        const std::vector<int> want{1, 2, 3};
        QVERIFY(ran == want);
    }

    void coalescedVolumeRunsOnlyLatestBehindBlockedTask() {
        Latch started;
        Latch release;
        Latch done;
        std::vector<int> ran;
        {
            SerialWorker w;
            w.post([&started, &release] {
                started.set();
                release.waitFor(5000);
            });
            QVERIFY(started.waitFor(5000));
            constexpr int kVolumeKey = 7;
            w.post([&ran] { ran.push_back(10); }, kVolumeKey);
            w.post([&ran] { ran.push_back(20); }, kVolumeKey);
            w.post([&ran] { ran.push_back(30); }, kVolumeKey);
            w.post([&done] { done.set(); });
            release.set();
            QVERIFY(done.waitFor(5000));
        }
        const std::vector<int> want{30};
        QVERIFY(ran == want);
    }

    void stopWaitsForRunningTaskAndDropsQueue() {
        Latch started;
        Latch release;
        Latch stopped;
        std::atomic<bool> blockedFinished{false};
        std::atomic<int> queuedRuns{0};
        SerialWorker w;
        w.post([&started, &release, &blockedFinished] {
            started.set();
            release.waitFor(5000);
            blockedFinished.store(true);
        });
        QVERIFY(started.waitFor(5000));
        w.post([&queuedRuns] { ++queuedRuns; });
        w.post([&queuedRuns] { ++queuedRuns; });
        std::thread stopper([&w, &stopped] {
            w.stop();
            stopped.set();
        });
        const bool returnedEarly = stopped.waitFor(100);
        release.set();
        const bool returned = stopped.waitFor(5000);
        stopper.join();
        QVERIFY(!returnedEarly);
        QVERIFY(returned);
        QVERIFY(blockedFinished.load());
        QCOMPARE(queuedRuns.load(), 0);
    }

    void flushReportsStuckSinkThenDrainsAfterRelease() {
        BlockingSink sink;
        Playout p(&sink, nullptr, 10, 40, 0);
        p.start();
        QVERIFY(sink.entered.waitFor(5000));
        QVERIFY(!p.flush(0.05));
        sink.release.set();
        QVERIFY(p.flush(1.0));
    }

    void diagStopWakesWaitingThreadPromptly() {
        NullSink sink;
        Playout playout(&sink, nullptr, 10, 40, 0);
        auto diag = std::make_unique<Diag>(playout, nullptr, std::string("zalos-test"));
        QElapsedTimer timer;
        timer.start();
        diag->requestStop();
        diag.reset();
        QVERIFY(timer.elapsed() < 100);
    }
};

QTEST_GUILESS_MAIN(TestCallAudioWorker)
#include "test_call_audio_worker.moc"
