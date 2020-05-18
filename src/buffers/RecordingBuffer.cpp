/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "RecordingBuffer.h"

using namespace timeshift;

PVR_ERROR RecordingBuffer::GetStreamTimes(kodi::addon::PVRStreamTimes& stimes)
{
  stimes.SetStartTime(m_startTime);
  stimes.SetPTSStart(0);
  stimes.SetPTSBegin(0);
  stimes.SetPTSEnd(((int64_t)Duration()) * DVD_TIME_BASE);
  return PVR_ERROR_NO_ERROR;
}

int RecordingBuffer::Duration(void)
{
  if (m_recordingTime)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    time_t endTime =  time(nullptr);
    int diff = (int) (endTime - m_recordingTime -10);
    if (diff > 0)
      {
      int64_t bps = m_inputHandle.GetLength() / diff;
      if ((m_inputHandle.GetLength() - m_inputHandle.GetPosition()) * bps < 10)
      {
        m_isLive = false;
      }
      else
      {
        m_isLive = true;
      }
      return diff;
    }
    else
    {
      m_isLive = false;
      return 0;
    }
  }
  else
  {
    return m_Duration;
  }
}

bool RecordingBuffer::Open(const std::string inputUrl, const kodi::addon::PVRRecording& recording)
{
  m_Duration = recording.GetDuration();

  kodi::Log(ADDON_LOG_DEBUG, "RecordingBuffer::Open In Progress %d %lld", recording.GetDuration(), recording.GetRecordingTime());
  if (recording.GetDuration() + recording.GetRecordingTime() > time(nullptr))
  {
    m_recordingTime = recording.GetRecordingTime() + m_settings.m_serverTimeOffset;
    m_isLive = true;
  }
  else
  {
    m_recordingTime = 0;
    m_isLive = false;
  }
  m_recordingURL = inputUrl;
  if (!recording.GetDirectory().empty())
  {
    std::string kodiDirectory = recording.GetDirectory();
    StringUtils::Replace(kodiDirectory, '\\', '/');
    if (StringUtils::StartsWith(kodiDirectory, "//"))
    {
      kodiDirectory = "smb:" + kodiDirectory;
    }
    if ( kodi::vfs::FileExists(kodiDirectory,false))
    {
      m_recordingURL = kodiDirectory;
    }
  }
  return Buffer::Open(m_recordingURL,0);
}

int RecordingBuffer::Read(byte *buffer, size_t length)
{
  if (m_recordingTime)
    std::unique_lock<std::mutex> lock(m_mutex);
  int dataRead = (int) m_inputHandle.Read(buffer, length);
  if (dataRead == 0 && m_isLive)
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %lld %lld", __FUNCTION__, __LINE__, m_inputHandle.GetLength() ,m_inputHandle.GetPosition());
    int64_t position = m_inputHandle.GetPosition();
    Buffer::Close();
    Buffer::Open(m_recordingURL,0);
    Seek(position,0);
    dataRead = (int) m_inputHandle.Read(buffer, length);
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %lld %lld", __FUNCTION__, __LINE__, m_inputHandle.GetLength() ,m_inputHandle.GetPosition());
  }
  return dataRead;
}
