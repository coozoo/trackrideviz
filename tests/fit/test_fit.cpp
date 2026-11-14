#include <QtTest>

#include <QDir>

#include "telemetry/channelids.h"
#include "telemetry/fit/fitdecoder.h"
#include "telemetry/fit/fitimporter.h"

// Writes FIT byte streams for the tests. Deliberately independent of the
// decoder: it shares no code with it.
class FitBuilder
{
public:
    struct Field
    {
        quint8 num;
        quint8 size;
        quint8 type; // full base type byte, e.g. 0x86 for uint32
    };

    void define(int local, quint16 global, const QVector<Field> &fields, bool bigEndian = false,
                const QVector<Field> &developerFields = {})
    {
        m_body.append(char(0x40 | (developerFields.isEmpty() ? 0 : 0x20) | local));
        m_body.append(char(0));
        m_body.append(char(bigEndian ? 1 : 0));
        number(global, 2, bigEndian);
        m_body.append(char(fields.size()));
        for (const Field &f : fields)
            {
                m_body.append(char(f.num)).append(char(f.size)).append(char(f.type));
            }
        if (!developerFields.isEmpty())
            {
                m_body.append(char(developerFields.size()));
                for (const Field &f : developerFields)
                    {
                        // for developer fields `type` carries the developer data index
                        m_body.append(char(f.num)).append(char(f.size)).append(char(f.type));
                    }
            }
    }

    void data(int local)
    {
        m_body.append(char(local));
    }
    void compressedData(int local, int timeOffset)
    {
        m_body.append(char(0x80 | (local << 5) | timeOffset));
    }
    void number(quint64 value, int size, bool bigEndian = false)
    {
        for (int i = 0; i < size; ++i)
            {
                const int shift = 8 * (bigEndian ? size - 1 - i : i);
                m_body.append(char((value >> shift) & 0xFF));
            }
    }
    void text(const QByteArray &value, int size)
    {
        m_body.append(value.leftJustified(size, '\0', true));
    }

    QByteArray file(int headerSize = 14) const
    {
        QByteArray out;
        out.append(char(headerSize));
        out.append(char(0x20));       // protocol 2.0
        out.append("\x54\x08", 2);    // profile version
        const quint32 size = quint32(m_body.size());
        for (int i = 0; i < 4; ++i)
            {
                out.append(char((size >> (8 * i)) & 0xFF));
            }
        out.append(".FIT");
        if (headerSize == 14)
            {
                const quint16 crc = FitDecoder::crc16(out.constData(), 12);
                out.append(char(crc & 0xFF)).append(char(crc >> 8));
            }
        out.append(m_body);
        const quint16 crc = FitDecoder::crc16(out.constData(), out.size());
        out.append(char(crc & 0xFF)).append(char(crc >> 8));
        return out;
    }

private:
    QByteArray m_body;
};

namespace
{
const quint8 kUInt8 = 0x02;
const quint8 kSInt8 = 0x01;
const quint8 kUInt16 = 0x84;
const quint8 kSInt32 = 0x85;
const quint8 kUInt32 = 0x86;
const quint8 kString = 0x07;

const quint32 kT0 = 1000000000; // 2021-09-08T01:46:40Z in FIT time

qint32 semicircles(double degrees)
{
    return qint32(qRound64(degrees * 2147483648.0 / 180.0));
}

QVector<FitMessage> decodeAll(const QByteArray &data, FitDecoder::Result *result = nullptr)
{
    QVector<FitMessage> messages;
    FitDecoder decoder;
    const FitDecoder::Result r = decoder.decode(data, [&](const FitMessage &m) { messages.append(m); });
    if (result)
        {
            *result = r;
        }
    return messages;
}

// record: timestamp, lat, lon, altitude, heart rate, speed
const QVector<FitBuilder::Field> kRecordFields =
{
    {253, 4, kUInt32}, {0, 4, kSInt32}, {1, 4, kSInt32}, {2, 2, kUInt16}, {3, 1, kUInt8}, {6, 2, kUInt16}
};

void addRecord(FitBuilder &b, int local, quint32 timestamp, double lat, double lon,
               double altitude, int heartRate, double speed, bool bigEndian = false)
{
    b.data(local);
    b.number(timestamp, 4, bigEndian);
    b.number(quint32(semicircles(lat)), 4, bigEndian);
    b.number(quint32(semicircles(lon)), 4, bigEndian);
    b.number(quint64((altitude + 500.0) * 5.0), 2, bigEndian);
    b.number(quint64(heartRate), 1, bigEndian);
    b.number(quint64(speed * 1000.0), 2, bigEndian);
}
}

class TestFit : public QObject
{
    Q_OBJECT

private slots:
    void crcKnownValue();
    void basicRecords();
    void twelveByteHeader();
    void bigEndianDefinition();
    void invalidValuesAreSkipped();
    void compressedTimestamps();
    void redefinedLocalType();
    void developerFields();
    void undersizedField();
    void unknownMessagesAndFieldsAreKept();
    void enhancedFieldsWin();
    void pedalDynamics();
    void lapsAndSource();
    void pausesAndUtcOffset();
    void chainedFiles();
    void truncatedFileKeepsRecords();
    void unfinalizedHeader();
    void badCrcIsOnlyAWarning();
    void irregularAndRepeatedTimestamps();
    void timestampOutliersAreDropped();
    void clockSetDuringRecording();
    void relativeTimeHasNoUtc();
    void notAFitFile();
    void fileWithoutRecords();
    void sampleFiles();
};

void TestFit::crcKnownValue()
{
    // CRC-16/ARC check value
    QCOMPARE(FitDecoder::crc16("123456789", 9), quint16(0xBB3D));
}

void TestFit::basicRecords()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0);
    addRecord(b, 0, kT0 + 10, 49.001, 28.002, 110.0, 140, 12.5);

    QString error;
    const TelemetrySessionPtr session = FitImporter::read(b.file(), &error);
    QVERIFY2(session, qPrintable(error));
    QVERIFY(session->warnings().isEmpty());
    QCOMPARE(session->startUtc(), QDateTime::fromSecsSinceEpoch(qint64(kT0) + 631065600).toUTC());
    QCOMPARE(session->duration(), 10.0);

    QVERIFY(qAbs(session->channel(ChannelId::PositionLat)->valueAt(0.0) - 49.0) < 1e-6);
    QVERIFY(qAbs(session->channel(ChannelId::PositionLon)->valueAt(10.0) - 28.002) < 1e-6);
    QCOMPARE(session->channel(ChannelId::Elevation)->valueAt(5.0), 105.0);
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(5.0), 130.0);
    QCOMPARE(session->channel(ChannelId::Speed)->valueAt(10.0), 12.5);
    QCOMPARE(session->channel(ChannelId::Speed)->unit(), QString("m/s"));
}

void TestFit::twelveByteHeader()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0);

    const TelemetrySessionPtr session = FitImporter::read(b.file(12));
    QVERIFY(session);
    QVERIFY(session->warnings().isEmpty());
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(0.0), 120.0);
}

void TestFit::bigEndianDefinition()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields, true);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0, true);
    addRecord(b, 0, kT0 + 1, 49.5, 28.5, 200.0, 150, 20.0, true);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->duration(), 1.0);
    QVERIFY(qAbs(session->channel(ChannelId::PositionLat)->valueAt(1.0) - 49.5) < 1e-6);
    QCOMPARE(session->channel(ChannelId::Elevation)->valueAt(1.0), 200.0);
    QCOMPARE(session->channel(ChannelId::Speed)->valueAt(1.0), 20.0);
}

void TestFit::invalidValuesAreSkipped()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0);
    // second record: no fix, no heart rate, no altitude, no speed
    b.data(0);
    b.number(kT0 + 1, 4);
    b.number(0x7FFFFFFF, 4);
    b.number(0x7FFFFFFF, 4);
    b.number(0xFFFF, 2);
    b.number(0xFF, 1);
    b.number(0xFFFF, 2);
    // third record: 0/0 position, which some devices write before the first fix
    addRecord(b, 0, kT0 + 2, 0.0, 0.0, 120.0, 125, 11.0);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::PositionLat)->count(), 1);
    QCOMPARE(session->channel(ChannelId::HeartRate)->count(), 2);
    QCOMPARE(session->channel(ChannelId::Elevation)->count(), 2);
    QCOMPARE(session->channel(ChannelId::Speed)->count(), 2);
    // interpolated across the missing sample, not dragged to zero
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(1.0), 122.5);
}

void TestFit::compressedTimestamps()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    // local type 1: heart rate only, time comes from the compressed header
    b.define(1, Fit::Record, {{3, 1, kUInt8}});

    const quint32 base = (kT0 & ~quint32(0x1F)) + 29; // 5-bit counter is at 29
    addRecord(b, 0, base, 49.0, 28.0, 100.0, 100, 10.0);
    b.compressedData(1, 30); // +1 s
    b.number(101, 1);
    b.compressedData(1, 31); // +2 s
    b.number(102, 1);
    b.compressedData(1, 1);  // counter rolled over: +4 s
    b.number(104, 1);

    FitDecoder::Result result;
    const QVector<FitMessage> messages = decodeAll(b.file(), &result);
    QVERIFY(result.ok);
    QCOMPARE(messages.size(), 4);
    QCOMPARE(messages.at(1).value(Fit::TimestampField), double(base + 1));
    QCOMPARE(messages.at(3).value(Fit::TimestampField), double(base + 4));

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::HeartRate)->count(), 4);
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(4.0), 104.0);
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(3.0), 103.0);
}

void TestFit::redefinedLocalType()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0);
    // same local type, new layout: timestamp + power
    b.define(0, Fit::Record, {{253, 4, kUInt32}, {7, 2, kUInt16}});
    b.data(0);
    b.number(kT0 + 1, 4);
    b.number(250, 2);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::Power)->valueAt(1.0), 250.0);
    QCOMPARE(session->channel(ChannelId::HeartRate)->count(), 1);
}

void TestFit::developerFields()
{
    FitBuilder b;
    // field_description: developer 0, field 5, uint16, "Core Temp", scale 100, units "C"
    b.define(1, Fit::FieldDescription,
             {{0, 1, kUInt8}, {1, 1, kUInt8}, {2, 1, kUInt8}, {3, 16, kString}, {6, 1, kUInt8}, {8, 8, kString}});
    b.data(1);
    b.number(0, 1);
    b.number(5, 1);
    b.number(kUInt16, 1);
    b.text("Core Temp", 16);
    b.number(100, 1);
    b.text("C", 8);

    b.define(0, Fit::Record, {{253, 4, kUInt32}, {3, 1, kUInt8}}, false, {{5, 2, 0}});
    b.data(0);
    b.number(kT0, 4);
    b.number(120, 1);
    b.number(3712, 2);
    b.data(0);
    b.number(kT0 + 1, 4);
    b.number(121, 1);
    b.number(3720, 2);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    const Channel *core = session->channel("dev.Core Temp");
    QVERIFY(core);
    QCOMPARE(core->unit(), QString("C"));
    QCOMPARE(core->valueAt(0.0), 37.12);
    QCOMPARE(core->valueAt(1.0), 37.2);
    // the native field after... before the developer block is still aligned
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(1.0), 121.0);
}

void TestFit::undersizedField()
{
    // heart rate declared as uint32 but stored in one byte
    FitBuilder b;
    b.define(0, Fit::Record, {{253, 4, kUInt32}, {3, 1, kUInt32}, {7, 2, kUInt16}});
    b.data(0);
    b.number(kT0, 4);
    b.number(133, 1);
    b.number(210, 2);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(0.0), 133.0);
    QCOMPARE(session->channel(ChannelId::Power)->valueAt(0.0), 210.0);
}

void TestFit::unknownMessagesAndFieldsAreKept()
{
    FitBuilder b;
    // a vendor-private message in the middle of the file
    b.define(2, 0xFF42, {{0, 3, 0x0D}, {1, 2, kUInt16}});
    b.data(2);
    b.number(0xABCDEF, 3);
    b.number(7, 2);
    // record with a field number that is not in the profile
    b.define(0, Fit::Record, {{253, 4, kUInt32}, {3, 1, kUInt8}, {200, 2, kUInt16}});
    b.data(0);
    b.number(kT0, 4);
    b.number(120, 1);
    b.number(4321, 2);

    FitDecoder::Result result;
    const QVector<FitMessage> messages = decodeAll(b.file(), &result);
    QVERIFY(result.ok);
    QVERIFY(result.warnings.isEmpty());
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).globalNum, 0xFF42);
    QCOMPARE(messages.at(0).value(1), 7.0);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->channel("fit.record.200")->valueAt(0.0), 4321.0);
}

void TestFit::enhancedFieldsWin()
{
    FitBuilder b;
    // speed (saturated 16 bit) + enhanced_speed, altitude + enhanced_altitude
    b.define(0, Fit::Record, {{253, 4, kUInt32}, {6, 2, kUInt16}, {73, 4, kUInt32}, {2, 2, kUInt16}, {78, 4, kUInt32}});
    b.data(0);
    b.number(kT0, 4);
    b.number(0xFFFE, 2);
    b.number(90000, 4);               // 90 m/s
    b.number(0xFFFE, 2);
    b.number((20000 + 500) * 5, 4);   // 20 km

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::Speed)->valueAt(0.0), 90.0);
    QCOMPARE(session->channel(ChannelId::Elevation)->valueAt(0.0), 20000.0);
    QVERIFY(!session->hasChannel("fit.record.73"));
}

void TestFit::pedalDynamics()
{
    FitBuilder b;
    // power, left/right balance, torque effectiveness and pedal smoothness, platform centre offsets,
    // power phase and peak phase of each leg (start and end, 256 steps to the turn)
    b.define(0, Fit::Record, {{253, 4, kUInt32}, {7, 2, kUInt16}, {30, 1, kUInt8}, {43, 1, kUInt8}, {44, 1, kUInt8},
                              {45, 1, kUInt8}, {46, 1, kUInt8}, {47, 1, kUInt8}, {67, 1, kSInt8}, {68, 1, kSInt8},
                              {69, 2, kUInt8}, {70, 2, kUInt8}, {71, 2, kUInt8}, {72, 2, kUInt8}});
    auto record = [&](quint32 time, int balance)
    {
        b.data(0);
        b.number(time, 4);
        b.number(300, 2);
        b.number(quint64(balance), 1);
        for (int raw : {180, 170, 50, 46, 48})
            {
                b.number(quint64(raw), 1);
            }
        b.number(quint64(quint8(qint8(-3))), 1);
        b.number(4, 1);
        for (int raw : {32, 160, 64, 96, 224, 64, 0, 32})
            {
                b.number(quint64(raw), 1);
            }
    };
    record(kT0, 0x80 | 52);   // 52 % from the right leg
    record(kT0 + 1, 52);      // without the "right" bit the side is unknown: left out

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::PowerBalanceLeft)->count(), 1);
    QCOMPARE(session->channel(ChannelId::PowerBalanceLeft)->valueAt(0.0), 48.0);
    QCOMPARE(session->channel(ChannelId::LeftPower)->valueAt(0.0), 144.0);
    QCOMPARE(session->channel(ChannelId::RightPower)->valueAt(0.0), 156.0);
    QCOMPARE(session->channel(ChannelId::LeftTorqueEffectiveness)->valueAt(0.0), 90.0);
    QCOMPARE(session->channel(ChannelId::RightTorqueEffectiveness)->valueAt(0.0), 85.0);
    QCOMPARE(session->channel(ChannelId::LeftPedalSmoothness)->valueAt(0.0), 25.0);
    QCOMPARE(session->channel(ChannelId::RightPedalSmoothness)->valueAt(0.0), 23.0);
    QCOMPARE(session->channel(ChannelId::CombinedPedalSmoothness)->valueAt(0.0), 24.0);
    QCOMPARE(session->channel(ChannelId::LeftPlatformCenterOffset)->valueAt(0.0), -3.0);
    QCOMPARE(session->channel(ChannelId::RightPlatformCenterOffset)->valueAt(0.0), 4.0);
    QCOMPARE(session->channel(ChannelId::LeftPowerPhaseStart)->valueAt(0.0), 45.0);
    QCOMPARE(session->channel(ChannelId::LeftPowerPhaseLength)->valueAt(0.0), 180.0);
    QCOMPARE(session->channel(ChannelId::LeftPowerPhasePeakStart)->valueAt(0.0), 90.0);
    QCOMPARE(session->channel(ChannelId::LeftPowerPhasePeakLength)->valueAt(0.0), 45.0);
    // past top dead centre: from 315 degrees on to 90
    QCOMPARE(session->channel(ChannelId::RightPowerPhaseStart)->valueAt(0.0), 315.0);
    QCOMPARE(session->channel(ChannelId::RightPowerPhaseLength)->valueAt(0.0), 135.0);
    QCOMPARE(session->channel(ChannelId::RightPowerPhasePeakStart)->valueAt(0.0), 0.0);
    QCOMPARE(session->channel(ChannelId::RightPowerPhasePeakLength)->valueAt(0.0), 45.0);
    for (int raw : {30, 43, 67, 69, 72})
        {
            QVERIFY(!session->hasChannel(QString("fit.record.%1").arg(raw)));
        }
}

void TestFit::lapsAndSource()
{
    FitBuilder b;
    b.define(3, Fit::FileId, {{1, 2, kUInt16}, {2, 2, kUInt16}});
    b.data(3);
    b.number(1, 2);
    b.number(2713, 2);
    b.define(0, Fit::Record, kRecordFields);
    for (int i = 0; i <= 120; i += 10)
        {
            addRecord(b, 0, kT0 + i, 49.0 + i * 1e-5, 28.0, 100.0, 120, 10.0);
        }
    // lap: timestamp (end), start_time
    b.define(1, Fit::Lap, {{253, 4, kUInt32}, {2, 4, kUInt32}});
    b.data(1);
    b.number(kT0 + 60, 4);
    b.number(kT0, 4);
    b.data(1);
    b.number(kT0 + 120, 4);
    b.number(kT0 + 60, 4);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->laps().size(), 2);
    QCOMPARE(session->laps().at(0).startTime, 0.0);
    QCOMPARE(session->laps().at(0).endTime, 60.0);
    QCOMPARE(session->laps().at(1).endTime, 120.0);
    QVERIFY(session->sourceInfo().contains("2713"));
}

void TestFit::pausesAndUtcOffset()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    b.define(1, Fit::Event, {{253, 4, kUInt32}, {0, 1, 0x00}, {1, 1, 0x00}});
    auto timerEvent = [&](quint32 when, int type)
    {
        b.data(1);
        b.number(when, 4);
        b.number(0, 1);
        b.number(quint64(type), 1);
    };
    timerEvent(kT0, 0);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0);
    addRecord(b, 0, kT0 + 10, 49.0, 28.0, 100.0, 120, 10.0);
    timerEvent(kT0 + 10, 1);       // auto-pause
    timerEvent(kT0 + 40, 0);
    addRecord(b, 0, kT0 + 40, 49.0, 28.0, 100.0, 120, 10.0);
    addRecord(b, 0, kT0 + 100, 49.0, 28.0, 100.0, 120, 10.0);
    timerEvent(kT0 + 100, 4);      // stop_all at the end: not a pause
    // a non-timer event must be ignored
    b.data(1);
    b.number(kT0 + 100, 4);
    b.number(8, 1);
    b.number(1, 1);
    // activity: timestamp, local_timestamp (+3 h)
    b.define(2, Fit::Activity, {{253, 4, kUInt32}, {5, 4, kUInt32}});
    b.data(2);
    b.number(kT0 + 100, 4);
    b.number(kT0 + 100 + 3 * 3600, 4);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->pauses().size(), 1);
    QCOMPARE(session->pauses().at(0).startTime, 10.0);
    QCOMPARE(session->pauses().at(0).endTime, 40.0);
    QVERIFY(session->hasUtcOffset());
    QCOMPARE(session->utcOffsetSeconds(), 3 * 3600);
}

void TestFit::chainedFiles()
{
    FitBuilder first;
    first.define(0, Fit::Record, kRecordFields);
    addRecord(first, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0);
    FitBuilder second;
    // different layout under the same local type: definitions must not leak across files
    second.define(0, Fit::Record, {{253, 4, kUInt32}, {3, 1, kUInt8}});
    second.data(0);
    second.number(kT0 + 5, 4);
    second.number(150, 1);

    FitDecoder::Result result;
    const QVector<FitMessage> messages = decodeAll(first.file() + second.file(), &result);
    QVERIFY(result.ok);
    QCOMPARE(result.fileCount, 2);
    QCOMPARE(messages.size(), 2);

    const TelemetrySessionPtr session = FitImporter::read(first.file() + second.file());
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(5.0), 150.0);
    QVERIFY(session->warnings().isEmpty());
}

void TestFit::truncatedFileKeepsRecords()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    for (int i = 0; i < 10; ++i)
        {
            addRecord(b, 0, kT0 + i, 49.0, 28.0, 100.0, 120 + i, 10.0);
        }
    QByteArray data = b.file();
    data.chop(2 + 18 + 7); // CRC, the last record and part of the one before

    const TelemetrySessionPtr session = FitImporter::read(data);
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::HeartRate)->count(), 8);
    QCOMPARE(session->warnings().size(), 1);
}

void TestFit::unfinalizedHeader()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0);
    addRecord(b, 0, kT0 + 1, 49.0, 28.0, 100.0, 121, 10.0);
    QByteArray data = b.file();
    data.chop(2);                      // recorder died before writing the CRC...
    data.replace(4, 4, QByteArray(4, '\0')); // ...and the data size

    const TelemetrySessionPtr session = FitImporter::read(data);
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::HeartRate)->count(), 2);
    QVERIFY(!session->warnings().isEmpty());
}

void TestFit::badCrcIsOnlyAWarning()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0);
    QByteArray data = b.file();
    data[data.size() - 1] = char(data.at(data.size() - 1) ^ 0x55);

    const TelemetrySessionPtr session = FitImporter::read(data);
    QVERIFY(session);
    QCOMPARE(session->warnings().size(), 1);
    QCOMPARE(session->channel(ChannelId::HeartRate)->count(), 1);
}

void TestFit::irregularAndRepeatedTimestamps()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 100, 10.0);
    addRecord(b, 0, kT0 + 1, 49.0, 28.0, 100.0, 110, 10.0);
    addRecord(b, 0, kT0 + 1, 49.0, 28.0, 100.0, 190, 10.0); // repeated second
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 195, 10.0);     // time went backwards
    addRecord(b, 0, kT0 + 9, 49.0, 28.0, 100.0, 150, 10.0); // smart recording gap

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    const Channel *hr = session->channel(ChannelId::HeartRate);
    QCOMPARE(hr->count(), 3);
    QCOMPARE(hr->valueAt(1.0), 110.0);
    QCOMPARE(hr->valueAt(5.0), 130.0);
}

void TestFit::timestampOutliersAreDropped()
{
    // Seen in a real Garmin file: 14 records stamped two years ahead, then back to normal.
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    for (int i = 0; i < 5; ++i)
        {
            addRecord(b, 0, kT0 + i, 49.0, 28.0, 100.0, 100 + i, 10.0);
        }
    for (int i = 0; i < 3; ++i)
        {
            addRecord(b, 0, kT0 + 68342403 + i, 49.0, 28.0, 100.0, 200, 10.0);
        }
    for (int i = 5; i < 20; ++i)
        {
            addRecord(b, 0, kT0 + i, 49.0, 28.0, 100.0, 100 + i, 10.0);
        }

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QCOMPARE(session->duration(), 19.0);
    QCOMPARE(session->channel(ChannelId::HeartRate)->count(), 20);
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(19.0), 119.0);
    QCOMPARE(session->warnings().size(), 1);
}

void TestFit::clockSetDuringRecording()
{
    // Power-on-relative stamps until the device learns the time
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    addRecord(b, 0, 40, 49.0, 28.0, 100.0, 90, 10.0);
    addRecord(b, 0, 41, 49.0, 28.0, 100.0, 91, 10.0);
    addRecord(b, 0, kT0, 49.0, 28.0, 100.0, 120, 10.0);
    addRecord(b, 0, kT0 + 1, 49.0, 28.0, 100.0, 121, 10.0);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QVERIFY(session->startUtc().isValid());
    QCOMPARE(session->duration(), 1.0);
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(0.0), 120.0);
}

void TestFit::relativeTimeHasNoUtc()
{
    FitBuilder b;
    b.define(0, Fit::Record, kRecordFields);
    addRecord(b, 0, 500, 49.0, 28.0, 100.0, 120, 10.0); // seconds since power-on
    addRecord(b, 0, 505, 49.0, 28.0, 100.0, 125, 10.0);

    const TelemetrySessionPtr session = FitImporter::read(b.file());
    QVERIFY(session);
    QVERIFY(!session->startUtc().isValid());
    QCOMPARE(session->duration(), 5.0);
    QCOMPARE(session->warnings().size(), 1);
}

void TestFit::notAFitFile()
{
    QString error;
    QVERIFY(!FitImporter::read("<?xml version=\"1.0\"?><gpx></gpx>", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!FitImporter::read(QByteArray(), &error));
    QVERIFY(!FitImporter::read(QByteArray(5, '\x0E'), &error));
}

void TestFit::fileWithoutRecords()
{
    FitBuilder b;
    b.define(3, Fit::FileId, {{1, 2, kUInt16}});
    b.data(3);
    b.number(1, 2);

    QString error;
    QVERIFY(!FitImporter::read(b.file(), &error));
    QVERIFY(error.contains("no timed records"));
}

// Optional: RTV_FIT_SAMPLES=/dir runs the importer over every *.fit in that
// directory. Real files from real devices are the only test for the quirks
// nobody thought of; they are not kept in the repository.
void TestFit::sampleFiles()
{
    const QString dirPath = qEnvironmentVariable("RTV_FIT_SAMPLES");
    if (dirPath.isEmpty())
        {
            QSKIP("RTV_FIT_SAMPLES is not set");
        }
    const QFileInfoList files = QDir(dirPath).entryInfoList({"*.fit", "*.FIT"}, QDir::Files, QDir::Name);
    QVERIFY2(!files.isEmpty(), "no .fit files in RTV_FIT_SAMPLES");
    for (const QFileInfo &info : files)
        {
            QString error;
            const TelemetrySessionPtr session = FitImporter::load(info.filePath(), &error);
            if (!session)
                {
                    qInfo().noquote() << info.fileName() << "-> REJECTED:" << error;
                    continue;
                }
            qInfo().noquote() << info.fileName() << "->" << session->sourceInfo()
                              << session->startUtc().toString(Qt::ISODate)
                              << QString("%1 s").arg(session->duration())
                              << session->laps().size() << "laps," << session->pauses().size() << "pauses,"
                              << (session->hasUtcOffset() ? QString("UTC%1%2 h,").arg(session->utcOffsetSeconds() < 0 ? "" : "+").arg(session->utcOffsetSeconds() / 3600.0) : QString("no UTC offset,"))
                              << session->channelIds().size() << "channels:"
                              << session->channelIds().join(' ')
                              << (session->warnings().isEmpty() ? QString() : "WARNINGS: " + session->warnings().join(" | "));
        }
}

QTEST_APPLESS_MAIN(TestFit)
#include "test_fit.moc"
