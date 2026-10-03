# Web UI

## Overview

The ESP32 Smart Relay Controller v3.2.9 provides a responsive Smart Management web interface for controlling and monitoring the four relay channels.

The interface supports desktop and mobile devices.

## Dashboard

The dashboard provides:

- Active relay count
- Today's runtime
- Estimated power / energy usage
- Next scheduled action
- Relay control cards
- Power settings
- Quick settings
- System status
- Activity log

## Relay Control

Each relay provides:

- Configurable device name
- Configurable icon
- Relay number
- Current ON/OFF state
- AUTO / MANUAL mode
- Runtime information
- Schedule information
- Next scheduled action
- Temporary manual control
- ON button
- OFF button
- Schedule configuration
- Relay settings
- Emergency OFF

## Scheduling

Each relay can have multiple schedules.

Schedules support:

- Start time
- Stop time
- Weekday selection
- AUTO mode
- Overnight operation
- Multiple schedules per relay

## Emergency Control

The dashboard provides:

### Emergency OFF

Turns the selected relay OFF while preserving the previous operating state.

### Emergency ALL OFF

Turns all relays OFF while preserving their previous states.

### Resume

Restores the states that were active before emergency operation.

## Power Settings

Each relay can have an individual power rating.

These values are used to estimate:

- Current power
- Daily energy
- Monthly energy
- Estimated electricity cost

The values are software estimates and are not measurements from an electrical power meter.

## Feeding / Maintenance

The Feeding / Maintenance function temporarily pauses selected AUTO relay schedules.

Stored schedules are not deleted.

## System Status

The dashboard displays:

- ESP32 connection status
- IP address
- Wi-Fi RSSI
- Uptime
- Firmware version
- OTA status
- Network/time status

## Activity Log

The controller maintains a persistent activity log containing up to 300 events.

The log records important relay and system operations.

The log can be searched, exported, and cleared from the dashboard.

## Responsive Design

The interface supports:

- Desktop
- Mobile
- Light theme
- Dark theme

## UI Screenshots

### Desktop — Dark Theme

![v3.2.9 desktop dark](ui/v3.2.9-desktop-dashboard-dark.png)

### Desktop — Light Theme

![v3.2.9 desktop light](ui/v3.2.9-desktop-dashboard-light.png)

### Mobile — Dark Theme

![v3.2.9 mobile dark](ui/v3.2.9-mobile-dashboard-dark.png)

### Mobile — Light Theme

![v3.2.9 mobile light](ui/v3.2.9-mobile-dashboard-light.png)