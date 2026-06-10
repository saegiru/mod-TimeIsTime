# mod-TimeIsTime

TimeIsTime is an AzerothCore module for World of Warcraft 3.3.5a that changes the client-side day/night cycle through `SMSG_LOGIN_SETTIMESPEED`.

The module keeps one deterministic realm-wide day/night phase:

```text
phase = anchor time of day + (current Unix time - anchor) * speed + hour offset
```

This keeps the cycle consistent between players and across worldserver restarts. It changes the client clock and lighting cycle only; server-side calendars, resets, events, and database timestamps are unaffected.

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

Controls how fast the client-side game clock advances. The accepted range is `0.01` to `240.0`; invalid configuration values are clamped and logged.

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

Optional non-negative Unix timestamp used as the virtual clock phase anchor. At that timestamp, the displayed clock uses the timestamp's time of day. Leave this at `0` to use Unix epoch time.

`TimeIsTime.HourOffset`

Optional hour offset added to the virtual clock. The accepted range is `-24.0` to `24.0`.

## Reload Behavior

Reloading AzerothCore's configuration resets runtime values to the configured defaults. Reloaded settings apply as players log in; already connected players should reconnect.

## GM Commands

The following commands require `SEC_GAMEMASTER` or higher:

```text
.timeistime status
.timeistime speed <0.01-240.0>
.timeistime offset <-24.0-24.0>
.timeistime reset
```

`speed` changes the rate without changing the current displayed time. `offset` shifts the displayed time by the difference from the previous offset. Both commands immediately update online players.

Runtime command changes are temporary. They reset when the configuration is reloaded or worldserver restarts. Use the configuration file for permanent changes.
