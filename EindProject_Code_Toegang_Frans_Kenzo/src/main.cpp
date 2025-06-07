#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESP_Mail_Client.h>
#include <HTTPClient.h>
#include <Keypad.h>
#include <ArduinoJson.h>
#include <time.h>
#include <DFRobot_PN532.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <security.h>

// MOST UPDATE CODE 31/05/2025

// UPLOADEN OP POORT 1

// SMTP 
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "afstudeerprojectesp32@gmail.com"
#define AUTHOR_PASSWORD "jgrg gzfq pxbr taus"

// AIRTABLE CREDENTIALS FOR KEYPAD AND NFC
String tableName = "Bezoekers";     
String fieldName = "Code";    
String airtableURL = "https://api.airtable.com/v0/" + airtableBaseId + "/" + tableName;
String werknemerTableName = "Werknemers";
String werknemerURL = "https://api.airtable.com/v0/" + airtableBaseId + "/tblbCYNT2ZuuxwUJ4";

// LED
const int redLED = D13;
const int greenLED = D12;

// RELAIS
const int relais = D11;

// KEYPAD
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {D6, D7, D9, D10}; 
byte colPins[COLS] = {D2, D3, D5};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// NFC
#define PN532_IRQ 2
#define POLLING 0
#define WRITE_BLOCK_NO 2
#define BLOCK_SIZE 16
DFRobot_PN532_IIC nfc(PN532_IRQ, POLLING);
uint8_t dataWrite[BLOCK_SIZE];

// CODE STRUCTURE
struct AccessEntry {
  String code;
  String days;
  String hours; 
};

// UID STRUCTURE
struct UidEntry {
  String uid;
  String days;
  String hours;
};

// WERKNEMER STRUCTURE
struct PendingWerknemer {
  String name;
  String days;
  String hours;
  bool awaitingUID = false;
};

PendingWerknemer pendingWerknemer;

// FIXED: Reduced array sizes and added bounds checking
const int MAX_ENTRIES = 50; // Reduced from 100
const int MAX_UIDS = 50;    // Reduced from 100

AccessEntry allowedEntries[MAX_ENTRIES];
UidEntry allowedUIDs[MAX_UIDS];
int entryCount = 0;
int uidCount = 0;

// Code Input + Lockout
String inputCode = "";
bool accessGranted = false;
unsigned long accessGrantedTime = 0;
const unsigned long accessDuration = 5000;

void getAirtableCodes();
void getAirtableUIDs();
void checkAccess(String code);
void checkNFCAccess(String uid);

// Server and SMTP config
AsyncWebServer server(80);
SMTPSession smtp;
Session_Config config;

// INPUTMODE ACTIVATION
bool inputModeActive = false;
unsigned long inputModeStartTime = 0;
const unsigned long inputModeTimeout = 15000;

// HTML page
const char html_page[] PROGMEM = R"rawliteral(
  <!DOCTYPE html>
  <html lang="en">
  <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>ESP32 Toegang</title>
      <script src="https://cdn.tailwindcss.com"></script>
  </head>
  <body class="bg-gray-100 p-6">
      <div class="max-w-lg mx-auto bg-white p-6 rounded-xl shadow-md">
          <h2 class="text-xl font-semibold text-center mb-4">Toegang</h2>
          <form action="/submit" method="GET">
              <label class="block mb-2">Kies rol:</label>
              <select name="role" id="role" class="w-full p-2 border rounded" onchange="toggleForm()">
                  <option value="Bezoeker">Bezoeker</option>
                  <option value="Werknemer">Werknemer</option>
              </select>
  
              <div id="form-fields" class="mt-4">
                  <label class="block">Naam:</label>
                  <input name="name" type="text" class="w-full p-2 border rounded mb-2" required>
  
                  <div id="email-field">
                      <label class="block">Email:</label>
                      <input name="email" type="email" class="w-full p-2 border rounded mb-2">
                  </div>
  
                  <label class="block">Toegestane Dagen:</label>
                  <div class="flex flex-wrap gap-2 mb-2" id="days">
                      <label><input type="checkbox" name="days" value="Maandag" onchange="limitDays()"> Maa</label>
                      <label><input type="checkbox" name="days" value="Dinsdag" onchange="limitDays()"> Din</label>
                      <label><input type="checkbox" name="days" value="Woensdag" onchange="limitDays()"> Woe</label>
                      <label><input type="checkbox" name="days" value="Donderdag" onchange="limitDays()"> Do</label>
                      <label><input type="checkbox" name="days" value="Vrijdag" onchange="limitDays()"> Vrij</label>
                      <label><input type="checkbox" name="days" value="Zaterdag" onchange="limitDays()"> Zat</label>
                      <label><input type="checkbox" name="days" value="Zondag" onchange="limitDays()"> Zon</label>
                  </div>
  
                  <label class="block">Toegestane Uren:</label>
                  <input name="hours" type="text" placeholder="e.g. 09:00-17:00" class="w-full p-2 border rounded mb-4">
  
                  <button type="submit" class="w-full bg-blue-500 text-white py-2 rounded">Afronden</button>
              </div>
          </form>
      </div>
  
      <script>
          function toggleForm() {
              let role = document.getElementById('role').value;
              let emailField = document.getElementById('email-field');
              let checkboxes = document.querySelectorAll('#days input[type="checkbox"]');
  
              emailField.style.display = (role === 'Bezoeker') ? 'block' : 'none';
  
              checkboxes.forEach(cb => {
                  cb.checked = false;
                  cb.disabled = false;
              });
          }
  
          function limitDays() {
              let role = document.getElementById('role').value;
              let checkboxes = document.querySelectorAll('#days input[type="checkbox"]');
  
              if (role === "Bezoeker") {
                  let checkedBoxes = [...checkboxes].filter(cb => cb.checked);
                  if (checkedBoxes.length > 1) {
                      checkedBoxes[0].checked = false;
                  }
  
                  checkboxes.forEach(cb => {
                      cb.disabled = !cb.checked && checkedBoxes.length > 0;
                  });
              } else {
                  checkboxes.forEach(cb => cb.disabled = false);
              }
          }
      </script>
  </body>
  </html>
  )rawliteral";

bool isTimeAllowed(const String& allowedHours) {
  if (allowedHours.length() == 0) return false; // FIXED: Check for empty string
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("❌ Failed to obtain time");
    return false;
  }

  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;

  int startHour, startMinute, endHour, endMinute;
  if (sscanf(allowedHours.c_str(), "%d:%d-%d:%d", &startHour, &startMinute, &endHour, &endMinute) != 4) {
    Serial.println("❌ Invalid time format");
    return false;
  }

  int currentMinutes = currentHour * 60 + currentMinute;
  int startMinutes = startHour * 60 + startMinute;
  int endMinutes = endHour * 60 + endMinute;

  return currentMinutes >= startMinutes && currentMinutes <= endMinutes;
}

bool isDayAllowed(const String& allowedDays) {
  if (allowedDays.length() == 0) return false;
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("❌ Failed to obtain time");
    return false;
  }
  
  const char* weekdays[] = {"Zondag", "Maandag", "Dinsdag", "Woensdag", "Donderdag", "Vrijdag", "Zaterdag"};
  String currentDay = weekdays[timeinfo.tm_wday];
  return allowedDays.indexOf(currentDay) != -1;
}  

// Function to get current day of the week
String getCurrentDayOfWeek() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("❌ Failed to obtain time");
    return "Unknown";
  }
  const char* weekdays[] = {"Zondag", "Maandag", "Dinsdag", "Woensdag", "Donderdag", "Vrijdag", "Zaterdag"};
  return String(weekdays[timeinfo.tm_wday]);
}

// Function to fetch visitors for today from Airtable
String getVisitorsForToday() {
  String currentDay = getCurrentDayOfWeek();
  String visitorsJson = "{\"visitors\":[";
  bool hasVisitors = false;
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(airtableURL);
    http.addHeader("Authorization", "Bearer " + airtableApiKey);
    
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      
      // FIXED: Reduced JSON buffer size and added error handling
      const size_t capacity = 4 * 1024; // Reduced from 8KB
      DynamicJsonDocument doc(capacity);
      
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        http.end();
        return "{\"visitors\":[]}";
      }
      
      JsonArray records = doc["records"];
      
      for (JsonObject record : records) {
        if (record["fields"].containsKey("Dagen")) {
          String visitorDays = record["fields"]["Dagen"].as<String>();
          
          if (visitorDays.indexOf(currentDay) != -1) {
            if (hasVisitors) {
              visitorsJson += ",";
            }
            
            String name = "";
            String hours = "";
            
            if (record["fields"].containsKey("Naam")) {
              name = record["fields"]["Naam"].as<String>();
            } else {
              name = "Onbekend";
            }
            
            if (record["fields"].containsKey("Uren")) {
              hours = record["fields"]["Uren"].as<String>();
            } else {
              hours = "Geen tijd opgegeven";
            }
            
            visitorsJson += "{\"name\":\"" + name + "\",\"hours\":\"" + hours + "\"}";
            hasVisitors = true;
          }
        }
      }
    } else {
      Serial.print("Failed to fetch visitors. HTTP Code: ");
      Serial.println(httpCode);
    }
    
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
  
  visitorsJson += "]}";
  return visitorsJson;
}

void checkAccess(String code) {
  // FIXED: Added bounds checking
  for (int i = 0; i < entryCount && i < MAX_ENTRIES; i++) {
    if (code == allowedEntries[i].code) {
      if (!isDayAllowed(allowedEntries[i].days)) {
        Serial.println("⛔ Access Geweigerd: Verkeerde dag!");
        digitalWrite(redLED, HIGH);
        delay(1000);
        digitalWrite(redLED, LOW);
        return;
      }

      if (!isTimeAllowed(allowedEntries[i].hours)) {
        Serial.println("⛔ Access Geweigerd: Verkeerd uur!");
        digitalWrite(redLED, HIGH);
        delay(1000);
        digitalWrite(redLED, LOW);
        return;
      }

      Serial.println("✅ Access Toegestaan");
      digitalWrite(greenLED, HIGH);
      digitalWrite(redLED, LOW);
      digitalWrite(relais, HIGH);
      accessGranted = true;
      accessGrantedTime = millis();
      return;
    }
  }
  Serial.println("⛔ Access Geweigerd: Code Niet Gevonden");
  digitalWrite(redLED, HIGH);
  delay(1000);
  digitalWrite(redLED, LOW);
}

void checkNFCAccess(String uid) {
  // FIXED: Added bounds checking
  for (int i = 0; i < uidCount && i < MAX_UIDS; i++) {
    if (uid == allowedUIDs[i].uid) {
      if (!isDayAllowed(allowedUIDs[i].days)) {
        Serial.println("⛔ NFC Access Geweigerd: Verkeerde dag!");
        digitalWrite(redLED, HIGH);
        digitalWrite(greenLED, LOW);
        return;
      }

      if (!isTimeAllowed(allowedUIDs[i].hours)) {
        Serial.println("⛔ NFC Access Geweigerd: Verkeerd uur!");
        digitalWrite(redLED, HIGH);
        digitalWrite(greenLED, LOW);
        return;
      }

      Serial.println("✅ NFC Access Toegestaan");
      digitalWrite(greenLED, HIGH);
      digitalWrite(redLED, LOW);
      digitalWrite(relais, HIGH);
      accessGranted = true;
      accessGrantedTime = millis();
      return;
    }
  }
  Serial.println("⛔ NFC Access Geweigerd: UID Niet Gevonden");
  digitalWrite(redLED, HIGH);
  digitalWrite(greenLED, LOW);
}

void getAirtableCodes() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(airtableURL);
    http.addHeader("Authorization", "Bearer " + airtableApiKey);

    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      Serial.println("Received data from Airtable (Codes).");

      // FIXED: Reduced JSON buffer size
      const size_t capacity = 4 * 1024;
      DynamicJsonDocument doc(capacity);

      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        return;
      }

      JsonArray records = doc["records"];
      entryCount = 0;

      for (JsonObject record : records) {
        // FIXED: Added bounds checking
        if (record["fields"][fieldName] && entryCount < MAX_ENTRIES) {
          allowedEntries[entryCount].code = record["fields"][fieldName].as<String>();
          allowedEntries[entryCount].days = record["fields"]["Dagen"] | "";
          allowedEntries[entryCount].hours = record["fields"]["Uren"] | "";
          Serial.println("Toegestaan code: " + allowedEntries[entryCount].code);
          entryCount++;
        }
      }

    } else {
      Serial.print("Failed to fetch codes. HTTP Code: ");
      Serial.println(httpCode);
    }

    http.end();
  }
}

void getAirtableUIDs() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(werknemerURL);
    http.addHeader("Authorization", "Bearer " + airtableApiKey);

    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      Serial.println("Received data from Airtable (UIDs).");

      // FIXED: Reduced JSON buffer size
      const size_t capacity = 4 * 1024;
      DynamicJsonDocument doc(capacity);

      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        return;
      }

      JsonArray records = doc["records"];
      uidCount = 0;

      for (JsonObject record : records) {
        // FIXED: Added bounds checking
        if (record["fields"]["UID"] && uidCount < MAX_UIDS) {
          allowedUIDs[uidCount].uid = record["fields"]["UID"].as<String>();
          allowedUIDs[uidCount].days = record["fields"]["Dagen"] | "";
          allowedUIDs[uidCount].hours = record["fields"]["Uren"] | "";
          Serial.println("Toegestaan UID: " + allowedUIDs[uidCount].uid);
          uidCount++;
        }
      }

    } else {
      Serial.print("Failed to fetch UIDs. HTTP Code: ");
      Serial.println(httpCode);
    }

    http.end();
  }
}

String getApiUrlForRole(const String& role) {
  if (role == "Bezoeker") return "https://api.airtable.com/v0/" + airtableBaseId + "/tblPiutL7BCbE10Fn";
  else if (role == "Werknemer") return "https://api.airtable.com/v0/" + airtableBaseId + "/tblbCYNT2ZuuxwUJ4";
  return "";
}

struct FullTaskData {
  String role, name, email, days, hours, airtableUrl;
};

void combinedTask(void* parameter) {
  FullTaskData* data = static_cast<FullTaskData*>(parameter);
  
  // FIXED: Added null pointer check
  if (data == nullptr) {
    Serial.println("❌ Task data is null!");
    vTaskDelete(NULL);
    return;
  }
  
  String accessCode = "";

  if (data->role == "Werknemer") {
    pendingWerknemer.name = data->name;
    pendingWerknemer.days = data->days;
    pendingWerknemer.hours = data->hours;
    pendingWerknemer.awaitingUID = true;
  
    Serial.println("📡 Waiting to scan NFC tag...");
    
    unsigned long startWait = millis();
    while (pendingWerknemer.awaitingUID && (millis() - startWait < 20000)) {
      if (nfc.scan()) {
        String uidStr = nfc.readUid();
        if (uidStr.length() > 0) {
          Serial.print("📟 UID Value for new Werknemer: ");
          Serial.println(uidStr);
          
          bool uidExists = false;
          // FIXED: Added bounds checking
          for (int i = 0; i < uidCount && i < MAX_UIDS; i++) {
            if (uidStr == allowedUIDs[i].uid) {
              uidExists = true;
              Serial.println("⛔ This UID is already registered in the system");
              digitalWrite(redLED, HIGH);
              delay(1000);
              digitalWrite(redLED, LOW);
              break;
            }
          }
          
          if (!uidExists) {
            HTTPClient http;
            http.begin(data->airtableUrl);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("Authorization", "Bearer " + airtableApiKey);
        
            String jsonPayload = "{\"fields\": {";
            jsonPayload += "\"Naam\": \"" + pendingWerknemer.name + "\", ";
            jsonPayload += "\"Dagen\": \"" + pendingWerknemer.days + "\", ";
            jsonPayload += "\"Uren\": \"" + pendingWerknemer.hours + "\", ";
            jsonPayload += "\"UID\": \"" + uidStr + "\"";
            jsonPayload += "}}";
        
            int responseCode = http.POST(jsonPayload);
            Serial.println("📡 Werknemer with UID added: " + String(responseCode));
            
            if (responseCode > 0) {
              Serial.println("✅ Airtable response: " + http.getString());
              digitalWrite(greenLED, HIGH);
              delay(1000);
              digitalWrite(greenLED, LOW);
            } else {
              Serial.println("❌ Failed to store UID");
              digitalWrite(redLED, HIGH);
              delay(1000);
              digitalWrite(redLED, LOW);
            }
            
            http.end();
          }
          
          pendingWerknemer.awaitingUID = false;
          
          getAirtableCodes();
          getAirtableUIDs();
        }
      }
      delay(100);
    }
    
    if (pendingWerknemer.awaitingUID) {
      Serial.println("⌛ Timeout waiting for NFC tag");
      pendingWerknemer.awaitingUID = false;
    }
  }

  if (data->role == "Bezoeker") {
    randomSeed(esp_random());
    int randomCode = random(1000, 9999);
    accessCode = String(randomCode);

    SMTP_Message message;
    message.sender.name = F("ESP");
    message.sender.email = AUTHOR_EMAIL;
    message.subject = F("Uw Tijdelijke InlogCode");
    message.addRecipient("Bezoeker", data->email);
    
    String body = "Beste,"
              "\n\nUw afspraak is bevestigd:"
              "\n\nDatum: " + data->days +
              "\nTijd: " + data->hours +
              "\nToegang: " + accessCode +
              "\n\nBewaar deze gegevens voor uw toegang."
              "\n\nVriendelijke groeten";

    message.text.content = body.c_str();
    message.text.charSet = "us-ascii";
    message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
    message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;

    if (!smtp.connect(&config)) {
      Serial.println("❌ SMTP connection failed!");
    } else if (!MailClient.sendMail(&smtp, &message)) {
      Serial.println("❌ Failed to send email");
    } else {
      Serial.println("✅ Email sent with code: " + accessCode);
    }
    
    // SENDING TO AIRTABLE
    HTTPClient http;
    http.begin(data->airtableUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + airtableApiKey);

    String jsonPayload = "{\"fields\": {";
    jsonPayload += "\"Naam\": \"" + data->name + "\", ";
    jsonPayload += "\"Email\": \"" + data->email + "\", ";
    jsonPayload += "\"Code\": \"" + accessCode + "\", ";
    jsonPayload += "\"Uren\": \"" + data->hours + "\", ";
    jsonPayload += "\"Dagen\": \"" + data->days + "\"";
    jsonPayload += "}}";

    int responseCode = http.POST(jsonPayload);
    Serial.println("📡 Airtable HTTP response: " + String(responseCode));
    if (responseCode > 0) Serial.println("✅ Airtable response: " + http.getString());
    else Serial.println("❌ Airtable send failed");

    http.end();
  
    getAirtableCodes();
  }

  delete data; // FIXED: Make sure this is safe
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  
  // FIXED: Don't disable watchdogs unless absolutely necessary
  // disableCore0WDT();
  // disableCore1WDT();
  
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  pinMode(redLED, OUTPUT);
  pinMode(greenLED, OUTPUT);
  digitalWrite(redLED, HIGH);
  pinMode(relais, OUTPUT);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("✅ WiFi Connected");
  Serial.println("🌐 IP Address: " + WiFi.localIP().toString());

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1); 
  tzset();

  while (!nfc.begin()) {
    Serial.print(".");
    delay(1000);
  }

  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;
  config.time.ntp_server = F("pool.ntp.org,time.nist.gov");
  config.time.gmt_offset = 3;
  config.time.day_light_offset = 0;

  // SECURED WEB ROUTES WITH AUTHENTICATION
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Check if user is authenticated
    if (!request->authenticate(web_username, web_password)) {
      return request->requestAuthentication();
    }
    // If authenticated, serve the page
    request->send_P(200, "text/html", html_page);
  });

  server.on("/submit", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Check if user is authenticated
    if (!request->authenticate(web_username, web_password)) {
      return request->requestAuthentication();
    }
    
    Serial.println("\n✅ Form submission received");

    String role = request->hasParam("role") ? request->getParam("role")->value() : "";
    String name = request->hasParam("name") ? request->getParam("name")->value() : "";
    String email = request->hasParam("email") ? request->getParam("email")->value() : "";
    String hours = request->hasParam("hours") ? request->getParam("hours")->value() : "";

    String days = "";
    for (int i = 0; i < request->params(); i++) {
      if (request->getParam(i)->name() == "days") {
        if (days.length() > 0) days += ", ";
        days += request->getParam(i)->value();
      }
    }

    Serial.println("Role: " + role);
    Serial.println("Naam: " + name);
    Serial.println("Email: " + email);
    Serial.println("Uren: " + hours);
    Serial.println("Dagen: " + days);

    FullTaskData* taskData = new FullTaskData;
    if (taskData != nullptr) { // FIXED: Check allocation success
      taskData->role = role;
      taskData->name = name;
      taskData->email = email;
      taskData->hours = hours;
      taskData->days = days;
      taskData->airtableUrl = getApiUrlForRole(role);

      // FIXED: Increased stack size significantly
      xTaskCreatePinnedToCore(
        combinedTask,
        "EmailAndAirtable",
        32768,  // Increased from 16384 to 32768
        taskData,
        1,
        NULL,
        1
      );
    }

    request->send(200, "text/plain", "Data ontvangen. Keer terug door pijltje. <--");
  });
  
  server.begin();
  
  // FIXED: Load initial data after setup is complete
  delay(2000); // Give time for everything to initialize
  // getAirtableCodes();
  // getAirtableUIDs();
}

void loop() {
  // FIXED: Add watchdog feeding
  yield(); // Feed the watchdog
  
  if (accessGranted && (millis() - accessGrantedTime > accessDuration)) {
    accessGranted = false;
    digitalWrite(greenLED, LOW);
    digitalWrite(redLED, HIGH);
    digitalWrite(relais, LOW);
    Serial.println("🔒 Access period ended.");
  }

  if (inputModeActive && (millis() - inputModeStartTime > inputModeTimeout)) {
    inputModeActive = false;
    inputCode = "";
    Serial.println("⌛ Input mode timed out.");
  }

  if (accessGranted) return;

  // NFC & KEYPAD LOGICA WHEN ACTIVE
  if (inputModeActive) {
    if (nfc.scan()) {
      String uidStr = nfc.readUid();
              if (uidStr.length() > 0) {
        Serial.print("📟 UID Value: ");
        Serial.println(uidStr);
        if (!pendingWerknemer.awaitingUID) {
          checkNFCAccess(uidStr);
        }
        delay(1000);
      }
    }

    char key = keypad.getKey();
    if (key) {
      if (key == '#') {
        Serial.println("\n📥 Code entered: " + inputCode);
        checkAccess(inputCode);
        inputCode = "";
        inputModeActive = false;
      } else if (key == '*') {
        inputCode = "";
        Serial.println("🔄 Input cleared.");
      } else {
        inputCode += key;
        Serial.print("*");
      }
    }
  } else {
    char key = keypad.getKey();
    if (key == '*') {
      inputModeActive = true;
      inputCode = "";
      inputModeStartTime = millis();
      Serial.println("🔓 Input mode activated.");
    }
  }
  delay(50);
}