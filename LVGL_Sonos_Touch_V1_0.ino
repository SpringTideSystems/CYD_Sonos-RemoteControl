/*
CYD Sonos Remote
Remote Control to use Sonos-Speakers Internet-Radio
4 Programmable Radio Stations
2 Programmable Sonos Systems 
(c) Florian Lenz
*/

#include <lvgl.h>
// 8.3.11

#include <TFT_eSPI.h>
// 2.5.43
//#include "examples/lv_examples.h"
//#include "demos/lv_demos.h"

#include <FS.h>
#include <SD.h>
// 1.3.0

#include <FileConfig.h>
// 1.0.0

#include <SPI.h>


/* ------------- Place for defining your personal data -----------------------------
store this information on a config file named "setting.txt" placed on the sd-card

SSID=your_ssid
Password=your_wifi_password
Radiostation_0=SWR 1 RP
Radiostation_1=Rock FM
Radiostation_2=SWR 3
Radiostation_3=BOB
RadiostationUrl_0=//liveradio.swr.de/sw282p3/swr1rp/
RadiostationUrl_1=//https://stream.regenbogen2.de/rheinneckar/mp3-128/private
RadiostationUrl_2=//liveradio.swr.de/sw282p3/swr3/
RadiostationUrl_3=//streams.radiobob.de/bob-national/mp3-192/mediaplayer
SonosName_0=Sonos1
SonosName_1=Sonos2
SonosIP_0=192.168.0.83
SonosIP_1=192.168.0.85

*/


// Define Sonos Devices - You don't have to update this part, if you use a SD-Card with the settings.txt file.
char* SonosName[] = {
  "Sonos1",
  "Sonos2"
};

IPAddress SonosIP[] = {
  {192, 168, 0, 83},
  {192, 168, 0, 85}
};

// Define Radio Stations
const int Stations = 4;
/*
char* RadioStation_1;
char* RadioStation_2;
char* RadioStation_3;
char* RadioStation_4;
*/
char RadioStation [Stations] [20] = {
 { "SWR 1 RP" },
 { "Rock FM"},
 { "SWR 3"},
 { "BOB"},
};

char RadioStationUrl [Stations] [255] = {
    {"//liveradio.swr.de/sw282p3/swr1rp/"},
    {"//stream.regenbogen2.de/dab/mp3-128/private"},
    {"//liveradio.swr.de/sw282p3/swr3/"},
    {"//streams.radiobob.de/bob-national/mp3-192/mediaplayer"},
};

// Wifi
#include <WiFi.h>
char* ssid     = "your_ssid";
char* password = "your_password";
//
// ------------------- end of individual definitions --------------------------------


// Batterie
#include <Arduino.h>

#define BATTERY_PIN 34        // CYD Batterie-Messpin
#define ADC_MAX     4095.0
#define VREF        3.3       // ESP32 Referenzspannung
#define SAMPLES     25

// Interner Spannungsteiler CYD (100k / 100k)
#define DIVIDER_FACTOR 2.0

struct BatteryPoint {
  float voltage;
  int percent;
};

BatteryPoint curve[] = {
  {4.20, 100},
  {4.10, 90},
  {4.00, 80},
  {3.90, 65},
  {3.80, 50},
  {3.70, 35},
  {3.60, 20},
  {3.50, 10},
  {3.40, 5},
  {3.30, 0}
};

const int curveSize = sizeof(curve) / sizeof(curve[0]);

float readBatteryVoltage() {
  uint32_t sum = 0;

  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(BATTERY_PIN);
    delay(3);
  }

  float adc = sum / (float)SAMPLES;
  float voltage = (adc / ADC_MAX) * VREF;

  // internen Spannungsteiler rückrechnen
  voltage *= DIVIDER_FACTOR;

  return voltage;
}

int voltageToPercent(float v) {
  if (v >= curve[0].voltage) return 100;
  if (v <= curve[curveSize - 1].voltage) return 0;

  for (int i = 0; i < curveSize - 1; i++) {
    if (v <= curve[i].voltage && v > curve[i + 1].voltage) {
      float v1 = curve[i].voltage;
      float v2 = curve[i + 1].voltage;
      int p1 = curve[i].percent;
      int p2 = curve[i + 1].percent;

      return p2 + (v - v2) * (p1 - p2) / (v1 - v2);
    }
  }
  return 0;
}

// End of Battery


#include <esp_sleep.h>

#define TOUCH_IRQ_PIN 36   // XPT2046 T_IRQ beim ESP32-2432S028R

#include <XPT2046_Touchscreen.h>
// 1.4
// A library for interfacing with the touch screen
//
// Can be installed from the library manager (Search for "XPT2046")
//https://github.com/PaulStoffregen/XPT2046_Touchscreen
// ----------------------------
// Touch Screen pins
// ----------------------------

// The CYD touch uses some non default
// SPI pins

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39 
#define XPT2046_CLK 25
#define XPT2046_CS 33
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
uint16_t touchScreenMinimumX = 200, touchScreenMaximumX = 3700, touchScreenMinimumY = 240,touchScreenMaximumY = 3800;

#define SD_CS 5 // Adjust to your SD card CS pin

// Timer
// Setup Timer
  float TimeOut = 0;
  float SetTime = 0;

/*Change to your screen resolution*/
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[ screenWidth * screenHeight / 10 ];
lv_obj_t * gslider;
lv_obj_t * slider_label;

TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight); /* TFT instance */

/* Sonos
  Download Sonos library from https://github.com/javos65/Sonos-ESP32 and place the unzipped folder "sonos-master"
  at your libraries folder.
  This library is dependant on "microxpath-master" which is needed as well.
*/
#include <SonosUPnP.h>
#include <MicroXPath_P.h>



// Network connection to Sonos
#define ETHERNET_ERROR_DHCP "E: DHCP"
#define ETHERNET_ERROR_CONNECT "E: Connect"

void ethConnectError();
WiFiClient client;
SonosUPnP g_sonos = SonosUPnP(client, ethConnectError);

void ethConnectError()
{
  Serial.println(ETHERNET_ERROR_CONNECT);
  Serial.println("Wifi died.");
}


int volume = 0;
int sonos_selected = 0;
bool sonos_state = true;
char* sonos_station;


/* Display flushing */
void my_disp_flush( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p )
{
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tft.startWrite();
    tft.setAddrWindow( area->x1, area->y1, w, h );
    tft.pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tft.endWrite();

    lv_disp_flush_ready( disp_drv );
}

/*Read the touchpad*/
void my_touchpad_read( lv_indev_drv_t * indev_drv, lv_indev_data_t * data )
{
    if(ts.touched())
    {
        TS_Point p = ts.getPoint();
        //Some very basic auto calibration so it doesn't go out of range
        if(p.x < touchScreenMinimumX) touchScreenMinimumX = p.x;
        if(p.x > touchScreenMaximumX) touchScreenMaximumX = p.x;
        if(p.y < touchScreenMinimumY) touchScreenMinimumY = p.y;
        if(p.y > touchScreenMaximumY) touchScreenMaximumY = p.y;
        //Map this to the pixel position
        data->point.x = map(p.x,touchScreenMinimumX,touchScreenMaximumX,1,screenWidth); /* Touchscreen X calibration */
        data->point.y = map(p.y,touchScreenMinimumY,touchScreenMaximumY,1,screenHeight); /* Touchscreen Y calibration */
        data->state = LV_INDEV_STATE_PR;

        // Serial.print( "Touch x " );
        // Serial.print( data->point.x );
        // Serial.print( " y " );
        // Serial.println( data->point.y );

      //Timer
      // If timer is still running, reset the timer.

        Serial.println("Touch!");
        SetTime = millis();
        

    


    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }
}


// Radio Button (Station and selected Sonos Device) definition
static lv_style_t style_radio;
static lv_style_t style_radio_chk;
static uint32_t active_index_1 = 2;
static uint32_t active_index_2 = 0;
lv_obj_t * cont1;
lv_obj_t * cont2;
lv_obj_t * sw;

static void radio_event_handler(lv_event_t * e)
{
    uint32_t * active_id = (uint32_t *)lv_event_get_user_data(e);
    lv_obj_t * cont = lv_event_get_current_target(e);
    lv_obj_t * act_cb = lv_event_get_target(e);
    lv_obj_t * old_cb = lv_obj_get_child(cont, *active_id);

    /*Do nothing if the container was clicked*/
    if(act_cb == cont) return;

    lv_obj_clear_state(old_cb, LV_STATE_CHECKED);   /*Uncheck the previous radio button*/
    lv_obj_add_state(act_cb, LV_STATE_CHECKED);     /*Uncheck the current radio button*/

    *active_id = lv_obj_get_index(act_cb);
    Serial.print(RadioStation[(int)active_index_1]);
    Serial.println();
    Serial.print(RadioStationUrl[(int)active_index_1]);
    Serial.println();
    g_sonos.playRadio(SonosIP[sonos_selected], RadioStationUrl[(int)active_index_1], RadioStation[(int)active_index_1]);



}

static void radio_event_handler2(lv_event_t * e)
{
    uint32_t * active_id = (uint32_t *)lv_event_get_user_data(e);
    lv_obj_t * cont = lv_event_get_current_target(e);
    lv_obj_t * act_cb = lv_event_get_target(e);
    lv_obj_t * old_cb = lv_obj_get_child(cont, *active_id);

    /*Do nothing if the container was clicked*/
    if(act_cb == cont) return;

    lv_obj_clear_state(old_cb, LV_STATE_CHECKED);   /*Uncheck the previous radio button*/
    lv_obj_add_state(act_cb, LV_STATE_CHECKED);     /*Uncheck the current radio button*/

    *active_id = lv_obj_get_index(act_cb);


    LV_LOG_USER("Selected radio buttons: %d", (int)active_index_2);

    sonos_selected = active_index_2;
    // get current volume from selected Sonos and set volume slider
    get_sonos_state();
    set_sonos_state_to_display();

    // volume = g_sonos.getVolume(SonosIP[(int)sonos_selected]);
    // lv_slider_set_value(gslider, volume, LV_ANIM_ON);
    // lv_label_set_text_fmt(slider_label, "%d%%", (int)volume);
    // Serial.printf("Volume: %d",(int)volume);

}



static void radiobutton_create(lv_obj_t * parent, const char * txt)
{
    lv_obj_t * obj = lv_checkbox_create(parent);
    lv_checkbox_set_text(obj, txt);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_style(obj, &style_radio, LV_PART_INDICATOR);
    lv_obj_add_style(obj, &style_radio_chk, LV_PART_INDICATOR | LV_STATE_CHECKED);
}

/**
 * Checkboxes as radio buttons
 */
void lv_radiobuttons(void)
{
    /* The idea is to enable `LV_OBJ_FLAG_EVENT_BUBBLE` on checkboxes and process the
     * `LV_EVENT_CLICKED` on the container.
     * A variable is passed as event user data where the index of the active
     * radiobutton is saved */


    lv_style_init(&style_radio);
    lv_style_set_radius(&style_radio, LV_RADIUS_CIRCLE);

    lv_style_init(&style_radio_chk);
    lv_style_set_bg_img_src(&style_radio_chk, NULL);

    uint32_t i;
    char buf[32];

    cont1 = lv_obj_create(lv_scr_act());
    lv_obj_set_flex_flow(cont1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(cont1, lv_pct(40), lv_pct(60));
    lv_obj_set_x(cont1, lv_pct(5));
    lv_obj_set_y(cont1, lv_pct(10));
    lv_obj_add_event_cb(cont1, radio_event_handler, LV_EVENT_CLICKED, &active_index_1);

    for(i = 0; i < 4; i++) {
        lv_snprintf(buf, sizeof(buf), RadioStation[i]);
        radiobutton_create(cont1, buf);

    }
    /*Make checkbox checked*/
    lv_obj_add_state(lv_obj_get_child(cont1, active_index_1), LV_STATE_CHECKED);

    cont2 = lv_obj_create(lv_scr_act());
    lv_obj_set_flex_flow(cont2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(cont2, lv_pct(40), lv_pct(35));
    lv_obj_set_x(cont2, lv_pct(55));
    lv_obj_set_y(cont2, lv_pct(10));
    lv_obj_add_event_cb(cont2, radio_event_handler2, LV_EVENT_CLICKED, &active_index_2);

    for(i = 0; i < 2; i++) {
        lv_snprintf(buf, sizeof(buf), SonosName[i]);
        radiobutton_create(cont2, buf);
    }

    /*Make checkbox checked*/
    lv_obj_add_state(lv_obj_get_child(cont2, active_index_2), LV_STATE_CHECKED);
}
// End of Radio Button Definition

// --- On-Off Switch ---
static void event_handler_sw(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        LV_UNUSED(obj);
        Serial.printf("State: %s\n", lv_obj_has_state(obj, LV_STATE_CHECKED) ? "On" : "Off");
        if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        //  g_sonos.play(SonosIP[sonos_selected]);
        g_sonos.playRadio(SonosIP[sonos_selected], RadioStationUrl[(int)active_index_1], RadioStation[(int)active_index_1]);
        }
        else {
          g_sonos.stop(SonosIP[sonos_selected]);
        }
    }

}

void lv_on_off_Switch(void)
{
  //lv_obj_t * sw;
  sw = lv_switch_create(lv_scr_act());
  lv_obj_add_event_cb(sw, event_handler_sw, LV_EVENT_ALL, NULL);
  lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_set_x(sw, lv_pct(55));
  lv_obj_set_y(sw, lv_pct(55));

}
// --- end of On-Off Switch ---


// Start of Slider for Volume Control
static void slider_event_cb(lv_event_t * e);

/**
 * A default slider with a label displaying the current value
 */
void lv_volume_slider(int volume)
{
    /*Create a slider in the center of the display*/
//    lv_obj_t * slider = lv_slider_create(lv_scr_act());
    gslider = lv_slider_create(lv_scr_act());
    lv_obj_set_x(gslider, lv_pct(10));
    lv_obj_set_y(gslider, lv_pct(80));
    // lv_obj_center(slider);
    lv_obj_add_event_cb(gslider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /*Create a label below the slider*/
    slider_label = lv_label_create(lv_scr_act());
    lv_slider_set_value(gslider, volume, LV_ANIM_OFF);
    lv_label_set_text_fmt(slider_label, "%d%%", (int)volume);
    lv_obj_align_to(slider_label, gslider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

static void slider_event_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(slider));
    lv_label_set_text(slider_label, buf);
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    volume = (int)lv_slider_get_value(slider);
    // Set Volume at Sonos
    Serial.printf("Volume: %d%% on Sonos #%d% \n", volume, sonos_selected);
    g_sonos.setVolume(SonosIP[sonos_selected], volume);


}
// End of Slider for Volume Control


// --- get sonos state ---
bool get_sonos_state(void) {
  char * s_resultBuffer;
  size_t s_resultBufferSize;

  volume = g_sonos.getVolume(SonosIP[sonos_selected]);
  char playerstate = g_sonos.getState(SonosIP[sonos_selected]);
  switch (playerstate) {
    case SONOS_STATE_PLAYING:
        Serial.print("Playing, ");
        sonos_state = true;
        return true;
    case SONOS_STATE_STOPPED:
        Serial.print("Stopped, ");
        sonos_state = false;
        return false;
  }
}

void set_sonos_state_to_display() {
    lv_slider_set_value(gslider, volume, LV_ANIM_ON);
    lv_label_set_text_fmt(slider_label, "%d %", (int)volume);
    Serial.printf("Volume: %d",(int)volume);
    if (sonos_state) {
      lv_obj_add_state(sw, LV_STATE_CHECKED);
    } 
    else {
      lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }
}

void  wifi_active(void) {
    lv_obj_t * label1 = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP);     /*Break the long lines*/
    lv_label_set_recolor(label1, true);                      /*Enable re-coloring by commands in the text*/
    lv_label_set_text(label1, LV_SYMBOL_WIFI);
    lv_obj_set_width(label1, 20);  /*Set smaller width to make the lines wrap*/
    lv_obj_set_x(label1, lv_pct(95));
//    lv_obj_set_style_text_align(label1, LV_TEXT_ALIGN_RIGHT, 0);
//    lv_obj_align(label1, LV_ALIGN_RIGHT, 0, -40);
}  

void battery_state(void) {
  lv_obj_t * label2 = lv_label_create(lv_scr_act());
  float voltage = readBatteryVoltage();
  int percent = voltageToPercent(voltage);
  char Ladezustand[10];
  snprintf(Ladezustand, sizeof(Ladezustand), "%d%% %s", percent, LV_SYMBOL_BATTERY_3);
  // + LV_SYMBOL_BATTERY_3;

    lv_label_set_long_mode(label2, LV_LABEL_LONG_WRAP);     /*Break the long lines*/
    lv_label_set_recolor(label2, true);                      /*Enable re-coloring by commands in the text*/
    lv_label_set_text(label2, Ladezustand);
    Serial.println(Ladezustand);
    lv_obj_set_width(label2, 120);  /*Set smaller width to make the lines wrap*/
    lv_obj_set_x(label2, lv_pct(5));
}

void sd_config(void) {
  int maxLineLength = 200;
  int maxSectionLength = 20;
  bool ignoreCase = true;
  bool ignoreError = true;

    // Could be SD, SD_MMC or any other FS compatible object
  SD.begin();
  FileConfig cfg;
  fs::FS &fs = SD;
  if (cfg.begin(fs, "/settings.txt", maxLineLength, maxSectionLength, ignoreCase, ignoreError)) {
    while (cfg.readNextSetting()) {
      if (cfg.nameIs("SSID")) {
      //  char *string = cfg.copyValue();
        ssid = cfg.copyValue();
      //  Serial.printf("SSID = %s\n", string);
      //  *ssid = *string;
        Serial.printf("SSID = %s\n", ssid);
      } else if (cfg.nameIs("Password")) {
      //  char *string = cfg.copyValue();
        password = cfg.copyValue();
        Serial.printf("Password = %s\n", password);
      //  password = string;
      } else if (cfg.nameIs("Radiostation_0")) {
        char *string = cfg.copyValue();
        Serial.printf("Radiostation_0 = %s\n", string);
        memcpy(RadioStation[0], string, strlen(string)+1);
      } else if (cfg.nameIs("Radiostation_1")) {
        char *string = cfg.copyValue();
        Serial.printf("Radiostation_1 = %s\n", string);
        memcpy(RadioStation[1], string, strlen(string)+1);
      } else if (cfg.nameIs("Radiostation_2")) {
        char *string = cfg.copyValue();
        Serial.printf("Radiostation_2 = %s\n", string);
        memcpy(RadioStation[2], string, strlen(string)+1);
      } else if (cfg.nameIs("Radiostation_3")) {
        char *string = cfg.copyValue();
        Serial.printf("Radiostation_3 = %s\n", string);
        memcpy(RadioStation[3], string, strlen(string)+1);
      } else if (cfg.nameIs("RadiostationUrl_0")) {
        char *string = cfg.copyValue();
        Serial.printf("RadiostationUrl_0 = %s\n", string);
        memcpy(RadioStationUrl[0], string, strlen(string)+1);
      } else if (cfg.nameIs("RadiostationUrl_1")) {
        char *string = cfg.copyValue();
        Serial.printf("RadiostationUrl_1 = %s\n", string);
        memcpy(RadioStationUrl[1], string, strlen(string)+1);
      } else if (cfg.nameIs("RadiostationUrl_2")) {
        char *string = cfg.copyValue();
        Serial.printf("RadiostationUrl_2 = %s\n", string);
        memcpy(RadioStationUrl[2], string, strlen(string)+1);
      } else if (cfg.nameIs("RadiostationUrl_3")) {
        char *string = cfg.copyValue();
        Serial.printf("RadiostationUrl_3 = %s\n", string);
        memcpy(RadioStationUrl[3], string, strlen(string)+1);
      } else if (cfg.nameIs("Test")) {
        bool boolean = cfg.getBooleanValue();
        Serial.printf("setting4 = %s.\n", boolean ? "true" : "false");
      } else if (cfg.nameIs("SonosName_0")) {
        char *string = cfg.copyValue();
        Serial.printf("SonosName_0 = %s\n", string);
        SonosName[0] = string;
        Serial.printf("SonosName_0 = %s\n", SonosName[0]);
      } else if (cfg.nameIs("SonosName_1")) {
        char *string = cfg.copyValue();
        Serial.printf("SonosName_1 = %s\n", string);
        SonosName[1] = string;
        Serial.printf("SonosName_1 = %s\n", SonosName[1]);
      } else if (cfg.nameIs("SonosIP_0")) {
        IPAddress ip = cfg.getIPAddress();
        Serial.print("SonosIP_0 = ");
        Serial.print(ip);
        Serial.println(".");
        SonosIP[0]=ip;
        Serial.println(SonosIP[0]);
      } else if (cfg.nameIs("SonosIP_1")) {
        IPAddress ip = cfg.getIPAddress();
        Serial.print("SonosIP_1 = ");
        Serial.print(ip);
        Serial.println(".");
        SonosIP[1]=ip;
        Serial.println(SonosIP[1]);
      } else if (cfg.sectionIs("section2") && cfg.nameIs("dupSetting")) {
        // Notice that once the getValue is called with trim = true,
        // the whole raw value is no longer available: it remains right-trimed.
        Serial.printf("dupSetting in section2: raw value    = \"%s\".\n", cfg.getValue(false));
        Serial.printf("dupSetting in section2: trimed value = \"%s\".\n", cfg.getValue());
      } else {
        Serial.printf("Current setting is: %s = %s.\n", cfg.getName(), cfg.getValue());
      }
    Serial.printf("RadioStation_1: %s\n", RadioStation[1]);
    Serial.printf("RadioStationUrl_1: %s\n", RadioStationUrl[1]);
    }
    Serial.flush();
    cfg.end();
  }
  SD.end();

}

void activate_wifi(void) {
      // Activate WiFi
    // Add Timeout and Message: No WiFi connection
    WiFi.begin(ssid, password);
    delay(2000);
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("No WiFi Connection!");
      } else {
      Serial.println("connected to WiFi");
      wifi_active(); 
    }
    /*
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("connected to WiFi");
    wifi_active(); 
  */
}

void battery_check(void) {
    //Battery Check
    analogReadResolution(12);
    analogSetPinAttenuation(BATTERY_PIN, ADC_11db);

    Serial.println("CYD Batterie-Messung gestartet");
      float voltage = readBatteryVoltage();
      int percent = voltageToPercent(voltage);

      Serial.print("Akku: ");
      Serial.print(voltage, 2);
      Serial.print(" V  | ");
      Serial.print(percent);
      Serial.println(" %");
}

void wake_on_touch(void) {
    // IRQ-Pin konfigurieren (sehr wichtig!)
  pinMode(TOUCH_IRQ_PIN, INPUT_PULLUP);
  //pinMode(TOUCH_IRQ_PIN, INPUT);

  // Wake-Up konfigurieren: Touch zieht IRQ auf LOW
  esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_IRQ_PIN, 0);

}

void setup()
{
  Serial.begin( 115200 ); /* prepare for possible serial debug */
  delay(500);  // Wait up to 0.5 secs for Serial to be ready
  Serial.println("**********************************************************");
  Serial.println("                  Debugging");
  Serial.println("**********************************************************");
  
  sd_config();

  wake_on_touch();
 
  battery_check();

  // Initialize LVGL and Display
  String LVGL_Arduino = "LVGL version ";
  LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();
  Serial.println( LVGL_Arduino );
  lv_init();

    mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS); /* Start second SPI bus for touchscreen */
    ts.begin(mySpi); /* Touchscreen init */
    ts.setRotation(1); /* Landscape orientation */

    tft.begin();          /* TFT init */
    tft.setRotation( 1 ); /* Landscape orientation */

    lv_disp_draw_buf_init( &draw_buf, buf, NULL, screenWidth * screenHeight / 10 );

    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init( &disp_drv );
    /*Change the following line to your display resolution*/
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register( &disp_drv );

    /*Initialize the (dummy) input device driver*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init( &indev_drv );
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_t * my_indev = lv_indev_drv_register( &indev_drv );
  
    activate_wifi();
  
    battery_state();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("No WiFi Connection - No Sonos Connection!");
    } else {
      //Switch on, if Sonos is off
      if (get_sonos_state()) {
        Serial.println("Sonos is on");
        } else {
        Serial.println("Sonos is off");
        g_sonos.playRadio(SonosIP[sonos_selected], RadioStationUrl[(int)active_index_1], RadioStation[(int)active_index_1]);
        lv_obj_add_state(sw, LV_STATE_CHECKED);
        // lv_switch_on(sw, LV_ANIM_ON);
      };
    }
    // Activate Radio buttons and Volume Slider
    lv_radiobuttons();  
    lv_volume_slider(volume);
    lv_on_off_Switch();
    set_sonos_state_to_display();
    Serial.println( "Setup done" );
}

void loop()
{
  lv_timer_handler(); /* let the GUI do its work */
  delay( 5 );

  // Timer
  TimeOut = millis() - SetTime;
  //Serial.println(TimeOut);

  if (TimeOut > 30000) {
    Serial.println("Shutting down.");
    esp_deep_sleep_start();
    }    
}
