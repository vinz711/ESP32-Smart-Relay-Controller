# Scheduler

The v3.2.8 scheduler controls each relay independently using start/stop times and selected weekdays.

## Supported behavior

- Up to 6 schedules per relay
- Start and stop times
- Weekday selection
- AUTO / MANUAL modes
- Overnight schedules
- Automatic schedule ON/OFF execution
- Next scheduled action display
- Activity-log entries for schedule transitions
- Emergency OFF without losing the pre-emergency relay state

## Recommended test

For a quick validation, create a schedule a few minutes in the future, enable AUTO and select the current weekday. The dashboard should show the upcoming action, the relay should change automatically at the scheduled time, and the event should appear in the activity log with the synchronized local date/time.

## Emergency interaction

Emergency ALL OFF is an override. It turns the relays off immediately while preserving their states. Resume ALL restores the states that existed immediately before the emergency action.

The scheduler remains part of the controller state and should not be replaced with browser-side timers; schedule execution is performed by the ESP32.
