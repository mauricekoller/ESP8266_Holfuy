#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <URLEncoder.h>
#include <WebSocketClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <b64.h>
#include <math.h>

#include "defines.h"

void RenderWeather(String station_name, DynamicJsonDocument json);
void RenderCurve(DynamicJsonDocument curve);
DynamicJsonDocument FetchData(String url);
String StrPad(int value, int width, bool trailing = true);
String BuildUrl(String url);

Adafruit_SSD1306 display = Adafruit_SSD1306(DISPLAY_W, DISPLAY_H, &Wire, -1);

// Create an instance of the HttpClient
WiFiClient client;
HTTPClient http;

void setup() {
    Serial.begin(115200);

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.setRotation(0);
    display.clearDisplay();
    display.display();

    WiFi.begin(SSID, PWD);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Connecting to Wifi...\n");
    display.display();

    int i = 0;
    while (WiFi.status() != WL_CONNECTED) {  // Wait for the Wi-Fi to connect
        delay(500);
        Serial.print(".");
    }

    Serial.println('\n');
    Serial.println("Connection established!");
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP());

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("WiFi established\n");
    display.print("IP: ");
    display.print(WiFi.localIP());
    display.display();
    delay(1000);
    display.clearDisplay();
    display.display();
}
String live_url = BuildUrl(HOLFUY_LIVE);
String archive_url = BuildUrl(HOLFUY_ARCHIVE);

unsigned long lastUpdate = UPDATE_INTERVAL;

void loop() {
    if (lastUpdate >= UPDATE_INTERVAL) {
        DynamicJsonDocument live_json = FetchData(HOLFUY_URL + live_url);
        RenderWeather(STATION_NAME, live_json);
        DynamicJsonDocument archive_json = FetchData(HOLFUY_URL + archive_url);
        lastUpdate = archive_json["measurements"][0]["secondsBack"];
        RenderCurve(archive_json);
    }
    display.setCursor(88, 0);
    display.fillRect(88, 0, 43, 8, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE);
    display.print(StrPad(lastUpdate++, 3));
    display.print("s");
    display.display();
    delay(1000);
}

/**
 * @brief Renders weather information.
 * Does not actually display, use display.display() afterwards.
 * @param station_name The name of the weather station.
 * @param json The weather information in a DynamicJsonDocument object.
 */
void RenderWeather(String station_name, DynamicJsonDocument json) {
    int speed = json["wind"]["speed"];
    int gust = json["wind"]["gust"];
    int dir = json["wind"]["direction"];
    int temperature = json["temperature"];

    display.clearDisplay();

    // display station_name
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println(station_name);

    // display wind and temperature
    display.setTextSize(1);
    display.println();
    display.print(StrPad(speed, 3));
    display.println("kmh ");
    display.print(StrPad(gust, 3));
    display.println("kmh");
    display.print(StrPad(dir, 3));
    display.println((char)247);
    display.print(StrPad(temperature, 3));
    display.print((char)247);
    display.println("C");
}

/**
 * @brief Renders a wind curve.
 * Does not actually display, use display.display() afterwards.
 * @param curve A DynamicJsonDocument containing wind measurements.
 */
void RenderCurve(DynamicJsonDocument curve) {
    int len = curve["measurements"].size();
    int min_speed = 0, max_speed = 0, min_gust = 0, max_gust = 0;
    int speeds[len], gusts[len], dirs[len];
    int idx = 0;
    for (JsonObject measurement : curve["measurements"].as<JsonArray>()) {
        int speed = measurement["wind"]["speed"];
        speeds[idx] = speed;
        max_speed = max(max_speed, speed);

        int gust = measurement["wind"]["gust"];
        gusts[idx] = gust;
        max_gust = max(max_gust, gust);

        dirs[idx] = measurement["wind"]["directions"];
        idx++;
    }

    int start_x = DISPLAY_W / 2;
    int start_y = 60;
    int y_height = 40;   // height of the y axis
    int pixel_size = 1;  // with of each measurement
    float factor = y_height / (max_gust + 1.0);

    // Draw x,y-axis
    display.drawLine(start_x, start_y, DISPLAY_W, start_y, SSD1306_WHITE);
    display.drawLine(start_x, start_y, start_x, start_y - y_height,
                     SSD1306_WHITE);

    // Display y-axis labels
    display.setCursor(start_x - 12, start_y - y_height);
    display.print(StrPad(max_gust, 2, false));
    display.setCursor(start_x - 7, start_y - 8);
    display.print("0");

    // Draw measurements
    int offset = 1;
    for (int i = len - 1; i > 0; i--) {
        int speed = speeds[i];
        int gust = gusts[i];
        Serial.println(round(gust * factor));
        // Draw gusts as white bar
        display.fillRect(start_x + offset * pixel_size,
                         round(start_y - gust * factor), pixel_size,
                         round(gust * factor), SSD1306_WHITE);
        // Overlay speed as black bar
        int top_y = round(start_y - speed * factor);
        display.fillRect(start_x + offset * pixel_size, top_y, pixel_size,
                         start_y - top_y, SSD1306_BLACK);
        offset++;
    }
}

/**
 * @brief Fetches JSON data from the specified URL and returns a
 * DynamicJsonDocument object.
 *
 * @param url The URL to fetch JSON data from.
 * @return A DynamicJsonDocument object containing the fetched JSON data.
 */
DynamicJsonDocument FetchData(String url) {
    DynamicJsonDocument jsonDoc(JSON_BUFFER_SIZE);
    Serial.println("fetching: " + url);
    http.useHTTP10(true);
    http.begin(client, url);
    // Check HTTP status code
    int statusCode = http.GET();
    // Check if the request was successful
    if (statusCode == HTTP_CODE_OK ||
        statusCode == HTTP_CODE_MOVED_PERMANENTLY) {
        deserializeJson(jsonDoc, http.getStream());
    } else {
        Serial.println("Failed to fetch JSON data");
    }
    http.end();
    return jsonDoc;
}

/**
 * @brief Pads a string with spaces to a specified width.
 *
 * @param value The integer value to be converted to a string and padded.
 * @param width The desired width of the resulting string.
 * @param trailing If true, pads the string with spaces after the value. If
 * false, pads the string with spaces before the value.
 * @return String The padded string.
 */
String StrPad(int value, int width, bool trailing) {
    String str = String(value);
    while (str.length() < width) {
        str = " " + str;
    }
    if (!trailing) return str;
    return str + " ";
}

/**
 * @brief Replaces placeholders in the given URL string with the STATION_ID and
 * API_KEY, and returns the modified URL.
 * @param url The URL string with placeholders for the STATION_ID and API_KEY.
 * @return The modified URL string with the placeholders replaced by the actual
 * STATION_ID and API_KEY.
 */
String BuildUrl(String url) {
    url.replace("STATION", STATION_ID);
    url.replace("API-KEY", API_KEY);
    return url;
}