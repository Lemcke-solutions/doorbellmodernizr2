![Imgur](https://i.imgur.com/4t2Ilsw.png)

This repository contains sample code and eagle/gerber/STL files for the Doorbell Modernizr 2 that is for sale on tindie: https://www.tindie.com/products/ErikLemcke/doorbell-modernizr-2/

The Doorbell Modernizr is a device that can transform your ordinary doorbell (running on 8 - 24v ac) into a "smart" wifi doorbell.
Whenever someone presses your doorbell, the Doorbell Modernizr can send a signal to your home automation system (currently supported: Home assistant, Domoticz and Openhab).
With the doorbell modernizr 2 it is also possible to turn your doorbell on and off from your home automation system.

If you want to program the device yourself (for example with the Arduino IDE), you can do so by connecting it via USB to your computer.

I use the folowing settings for programming:

- Board: Generic EP8266 module
- Flash mode: QIO
- Flash size: 512K (64K SPIFFS)
- Debugging port: Disabled
- Debug level: None
- IwIP variant: V2 lower memory
- reset method: ck
- crystal frequency: 26Mhz
- Flash frequency: 40Mhz
- CPU frequency: 80Mhz
- Builtin led: 2
- Upload speed: 115200
- Erase flash: Only sketch
