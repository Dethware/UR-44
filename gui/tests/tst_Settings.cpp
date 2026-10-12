#include <QtTest>

#include "FileSystem.h"
#include "Settings.h"

class SettingsTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_counter = 0;

    QString newPath()
    {
        return m_dir.filePath(QString("config%1.json").arg(m_counter++));
    }

    static QJsonObject readFile(const QString& path)
    {
        return QJsonDocument::fromJson(FS::read(path)).object();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void load_readsValues()
    {
        QString path = newPath();
        FS::write(path, R"({"midi_port": "hw:1,0,1"})");

        Settings settings;
        settings.setPath(path);
        QVERIFY(settings.load());
        QCOMPARE(settings.midiPort, QString("hw:1,0,1"));
    }

    void load_failsWithoutTouchingDefaults()
    {
        Settings settings;
        settings.setPath(m_dir.filePath("missing.json"));
        QVERIFY(!settings.load());
        QCOMPARE(settings.midiPort, QString());

        QString path = newPath();
        FS::write(path, "not json");
        settings.setPath(path);
        QVERIFY(!settings.load());
    }

    void save_roundTrip()
    {
        QString path = newPath();
        {
            Settings settings;
            settings.setPath(path);
            settings.midiPort = "hw:2,0,1";
            QVERIFY(settings.save());
        }
        Settings settings;
        settings.setPath(path);
        QVERIFY(settings.load());
        QCOMPARE(settings.midiPort, QString("hw:2,0,1"));
    }

    void save_keepsUnknownKeys()
    {
        QString path = newPath();
        FS::write(path, R"({"midi_port": "x", "windowGeometry": [1, 2, 3, 4]})");

        Settings settings;
        settings.setPath(path);
        QVERIFY(settings.load());
        settings.midiPort = "y";
        QVERIFY(settings.save());

        QJsonObject written = readFile(path);
        QCOMPARE(written["windowGeometry"].toArray(), (QJsonArray{1, 2, 3, 4}));
        QCOMPARE(written["midi_port"].toString(), QString("y"));
    }

    void scheduleSave_isDelayed()
    {
        QString path = newPath();
        Settings settings;
        settings.setPath(path);
        settings.midiPort = "delayed";
        settings.scheduleSave();
        QVERIFY(!QFile::exists(path));

        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), Settings::SAVE_DELAY_MS * 3);
        QCOMPARE(readFile(path)["midi_port"].toString(), QString("delayed"));
    }

    void scheduleSave_isNotPostponedByConstantChanges()
    {
        QString path = newPath();
        Settings settings;
        settings.setPath(path);

        // Change the value constantly for longer than the delay - it must get written meanwhile anyway
        QElapsedTimer timer;
        timer.start();
        int changes = 0;
        while (!QFile::exists(path) && timer.elapsed() < Settings::SAVE_DELAY_MS * 3)
        {
            settings.midiPort = QString::number(++changes);
            settings.scheduleSave();
            QTest::qWait(5);
        }
        QVERIFY(QFile::exists(path));
        QVERIFY(changes > 1);
    }

    void scheduleSave_writesOncePerInterval()
    {
        QString path = newPath();
        Settings settings;
        settings.setPath(path);

        // Many changes in quick succession result in one write, containing the last value
        for (int i = 0; i < 1000; ++i)
        {
            settings.midiPort = QString::number(i);
            settings.scheduleSave();
        }
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), Settings::SAVE_DELAY_MS * 3);
        QCOMPARE(readFile(path)["midi_port"].toString(), QString("999"));

        // Nothing is pending anymore, so nothing else gets written
        QFile::remove(path);
        QTest::qWait(Settings::SAVE_DELAY_MS * 2);
        QVERIFY(!QFile::exists(path));
    }

    void flush_writesPendingChanges()
    {
        QString path = newPath();
        Settings settings;
        settings.setPath(path);

        settings.flush();
        QVERIFY(!QFile::exists(path));

        settings.midiPort = "flushed";
        settings.scheduleSave();
        settings.flush();
        QCOMPARE(readFile(path)["midi_port"].toString(), QString("flushed"));
    }

    void destructor_writesPendingChanges()
    {
        QString path = newPath();
        {
            Settings settings;
            settings.setPath(path);
            settings.midiPort = "on exit";
            settings.scheduleSave();
            QVERIFY(!QFile::exists(path));
        }
        QCOMPARE(readFile(path)["midi_port"].toString(), QString("on exit"));
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_Settings.moc"
