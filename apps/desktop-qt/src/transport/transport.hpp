#pragma once
/**
 * @file transport.hpp
 * @brief Byte-stream link to a gateway (serial port or in-process simulation).
 *
 * Contract for implementations:
 *  - All signals are delivered asynchronously (never from inside write()), like a real serial port,
 *    so callers never see re-entrant callbacks.
 *  - Blocking driver calls (e.g. QSerialPort::close on an unplugged CP210x) must not run on the GUI
 *    thread (reference-system lessons `70ba8cc`, `e70dc4f`); a serial implementation lives on a
 *    worker thread and talks to the client only through these signals.
 *  - closed() is emitted exactly once per successful open(), whatever the reason.
 */

#include <QByteArray>
#include <QObject>
#include <QString>

class Transport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~Transport() override = default;

    /// Human-readable link description for the UI, e.g. "COM5" or "In-process simulation".
    virtual QString description() const = 0;
    /// Opens the link. @return false on immediate failure (errorOccurred() carries the reason).
    virtual bool open() = 0;
    /// Closes the link; emits closed("closed by user") if it was open.
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    /// Queues bytes for transmission. Ignored when not open.
    virtual void write(const QByteArray& bytes) = 0;

signals:
    void bytesReceived(const QByteArray& bytes);
    void closed(const QString& reason);
    void errorOccurred(const QString& message);
};
