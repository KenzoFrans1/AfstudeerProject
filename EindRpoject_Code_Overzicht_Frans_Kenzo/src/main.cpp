#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <security.h>

// MOST UPDATE CODE 31/05/2025

// UPLOADEN OP POORT 2

String visitorsURL = "https://api.airtable.com/v0/" + airtableBaseId + "/Bezoekers";
String employeesURL = "https://api.airtable.com/v0/" + airtableBaseId + "/tblbCYNT2ZuuxwUJ4";

AsyncWebServer server(80);

String getCurrentDay() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Unknown";
  const char* weekdays[] = {"Zondag", "Maandag", "Dinsdag", "Woensdag", "Donderdag", "Vrijdag", "Zaterdag"};
  return String(weekdays[timeinfo.tm_wday]);
}

String getScheduleHTML() {
  String currentDay = getCurrentDay();
  String html = "";
  bool foundAny = false;
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Check visitors
    html += "<div class='section'><h3>👥 Bezoekers</h3>";
    http.begin(visitorsURL);
    http.addHeader("Authorization", "Bearer " + airtableApiKey);
    
    if (http.GET() == 200) {
      const size_t capacity = 2048;
      DynamicJsonDocument doc(capacity);
      
      if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
        bool foundVisitors = false;
        
        for (JsonObject record : doc["records"].as<JsonArray>()) {
          if (record["fields"].containsKey("Dagen")) {
            String days = record["fields"]["Dagen"].as<String>();
            if (days.indexOf(currentDay) != -1) {
              String name = record["fields"]["Naam"] | "Onbekend";
              String hours = record["fields"]["Uren"] | "Hele dag";
              String email = record["fields"]["Email"] | "Geen email";
              
              html += "<div class='person'>";
              html += "<div class='person-name'>" + name + "</div>";
              html += "<div class='person-info'>⏰ " + hours + "<br>📧 " + email + "</div>";
              html += "</div>";
              
              foundVisitors = true;
              foundAny = true;
            }
          }
        }
        
        if (!foundVisitors) {
          html += "<div class='no-data'>Geen bezoekers gepland</div>";
        }
      }
    } else {
      html += "<div class='no-data'>Fout bij ophalen bezoekers</div>";
    }
    http.end();
    html += "</div>";
    
    // Check employees
    html += "<div class='section'><h3>👷 Werknemers</h3>";
    http.begin(employeesURL);
    http.addHeader("Authorization", "Bearer " + airtableApiKey);
    
    if (http.GET() == 200) {
      const size_t capacity = 2048;
      DynamicJsonDocument doc(capacity);
      
      if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
        bool foundEmployees = false;
        
        for (JsonObject record : doc["records"].as<JsonArray>()) {
          if (record["fields"].containsKey("Dagen")) {
            String days = record["fields"]["Dagen"].as<String>();
            if (days.indexOf(currentDay) != -1) {
              String name = record["fields"]["Naam"] | "Onbekend";
              String hours = record["fields"]["Uren"] | "Hele dag";
              String uid = record["fields"]["UID"] | "Geen NFC";
              
              html += "<div class='person'>";
              html += "<div class='person-name'>" + name + "</div>";
              html += "<div class='person-info'>⏰ " + hours + "<br>🏷️ NFC: " + uid + "</div>";
              html += "</div>";
              
              foundEmployees = true;
              foundAny = true;
            }
          }
        }
        
        if (!foundEmployees) {
          html += "<div class='no-data'>Geen werknemers gepland</div>";
        }
      }
    } else {
      html += "<div class='no-data'>Fout bij ophalen werknemers</div>";
    }
    http.end();
    html += "</div>";
    
  } else {
    html = "<div class='no-data'>WiFi niet verbonden</div>";
  }
  
  if (!foundAny && WiFi.status() == WL_CONNECTED) {
    html = "<div class='no-data'>Geen personen gepland voor " + currentDay + "</div>";
  }
  
  return html;
}

// Complete HTML page with direct schedule display
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Dagplanning</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
        }
        .container {
            background: white;
            padding: 30px;
            border-radius: 15px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.3);
            max-width: 800px;
            margin: 0 auto;
        }
        h1 {
            color: #333;
            text-align: center;
            margin-bottom: 30px;
        }
        .refresh-btn {
            background: #4CAF50;
            color: white;
            border: none;
            padding: 10px 20px;
            font-size: 16px;
            border-radius: 8px;
            cursor: pointer;
            float: right;
            margin-bottom: 20px;
        }
        .refresh-btn:hover {
            background: #45a049;
        }
        .section {
            margin: 20px 0;
            clear: both;
        }
        .section h3 {
            background: #f0f0f0;
            padding: 10px;
            margin: 0 0 10px 0;
            border-radius: 5px;
        }
        .person {
            background: #f9f9f9;
            border-left: 4px solid #4CAF50;
            padding: 15px;
            margin: 8px 0;
            border-radius: 0 8px 8px 0;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        .person-name {
            font-weight: bold;
            color: #333;
            font-size: 18px;
        }
        .person-info {
            color: #666;
            font-size: 14px;
            margin-top: 8px;
            line-height: 1.4;
        }
        .loading {
            text-align: center;
            padding: 40px;
            color: #666;
            font-style: italic;
        }
        .no-data {
            text-align: center;
            color: #999;
            font-style: italic;
            padding: 20px;
        }
        .last-updated {
            text-align: center;
            color: #888;
            font-size: 12px;
            margin-top: 20px;
            clear: both;
        }
    </style>
</head>
<body>
    <div class="container">
        <button class="refresh-btn" onclick="loadSchedule()">🔄 Vernieuwen</button>
        <h1>📅 Planning</h1>
        
        <div id="scheduleContent">
            <div class="loading">Planning laden...</div>
        </div>
        
        <div class="last-updated" id="lastUpdated"></div>
    </div>

    <script>
        function loadSchedule() {
            document.getElementById('scheduleContent').innerHTML = '<div class="loading">Planning laden...</div>';
            
            fetch('/schedule')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('scheduleContent').innerHTML = data;
                    document.getElementById('lastUpdated').innerHTML = 'Laatst bijgewerkt: ' + new Date().toLocaleTimeString('nl-NL');
                })
                .catch(error => {
                    document.getElementById('scheduleContent').innerHTML = 
                        '<div class="no-data">Fout bij laden van planning</div>';
                });
        }

        // Load schedule when page loads
        window.onload = function() {
            loadSchedule();
        }

        // Auto-refresh every 5 minutes
        setInterval(loadSchedule, 300000);
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.println("IP address: " + WiFi.localIP().toString());
  
  // Setup time
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();
  
  // Web server routes with authentication
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->authenticate(www_username, www_password)) {
      return request->requestAuthentication();
    }
    request->send_P(200, "text/html", index_html);
  });
  
  server.on("/schedule", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->authenticate(www_username, www_password)) {
      return request->requestAuthentication();
    }
    String html = getScheduleHTML();
    request->send(200, "text/html", html);
  });
  
  server.begin();
  Serial.println("Web server started with authentication!");
  Serial.println("Open browser at: http://" + WiFi.localIP().toString());
  Serial.println("Username: " + String(www_username));
  Serial.println("Password: " + String(www_password));
}

void loop() {
  delay(1000);
}