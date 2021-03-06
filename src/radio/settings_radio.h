#pragma once

#include <QObject>
#include <QHash>

#include "ts_context_menu_qt.h"
#include "config_radio.h"
#include "mod_radio.h"
#include "ts_infodata_qt.h"

class SettingsRadio : public QObject, public InfoDataInterface, public ContextMenuInterface
{
    Q_OBJECT
    Q_INTERFACES(InfoDataInterface ContextMenuInterface)

public:
    static SettingsRadio* instance()
    {
        static QMutex mutex;
        if(!m_Instance)
        {
            mutex.lock();

            if(!m_Instance)
                m_Instance = new SettingsRadio;

            mutex.unlock();
        }
        return m_Instance;
    }

    static void drop()
    {
        static QMutex mutex;
        mutex.lock();
        delete m_Instance;
        m_Instance = nullptr;
        mutex.unlock();
    }
    
    void Init(Radio *radio);
    bool onInfoDataChanged(uint64 serverConnectionHandlerID, uint64 id, enum PluginItemType type, uint64 mine, QTextStream &data);

signals:
    void EnabledSet(QString,bool);
    void InLoFreqSet(QString,double);
    void InHiFreqSet(QString,double);
    void DestructionSet(QString,double);
    void RingModFrequencySet(QString,double);
    void RingModMixSet(QString,double);
    void OutLoFreqSet(QString,double);
    void OutHiFreqSet(QString,double);

    void ToggleClientBlacklisted(uint64,anyID);

    void settingsSave();

public slots:
    void onContextMenuEvent(uint64 serverConnectionHandlerID, PluginMenuType type, int menuItemID, uint64 selectedItemID);

private slots:
    void saveSettings(int);
    void on_channel_settings_finished(int r, QString setting_id);

private:
    explicit SettingsRadio();
    ~SettingsRadio() = default;
    static SettingsRadio* m_Instance;
    SettingsRadio(const SettingsRadio &);
    SettingsRadio& operator=(const SettingsRadio &);

    int m_ContextMenuUi = -1;
    int m_ContextMenuChannelUi = -1;
    int m_ContextMenuToggleClientBlacklisted = -1;
    QPointer<ConfigRadio> config;

    QPointer<Radio> mP_radio;

    QHash<QPair<uint64,uint64>,QPointer<ConfigRadio> > m_channel_configs;
};
