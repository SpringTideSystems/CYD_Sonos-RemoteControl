# CYD Sonos Remote Control

If you are using your Sonos as an internet radio, you mght be interested in this project

Sonos speakers have a power and volume buttons only. There are no buttons for pre-selected redio stations as a 'normal' radio has. You always need the app on your mobile. If you are using other stations than your preferred station not often, you have a good chance, that you first have to update your sonos app, before chosing you radio station.

## Functionality
This project is setting a Sunton 2.8" 240*320 Display (ESP32-2432S028R) or E32R28T aka CYD (Cheap Yellow Display) into a remote control for your sonos speakers:
- Chosing the speaker, you want to control
- Chose one of your favorite radio stations
- power on/off
- set volume

## Configuration
Configuration is stored on an SD-Card (file: settings.txt). See source code for example

## Used Libraries
Please check the versions for the used libraries. Especially LVGL and the SonosUPNP Library at https://github.com/javos65/Sonos-ESP32
