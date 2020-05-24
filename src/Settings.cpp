/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#include "Settings.h"
#include "BackendRequest.h"
#include "uri.h"
#include <kodi/General.h>
#include <kodi/util/XMLUtils.h>
#include <p8-platform/util/StringUtils.h>
#include <tinyxml.h>

using namespace std;

using namespace NextPVR;

/***************************************************************************
 * PVR settings
 **************************************************************************/
void Settings::ReadFromAddon()
{
  std::string buffer;

  /* Connection settings */
  /***********************/
  if (kodi::CheckSettingString("host", m_hostname))
  {
    uri::decode(m_hostname);
  }
  else
  {
    m_hostname = DEFAULT_HOST;
  }

  /* Read setting "port" from settings.xml */
  if (!kodi::CheckSettingInt("port", m_port))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'port' setting, falling back to '8866' as default");
    m_port = DEFAULT_PORT;
  }

  /* Read setting "pin" from settings.xml */

  if (!kodi::CheckSettingString("pin", m_PIN))
    m_PIN = DEFAULT_PIN;

  kodi::CheckSettingString("host_mac", m_hostMACAddress);

  if (m_hostMACAddress.empty())
    m_enableWOL = false;
  else if (!kodi::CheckSettingBoolean("wolenable", m_enableWOL))
    m_enableWOL = false;
  else if (m_hostname == "127.0.0.1" || m_hostname == "localhost" || m_hostname == "::1")
    m_enableWOL = false;

  if (!kodi::CheckSettingInt("woltimeout", m_timeoutWOL))
    m_timeoutWOL = 20;

  if (!kodi::CheckSettingBoolean("guideartwork", m_downloadGuideArtwork))
    m_downloadGuideArtwork = DEFAULT_GUIDE_ARTWORK;

  if (!kodi::CheckSettingBoolean("remoteaccess", m_remoteAccess))
    m_remoteAccess = false;

  if (!kodi::CheckSettingBoolean("flattenrecording", m_flattenRecording))
    m_flattenRecording = false;

  if (!kodi::CheckSettingBoolean("kodilook", m_kodiLook))
    m_kodiLook = false;

  if (!kodi::CheckSettingInt("prebuffer", m_prebuffer))
    m_prebuffer = 8;

  if (!kodi::CheckSettingInt("prebuffer5", m_prebuffer5))
    m_prebuffer5 = 0;

  if (!kodi::CheckSettingInt("chunklivetv", m_liveChunkSize))
    m_liveChunkSize = 64;

  if (!kodi::CheckSettingInt("chunkrecording", m_chunkRecording))
    m_chunkRecording = 32;

  if (!kodi::CheckSettingString("resolution",  m_resolution))
    m_resolution = "720";

  if (!kodi::CheckSettingBoolean("showradio", m_showRadio))
    m_showRadio = true;

  /* Log the current settings for debugging purposes */
  kodi::Log(ADDON_LOG_DEBUG, "settings: host='%s', port=%i, mac=%4.4s...", m_hostname.c_str(), m_port, m_hostMACAddress.c_str());

}

ADDON_STATUS Settings::ReadBackendSettings()
{
  // check server version
  std::string settings;
  Request& request = Request::GetInstance();
  if (request.DoRequest("/service?method=setting.list", settings) == HTTP_OK)
  {
    TiXmlDocument settingsDoc;
    if (settingsDoc.Parse(settings.c_str()) != nullptr)
    {
      //dump_to_log(&settingsDoc, 0);
      if (XMLUtils::GetInt(settingsDoc.RootElement(), "NextPVRVersion", m_backendVersion))
      {
        // NextPVR server
        kodi::Log(ADDON_LOG_INFO, "NextPVR version: %d", m_backendVersion);

        // is the server new enough
        if (m_backendVersion < 40204)
        {
          kodi::Log(ADDON_LOG_ERROR, "NextPVR version '%d' is too old. Please upgrade to '%s' or higher!", m_backendVersion, NEXTPVRC_MIN_VERSION_STRING);
          kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30050), StringUtils::Format(kodi::GetLocalizedString(30051).c_str(), NEXTPVRC_MIN_VERSION_STRING));
          return ADDON_STATUS_PERMANENT_FAILURE;
        }
      }

      // load padding defaults
      m_defaultPrePadding = 1;
      XMLUtils::GetInt(settingsDoc.RootElement(), "PrePadding", m_defaultPrePadding);

      m_defaultPostPadding = 2;
      XMLUtils::GetInt(settingsDoc.RootElement(), "PostPadding", m_defaultPostPadding);

      m_showNew = false;
      XMLUtils::GetBoolean(settingsDoc.RootElement(), "ShowNewInGuide", m_showNew);

      std::string recordingDirectories;
      if (XMLUtils::GetString(settingsDoc.RootElement(), "RecordingDirectories", recordingDirectories))
      {
        m_recordingDirectories = StringUtils::Split(recordingDirectories, ",", 0);
      }

      int serverTimestamp;
      if (XMLUtils::GetInt(settingsDoc.RootElement(), "TimeEpoch", serverTimestamp))
      {
        m_serverTimeOffset = time(nullptr) - serverTimestamp;
        kodi::Log(ADDON_LOG_INFO, "Server time offset in seconds: %d", m_serverTimeOffset);
      }

      if (XMLUtils::GetInt(settingsDoc.RootElement(), "SlipSeconds", m_timeshiftBufferSeconds))
        kodi::Log(ADDON_LOG_INFO, "time shift buffer in seconds == %d\n", m_timeshiftBufferSeconds);

      std::string serverMac;
      if (XMLUtils::GetString(settingsDoc.RootElement(), "ServerMAC", serverMac))
      {
        std::string macAddress = serverMac.substr(0,2) ;
        for (int i = 2; i < 12; i+=2)
        {
          macAddress+= ":" + serverMac.substr(i,2);
        }
        kodi::Log(ADDON_LOG_DEBUG, "Server MAC address %4.4s...", macAddress.c_str());
        if (m_hostMACAddress != macAddress)
        {
          kodi::SetSettingString("host_mac", macAddress);
        }
      }
    }
  }
  return ADDON_STATUS_OK;
}

void Settings::SetVersionSpecificSettings()
{
  m_liveStreamingMethod = DEFAULT_LIVE_STREAM;
  int eStream;
  if (kodi::CheckSettingInt("livestreamingmethod", eStream))
  {
    m_liveStreamingMethod = static_cast<eStreamingMethod>(eStream);
    // has v4 setting
    if (m_backendVersion < 50000)
    {
      // previous Matrix clients had a transcoding option
      if (m_liveStreamingMethod == eStreamingMethod::Transcoded)
      {
        m_liveStreamingMethod = eStreamingMethod::RealTime;
        kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30050), StringUtils::Format(kodi::GetLocalizedString(30051).c_str(), "5"));
      }
    }
    else if (m_backendVersion < 50002)
    {
      m_liveStreamingMethod = eStreamingMethod::RealTime;
      kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30050), StringUtils::Format(kodi::GetLocalizedString(30051).c_str(), "5.0.3"));
    }
    else
    {
      // check for new v5 setting with no settings.xml
      eStreamingMethod oldMethod = m_liveStreamingMethod;
      if (kodi::CheckSettingInt("livestreamingmethod5", eStream))
        m_liveStreamingMethod = static_cast<eStreamingMethod>(eStream);

      if (m_liveStreamingMethod == eStreamingMethod::Default)
        m_liveStreamingMethod = oldMethod;

      if (m_liveStreamingMethod == RollingFile || m_liveStreamingMethod == Timeshift)
        m_liveStreamingMethod = eStreamingMethod::ClientTimeshift;

    }
  }

  if (m_backendVersion >= 50000)
  {
    m_sendSidWithMetadata = false;
    bool remote;
    if (m_PIN != "0000" && m_remoteAccess)
    {
      m_downloadGuideArtwork = false;
      m_sendSidWithMetadata = true;
    }

    if (!kodi::CheckSettingBoolean("guideartworkportrait", m_guideArtPortrait))
      m_guideArtPortrait = false;

    if (!kodi::CheckSettingBoolean("recordingsize", m_showRecordingSize))
      m_showRecordingSize = false;
  }
  else
  {
    m_sendSidWithMetadata = true;
    m_showRecordingSize = false;
  }
}

bool Settings::SaveSettings(std::string name, std::string value)
{
  bool found = false;
  TiXmlDocument doc;

  std::string settings = kodi::vfs::TranslateSpecialProtocol("special://profile/addon_data/pvr.nextpvr/settings.xml");
  if (doc.LoadFile(settings))
  {
    //Get Root Node
    TiXmlElement* rootNode = doc.FirstChildElement("settings");
    if (rootNode)
    {
      TiXmlElement* childNode;
      std::string key_value;
      for (childNode = rootNode->FirstChildElement("setting"); childNode; childNode = childNode->NextSiblingElement())
      {
        if (childNode->QueryStringAttribute("id", &key_value) == TIXML_SUCCESS)
        {
          if (key_value == name)
          {
            if (childNode->FirstChild() != nullptr)
            {
              childNode->FirstChild()->SetValue(value);
              found = true;
              break;
            }
            return false;
          }
        }
      }
      if (found == false)
      {
        TiXmlElement* newSetting = new TiXmlElement("setting");
        TiXmlText* newvalue = new TiXmlText(value);
        newSetting->SetAttribute("id", name);
        newSetting->LinkEndChild(newvalue);
        rootNode->LinkEndChild(newSetting);
      }
      doc.SaveFile(settings);
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "Error loading settings.xml %s", settings.c_str());
  }
  return true;
}



ADDON_STATUS Settings::SetValue(const std::string& settingName, const kodi::CSettingValue& settingValue)
{
  //Connection
  if (g_pvrclient==nullptr)
  {
    // Don't want to cause a restart after the first time discovery
    return ADDON_STATUS_OK;
  }
  if (settingName == "host")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_hostname, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "port")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_port, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "pin")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_PIN, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "remoteaccess")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_remoteAccess, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "showradio")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_showRadio, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "guideartwork")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_downloadGuideArtwork, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "guideartworkportrait")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_guideArtPortrait, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "recordingsize")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_showRecordingSize, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "flattenrecording")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_flattenRecording, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "kodilook")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_kodiLook, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "host_mac")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_hostMACAddress, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "livestreamingmethod" && m_backendVersion < 50000)
    return SetSetting<eStreamingMethod, ADDON_STATUS>(settingName, settingValue, m_liveStreamingMethod, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "livestreamingmethod5" && m_backendVersion >= 50000 && static_cast <const eStreamingMethod>(settingValue.GetInt()) != eStreamingMethod::Default)
    return SetSetting<eStreamingMethod, ADDON_STATUS>(settingName, settingValue, m_liveStreamingMethod, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "prebuffer")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_prebuffer, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "prebuffer5")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_prebuffer5, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "chucksize")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_liveChunkSize, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "chuckrecordings")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_chunkRecording, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "resolution")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_resolution, ADDON_STATUS_OK, ADDON_STATUS_OK);
  return ADDON_STATUS_OK;
}
