#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

// Generic FIT protocol decoder: turns the byte stream into messages with
// numbered fields and knows nothing about what the numbers mean (that is
// FitImporter's job). Because of that an unknown message, an unknown field or
// a vendor's private extension is never an error - it is just passed on.
//
// Handles: 12 and 14 byte headers, several FIT files chained in one, files cut
// short or never finalized (data size 0), per-definition byte order, local
// types redefined mid-file, compressed timestamp headers, developer fields
// with their descriptions, arrays, and writers that declare a wrong field size.
// See docs/planning/06-fit-notes.md.

namespace Fit
{
enum BaseType
{
    Enum = 0, SInt8 = 1, UInt8 = 2, SInt16 = 3, UInt16 = 4, SInt32 = 5, UInt32 = 6,
    String = 7, Float32 = 8, Float64 = 9, UInt8z = 10, UInt16z = 11, UInt32z = 12,
    Byte = 13, SInt64 = 14, UInt64 = 15, UInt64z = 16
};

// Global message numbers the importer cares about.
enum MessageNum
{
    FileId = 0, Session = 18, Lap = 19, Record = 20, Event = 21, DeviceInfo = 23,
    Activity = 34, FieldDescription = 206, DeveloperDataId = 207
};

const int TimestampField = 253;
// FIT epoch 1989-12-31T00:00:00Z as Unix time.
const qint64 EpochOffset = 631065600;
// Timestamps below this are seconds since power-on, not a date.
const double MinAbsoluteTimestamp = 0x10000000;
}

struct FitField
{
    int num{0};
    int baseType{Fit::Byte};
    // Developer fields are numbered per developer, separately from the native ones.
    bool developer{false};
    int developerIndex{-1};
    QString name;  // developer fields only, from their field_description
    QString units; // developer fields only

    // One entry per array element; NaN where the file holds the type's "invalid" marker.
    // Empty for strings, byte arrays and fields that could not be interpreted.
    QVector<double> values;
    QByteArray raw;

    // First element, NaN if there is none or it is invalid.
    double value() const;
    bool isValid() const;
    // For string fields: text up to the first NUL.
    QString text() const;
};

struct FitMessage
{
    int globalNum{0};
    QVector<FitField> fields;

    // Native (non-developer) field by number, nullptr if absent.
    const FitField *field(int num) const;
    // Shortcut: NaN when the field is absent or invalid.
    double value(int num) const;
};

class FitDecoder
{
    Q_DECLARE_TR_FUNCTIONS(FitDecoder)

public:
    struct Result
    {
        bool ok{false};        // false: not a FIT file at all, see error
        QString error;
        QStringList warnings;  // recoverable problems; decoding went on or stopped early
        bool truncated{false}; // data ended before the header said it would
        int fileCount{0};      // number of chained FIT files found
        int messageCount{0};
    };

    using Handler = std::function<void(const FitMessage &)>;

    // Calls handler for every data message, in file order.
    Result decode(const QByteArray &data, const Handler &handler);

    static quint16 crc16(const char *data, qsizetype size, quint16 crc = 0);

private:
    struct FieldDef
    {
        quint8 num;
        quint8 size;
        quint8 baseType;
        bool developer;
        quint8 developerIndex;
    };
    struct MessageDef
    {
        bool defined{false};
        bool bigEndian{false};
        quint16 globalNum{0};
        int dataSize{0};
        QVector<FieldDef> fields;
    };
    struct DeveloperFieldDescription
    {
        int baseType{Fit::Byte};
        QString name;
        QString units;
        double scale{1.0};
        double offset{0.0};
    };

    // Returns false when decoding of this file cannot continue.
    bool decodeBody(const uchar *begin, const uchar *end, const Handler &handler, Result &result);
    void rememberDeveloperField(const FitMessage &message);
    static void decodeValues(const uchar *data, int size, bool bigEndian, FitField &field);

    MessageDef m_definitions[16];
    QHash<int, DeveloperFieldDescription> m_developerFields; // key: developerIndex << 8 | field number
    bool m_haveTimestamp{false};
    quint32 m_lastTimestamp{0};
};
