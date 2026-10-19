# Smart Emergency Shelter Occupancy Monitoring System

ECE1512 – Design Thinking in Practice | Team INVO-194
Supervisor: Dr. Ajit Kumar, Dept. of ECE

## Problem
Disaster shelters (floods, cyclones, earthquakes) have no real-time occupant count, causing overcrowding in some shelters and unused capacity in others.

## Solution
An IoT system that counts people entering and leaving a shelter, uploads the count to the cloud, and shows live occupancy on a dashboard. It alerts authorities when a shelter nears its capacity.

## Hardware
- ESP32 dev board
- 2x IR obstacle sensors (entry/exit detection)
- 16x2 I2C LCD
- Green / yellow / red LEDs and buzzer
- 18650 battery backup (Phase 3)

## Software
- Firmware: Arduino C++ (ESP32)
- Cloud: Firebase Realtime Database
- Dashboard: HTML/JS web app (PWA)

## Repository structure
- `firmware/`: ESP32 code
- `app/`: dashboard / PWA files
- `docs/`: reports, slides, diagrams

## Links
- Live dashboard: https://smart-shelter-monitor.vercel.app
- ESP32 simulation (Wokwi): https://wokwi.com/projects/478236280340982785
- Credentials are removed from the Wokwi project. Supply your own in `secrets.h`.

## Project status
- [x] Phase 1: Roadmap
- [x] Phase 2: 50% (counting, cloud upload, basic dashboard)
- [ ] Phase 3: 100% (alerts, multi-shelter view, app, battery backup)

## Team
- SARTHAK DWIVEDI (20261CBD0032)
- ADARSH KUMAR OJHA (20261CSD160)
- DIVYANSH PATEL (20261CSD0161)
- AKSHAY BS (20261CSD0163)
- TEJESH S (20261ADS0217)
