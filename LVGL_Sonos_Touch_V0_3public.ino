/* Using LVGL with Arduino requires some extra steps...
 *  
 * Be sure to read the docs here: https://docs.lvgl.io/8.3/get-started/platforms/arduino.html
 * but note you should use the lv_conf.h from the repo as it is pre-edited to work.
 * 
 * You can always edit your own lv_conf.h later and exclude the example options once the build environment is working.
 * 
 * Note you MUST move the 'examples' and 'demos' folders into the 'src' folder inside the lvgl library folder 
 * otherwise this will not compile, please see README.md in the repo.
 * 
 */

#include <lvgl.h>
#include <TFT_eSPI.h>
//#include "examples/lv_examples.h"
//#include "demos/lv_demos.h"

#include <SPI.h>


#include <XPT2046_Touchscreen.h>
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


// ------------- Place for defining your personal data -----------------------------
// for further development:
// store this information on a config file placed on the sd-card
//
// Define Sonos Devices
const char* SonosName[2] = {
  "Room 1",
  "Room 2"
};

IPAddress SonosIP[2] = {
  {192, 168, 0, 101},
  {192, 168, 0, 102}
};

// Define Radio Stations
char* RadioStation[4] = {
  "SWR 1 RP",
  "Regenbogen 2",
  "SWR 3",
  "BOB"
};

char* RadioStationUrl[4] = {
  "//liveradio.swr.de/sw282p3/swr1rp/",
  "//audiotainment-sw.streamabc.net/atsw-regenbogen2-aac-128-7495395",
  "//liveradio.swr.de/sw282p3/swr3/",
  "//streams.radiobob.de/bob-national/mp3-192/mediaplayer"
};

// Wifi
#include <WiFi.h>
const char* ssid     = "enter-your-ssid-here";
const char* password = "enter-your-password-here";
//
// ------------------- end of individual definitions --------------------------------


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

        Serial.print( "Touch x " );
        Serial.print( data->point.x );
        Serial.print( " y " );
        Serial.println( data->point.y );
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
void get_sonos_state() {
  char * s_resultBuffer;
  size_t s_resultBufferSize;

  volume = g_sonos.getVolume(SonosIP[sonos_selected]);
  char playerstate = g_sonos.getState(SonosIP[sonos_selected]);
  switch (playerstate) {
    case SONOS_STATE_PLAYING:
        Serial.print("Playing, ");
        sonos_state = true;
        break;
    case SONOS_STATE_STOPPED:
        Serial.print("Stopped, ");
        sonos_state = false;
        break;
  }
}

void set_sonos_state_to_display() {
    lv_slider_set_value(gslider, volume, LV_ANIM_ON);
    lv_label_set_text_fmt(slider_label, "%d%%", (int)volume);
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


void setup()
{
    Serial.begin( 115200 ); /* prepare for possible serial debug */

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
  
    // Activate WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("connected to WiFi");
    wifi_active(); 

    // Activate Radio buttons and Volume Slider
    lv_radiobuttons();  
    lv_volume_slider(volume);
    lv_on_off_Switch();
    get_sonos_state();
    set_sonos_state_to_display();
    Serial.println( "Setup done" );
}

void loop()
{
    lv_timer_handler(); /* let the GUI do its work */
    delay( 5 );
}
