#include "transport/serial_transport.hpp"

#include <QSerialPort>
#include <QSerialPortInfo>

namespace {
constexpr qint32 kBaudRate = 115200;
constexpr int kStopTimeoutMs = 2000;
}  // namespace

QList<SerialPortEntry> availableSerialPorts() {
    QList<SerialPortEntry> entries;
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : ports) {
        SerialPortEntry entry;
        entry.portName = info.portName();
        entry.description = info.description();
        entry.manufacturer = info.manufacturer();
        entry.hasIds = info.hasVendorIdentifier() && info.hasProductIdentifier();
        entry.vendorId = info.vendorIdentifier();
        entry.productId = info.productIdentifier();
        entry.likelyGateway = entry.hasIds && isCp210x(entry.vendorId, entry.productId);
        entries.append(entry);
    }
    return entries;
}

// ------------------------------------------------------------------ worker thread side

SerialWorker::~SerialWorker() {
    if (port_ != nullptr && port_->isOpen()) port_->close();
}

void SerialWorker::openPort() {
    port_ = new QSerialPort(this);  // created here, so it belongs to the worker thread
    port_->setPortName(portName_);
    port_->setBaudRate(kBaudRate);
    port_->setDataBits(QSerialPort::Data8);
    port_->setParity(QSerialPort::NoParity);
    port_->setStopBits(QSerialPort::OneStop);
    port_->setFlowControl(QSerialPort::NoFlowControl);
    // Bounded read buffer: a gateway flooding the link must not grow memory without limit. The
    // protocol reader discards anything that does not form a valid frame anyway.
    port_->setReadBufferSize(64 * 1024);

    if (!port_->open(QIODevice::ReadWrite)) {
        const QString message = port_->errorString();
        delete port_;
        port_ = nullptr;
        emit errorOccurred(QStringLiteral("%1: %2").arg(portName_, message));
        emit closed(QStringLiteral("open failed: %1").arg(message));
        return;
    }
    // ESP32 DevKit: RTS→EN (reset) and DTR→GPIO0 (boot mode). Keep both released.
    port_->setDataTerminalReady(false);
    port_->setRequestToSend(false);

    connect(port_, &QSerialPort::readyRead, this, [this] {
        if (port_ != nullptr) emit bytesReceived(port_->readAll());
    });
    connect(port_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || port_ == nullptr) return;
        // ResourceError = the device disappeared (USB unplugged, VM took the device).
        if (error == QSerialPort::ResourceError || error == QSerialPort::PermissionError) {
            closePort(QStringLiteral("device removed"));
        } else {
            emit errorOccurred(QStringLiteral("%1: %2").arg(portName_, port_->errorString()));
        }
    });
    emit opened();
}

void SerialWorker::writeBytes(const QByteArray& bytes) {
    if (port_ == nullptr || !port_->isOpen()) return;
    if (port_->write(bytes) != bytes.size()) {
        emit errorOccurred(QStringLiteral("%1: write failed: %2").arg(portName_, port_->errorString()));
    }
}

void SerialWorker::closePort(const QString& reason) {
    if (port_ == nullptr) return;
    QSerialPort* port = std::exchange(port_, nullptr);
    port->disconnect(this);
    // May block in the driver after an unplug – acceptable here, we are not on the GUI thread.
    if (port->isOpen()) port->close();
    port->deleteLater();
    emit closed(reason);
}

// ------------------------------------------------------------------ GUI side

SerialTransport::SerialTransport(QString portName, QObject* parent)
    : Transport(parent), portName_(std::move(portName)) {}

SerialTransport::~SerialTransport() { stopThread(); }

bool SerialTransport::open() {
    if (open_) return true;
    stopThread();  // a previous connection's worker, if any
    thread_ = new QThread;
    thread_->setObjectName(QStringLiteral("serial-%1").arg(portName_));
    worker_ = new SerialWorker(portName_);
    worker_->moveToThread(thread_);
    // The worker is destroyed in its own thread; its destructor closes the port there.
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &SerialWorker::bytesReceived, this, &Transport::bytesReceived);
    connect(worker_, &SerialWorker::errorOccurred, this, &Transport::errorOccurred);
    connect(worker_, &SerialWorker::closed, this, &SerialTransport::onWorkerClosed);
    thread_->start();
    open_ = true;
    QMetaObject::invokeMethod(worker_, &SerialWorker::openPort, Qt::QueuedConnection);
    return true;
}

void SerialTransport::close() {
    if (!open_ || worker_ == nullptr) return;
    QMetaObject::invokeMethod(worker_, [worker = worker_] { worker->closePort(QStringLiteral("closed by user")); },
                              Qt::QueuedConnection);
}

void SerialTransport::write(const QByteArray& bytes) {
    if (!open_ || worker_ == nullptr) return;
    QMetaObject::invokeMethod(worker_, [worker = worker_, bytes] { worker->writeBytes(bytes); }, Qt::QueuedConnection);
}

void SerialTransport::onWorkerClosed(const QString& reason) {
    if (!open_) return;
    open_ = false;
    emit closed(reason);
}

void SerialTransport::stopThread() {
    if (thread_ == nullptr) return;
    if (worker_ != nullptr) worker_->disconnect(this);  // no signals into a transport being torn down
    QThread* thread = std::exchange(thread_, nullptr);
    worker_ = nullptr;
    open_ = false;
    thread->quit();
    if (thread->wait(kStopTimeoutMs)) {
        delete thread;
        return;
    }
    // The worker is stuck in a driver call. Never terminate() (could corrupt driver state): let the
    // thread finish on its own and delete itself then.
    qWarning("serial worker for %s did not stop within %d ms", qPrintable(portName_), kStopTimeoutMs);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    if (thread->isFinished()) delete thread;  // finished between wait() and connect()
}
