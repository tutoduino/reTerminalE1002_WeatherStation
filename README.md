# reTerminalE1002_WeatherStation
An advanced weather and information dashboard for the Seeed Studio reTerminal E1002, programmed with the Arduino IDE and displayed on its 7.3" color e‑paper screen.

This project transforms the reTerminal E1002 into a multi‑source data station, combining local sensor readings with online APIs:
- Retrieves weather forecasts from the Open-Meteo API.
- Reads temperature sensors from Home Assistant via its REST API.
- Fetches the Bitcoin price (USD) from CoinGecko.
- Monitors the reTerminal’s battery level, internal temperature, and humidity via onboard sensors.
- Presents all information with clear icons and text on the high‑resolution color e‑paper display.
- Implements deep sleep to save power, with wake‑up triggered by a button press or a timer.
 
More details on this article:
(French) https://tutoduino.fr/reterminal-e1002-arduino-home-assistant/
(English) https://tutoduino.fr/en/reterminal-e1002-arduino-ide/

![reTerminal E1002 display](https://tutoduino.fr/ookoorsa/2025/10/reTerminal-Arduino-EN.jpg)
