#include "gateway/gateway_types.hpp"

QString toQString(std::string_view text) {
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

void copyFields(const proto::Message& message, QStringList& positional, QMap<QString, QString>& values) {
    for (std::size_t i = 0; i < message.argCount; ++i) {
        const QString field = toQString(message.args[i]);
        const qsizetype equals = field.indexOf(QLatin1Char('='));
        if (equals > 0) {
            values.insert(field.left(equals), field.mid(equals + 1));
        } else {
            positional.append(field);
        }
    }
}

QString GatewayReply::errorText() const {
    if (ok) return QStringLiteral("OK");
    return QStringLiteral("%1/%2/%3")
        .arg(QLatin1String(diag::toString(error.stage)), QLatin1String(diag::toString(error.code)))
        .arg(error.detail, 2, 16, QLatin1Char('0'))
        .toUpper();
}
