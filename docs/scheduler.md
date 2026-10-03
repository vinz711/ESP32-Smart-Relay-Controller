# Scheduler

The v3.2.9 scheduler controls each relay independently using start/stop times and selected weekdays.

## Supported behavior

- Up to 6 schedules per relay
- Start and stop times
- Weekday selection
- AUTO / MANUAL modes
- Overnight schedules
- 24×7 schedules
- Automatic schedule ON/OFF execution on the ESP32
- Next scheduled action display
- Temporary manual control durations in AUTO mode
- Feeding / Maintenance pause for selected AUTO relays
- Activity-log entries for schedule transitions
- Emergency OFF without losing the pre-emergency relay state
- Emergency ALL OFF with restoration of the pre-emergency relay states

## Recommended test

For a quick validation, create a schedule a few minutes in the future, enable AUTO and select the current weekday. The dashboard should show the upcoming action, the relay should change automatically at the scheduled time, and the event should appear in the activity log with the synchronized local date/time.

## Temporary manual control

When a relay is in AUTO mode, a temporary duration can be selected before pressing ON or OFF. The command temporarily overrides the next AUTO command for the selected duration. `Until changed` keeps the manual override until the relay is changed again or AUTO logic is restored by the applicable controller action.

## Feeding / Maintenance

Feeding / Maintenance temporarily pauses schedule processing for selected AUTO relays without deleting their stored schedules. The pause can be ended early from the same control.

## Emergency interaction

Emergency ALL OFF is an override. It turns the relays off immediately while preserving their states. Resume ALL restores the states that existed immediately before the emergency action.

The scheduler remains part of the controller state and schedule execution is performed by the ESP32 rather than browser-side timers.
