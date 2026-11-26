#pragma once
#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace lxcode::gui {

// A dynamically typed MessagePack value, sufficient for Neovim's RPC API.
struct MsgPackValue {
    enum class Type { Nil, Bool, Int, Str, Array, Map };

    Type type = Type::Nil;
    bool boolean = false;
    int64_t integer = 0;
    QByteArray string;
    std::vector<MsgPackValue> array;
    std::vector<std::pair<MsgPackValue, MsgPackValue>> map;

    static MsgPackValue nil();
    static MsgPackValue fromBool(bool value);
    static MsgPackValue fromInt(int64_t value);
    static MsgPackValue fromString(const QByteArray& value);
    static MsgPackValue fromArray(std::vector<MsgPackValue> values);
    static MsgPackValue fromMap(std::vector<std::pair<MsgPackValue, MsgPackValue>> values);

    bool isNil() const { return type == Type::Nil; }
    bool isBool() const { return type == Type::Bool; }
    bool isInt() const { return type == Type::Int; }
    bool isStr() const { return type == Type::Str; }
    bool isArray() const { return type == Type::Array; }
    bool isMap() const { return type == Type::Map; }

    bool toBool(bool fallback = false) const;
    int64_t toInt(int64_t fallback = 0) const;
    QByteArray toByteArray() const { return string; }
    QString toString() const { return QString::fromUtf8(string); }

    const MsgPackValue* at(std::size_t index) const;
    const MsgPackValue* get(const QByteArray& key) const;
};

void msgpackEncode(const MsgPackValue& value, QByteArray& output);
std::optional<MsgPackValue> msgpackDecode(const QByteArray& input, qsizetype& offset);

// Minimal msgpack-rpc client speaking to a headless Neovim over stdio.
class NvimRpc : public QObject {
    Q_OBJECT
public:
    using NotificationHandler = std::function<void(const QByteArray&, const MsgPackValue&)>;

    explicit NvimRpc(QObject* parent = nullptr);
    ~NvimRpc() override;

    bool start(const QString& executable, const QStringList& arguments, QString* error = nullptr);
    void stop();
    bool isRunning() const;
    void setNotificationHandler(NotificationHandler handler) { handler_ = std::move(handler); }

    // Sends a request and blocks the calling thread until its response arrives.
    // Nested event processing keeps UI redraw notifications flowing.
    MsgPackValue call(const QByteArray& method, const MsgPackValue& params,
                      QString* error = nullptr);
    void notify(const QByteArray& method, const MsgPackValue& params);
    void input(const QByteArray& keys) {
        notify("nvim_input", MsgPackValue::fromArray({MsgPackValue::fromString(keys)}));
    }

private slots:
    void readOutput();

private:
    QProcess process_;
    QByteArray buffer_;
    int64_t nextRequestId_ = 1;
    int64_t waitingId_ = -1;
    bool waiting_ = false;
    MsgPackValue waitResult_;
    QString waitError_;
    QObject* waitLoop_ = nullptr;
    NotificationHandler handler_;
};

} // namespace lxcode::gui
