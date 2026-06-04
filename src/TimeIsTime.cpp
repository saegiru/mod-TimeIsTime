/*
 * Azerothcore Module:     TimeIsTime
 * Author:                 Dunjeon
 * Contributing Author(s): lasyan3, vratam @ RegWorks
 * Version:                20250608
 * License:                GNU Affero General Public License v3.0.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"
#include "GameTime.h"
#include "Log.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>

namespace
{
    constexpr float DefaultSpeedRate = 1.0f;
    constexpr float MinSpeedRate = 0.001f;
    constexpr float MaxSpeedRate = 240.0f;
    constexpr float MinHourOffset = -24.0f;
    constexpr float MaxHourOffset = 24.0f;
    constexpr float ClientBaseTimeSpeed = 0.01666667f;
    constexpr int64 SecondsPerDay = 24 * 60 * 60;

    bool TimeIsTimeEnable = true;
    bool TimeIsTimeAnnounce = true;
    float TimeIsTimeSpeedRate = DefaultSpeedRate;
    float TimeIsTimeHourOffset = 0.0f;
    uint32 TimeIsTimeTimeStart = 0;

    float ValidateFloat(char const* configName, float value, float defaultValue, float minValue, float maxValue)
    {
        if (!std::isfinite(value))
        {
            LOG_ERROR("module.timeistime", "{} must be finite. Using {}.", configName, defaultValue);
            return defaultValue;
        }

        if (value < minValue || value > maxValue)
        {
            float clampedValue = std::clamp(value, minValue, maxValue);
            LOG_ERROR("module.timeistime", "{}={} is outside the supported range [{}, {}]. Using {}.", configName, value, minValue, maxValue, clampedValue);
            return clampedValue;
        }

        return value;
    }

    uint32 GetTimeStartConfig()
    {
        std::string const value = sConfigMgr->GetOption<std::string>("TimeIsTime.TimeStart", "0");

        try
        {
            size_t parsedChars = 0;
            int64 const parsedValue = std::stoll(value, &parsedChars);

            if (parsedChars != value.length() || parsedValue < 0 || parsedValue > static_cast<int64>(std::numeric_limits<uint32>::max()))
            {
                LOG_ERROR("module.timeistime", "TimeIsTime.TimeStart={} is not a valid unsigned 32-bit Unix timestamp. Using 0.", value);
                return 0;
            }

            return static_cast<uint32>(parsedValue);
        }
        catch (std::exception const&)
        {
            LOG_ERROR("module.timeistime", "TimeIsTime.TimeStart={} is not a valid unsigned 32-bit Unix timestamp. Using 0.", value);
            return 0;
        }
    }

    int64 PositiveModulo(int64 value, int64 divisor)
    {
        int64 result = value % divisor;
        return result < 0 ? result + divisor : result;
    }

    int64 GetClockAnchor()
    {
        if (TimeIsTimeTimeStart)
            return static_cast<int64>(TimeIsTimeTimeStart);

        return 0;
    }

    uint32 GetAdjustedGameTime()
    {
        int64 const now = GameTime::GetGameTime().count();
        int64 const anchor = GetClockAnchor();
        int64 const today = now - PositiveModulo(now, SecondsPerDay);
        double const elapsed = static_cast<double>(now - anchor);
        double const phase = static_cast<double>(PositiveModulo(anchor, SecondsPerDay)) + (elapsed * TimeIsTimeSpeedRate) + (static_cast<double>(TimeIsTimeHourOffset) * 3600.0);
        int64 const secondsOfDay = PositiveModulo(static_cast<int64>(std::floor(phase)), SecondsPerDay);
        int64 const adjusted = today + secondsOfDay;

        if (adjusted <= 0)
            return 0;

        if (adjusted >= static_cast<int64>(std::numeric_limits<uint32>::max()))
            return std::numeric_limits<uint32>::max();

        return static_cast<uint32>(adjusted);
    }

    void SendTimeSpeedPacket(WorldSession* session)
    {
        if (!session)
            return;

        WorldPacket data(SMSG_LOGIN_SETTIMESPEED, 4 + 4 + 4);
        data.AppendPackedTime(TimeIsTimeEnable ? GetAdjustedGameTime() : GameTime::GetGameTime().count());
        data << float(ClientBaseTimeSpeed * (TimeIsTimeEnable ? TimeIsTimeSpeedRate : DefaultSpeedRate));
        data << uint32(0);

        session->SendPacket(&data);
    }

    void SendTimeSpeedPacket(Player* player)
    {
        if (!player)
            return;

        SendTimeSpeedPacket(player->GetSession());
    }

    void SendTimeSpeedPacketToOnlinePlayers()
    {
        WorldSessionMgr::SessionMap const& sessionMap = sWorldSessionMgr->GetAllSessions();
        for (WorldSessionMgr::SessionMap::const_iterator itr = sessionMap.begin(); itr != sessionMap.end(); ++itr)
        {
            if (WorldSession* session = itr->second)
                if (session->GetPlayer())
                    SendTimeSpeedPacket(session);
        }
    }

    void LoadTimeIsTimeConfig()
    {
        TimeIsTimeEnable = sConfigMgr->GetOption<bool>("TimeIsTime.Enable", true);
        TimeIsTimeAnnounce = sConfigMgr->GetOption<bool>("TimeIsTime.Announce", true);
        TimeIsTimeSpeedRate = ValidateFloat("TimeIsTime.SpeedRate", sConfigMgr->GetOption<float>("TimeIsTime.SpeedRate", DefaultSpeedRate), DefaultSpeedRate, MinSpeedRate, MaxSpeedRate);
        TimeIsTimeHourOffset = ValidateFloat("TimeIsTime.HourOffset", sConfigMgr->GetOption<float>("TimeIsTime.HourOffset", 0.0f), 0.0f, MinHourOffset, MaxHourOffset);
        TimeIsTimeTimeStart = GetTimeStartConfig();
    }
}

class TimeIsTimeWorld : public WorldScript {
public:

    TimeIsTimeWorld() : WorldScript("TimeIsTimeWorld") { }

    void OnAfterConfigLoad(bool reload) override {
        LoadTimeIsTimeConfig();

        if (reload)
            SendTimeSpeedPacketToOnlinePlayers();
    }
};

class TimeIsTime : public PlayerScript {
public:

    TimeIsTime() : PlayerScript("TimeIsTime") { }

    void OnPlayerLogin(Player* player) override {
        if (TimeIsTimeEnable && TimeIsTimeAnnounce)
            ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00TimeIsTime |rmodule");
    }

    void OnPlayerSendInitialPacketsBeforeAddToMap(Player* player, WorldPacket& /*data*/) override {
        if (!TimeIsTimeEnable)
            return;

        SendTimeSpeedPacket(player);
    }
};

void AddTimeIsTimeScripts() {
    new TimeIsTimeWorld();
    new TimeIsTime();
}
