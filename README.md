PC Control with OTA ()
It can add buttons to esp pins, with managed delay of push, and control them from web and mqtt. Output buttons can be assigned to mqtt topics. 
Ouptocoupler connects to maiboard pins like Power, Reset, Pwr led and controls it.
You need ESP32 board like lolin s2 mini, optional 2x ds18b20 and PC817 3.6-30V 2 4 Channel Optocoupler (search on ali)
Project uses PlatformIO CI/CD template to generate new fw, in futere i plan to add autoupdate function to boards

~~~PROJECT IS PRE ALPHA, use it for own risks!~~~

!!!Check MB pins polarity before connect it to ouptocoupler!!!
