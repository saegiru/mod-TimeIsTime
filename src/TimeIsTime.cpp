/*
 * Azerothcore Module:     TimeIsTime
 * Author:                 Dunjeon
 * Contributing Author(s): lasyan3, vratam @ RegWorks
 * Version:                20260609
 * License:                GNU Affero General Public License v3.0.
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "GameTime.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace Acore::ChatCommands;

namespace
{
    constexpr float DefaultSpeedRate = 1.0f;
    constexpr float MinSpeedRate = 0.01f;
    constexpr float MaxSpeedRate = 240.0f;
    constexpr float MinHourOffset = -24.0f;
    constexpr float MaxHourOffset = 24.0f;
    constexpr float ClientBaseTimeSpeed = 0.01666667f;
    constexpr int64 SecondsPerHour = 60 * 60;
    constexpr int64 SecondsPerDay = 24 * SecondsPerHour;

    bool TimeIsTimeEnable = true;
    bool TimeIsTimeAnnounce = true;
    float ConfigSpeedRate = DefaultSpeedRate;
    float ConfigHourOffset = 0.0f;
    uint32 ConfigTimeStart = 0;

    float RuntimeSpeedRate = DefaultSpeedRate;
    float RuntimeHourOffset = 0.0f;
    int64 ReferenceRealTime = 0;
    double ReferencePhase = 0.0;

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

    uint32 ValidateTimeStart(int64 value)
    {
        if (value < 0 || value > static_cast<int64>(std::numeric_limits<uint32>::max()))
        {
            LOG_ERROR("module.timeistime", "TimeIsTime.TimeStart={} is outside the supported range [0, {}]. Using 0.",
                value, std::numeric_limits<uint32>::max());
            return 0;
        }

        return static_cast<uint32>(value);
    }

    double NormalizePhase(double value)
    {
        double result = std::fmod(value, static_cast<double>(SecondsPerDay));
        return result < 0.0 ? result + SecondsPerDay : result;
    }

    double CalculateConfiguredPhase(int64 now)
    {
        int64 const anchor = static_cast<int64>(ConfigTimeStart);
        double const anchorPhase = NormalizePhase(static_cast<double>(anchor));
        double const elapsed = static_cast<double>(now - anchor);
        return NormalizePhase(anchorPhase + (elapsed * ConfigSpeedRate) + (static_cast<double>(ConfigHourOffset) * SecondsPerHour));
    }

    double GetCurrentPhase(int64 now)
    {
        return NormalizePhase(ReferencePhase + (static_cast<double>(now - ReferenceRealTime) * RuntimeSpeedRate));
    }

    void ResetRuntimeClock()
    {
        ReferenceRealTime = GameTime::GetGameTime().count();
        ReferencePhase = CalculateConfiguredPhase(ReferenceRealTime);
        RuntimeSpeedRate = ConfigSpeedRate;
        RuntimeHourOffset = ConfigHourOffset;
    }

    uint32 GetAdjustedGameTime()
    {
        int64 const now = GameTime::GetGameTime().count();
        int64 const today = now - (now % SecondsPerDay);
        int64 const secondsOfDay = static_cast<int64>(std::floor(GetCurrentPhase(now)));
        int64 const adjusted = today + secondsOfDay;

        return static_cast<uint32>(std::clamp<int64>(adjusted, 0, std::numeric_limits<uint32>::max()));
    }

    void SendTimeSpeedPacket(WorldSession* session)
    {
        if (!session)
            return;

        WorldPacket data(SMSG_LOGIN_SETTIMESPEED, 4 + 4 + 4);
        data.AppendPackedTime(TimeIsTimeEnable ? GetAdjustedGameTime() : GameTime::GetGameTime().count());
        data << float(ClientBaseTimeSpeed * (TimeIsTimeEnable ? RuntimeSpeedRate : DefaultSpeedRate));
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

    void SetRuntimeSpeed(float speedRate)
    {
        int64 const now = GameTime::GetGameTime().count();
        ReferencePhase = GetCurrentPhase(now);
        ReferenceRealTime = now;
        RuntimeSpeedRate = speedRate;
    }

    void SetRuntimeOffset(float hourOffset)
    {
        int64 const now = GameTime::GetGameTime().count();
        double const offsetChange = static_cast<double>(hourOffset - RuntimeHourOffset) * SecondsPerHour;
        ReferencePhase = NormalizePhase(GetCurrentPhase(now) + offsetChange);
        ReferenceRealTime = now;
        RuntimeHourOffset = hourOffset;
    }

    void LoadTimeIsTimeConfig()
    {
        TimeIsTimeEnable = sConfigMgr->GetOption<bool>("TimeIsTime.Enable", true);
        TimeIsTimeAnnounce = sConfigMgr->GetOption<bool>("TimeIsTime.Announce", true);
        ConfigSpeedRate = ValidateFloat("TimeIsTime.SpeedRate", sConfigMgr->GetOption<float>("TimeIsTime.SpeedRate", DefaultSpeedRate), DefaultSpeedRate, MinSpeedRate, MaxSpeedRate);
        ConfigHourOffset = ValidateFloat("TimeIsTime.HourOffset", sConfigMgr->GetOption<float>("TimeIsTime.HourOffset", 0.0f), 0.0f, MinHourOffset, MaxHourOffset);
        ConfigTimeStart = ValidateTimeStart(sConfigMgr->GetOption<int64>("TimeIsTime.TimeStart", 0));
        ResetRuntimeClock();

        LOG_INFO("module.timeistime", "TimeIsTime loaded: enabled={}, speed={}, offset={}, anchor={}.",
            TimeIsTimeEnable, ConfigSpeedRate, ConfigHourOffset, ConfigTimeStart);
    }
}

class TimeIsTimeWorld : public WorldScript
{
public:

    TimeIsTimeWorld() : WorldScript("TimeIsTimeWorld") { }

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        LoadTimeIsTimeConfig();
    }
};

class TimeIsTimeCommandScript : public CommandScript
{
public:

    TimeIsTimeCommandScript() : CommandScript("TimeIsTimeCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable timeIsTimeCommandTable =
        {
            { "speed",  HandleSpeedCommand,  SEC_GAMEMASTER, Console::Yes },
            { "offset", HandleOffsetCommand, SEC_GAMEMASTER, Console::Yes },
            { "status", HandleStatusCommand, SEC_GAMEMASTER, Console::Yes },
            { "reset",  HandleResetCommand,  SEC_GAMEMASTER, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "timeistime", timeIsTimeCommandTable }
        };

        return commandTable;
    }

    static bool HandleSpeedCommand(ChatHandler* handler, float speedRate)
    {
        if (!std::isfinite(speedRate) || speedRate < MinSpeedRate || speedRate > MaxSpeedRate)
        {
            handler->PSendSysMessage("TimeIsTime speed must be between {} and {}.", MinSpeedRate, MaxSpeedRate);
            return true;
        }

        SetRuntimeSpeed(speedRate);
        SendTimeSpeedPacketToOnlinePlayers();
        handler->PSendSysMessage("TimeIsTime speed changed to {}. This temporary value resets on config reload or restart.", RuntimeSpeedRate);
        return true;
    }

    static bool HandleOffsetCommand(ChatHandler* handler, float hourOffset)
    {
        if (!std::isfinite(hourOffset) || hourOffset < MinHourOffset || hourOffset > MaxHourOffset)
        {
            handler->PSendSysMessage("TimeIsTime offset must be between {} and {} hours.", MinHourOffset, MaxHourOffset);
            return true;
        }

        SetRuntimeOffset(hourOffset);
        SendTimeSpeedPacketToOnlinePlayers();
        handler->PSendSysMessage("TimeIsTime offset changed to {} hours. This temporary value resets on config reload or restart.", RuntimeHourOffset);
        return true;
    }

    static bool HandleStatusCommand(ChatHandler* handler)
    {
        time_t const displayedTimestamp = TimeIsTimeEnable ? GetAdjustedGameTime() : GameTime::GetGameTime().count();
        std::tm const displayedTime = Acore::Time::TimeBreakdown(displayedTimestamp);

        handler->PSendSysMessage("TimeIsTime: enabled={}, runtime speed={}, runtime offset={} hours, displayed time={:02}:{:02}.",
            TimeIsTimeEnable, RuntimeSpeedRate, RuntimeHourOffset, displayedTime.tm_hour, displayedTime.tm_min);
        handler->PSendSysMessage("Configured defaults: speed={}, offset={} hours, anchor={}.",
            ConfigSpeedRate, ConfigHourOffset, ConfigTimeStart);
        return true;
    }

    static bool HandleResetCommand(ChatHandler* handler)
    {
        ResetRuntimeClock();
        SendTimeSpeedPacketToOnlinePlayers();
        handler->SendSysMessage("TimeIsTime runtime values reset to the configured defaults.");
        return true;
    }
};

class TimeIsTime : public PlayerScript
{
public:

    TimeIsTime() : PlayerScript("TimeIsTime") { }

    void OnPlayerLogin(Player* player) override
    {
        if (TimeIsTimeEnable && TimeIsTimeAnnounce)
            ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00TimeIsTime |rmodule");
    }

    void OnPlayerSendInitialPacketsBeforeAddToMap(Player* player, WorldPacket& /*data*/) override
    {
        if (!TimeIsTimeEnable)
            return;

        SendTimeSpeedPacket(player);
    }
};

void AddTimeIsTimeScripts()
{
    new TimeIsTimeWorld();
    new TimeIsTimeCommandScript();
    new TimeIsTime();
}
