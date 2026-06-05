# mod-TimeIsTime

TimeIsTime is an AzerothCore module for World of Warcraft 3.3.5a that changes the client-side day/night cycle speed sent by `SMSG_LOGIN_SETTIMESPEED`.

The module keeps one realm-wide virtual day/night phase. By default the phase is calculated from Unix epoch time, so it stays continuous across worldserver restarts. You can set `TimeIsTime.TimeStart` to choose a custom phase anchor, then the cycle advances by real elapsed time multiplied by `TimeIsTime.SpeedRate`.

## Installation

Clone or copy this repository into your AzerothCore `modules` directory and rebuild AzerothCore.

Copy:

```text
conf/mod-time_is_time.conf.dist
```

to your server configuration directory as:

```text
mod-time_is_time.conf
```

Then edit the settings under `[worldserver]`.

## Settings

`TimeIsTime.Enable`

Enables or disables the module.

`TimeIsTime.Announce`

Shows a login message when the module is enabled.

`TimeIsTime.SpeedRate`

Controls how fast the client-side game clock advances. The accepted range is `0.001` to `240.0`; invalid values are clamped and logged.

Examples:

```text
60.0  = 1 game day every 24 minutes
30.0  = 1 game day every 48 minutes
15.0  = 1 game day every 96 minutes
7.5   = 1 game day every 192 minutes
3.75  = 1 game day every 384 minutes
1.875 = 1 game day every 768 minutes
1.0   = 1 game day every 24 hours
```

`TimeIsTime.TimeStart`

Optional non-negative Unix timestamp used as the virtual clock phase anchor. Leave this at `0` to use Unix epoch time. This setting changes the day/night phase, not server-side calendar systems.

`TimeIsTime.HourOffset`

Optional hour offset added to the virtual clock. The accepted range is `-24.0` to `24.0`.

## Reload Behavior

The module reloads its settings through AzerothCore's config reload flow. Reloaded settings apply to players as they log in. Already connected players should reconnect to receive the new client-side time-speed packet.
