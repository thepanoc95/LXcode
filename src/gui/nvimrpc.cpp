#include "gui/nvimrpc.hpp"

#include <QEventLoop>
#include <QTimer>

#include <cstring>
#include <limits>

namespace lxcode::gui {

namespace {

MsgPackValue valueOf(MsgPackValue::Type type) {
    MsgPackValue value;
    value.type = type;
    return value;
}

void writeUint(QByteArray& output, quint64 value, int bytes) {
    for (int i = bytes - 1; i >= 0; --i)
        output.append(static_cast<char>((value >> (i * 8)) & 0xffu));
}

std::optional<MsgPackValue> decodeValue(const QByteArray& input, qsizetype& position) {
    const auto available = [&](qsizetype count) {
        return count >= 0 && position <= input.size() && count <= input.size() - position;
    };
    const auto readUint = [&](int bytes, quint64& out) {
        if (!available(bytes)) return false;
        out = 0;
        for (int i = 0; i < bytes; ++i)
            out = (out << 8) | static_cast<unsigned char>(input[position++]);
        return true;
    };

    if (position >= input.size()) return std::nullopt;
    const auto byte = static_cast<unsigned char>(input[position++]);
    quint64 number = 0;

    if (byte <= 0x7f) return MsgPackValue::fromInt(byte);
    if (byte >= 0xe0) return MsgPackValue::fromInt(static_cast<int8_t>(byte));

    if (byte >= 0xa0 && byte <= 0xbf) {
        const auto length = static_cast<qsizetype>(byte & 0x1fu);
        if (!available(length)) return std::nullopt;
        auto value = MsgPackValue::fromString(input.mid(position, length));
        position += length;
        return value;
    }
    if (byte >= 0x90 && byte <= 0x9f) {
        auto value = valueOf(MsgPackValue::Type::Array);
        const auto count = static_cast<qsizetype>(byte & 0x0fu);
        value.array.reserve(static_cast<std::size_t>(count));
        for (qsizetype i = 0; i < count; ++i) {
            auto element = decodeValue(input, position);
            if (!element) return std::nullopt;
            value.array.push_back(std::move(*element));
        }
        return value;
    }
    if (byte >= 0x80 && byte <= 0x8f) {
        auto value = valueOf(MsgPackValue::Type::Map);
        const auto count = static_cast<qsizetype>(byte & 0x0fu);
        value.map.reserve(static_cast<std::size_t>(count));
        for (qsizetype i = 0; i < count; ++i) {
            auto key = decodeValue(input, position);
            auto mapped = decodeValue(input, position);
            if (!key || !mapped) return std::nullopt;
            value.map.emplace_back(std::move(*key), std::move(*mapped));
        }
        return value;
    }

    switch (byte) {
    case 0xc0: return MsgPackValue::nil();
    case 0xc2: return MsgPackValue::fromBool(false);
    case 0xc3: return MsgPackValue::fromBool(true);
    case 0xcc:
    case 0xcd:
    case 0xce:
    case 0xcf: {
        const int bytes = 1 << (byte - 0xcc);
        if (!readUint(bytes, number)) return std::nullopt;
        return MsgPackValue::fromInt(static_cast<int64_t>(number));
    }
    case 0xd0:
    case 0xd1:
    case 0xd2:
    case 0xd3: {
        const int bytes = 1 << (byte - 0xd0);
        if (!readUint(bytes, number)) return std::nullopt;
        int64_t signedValue = static_cast<int64_t>(number);
        if (bytes == 1) signedValue = static_cast<int8_t>(number);
        else if (bytes == 2) signedValue = static_cast<int16_t>(number);
        else if (bytes == 4) signedValue = static_cast<int32_t>(number);
        return MsgPackValue::fromInt(signedValue);
    }
    case 0xca: {
        if (!readUint(4, number)) return std::nullopt;
        float floating = 0.0f;
        std::memcpy(&floating, &number, sizeof(floating));
        return MsgPackValue::fromInt(static_cast<int64_t>(floating));
    }
    case 0xcb: {
        if (!readUint(8, number)) return std::nullopt;
        double floating = 0.0;
        std::memcpy(&floating, &number, sizeof(floating));
        return MsgPackValue::fromInt(static_cast<int64_t>(floating));
    }
    case 0xd9:
    case 0xda:
    case 0xdb:
    case 0xc4:
    case 0xc5:
    case 0xc6: {
        const int lengthBytes = (byte == 0xd9 || byte == 0xc4) ? 1
                                : (byte == 0xda || byte == 0xc5) ? 2 : 4;
        if (!readUint(lengthBytes, number)) return std::nullopt;
        if (number > static_cast<quint64>(std::numeric_limits<qsizetype>::max()))
            return std::nullopt;
        const auto length = static_cast<qsizetype>(number);
        if (!available(length)) return std::nullopt;
        auto value = MsgPackValue::fromString(input.mid(position, length));
        position += length;
        return value;
    }
    case 0xdc:
    case 0xdd: {
        const int countBytes = byte == 0xdc ? 2 : 4;
        if (!readUint(countBytes, number)) return std::nullopt;
        auto value = valueOf(MsgPackValue::Type::Array);
        value.array.reserve(static_cast<std::size_t>(number));
        for (quint64 i = 0; i < number; ++i) {
            auto element = decodeValue(input, position);
            if (!element) return std::nullopt;
            value.array.push_back(std::move(*element));
        }
        return value;
    }
    case 0xde:
    case 0xdf: {
        const int countBytes = byte == 0xde ? 2 : 4;
        if (!readUint(countBytes, number)) return std::nullopt;
        auto value = valueOf(MsgPackValue::Type::Map);
        value.map.reserve(static_cast<std::size_t>(number));
        for (quint64 i = 0; i < number; ++i) {
            auto key = decodeValue(input, position);
            auto mapped = decodeValue(input, position);
            if (!key || !mapped) return std::nullopt;
            value.map.emplace_back(std::move(*key), std::move(*mapped));
        }
        return value;
    }
    default: return std::nullopt;
    }
}

} // namespace

MsgPackValue MsgPackValue::nil() { return valueOf(Type::Nil); }

MsgPackValue MsgPackValue::fromBool(bool value) {
    auto result = valueOf(Type::Bool);
    result.boolean = value;
    return result;
}

MsgPackValue MsgPackValue::fromInt(int64_t value) {
    auto result = valueOf(Type::Int);
    result.integer = value;
    return result;
}

MsgPackValue MsgPackValue::fromString(const QByteArray& value) {
    auto result = valueOf(Type::Str);
    result.string = value;
    return result;
}

MsgPackValue MsgPackValue::fromArray(std::vector<MsgPackValue> values) {
    auto result = valueOf(Type::Array);
    result.array = std::move(values);
    return result;
}

MsgPackValue MsgPackValue::fromMap(std::vector<std::pair<MsgPackValue, MsgPackValue>> values) {
    auto result = valueOf(Type::Map);
    result.map = std::move(values);
    return result;
}

bool MsgPackValue::toBool(bool fallback) const {
    if (type == Type::Bool) return boolean;
    if (type == Type::Int) return integer != 0;
    return fallback;
}

int64_t MsgPackValue::toInt(int64_t fallback) const {
    if (type == Type::Int) return integer;
    if (type == Type::Bool) return boolean ? 1 : 0;
    return fallback;
}

const MsgPackValue* MsgPackValue::at(std::size_t index) const {
    if (!isArray() || index >= array.size()) return nullptr;
    return &array[index];
}

const MsgPackValue* MsgPackValue::get(const QByteArray& key) const {
    if (!isMap()) return nullptr;
    for (const auto& entry : map) {
        if (entry.first.isStr() && entry.first.string == key) return &entry.second;
    }
    return nullptr;
}

void msgpackEncode(const MsgPackValue& value, QByteArray& output) {
    switch (value.type) {
    case MsgPackValue::Type::Nil:
        output.append(static_cast<char>(0xc0));
        return;
    case MsgPackValue::Type::Bool:
        output.append(static_cast<char>(value.boolean ? 0xc3 : 0xc2));
        return;
    case MsgPackValue::Type::Int: {
        const auto number = static_cast<int64_t>(value.integer);
        if (number >= 0 && number <= 0x7f) {
            output.append(static_cast<char>(number));
        } else if (number >= -32 && number < 0) {
            output.append(static_cast<char>(number));
        } else {
            output.append(static_cast<char>(0xd3));
            writeUint(output, static_cast<quint64>(number), 8);
        }
        return;
    }
    case MsgPackValue::Type::Str: {
        const auto size = static_cast<quint64>(value.string.size());
        if (size <= 0x1f) {
            output.append(static_cast<char>(0xa0u | size));
        } else if (size <= 0xff) {
            output.append(static_cast<char>(0xd9));
            output.append(static_cast<char>(size));
        } else if (size <= 0xffff) {
            output.append(static_cast<char>(0xda));
            writeUint(output, size, 2);
        } else {
            output.append(static_cast<char>(0xdb));
            writeUint(output, size, 4);
        }
        output.append(value.string);
        return;
    }
    case MsgPackValue::Type::Array: {
        const auto count = value.array.size();
        if (count <= 0x0f) {
            output.append(static_cast<char>(0x90u | count));
        } else if (count <= 0xffff) {
            output.append(static_cast<char>(0xdc));
            writeUint(output, static_cast<quint64>(count), 2);
        } else {
            output.append(static_cast<char>(0xdd));
            writeUint(output, static_cast<quint64>(count), 4);
        }
        for (const auto& element : value.array) msgpackEncode(element, output);
        return;
    }
    case MsgPackValue::Type::Map: {
        const auto count = value.map.size();
        if (count <= 0x0f) {
            output.append(static_cast<char>(0x80u | count));
        } else if (count <= 0xffff) {
            output.append(static_cast<char>(0xde));
            writeUint(output, static_cast<quint64>(count), 2);
        } else {
            output.append(static_cast<char>(0xdf));
            writeUint(output, static_cast<quint64>(count), 4);
        }
        for (const auto& entry : value.map) {
            msgpackEncode(entry.first, output);
            msgpackEncode(entry.second, output);
        }
        return;
    }
    }
}

std::optional<MsgPackValue> msgpackDecode(const QByteArray& input, qsizetype& offset) {
    qsizetype position = offset;
    auto value = decodeValue(input, position);
    if (!value) return std::nullopt;
    offset = position;
    return value;
}

NvimRpc::NvimRpc(QObject* parent) : QObject(parent) {
    setObjectName("NvimRpc");
}

NvimRpc::~NvimRpc() {
    stop();
}

bool NvimRpc::start(const QString& executable, const QStringList& arguments, QString* error) {
    process_.setProgram(executable);
    process_.setArguments(arguments);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &NvimRpc::readOutput);
    connect(&process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        waiting_ = false;
        waitError_ = "Neovim exited unexpectedly";
        if (waitLoop_) static_cast<QEventLoop*>(waitLoop_)->quit();
    });
    process_.start();
    if (!process_.waitForStarted(5000)) {
        if (error) *error = process_.errorString();
        return false;
    }
    return true;
}

void NvimRpc::stop() {
    if (process_.state() == QProcess::NotRunning) return;
    process_.blockSignals(true);
    process_.terminate();
    if (!process_.waitForFinished(1000)) process_.kill();
    process_.waitForFinished(1000);
    process_.blockSignals(false);
}

bool NvimRpc::isRunning() const {
    return process_.state() == QProcess::Running;
}

void NvimRpc::notify(const QByteArray& method, const MsgPackValue& params) {
    if (!isRunning()) return;
    const auto message = MsgPackValue::fromArray({MsgPackValue::fromInt(2),
                                                  MsgPackValue::fromString(method), params});
    QByteArray encoded;
    msgpackEncode(message, encoded);
    process_.write(encoded);
}

MsgPackValue NvimRpc::call(const QByteArray& method, const MsgPackValue& params, QString* error) {
    if (!isRunning()) {
        if (error) *error = "Neovim is not running";
        return MsgPackValue::nil();
    }
    const auto requestId = nextRequestId_++;
    const auto message = MsgPackValue::fromArray({MsgPackValue::fromInt(0),
                                                  MsgPackValue::fromInt(requestId),
                                                  MsgPackValue::fromString(method), params});
    QByteArray encoded;
    msgpackEncode(message, encoded);

    waitingId_ = requestId;
    waiting_ = true;
    waitResult_ = MsgPackValue::nil();
    waitError_.clear();

    QEventLoop loop;
    waitLoop_ = &loop;
    process_.write(encoded);
    if (waiting_) {
        QTimer::singleShot(4000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    waitLoop_ = nullptr;
    waiting_ = false;
    if (error) *error = waitError_;
    return waitResult_;
}

void NvimRpc::readOutput() {
    buffer_.append(process_.readAllStandardOutput());
    qsizetype offset = 0;
    while (true) {
        qsizetype start = offset;
        auto message = msgpackDecode(buffer_, offset);
        if (!message) {
            offset = start;
            break;
        }
        if (!message->isArray() || message->array.empty()) continue;
        const auto kind = message->array.front().toInt(-1);
        if (kind == 1 && message->array.size() >= 4) {
            const auto id = message->array[1].toInt(-1);
            if (waiting_ && id == waitingId_) {
                const auto& failure = message->array[2];
                if (!failure.isNil()) {
                    if (const auto* detail = failure.at(1))
                        waitError_ = detail->toString();
                    else
                        waitError_ = "Neovim request failed";
                }
                waitResult_ = message->array[3];
                waiting_ = false;
                if (waitLoop_) static_cast<QEventLoop*>(waitLoop_)->quit();
            }
        } else if (kind == 2 && message->array.size() >= 3 && handler_) {
            handler_(message->array[1].toByteArray(), message->array[2]);
        }
    }
    if (offset > 0) buffer_.remove(0, offset);
}

} // namespace lxcode::gui
