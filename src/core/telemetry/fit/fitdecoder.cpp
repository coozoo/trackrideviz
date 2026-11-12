#include "fitdecoder.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace
{
const double kInvalid = std::numeric_limits<double>::quiet_NaN();
const int kBaseTypeSize[] = {1, 1, 1, 2, 2, 4, 4, 1, 4, 8, 1, 2, 4, 1, 8, 8, 8};
const int kBaseTypeCount = sizeof(kBaseTypeSize) / sizeof(kBaseTypeSize[0]);

quint64 readUnsigned(const uchar *data, int size, bool bigEndian)
{
    quint64 v = 0;
    for (int i = 0; i < size; ++i)
        {
            const int index = bigEndian ? i : size - 1 - i;
            v = (v << 8) | data[index];
        }
    return v;
}

double decodeOne(const uchar *data, int baseType, bool bigEndian)
{
    const quint64 u = readUnsigned(data, kBaseTypeSize[baseType], bigEndian);
    switch (baseType)
        {
        case Fit::Enum:
        case Fit::UInt8:
        case Fit::Byte:
            return u == 0xFF ? kInvalid : double(u);
        case Fit::SInt8:
            return u == 0x7F ? kInvalid : double(qint8(u));
        case Fit::SInt16:
            return u == 0x7FFF ? kInvalid : double(qint16(u));
        case Fit::UInt16:
            return u == 0xFFFF ? kInvalid : double(u);
        case Fit::SInt32:
            return u == 0x7FFFFFFF ? kInvalid : double(qint32(u));
        case Fit::UInt32:
            return u == 0xFFFFFFFF ? kInvalid : double(u);
        case Fit::UInt8z:
        case Fit::UInt16z:
        case Fit::UInt32z:
        case Fit::UInt64z:
            return u == 0 ? kInvalid : double(u);
        case Fit::SInt64:
            return u == Q_UINT64_C(0x7FFFFFFFFFFFFFFF) ? kInvalid : double(qint64(u));
        case Fit::UInt64:
            return u == Q_UINT64_C(0xFFFFFFFFFFFFFFFF) ? kInvalid : double(u);
        case Fit::Float32:
        {
            if (u == 0xFFFFFFFF)
                {
                    return kInvalid;
                }
            const quint32 bits = quint32(u);
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            return double(f);
        }
        case Fit::Float64:
        {
            if (u == Q_UINT64_C(0xFFFFFFFFFFFFFFFF))
                {
                    return kInvalid;
                }
            double d;
            std::memcpy(&d, &u, sizeof(d));
            return d;
        }
        default:
            return kInvalid;
        }
}
}

double FitField::value() const
{
    return values.isEmpty() ? kInvalid : values.first();
}

bool FitField::isValid() const
{
    return !std::isnan(value());
}

QString FitField::text() const
{
    const int end = raw.indexOf('\0');
    return QString::fromUtf8(end < 0 ? raw : raw.left(end));
}

const FitField *FitMessage::field(int num) const
{
    for (const FitField &f : fields)
        {
            if (!f.developer && f.num == num)
                {
                    return &f;
                }
        }
    return nullptr;
}

double FitMessage::value(int num) const
{
    const FitField *f = field(num);
    return f ? f->value() : kInvalid;
}

quint16 FitDecoder::crc16(const char *data, qsizetype size, quint16 crc)
{
    static const quint16 table[16] =
    {
        0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
        0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400
    };
    for (qsizetype i = 0; i < size; ++i)
        {
            const quint8 byte = quint8(data[i]);
            quint16 tmp = table[crc & 0xF];
            crc = (crc >> 4) & 0x0FFF;
            crc = crc ^ tmp ^ table[byte & 0xF];
            tmp = table[crc & 0xF];
            crc = (crc >> 4) & 0x0FFF;
            crc = crc ^ tmp ^ table[(byte >> 4) & 0xF];
        }
    return crc;
}

void FitDecoder::decodeValues(const uchar *data, int size, bool bigEndian, FitField &field)
{
    field.raw = QByteArray(reinterpret_cast<const char *>(data), size);
    const int type = field.baseType;
    if (type < 0 || type >= kBaseTypeCount || type == Fit::String || size == 0)
        {
            return;
        }
    const int typeSize = kBaseTypeSize[type];
    if (size % typeSize == 0)
        {
            const int count = size / typeSize;
            field.values.reserve(count);
            for (int i = 0; i < count; ++i)
                {
                    field.values.append(decodeOne(data + i * typeSize, type, bigEndian));
                }
        }
    else if (size < typeSize && type != Fit::Float32 && type != Fit::Float64)
        {
            // Some writers declare e.g. a uint32 with size 1 (seen from Coros).
            // Take the bytes that are there as an unsigned number.
            field.values.append(double(readUnsigned(data, size, bigEndian)));
        }
    // otherwise: size and type disagree in a way we cannot interpret; raw bytes only
}

void FitDecoder::rememberDeveloperField(const FitMessage &message)
{
    // field_description: 0 developer_data_index, 1 field_definition_number,
    // 2 fit_base_type_id, 3 field_name, 6 scale, 7 offset, 8 units
    const double index = message.value(0);
    const double number = message.value(1);
    const double baseType = message.value(2);
    if (std::isnan(index) || std::isnan(number) || std::isnan(baseType))
        {
            return;
        }
    DeveloperFieldDescription description;
    description.baseType = int(baseType) & 0x1F;
    if (const FitField *name = message.field(3))
        {
            description.name = name->text();
        }
    if (const FitField *units = message.field(8))
        {
            description.units = units->text();
        }
    const double scale = message.value(6);
    if (!std::isnan(scale) && scale != 0.0)
        {
            description.scale = scale;
        }
    const double offset = message.value(7);
    if (!std::isnan(offset))
        {
            description.offset = offset;
        }
    m_developerFields.insert((int(index) << 8) | int(number), description);
}

bool FitDecoder::decodeBody(const uchar *begin, const uchar *end, const Handler &handler, Result &result)
{
    const uchar *p = begin;
    while (p < end)
        {
            const quint8 header = *p++;
            const bool compressedTime = header & 0x80;

            if (!compressedTime && (header & 0x40))
                {
                    // ---- definition message
                    const bool hasDeveloperFields = header & 0x20;
                    if (end - p < 5)
                        {
                            result.truncated = true;
                            return false;
                        }
                    MessageDef def;
                    def.defined = true;
                    def.bigEndian = p[1] != 0;
                    def.globalNum = quint16(readUnsigned(p + 2, 2, def.bigEndian));
                    const int fieldCount = p[4];
                    p += 5;
                    if (end - p < fieldCount * 3)
                        {
                            result.truncated = true;
                            return false;
                        }
                    def.fields.reserve(fieldCount);
                    for (int i = 0; i < fieldCount; ++i, p += 3)
                        {
                            def.fields.append({p[0], p[1], quint8(p[2] & 0x1F), false, 0});
                            def.dataSize += p[1];
                        }
                    if (hasDeveloperFields)
                        {
                            if (end - p < 1 || end - (p + 1) < p[0] * 3)
                                {
                                    result.truncated = true;
                                    return false;
                                }
                            const int developerCount = *p++;
                            for (int i = 0; i < developerCount; ++i, p += 3)
                                {
                                    def.fields.append({p[0], p[1], quint8(Fit::Byte), true, p[2]});
                                    def.dataSize += p[1];
                                }
                        }
                    m_definitions[header & 0x0F] = def;
                    continue;
                }

            // ---- data message
            const int localNum = compressedTime ? (header >> 5) & 0x03 : header & 0x0F;
            const MessageDef &def = m_definitions[localNum];
            if (!def.defined)
                {
                    // Nothing after this point can be trusted: we do not know how long the record is.
                    result.warnings << tr("Data for local message type %1 appears before its definition; the rest of the file was skipped.").arg(localNum);
                    return false;
                }
            if (end - p < def.dataSize)
                {
                    result.truncated = true;
                    return false;
                }

            FitMessage message;
            message.globalNum = def.globalNum;
            message.fields.reserve(def.fields.size() + 1);
            for (const FieldDef &fieldDef : def.fields)
                {
                    FitField field;
                    field.num = fieldDef.num;
                    field.baseType = fieldDef.baseType;
                    double scale = 1.0;
                    double offset = 0.0;
                    if (fieldDef.developer)
                        {
                            field.developer = true;
                            field.developerIndex = fieldDef.developerIndex;
                            const auto it = m_developerFields.constFind((int(fieldDef.developerIndex) << 8) | fieldDef.num);
                            if (it != m_developerFields.constEnd())
                                {
                                    field.baseType = it->baseType;
                                    field.name = it->name;
                                    field.units = it->units;
                                    scale = it->scale;
                                    offset = it->offset;
                                }
                        }
                    decodeValues(p, fieldDef.size, def.bigEndian, field);
                    if (scale != 1.0 || offset != 0.0)
                        {
                            for (double &v : field.values)
                                {
                                    v = v / scale - offset;
                                }
                        }
                    p += fieldDef.size;
                    message.fields.append(field);
                }

            // Keep the running timestamp that compressed headers are relative to.
            const double timestamp = message.value(Fit::TimestampField);
            if (!std::isnan(timestamp))
                {
                    m_lastTimestamp = quint32(timestamp);
                    m_haveTimestamp = true;
                }
            else if (compressedTime && m_haveTimestamp)
                {
                    // 5-bit seconds counter that rolls over every 32 s
                    const quint32 offset = header & 0x1F;
                    quint32 full = (m_lastTimestamp & ~quint32(0x1F)) | offset;
                    if (offset < (m_lastTimestamp & 0x1F))
                        {
                            full += 0x20;
                        }
                    m_lastTimestamp = full;
                    FitField field;
                    field.num = Fit::TimestampField;
                    field.baseType = Fit::UInt32;
                    field.values.append(double(full));
                    message.fields.append(field);
                }

            if (message.globalNum == Fit::FieldDescription)
                {
                    rememberDeveloperField(message);
                }
            ++result.messageCount;
            if (handler)
                {
                    handler(message);
                }
        }
    return true;
}

FitDecoder::Result FitDecoder::decode(const QByteArray &data, const Handler &handler)
{
    Result result;
    const uchar *bytes = reinterpret_cast<const uchar *>(data.constData());
    const qsizetype total = data.size();
    qsizetype pos = 0;

    while (total - pos >= 12)
        {
            const int headerSize = bytes[pos];
            const bool signatureOk = headerSize >= 12 && pos + headerSize <= total
                                     && std::memcmp(bytes + pos + 8, ".FIT", 4) == 0;
            if (!signatureOk)
                {
                    if (result.fileCount == 0)
                        {
                            result.error = tr("This is not a FIT file.");
                            return result;
                        }
                    result.warnings << tr("Unrecognized data after the end of the FIT file was ignored.");
                    break;
                }

            if (headerSize >= 14)
                {
                    const quint16 stored = quint16(readUnsigned(bytes + pos + 12, 2, false));
                    // 0 means "no header CRC" and is legal
                    if (stored != 0 && stored != crc16(data.constData() + pos, 12))
                        {
                            result.warnings << tr("The file header checksum is wrong.");
                        }
                }

            const qsizetype bodyStart = pos + headerSize;
            qsizetype dataSize = qsizetype(readUnsigned(bytes + pos + 4, 4, false));
            bool hasTrailingCrc = true;
            if (dataSize == 0 && total - bodyStart > 2)
                {
                    // The recorder never went back to fill in the size (power loss, crash).
                    dataSize = total - bodyStart;
                    hasTrailingCrc = false;
                    result.truncated = true;
                }
            else if (bodyStart + dataSize > total)
                {
                    dataSize = total - bodyStart;
                    hasTrailingCrc = false;
                    result.truncated = true;
                }

            // Local definitions, developer fields and the running timestamp do not carry over to the next chained file.
            for (MessageDef &def : m_definitions)
                {
                    def = MessageDef();
                }
            m_developerFields.clear();
            m_haveTimestamp = false;
            ++result.fileCount;

            const bool complete = decodeBody(bytes + bodyStart, bytes + bodyStart + dataSize, handler, result);
            if (!complete || !hasTrailingCrc)
                {
                    break;
                }

            const qsizetype bodyEnd = bodyStart + dataSize;
            if (total - bodyEnd < 2)
                {
                    result.truncated = true;
                    break;
                }
            const quint16 stored = quint16(readUnsigned(bytes + bodyEnd, 2, false));
            if (stored != crc16(data.constData() + pos, bodyEnd - pos))
                {
                    result.warnings << tr("The file checksum is wrong; the data may be damaged.");
                }
            pos = bodyEnd + 2;
        }

    if (result.fileCount == 0)
        {
            result.error = tr("This is not a FIT file.");
            return result;
        }
    if (result.truncated)
        {
            result.warnings << tr("The file is incomplete; everything recorded before the cut was read.");
        }
    result.ok = true;
    return result;
}
