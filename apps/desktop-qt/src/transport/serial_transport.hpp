#pragma once
/**
 * @file serial_transport.hpp
 * @brief Transport over a real COM port (ESP32 DevKit CP210x USB–UART), QSerialPort on a worker thread.
 *
 * Why a worker thread: on Windows a CP210x that is unplugged while open can make
 * QSerialPort::close()/read calls block inside the driver. In the reference WPF app this froze the UI
 * (commits `70ba8cc`, `e70dc4f`). Here the QSerialPort object is created, used and destroyed only
 * inside SerialWorker on its own QThread; the GUI side talks to it exclusively through queued calls
 * and signals, so a stuck driver call can stall the worker but never the window.
 *
 * Port settings: 115200 8N1, no flow control. DTR and RTS are forced low after opening because the
 * ESP32 DevKit auto-reset circuit uses them to reset / enter the bootloader. The open itself may
 * still pulse them on some drivers; the gateway then reboots, sends EVT BOOT and GatewayClient
 * repeats the handshake (expected, harmless).
 */

#include <QList>
#include <QString>
#include <QThread>

#include "transport/transport.hpp"

class QSerialPort;

/// One entry of the port list shown in the connection screen.
struct SerialPortEntry {
    QString portName;      ///< "COM5"
    QString description;   ///< driver description, e.g. "Silicon Labs CP210x USB to UART Bridge"
    QString manufacturer;
    quint16 vendorId = 0;
    quint16 productId = 0;
    bool hasIds = false;
    bool likelyGateway = false;  ///< CP210x VID/PID – what the ESP32 DevKit uses
};

/// Silicon Labs CP210x default USB ids (VID 0x10C4, PID 0xEA60).
constexpr bool isCp210x(quint16 vendorId, quint16 productId) { return vendorId == 0x10C4 && productId == 0xEA60; }

/// Enumerates COM ports (no port is opened).
QList<SerialPortEntry> availableSerialPorts();

/// Lives on the worker thread and owns the QSerialPort. Internal to SerialTransport.
class SerialWorker final : public QObject {
    Q_OBJECT
public:
    explicit SerialWorker(QString portName) : portName_(std::move(portName)) {}
    ~SerialWorker() override;

public slots:
    void openPort();
    void writeBytes(const QByteArray& bytes);
    void closePort(const QString& reason);

signals:
    void opened();
    void bytesReceived(const QByteArray& bytes);
    void closed(const QString& reason);
    void errorOccurred(const QString& message);

private:
    QString portName_;
    QSerialPort* port_ = nullptr;
};

class SerialTransport final : public Transport {
    Q_OBJECT
public:
    explicit SerialTransport(QString portName, QObject* parent = nullptr);
    /// Stops the worker. Waits at most 2 s; a thread stuck in the driver is left to finish on its own
    /// rather than terminated (QThread::terminate could corrupt driver state).
    ~SerialTransport() override;

    QString description() const override { return portName_; }
    /// Starts an asynchronous open and returns true. Failure is reported by errorOccurred() and
    /// closed("open failed: …") – the same path as a later unplug.
    bool open() override;
    void close() override;
    bool isOpen() const override { return open_; }
    void write(const QByteArray& bytes) override;

private:
    void onWorkerClosed(const QString& reason);
    void stopThread();

    QString portName_;
    /// Heap-allocated on purpose: if the worker is stuck in the driver when we are destroyed, the
    /// thread object must outlive us (destroying a running QThread aborts the program). It then
    /// deletes itself when it finally finishes.
    QThread* thread_ = nullptr;
    SerialWorker* worker_ = nullptr;  ///< lives on thread_, deleted (and port closed) when it finishes
    bool open_ = false;
};
