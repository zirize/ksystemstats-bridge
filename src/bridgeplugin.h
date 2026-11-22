// SPDX-FileCopyrightText: 2026 zirize
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QString>

#include <systemstats/SensorContainer.h>
#include <systemstats/SensorObject.h>
#include <systemstats/SensorPlugin.h>
#include <systemstats/SensorProperty.h>

class BridgeObject : public KSysGuard::SensorObject
{
    Q_OBJECT

public:
    BridgeObject(const QString &iface, KSysGuard::SensorContainer *parent);
    ~BridgeObject() override;

    // Returns false when the counters could not be read (interface likely gone).
    bool update();
    // Re-open the counter files after a failed read.
    void reopen();
    bool readFailed() const { return m_readFailed; }

private:
    void closeFds();
    void pushValue(KSysGuard::SensorProperty *prop);
    void resetBaseline();

    QString m_iface;
    // Counter files are kept open and re-read with pread() at offset 0.
    int m_rxFd = -1;
    int m_txFd = -1;
    bool m_readFailed = false;

    KSysGuard::SensorProperty *m_download = nullptr;
    KSysGuard::SensorProperty *m_upload = nullptr;
    KSysGuard::SensorProperty *m_downloadBits = nullptr;
    KSysGuard::SensorProperty *m_uploadBits = nullptr;
    KSysGuard::SensorProperty *m_downloadKbps = nullptr;
    KSysGuard::SensorProperty *m_uploadKbps = nullptr;
    KSysGuard::SensorProperty *m_downloadMbps = nullptr;
    KSysGuard::SensorProperty *m_uploadMbps = nullptr;
    KSysGuard::SensorProperty *m_downloadGbps = nullptr;
    KSysGuard::SensorProperty *m_uploadGbps = nullptr;
    KSysGuard::SensorProperty *m_totalDownload = nullptr;
    KSysGuard::SensorProperty *m_totalUpload = nullptr;

    bool m_hasBaseline = false;
    quint64 m_prevRx = 0;
    quint64 m_prevTx = 0;
    double m_rxRate = 0.0;
    double m_txRate = 0.0;
    QList<KSysGuard::SensorProperty *> m_props;
    QElapsedTimer m_timer;
};

class BridgePlugin : public KSysGuard::SensorPlugin
{
    Q_OBJECT

public:
    BridgePlugin(QObject *parent, const QVariantList &args);

    QString providerName() const override;
    void update() override;

private:
    void scanBridges();

    KSysGuard::SensorContainer *m_container = nullptr;
    QHash<QString, BridgeObject *> m_bridges;
    // Bridges are rarely added or removed, so /sys/class/net is rescanned only
    // every kRescanIntervalMs, or right after a counter read fails.
    QElapsedTimer m_scanTimer;
    bool m_rescanNeeded = false;
};
