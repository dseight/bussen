#include <ArduinoJson.h>            // ArduinoJson 7.2.1
#include <NTPClient.h>              // NTPClient 3.2.1
#include <Inkplate.h>               // Inkplate 10.0.0
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "font.h"

#include "credentials.h"

#define DISP_TIME_COORD_X 1080
#define DISP_TIME_COORD_Y 70
#define DISP_DEPART_COORD_X 30
#define DISP_DEPART_COORD_Y 130
#define DISP_DEPART_SPACING_Y 70
#define DISP_STATUS_COORD_X 10
#define DISP_STATUS_COORD_Y 700

#define FONT_NAME JetBrainsMono_Regular28pt8b

#define API_HOST "https://ext-api.vasttrafik.se"

// Draw a grid for easier visual elements alignment
static const bool enableGrid = false;

static WiFiClientSecure client;
static WiFiUDP ntpudp;
static HTTPClient http;
static Inkplate display(INKPLATE_3BIT);
static NTPClient ntpTime(ntpudp, "1.europe.pool.ntp.org", 3600, 60 * 60 * 1000);

static uint8_t currentHours = -1;
static uint8_t currentMinutes = -1;
static bool needsUpdate = false;

static String statusMessage;
static String accessToken;

static void displayStatus(String text)
{
    // display.fillRect(0, 680, E_INK_WIDTH, 40, 7);
    display.setTextSize(1);
    display.setCursor(DISP_STATUS_COORD_X, DISP_STATUS_COORD_Y);
    display.print(text);
}

static void print2Digits(uint8_t num)
{
    if (num < 10)
        display.print('0');
    display.print(num, DEC);
}

static void displayGrid()
{
    if (!enableGrid)
        return;

    display.setTextSize(2);
    for (int x = 0; x < E_INK_WIDTH; x += 10) {
        display.drawFastVLine(x, 0, E_INK_HEIGHT, 6);
    }
    for (int y = 0; y < E_INK_HEIGHT; y += 10) {
        display.drawFastHLine(0, y, E_INK_WIDTH, 6);
    }
    for (int x = 0; x < E_INK_WIDTH; x += 100) {
        display.drawFastVLine(x, 0, E_INK_HEIGHT, 2);
        if (x != 0) {
            display.setCursor(x + 10, 10);
            display.print(x, DEC);
        }
    }
    for (int y = 0; y < E_INK_HEIGHT; y += 100) {
        display.drawFastHLine(0, y, E_INK_WIDTH, 2);
        if (y != 0) {
            display.setCursor(10, y + 10);
            display.print(y, DEC);
        }
    }
}

static void updateHttpStatus(int httpCode)
{
    switch (httpCode) {
    case HTTPC_ERROR_CONNECTION_REFUSED:
        statusMessage = "HTTPC_ERROR_CONNECTION_REFUSED";
        break;
    case HTTPC_ERROR_SEND_HEADER_FAILED:
        statusMessage = "HTTPC_ERROR_SEND_HEADER_FAILED";
        break;
    case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
        statusMessage = "HTTPC_ERROR_SEND_PAYLOAD_FAILED";
        break;
    case HTTPC_ERROR_NOT_CONNECTED:
        statusMessage = "HTTPC_ERROR_NOT_CONNECTED";
        break;
    case HTTPC_ERROR_CONNECTION_LOST:
        statusMessage = "HTTPC_ERROR_CONNECTION_LOST";
        break;
    case HTTPC_ERROR_NO_STREAM:
        statusMessage = "HTTPC_ERROR_NO_STREAM";
        break;
    case HTTPC_ERROR_NO_HTTP_SERVER:
        statusMessage = "HTTPC_ERROR_NO_HTTP_SERVER";
        break;
    case HTTPC_ERROR_TOO_LESS_RAM:
        statusMessage = "HTTPC_ERROR_TOO_LESS_RAM";
        break;
    case HTTPC_ERROR_ENCODING:
        statusMessage = "HTTPC_ERROR_ENCODING";
        break;
    case HTTPC_ERROR_STREAM_WRITE:
        statusMessage = "HTTPC_ERROR_STREAM_WRITE";
        break;
    case HTTPC_ERROR_READ_TIMEOUT:
        statusMessage = "HTTPC_ERROR_READ_TIMEOUT";
        break;
    default:
        statusMessage = "HTTPC: " + String(httpCode);
        break;
    }
}

static void updateWifiStatus(uint8_t wifiStatus)
{
    switch (wifiStatus) {
    case WL_IDLE_STATUS:
        statusMessage = "WL_IDLE_STATUS";
        break;
    case WL_NO_SSID_AVAIL:
        statusMessage = "WL_NO_SSID_AVAIL";
        break;
    case WL_SCAN_COMPLETED:
        statusMessage = "WL_SCAN_COMPLETED";
        break;
    case WL_CONNECTED:
        // statusMessage = "Connected";
        break;
    case WL_CONNECT_FAILED:
        statusMessage = "WL_CONNECT_FAILED";
        break;
    case WL_CONNECTION_LOST:
        statusMessage = "WL_CONNECTION_LOST";
        break;
    case WL_DISCONNECTED:
        statusMessage = "WL_DISCONNECTED";
        break;
    default:
        statusMessage = "WL_UNKNOWN: ";
        statusMessage += String(wifiStatus);
        break;
    }
}

static void updateTime()
{
    static uint8_t oldHours = -1;
    static uint8_t oldMinutes = -1;

    display.rtcGetRtcData();

    currentHours = display.rtcGetHour();
    currentMinutes = display.rtcGetMinute();

    if (currentMinutes != oldMinutes) {
        oldMinutes = currentMinutes;
        needsUpdate = true;
    } else if (currentHours != oldHours) {
        oldHours = currentHours;
        needsUpdate = true;
    }
}

static void requestApiToken()
{
    http.begin(client, API_HOST "/token?format=json&grant_type=client_credentials");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", "Basic " API_KEY);

    int httpCode = http.POST(0, 0);
    if (httpCode < 0 || httpCode != 200) {
        updateHttpStatus(httpCode);
        return;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        statusMessage = String("deserializeJson() failed: ") + error.f_str();
        return;
    }

    accessToken = doc["access_token"].as<String>();
    Serial.print("access_token: ");
    Serial.println(accessToken);
}

static void updateAndRedrawSchedule()
{
    http.begin(client, API_HOST "/pr/v4/stop-areas/" AREA_CODE "/departures"
                                "?timeSpanInMinutes=60"
                                "&maxDeparturesPerLineAndDirection=4"
                                "&limit=10"
                                "&offset=0");
    http.addHeader("Authorization", String("Bearer ") + accessToken);

    int httpCode = http.GET();

    if (httpCode < 0 || httpCode != 200) {
        updateHttpStatus(httpCode);
        return;
    }

    String payload = http.getString();
    http.end();

    Serial.print(payload);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        statusMessage = String("deserializeJson() failed: ") + error.f_str();
        return;
    }

    JsonArray results = doc["results"];

    Serial.print("results.isNull(): ");
    Serial.println(results.isNull());
    Serial.print("results.size(): ");
    Serial.println(results.size());

    // Redraw the display contents
    display.clearDisplay();

    // Update the time
    display.setCursor(DISP_TIME_COORD_X, DISP_TIME_COORD_Y);
    display.setTextSize(1);
    print2Digits(currentHours);
    display.print(':');
    print2Digits(currentMinutes);

    // Update the tavla
    const int16_t x = DISP_DEPART_COORD_X;
    int16_t y = DISP_DEPART_COORD_Y;
    const int16_t y_spacing = DISP_DEPART_SPACING_Y;

    display.setTextSize(1);

    for (JsonObject o : results) {
        String depart = o["estimatedOtherwisePlannedTime"];

        int idxT = depart.indexOf('T');
        if (idxT < 0)
            continue;

        String departTime = depart.substring(idxT + 1, idxT + 6);

        String line = o["serviceJourney"]["line"]["shortName"];

        String platform = o["stopPoint"]["platform"];

        String direction = o["serviceJourney"]["directionDetails"]["shortDirection"];
        direction.replace("Å", "\xc5");
        direction.replace("å", "\xe5");
        direction.replace("Ä", "\xc4");
        direction.replace("ä", "\xe4");
        direction.replace("Ö", "\xd6");
        direction.replace("ö", "\xf6");

        if (o["isCancelled"].as<bool>())
            display.setTextColor(5, 7);
        else
            display.setTextColor(0, 7);

        display.setCursor(x, y);
        display.print(departTime);
        display.print(' ');
        display.print(line);
        display.print(' ');
        display.print(platform);
        display.print(' ');
        display.print(direction);

        y += y_spacing;
    }
}

void setup()
{
    Serial.begin(115200);

    display.begin();        // Init library (you should call this function ONLY ONCE)
    display.clearDisplay(); // Clear any data that may have been in (software) frame buffer.
    display.setFont(&FONT_NAME);

    display.setTextColor(0, 7);

    Serial.print("Connecting");
    displayGrid();
    displayStatus("Connecting...");
    display.display();

    WiFi.begin(wlanSSID, wlanPass);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }

    Serial.println();
    Serial.println("Connected");

    display.clearDisplay();
    displayGrid();
    displayStatus("Connected");
    display.display();

    ntpTime.begin();

    if (ntpTime.update()) {
        Serial.println("NTP Updated");
        display.rtcSetEpoch(ntpTime.getEpochTime());
    } else {
        Serial.println("NTP Update failed");
    }

    // Use https but don't use a certificate
    client.setInsecure();

    requestApiToken();
}

void loop()
{
    uint8_t wifiStatus = WiFi.status();

    // displayGrid();  // uncomment when modifying the layout
    updateWifiStatus(wifiStatus);
    updateTime();  // updates "needsUpdate"

    // Don't try updating the schedule if connections is lost
    if (wifiStatus == WL_CONNECTED) {
        if (needsUpdate) {
            updateAndRedrawSchedule();
        }
    }

    displayStatus(statusMessage);

    if (needsUpdate) {
        display.display();
        needsUpdate = false;
    }

    delay(1000);
}
