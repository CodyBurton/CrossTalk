#include "snt.h"

#include "public_errors.h"
#include "public_errors_rare.h"
#include "public_definitions.h"
#include "public_rare_definitions.h"
#include "ts3_functions.h"

#include "plugin.h"

//! Constructor
/*!
 * \brief SnT::SnT Creates an instance of this class
 * \param parent optional Qt Object
 */
SnT::SnT(QObject *parent) :
    QObject(parent)
{
    ts = TSFunctions::instance();
}

//! When the input hardware gets activated, check if there's a Ptt activation scheduled and if so, turn Ptt on
/*!
 * \brief SnT::onClientSelfVariableUpdateEvent equivalent TS Client SDK
 * \param serverConnectionHandlerID equivalent TS Client SDK
 * \param flag equivalent TS Client SDK
 * \param oldValue equivalent TS Client SDK
 * \param newValue equivalent TS Client SDK
 */
void SnT::onClientSelfVariableUpdateEvent(uint64 serverConnectionHandlerID, int flag, const char *oldValue, const char *newValue)
{
    if ((flag == CLIENT_INPUT_HARDWARE) && (strcmp (newValue,"1") == 0))
    {
        if (m_shallActivatePtt==true)
        {
            ts->SetPushToTalk(serverConnectionHandlerID, PTT_ACTIVATE);
            m_shallActivatePtt=false;
        }
        // seems to be equal to a ts3plugin_currentServerConnectionChanged WHEN initiated by the user
        // except that this version will fire when returning from a cross server ptt event
        // (ts3plugin_currentServerConnectionChanged is not even fired when ptt-switching for whatever reason)
        // Furtunately, we like that here.
    }
}

//! Parse Plugin Commands for this module
/*!
 * \brief SnT::ParseCommand Qt Slot
 * \param serverConnectionHandlerID the connection handler forwarded from the API
 * \param cmd the command
 * \param arg arguments (if any)
 */
void SnT::ParseCommand(uint64 serverConnectionHandlerID, QString cmd, QStringList args)
{
    unsigned int error = ERROR_ok;

    // Get the active server
    uint64 scHandlerID = ts->GetActiveServerConnectionHandlerID();
    if(scHandlerID == NULL)
    {
        ts3Functions.logMessage("Failed to get an active server, falling back to current server", LogLevel_DEBUG, PLUGIN_NAME, 0);
        scHandlerID = ts3Functions.getCurrentServerConnectionHandlerID();
        if(scHandlerID == NULL) return;
    }

    int status;
    if((error = ts3Functions.getConnectionStatus(scHandlerID, &status)) != ERROR_ok)
        ts->Error(scHandlerID,error,"Error retrieving connection status: ");

    /***** Communication *****/
    if(cmd == "TS3_PTT_ACTIVATE")
    {
        if(status != STATUS_DISCONNECTED)
            ts->SetPushToTalk(scHandlerID, PTT_ACTIVATE);
    }
    else if(cmd == "TS3_PTT_DEACTIVATE")
    {
        if(status != STATUS_DISCONNECTED)
            ts->SetPushToTalk(scHandlerID, PTT_DEACTIVATE);
    }
    else if(cmd == "TS3_PTT_TOGGLE")
    {
        if(status != STATUS_DISCONNECTED)
            ts->SetPushToTalk(scHandlerID, PTT_TOGGLE);
    }
    else if((cmd == "TS3_SWITCH_N_TALK_END") || (cmd == "TS3_NEXT_TAB_AND_TALK_END") || (cmd == "TS3_NEXT_TAB_AND_WHISPER_END")) // universal OnKeyUp Handler
    {
        if(status != STATUS_DISCONNECTED)
        {
            ts->SetPushToTalk(scHandlerID, false); //always do immediately regardless of delay settings
            if (m_shallClearWhisper)
            {
                if ((error = ts3Functions.requestClientSetWhisperList(scHandlerID,NULL,NULL,NULL,NULL)) != ERROR_ok)
                    ts->Error(scHandlerID,error, "Could not release whisperlist.");
                else
                    m_shallClearWhisper = false;
            }
        }
        if(m_returnToSCHandler != (uint64)NULL)
        {
            ts->SetActiveServer(m_returnToSCHandler);
            if((cmd == "TS3_NEXT_TAB_AND_TALK_END") || (cmd == "TS3_NEXT_TAB_AND_WHISPER_END")) // annoy user to upgrade
            {
                ts->Error(m_returnToSCHandler,NULL, "Hotkeys Next Tab and Talk Stop & Next Tab and Whisper Stop are being DEPRECATED! Please use SnT Stop instead!");
            }
            m_returnToSCHandler = (uint64)NULL;
        }
    }
    else if(cmd == "TS3_NEXT_TAB_AND_TALK_START")
    {
        uint64 nextServer;
        if ((error = ts->GetActiveServerRelative(scHandlerID,true,&nextServer)) != ERROR_ok)
            ts->Error(scHandlerID,error,"Could not get next server.");

        if (scHandlerID == nextServer)
            return;

        if(status != STATUS_DISCONNECTED)
            ts->SetPushToTalk(scHandlerID, false); //always do immediately regardless of delay settings; maybe not as necessary as below

        m_shallActivatePtt=true;
        m_returnToSCHandler=scHandlerID;
        ts->SetActiveServer(nextServer);
    }
    else if(cmd == "TS3_SWITCH_TAB_AND_TALK_START")
    {
        if (args.isEmpty())
        {
            ts->Error(serverConnectionHandlerID,NULL,"No target server specified.");
            return;
        }

        uint64 targetServer;
        if ((error = ts->GetServerHandler(args.at(0),&targetServer)) != ERROR_ok)
        {
            ts->Error(scHandlerID,error,"Could not find target server");
            return;
        }

        if(status != STATUS_DISCONNECTED)
            ts->SetPushToTalk(scHandlerID, false); //always do immediately regardless of delay settings; maybe not as necessary as below

        if (scHandlerID != targetServer)
        {
            m_shallActivatePtt=true;
            m_returnToSCHandler=scHandlerID;
            ts->SetActiveServer(targetServer);
        }
        else
            ts->SetPushToTalk(scHandlerID,true);

    }
    else if (cmd == "TS3_SWITCH_TAB_AND_WHISPER_START")
    {
        if (args.count() < 3)
        {
            ts->Error(scHandlerID,NULL,"Too few arguments.");
            return;
        }

        uint64 targetServer;
        if ((error = ts->GetServerHandler(args.at(0),&targetServer)) != ERROR_ok)
        {
            ts->Error(scHandlerID,error,"Could not get target server.");
            return;
        }

        anyID myID;
        if((error = ts->GetClientId(targetServer,&myID)) != ERROR_ok)
        {
            ts->Error(scHandlerID,error, "Could not get my id on the target server.");
            return;
        }

        QString arg_qs;
        arg_qs = args.at(1);
        GroupWhisperType groupWhisperType = GROUPWHISPERTYPE_ENDMARKER;
        if (arg_qs.contains("COMMANDER",Qt::CaseInsensitive))
                groupWhisperType = GROUPWHISPERTYPE_CHANNELCOMMANDER;
        else if (arg_qs.contains("ALLC",Qt::CaseInsensitive))
                groupWhisperType = GROUPWHISPERTYPE_ALLCLIENTS;

        if (groupWhisperType == GROUPWHISPERTYPE_ENDMARKER)
        {
            ts->Error(scHandlerID,NULL,"Unsupported group whisper type.");
            return;
        }

        arg_qs = args.at(2);
        GroupWhisperTargetMode groupWhisperTargetMode  = GROUPWHISPERTARGETMODE_ENDMARKER;
        if (arg_qs.contains("ALLPARENT"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_ALLPARENTCHANNELS;
        else if (arg_qs.contains("PARENT"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_PARENTCHANNEL;
        else if (arg_qs.contains("CURRENT"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_CURRENTCHANNEL;
        else if (arg_qs.contains("SUB"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_SUBCHANNELS;
        else if (arg_qs.contains("ANCESTORCHANNELFAMILY"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_ANCESTORCHANNELFAMILY;
        else if (arg_qs.contains("FAMILY"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_CHANNELFAMILY;
        else if (arg_qs.contains("ALL"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_ALL;

        if (groupWhisperTargetMode == GROUPWHISPERTARGETMODE_ENDMARKER)
        {
            ts->Error(scHandlerID,NULL,"Could not recognize group whisper target mode.");
            return;
        }
        else if (groupWhisperTargetMode == GROUPWHISPERTARGETMODE_ANCESTORCHANNELFAMILY)
        {
            ts->Error(scHandlerID,NULL,"The group whisper target mode ANCESTORCHANNELFAMILY is not supported.");
            return;
        }

        if ((error = ts->SetWhisperList(targetServer,groupWhisperType,groupWhisperTargetMode)) != ERROR_ok)
        {
            ts->Error(scHandlerID,error, "Could not set whisperlist: ");
            return;
        }

        if(status != STATUS_DISCONNECTED)
            ts->SetPushToTalk(scHandlerID, false); //always do immediately regardless of delay settings; maybe not as necessary as below

        m_shallClearWhisper = true;
        if (scHandlerID != targetServer)
        {
            m_shallActivatePtt=true;
            m_returnToSCHandler=scHandlerID;
            ts->SetActiveServer(targetServer);
        }
        else
            ts->SetPushToTalk(scHandlerID,true);
    }
    else if (cmd == "TS3_NEXT_TAB_AND_WHISPER_ALL_CC_START")
    {
        uint64 nextServer;
        if ((error = ts->GetActiveServerRelative(scHandlerID,true,&nextServer)) != ERROR_ok)
            ts->Error(scHandlerID,error,"Could not get next server.");

        if (scHandlerID == nextServer)
            return;

        anyID myID;
        if((error = ts->GetClientId(nextServer,&myID)) != ERROR_ok)
        {
            ts->Error(scHandlerID,error, "Could not get my id on the next server: ");
            return;
        }

        if ((error = ts->SetWhisperList(nextServer,GROUPWHISPERTYPE_CHANNELCOMMANDER,GROUPWHISPERTARGETMODE_ALL)) != ERROR_ok)
        {
            ts->Error(scHandlerID,error, "Could not set whisperlist: ");
            return;
        }

        if(status != STATUS_DISCONNECTED)
            ts->SetPushToTalk(scHandlerID, false); //always do immediately regardless of delay settings; maybe not as necessary as below

        m_shallActivatePtt=true;
        m_returnToSCHandler=scHandlerID;
        m_shallClearWhisper = true;
        ts->SetActiveServer(nextServer);
    }
    else if (cmd == "TS3_NEXT_TAB_AND_WHISPER_START")
    {
        uint64 nextServer;
        if ((error = ts->GetActiveServerRelative(scHandlerID,true,&nextServer)) != ERROR_ok)
            ts->Error(scHandlerID,error,"Could not get next server.");

        if (scHandlerID == nextServer)
            return;

        anyID myID;
        if((error = ts->GetClientId(nextServer,&myID)) != ERROR_ok)
        {
            ts->Error(scHandlerID,error, "Could not get my id on the next server: ");
            return;
        }

        if (args.count() < 2)
        {
            ts->Error(scHandlerID,NULL,"Too few arguments.");
            return;
        }

        QString arg_qs;
        arg_qs = args.at(0);
        GroupWhisperType groupWhisperType = GROUPWHISPERTYPE_ENDMARKER;
        if (arg_qs.contains("COMMANDER",Qt::CaseInsensitive))
                groupWhisperType = GROUPWHISPERTYPE_CHANNELCOMMANDER;
        else if (arg_qs.contains("ALLC",Qt::CaseInsensitive))
                groupWhisperType = GROUPWHISPERTYPE_ALLCLIENTS;

        if (groupWhisperType == GROUPWHISPERTYPE_ENDMARKER)
        {
            ts->Error(scHandlerID,NULL,"Unsupported group whisper type.");
            return;
        }

        arg_qs = args.at(1);
        GroupWhisperTargetMode groupWhisperTargetMode  = GROUPWHISPERTARGETMODE_ENDMARKER;
        if (arg_qs.contains("ALLPARENT"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_ALLPARENTCHANNELS;
        else if (arg_qs.contains("PARENT"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_PARENTCHANNEL;
        else if (arg_qs.contains("CURRENT"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_CURRENTCHANNEL;
        else if (arg_qs.contains("SUB"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_SUBCHANNELS;
        else if (arg_qs.contains("ANCESTORCHANNELFAMILY"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_ANCESTORCHANNELFAMILY;
        else if (arg_qs.contains("FAMILY"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_CHANNELFAMILY;
        else if (arg_qs.contains("ALL"))
            groupWhisperTargetMode = GROUPWHISPERTARGETMODE_ALL;

        if (groupWhisperTargetMode == GROUPWHISPERTARGETMODE_ENDMARKER)
        {
            ts->Error(scHandlerID,NULL,"Could not recognize group whisper target mode.");
            return;
        }
        else if (groupWhisperTargetMode == GROUPWHISPERTARGETMODE_ANCESTORCHANNELFAMILY)
        {
            ts->Error(scHandlerID,NULL,"The group whisper target mode ANCESTORCHANNELFAMILY is not supported.");
            return;
        }

        if ((error = ts->SetWhisperList(nextServer,groupWhisperType,groupWhisperTargetMode)) != ERROR_ok)
        {
            ts->Error(scHandlerID,error, "Could not set whisperlist: ");
            return;
        }

        if(status != STATUS_DISCONNECTED)
            ts->SetPushToTalk(scHandlerID, false); //always do immediately regardless of delay settings; maybe not as necessary as below

        m_shallActivatePtt=true;
        m_returnToSCHandler=scHandlerID;
        m_shallClearWhisper = true;
        ts->SetActiveServer(nextServer);
    }
}

//! Turn Ptt off delayed by the user client setting
/*!
 * \brief SnT::PttDelayFinished Timer slot
 */
void SnT::PttDelayFinished()
{
    // Turn off PTT
    ts->SetPushToTalk(ts->GetActiveServerConnectionHandlerID(), false);
}