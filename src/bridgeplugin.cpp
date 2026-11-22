// SPDX-FileCopyrightText: 2026 zirize
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bridgeplugin.h"

#include <QSet>

#include <KPluginFactory>
#include <formatter/Unit.h>

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

namespace
{
constexpr auto kSysClassNet = "/sys/class/net";
constexpr qint64 kRescanIntervalMs = 10000;

int openCounter(const QString &iface, const char *name)
{
    const QByteArray path = QByteArray(kSysClassNet) + '/' + iface.toLocal8Bit() + "/statistics/" + name;
    return ::open(path.constData(), O_RDONLY | O_CLOEXEC);
}

// sysfs regenerates the attribute on every read at offset 0, so one pread()
// per tick replaces open/read/close and needs no heap allocation.
bool readCounter(int fd, quint64 &out)
{
    if (fd < 0) {
        return false;
    }
    char buf[32];
    ssize_t n;
    do {
        n = ::pread(fd, buf, sizeof(buf) - 1, 0);
    } while (n < 0 && errno == EINTR);
    if (n <= 0) {
        return false;
    }
    quint64 value = 0;
    ssize_t i = 0;
    for (; i < n && buf[i] >= '0' && buf[i] <= '9'; ++i) {
        value = value * 10 + static_cast<quint64>(buf[i] - '0');
    }
    if (i == 0) {
        return false;
    }
    out = value;
    return true;
}

}

BridgeObject::BridgeObject(const QString &iface, KSysGuard::SensorContainer *parent)
    : KSysGuard::SensorObject(iface, iface, parent)
    , m_iface(iface)
{
    reopen();

    auto makeProp = [this, &iface](const QString &id, const QString &name, const QString &shortName,
                                   const QVariant &initial, KSysGuard::Unit unit) {
        auto *prop = new KSysGuard::SensorProperty(id, name, initial, this);
        prop->setShortName(shortName);
        prop->setPrefix(iface);
        prop->setUnit(unit);
        prop->setMin(0);
        // Properties are only refreshed while subscribed, so hand a new
        // subscriber the latest value instead of a stale one.
        connect(prop, &KSysGuard::SensorProperty::subscribedChanged, this, [this, prop](bool subscribed) {
            if (!subscribed) {
                return;
            }
            if (!m_hasBaseline) {
                // First subscriber: read the counters now so totals are not 0.
                update();
            }
            pushValue(prop);
        });
        return prop;
    };

    const QString dlName = QStringLiteral("Download Rate");
    const QString ulName = QStringLiteral("Upload Rate");
    const QString dlShort = QStringLiteral("Download");
    const QString ulShort = QStringLiteral("Upload");

    m_download = makeProp(QStringLiteral("download"), dlName, dlShort, 0.0, KSysGuard::UnitByteRate);
    m_upload = makeProp(QStringLiteral("upload"), ulName, ulShort, 0.0, KSysGuard::UnitByteRate);
    m_downloadBits = makeProp(QStringLiteral("downloadBits"), dlName, dlShort, 0.0, KSysGuard::UnitBitRate);
    m_uploadBits = makeProp(QStringLiteral("uploadBits"), ulName, ulShort, 0.0, KSysGuard::UnitBitRate);
    m_downloadKbps = makeProp(QStringLiteral("downloadKbps"), dlName, dlShort, 0.0, KSysGuard::UnitKiloBitRate);
    m_uploadKbps = makeProp(QStringLiteral("uploadKbps"), ulName, ulShort, 0.0, KSysGuard::UnitKiloBitRate);
    m_downloadMbps = makeProp(QStringLiteral("downloadMbps"), dlName, dlShort, 0.0, KSysGuard::UnitMegaBitRate);
    m_uploadMbps = makeProp(QStringLiteral("uploadMbps"), ulName, ulShort, 0.0, KSysGuard::UnitMegaBitRate);
    m_downloadGbps = makeProp(QStringLiteral("downloadGbps"), dlName, dlShort, 0.0, KSysGuard::UnitGigaBitRate);
    m_uploadGbps = makeProp(QStringLiteral("uploadGbps"), ulName, ulShort, 0.0, KSysGuard::UnitGigaBitRate);
    m_totalDownload = makeProp(QStringLiteral("totalDownload"), QStringLiteral("Total Downloaded"), QStringLiteral("Downloaded"), 0ULL, KSysGuard::UnitByte);
    m_totalUpload = makeProp(QStringLiteral("totalUpload"), QStringLiteral("Total Uploaded"), QStringLiteral("Uploaded"), 0ULL, KSysGuard::UnitByte);

    m_props = {m_download, m_upload, m_downloadBits, m_uploadBits, m_downloadKbps, m_uploadKbps,
               m_downloadMbps, m_uploadMbps, m_downloadGbps, m_uploadGbps, m_totalDownload, m_totalUpload};

    connect(this, &KSysGuard::SensorObject::subscribedChanged, this, [this](bool subscribed) {
        if (!subscribed) {
            resetBaseline();
        }
    });
}

BridgeObject::~BridgeObject()
{
    closeFds();
}

void BridgeObject::closeFds()
{
    if (m_rxFd >= 0) {
        ::close(m_rxFd);
        m_rxFd = -1;
    }
    if (m_txFd >= 0) {
        ::close(m_txFd);
        m_txFd = -1;
    }
}

void BridgeObject::reopen()
{
    closeFds();
    m_rxFd = openCounter(m_iface, "rx_bytes");
    m_txFd = openCounter(m_iface, "tx_bytes");
    m_readFailed = false;
    resetBaseline();
}

void BridgeObject::resetBaseline()
{
    m_hasBaseline = false;
    m_prevRx = 0;
    m_prevTx = 0;
    m_rxRate = 0.0;
    m_txRate = 0.0;
    m_timer.invalidate();
}

void BridgeObject::pushValue(KSysGuard::SensorProperty *prop)
{
    const double rxBits = m_rxRate * 8.0;
    const double txBits = m_txRate * 8.0;
    if (prop == m_totalDownload) {
        prop->setValue(m_prevRx);
    } else if (prop == m_totalUpload) {
        prop->setValue(m_prevTx);
    } else if (prop == m_download) {
        prop->setValue(m_rxRate);
    } else if (prop == m_upload) {
        prop->setValue(m_txRate);
    } else if (prop == m_downloadBits) {
        prop->setValue(rxBits);
    } else if (prop == m_uploadBits) {
        prop->setValue(txBits);
    } else if (prop == m_downloadKbps) {
        prop->setValue(rxBits / 1e3);
    } else if (prop == m_uploadKbps) {
        prop->setValue(txBits / 1e3);
    } else if (prop == m_downloadMbps) {
        prop->setValue(rxBits / 1e6);
    } else if (prop == m_uploadMbps) {
        prop->setValue(txBits / 1e6);
    } else if (prop == m_downloadGbps) {
        prop->setValue(rxBits / 1e9);
    } else if (prop == m_uploadGbps) {
        prop->setValue(txBits / 1e9);
    }
}

bool BridgeObject::update()
{
    if (!isSubscribed()) {
        return true;
    }

    quint64 rx = 0;
    quint64 tx = 0;
    if (!readCounter(m_rxFd, rx) || !readCounter(m_txFd, tx)) {
        m_readFailed = true;
        return false;
    }

    double rxRate = 0.0;
    double txRate = 0.0;
    if (!m_hasBaseline || !m_timer.isValid()) {
        m_timer.start();
        m_hasBaseline = true;
    } else {
        const qint64 elapsedNs = m_timer.nsecsElapsed();
        if (elapsedNs <= 0) {
            return true;
        }
        m_timer.restart();
        const double elapsedSec = static_cast<double>(elapsedNs) / 1e9;
        rxRate = (rx >= m_prevRx) ? static_cast<double>(rx - m_prevRx) / elapsedSec : 0.0;
        txRate = (tx >= m_prevTx) ? static_cast<double>(tx - m_prevTx) / elapsedSec : 0.0;
    }

    m_prevRx = rx;
    m_prevTx = tx;
    m_rxRate = rxRate;
    m_txRate = txRate;

    for (KSysGuard::SensorProperty *prop : m_props) {
        if (prop->isSubscribed()) {
            pushValue(prop);
        }
    }
    return true;
}

BridgePlugin::BridgePlugin(QObject *parent, const QVariantList &args)
    : KSysGuard::SensorPlugin(parent, args)
    , m_container(new KSysGuard::SensorContainer(QStringLiteral("bridge"), QStringLiteral("Bridge"), this))
{
    scanBridges();
}

QString BridgePlugin::providerName() const
{
    return QStringLiteral("bridge");
}

void BridgePlugin::scanBridges()
{
    m_scanTimer.start();
    m_rescanNeeded = false;

    // readdir() + one access() per entry; no QDir/QFileInfo stat calls.
    QSet<QString> current;
    if (DIR *dir = ::opendir(kSysClassNet)) {
        const int dirFd = ::dirfd(dir);
        while (const dirent *entry = ::readdir(dir)) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            const QByteArray bridgePath = QByteArray(entry->d_name) + "/bridge";
            if (::faccessat(dirFd, bridgePath.constData(), F_OK, 0) == 0) {
                current.insert(QString::fromLocal8Bit(entry->d_name));
            }
        }
        ::closedir(dir);
    }

    for (const QString &iface : std::as_const(current)) {
        auto it = m_bridges.constFind(iface);
        if (it == m_bridges.constEnd()) {
            m_bridges.insert(iface, new BridgeObject(iface, m_container));
        } else if (it.value()->readFailed()) {
            // Bridge recreated under the same name, or a transient read error.
            it.value()->reopen();
        }
    }

    for (auto it = m_bridges.begin(); it != m_bridges.end();) {
        if (!current.contains(it.key())) {
            m_container->removeObject(it.value());
            delete it.value();
            it = m_bridges.erase(it);
        } else {
            ++it;
        }
    }
}

void BridgePlugin::update()
{
    if (m_rescanNeeded || !m_scanTimer.isValid() || m_scanTimer.hasExpired(kRescanIntervalMs)) {
        scanBridges();
    }
    for (BridgeObject *bridge : std::as_const(m_bridges)) {
        if (!bridge->update()) {
            m_rescanNeeded = true;
        }
    }
}

K_PLUGIN_FACTORY_WITH_JSON(BridgePluginFactory, "metadata.json", registerPlugin<BridgePlugin>();)

#include "bridgeplugin.moc"
