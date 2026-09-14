

void initNetworkHardwarePins() {
  pinMode(ETH_RST, OUTPUT);
  digitalWrite(ETH_RST, LOW);   
  delay(50);                   
  digitalWrite(TFT_RST, HIGH);  
  digitalWrite(ETH_RST, HIGH);  
  delay(50);                   
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
}

void printBootStatusNotification() {
  Serial.println("\n==================================================");
  Serial.println("🚀 SYSTEM INITIALIZED AND RUNNING SUCCESSFULLY!");
  Serial.print("🌐 Login Dashboard URL Target: http://");
  Serial.println(Ethernet.localIP()); 
  Serial.print("🛠️ Hardware Status Code: ");
  Serial.println((int)Ethernet.hardwareStatus()); 
  Serial.println("==================================================\n");
}

void runWebserverProcessingBucket() {
  for (int i = 0; i < 4; i++) {
    EthernetClient client = server.available();
    if (client) {
      handleNativeWebTraffic(client); 
    }
  }
}

void sendNTPpacket(const char* address) {
  const int NTP_PACKET_SIZE = 48;
  byte packetBuffer[NTP_PACKET_SIZE];
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;   
  Udp.beginPacket(address, 123); 
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

time_t getNtpTime() {
  const char* ntpServerName = "pool.ntp.org";
  const int NTP_PACKET_SIZE = 48;
  byte packetBuffer[NTP_PACKET_SIZE];
  while (Udp.parsePacket() > 0) ; 
  Serial.println("Sending outbound atomic time request to NTP pool...");
  sendNTPpacket(ntpServerName);
  
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    if (Udp.parsePacket() >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  
      unsigned long secsSince1900;
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + (config.tzOffset * 3600);
    }
  }
  return 0; 
}


void handleNativeWebTraffic(EthernetClient& client) {
    String req = client.readStringUntil('\r'); 
    
    // --- Asynchronous Background JSON API Data Provider ---
    if (req.indexOf("GET /ajax_data") != -1) {
        client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n");
        
        float liveLocal   = config.isFahrenheit ? ((localTempC * 9.0 / 5.0) + 32.0) : localTempC;
        float liveNetwork = config.isFahrenheit ? ((networkTempC * 9.0 / 5.0) + 32.0) : networkTempC;
        float liveBlended = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;
        
        String json = "{";
        json += "\"local_temp\":" + String(liveLocal, 1) + ",";
        json += "\"net_temp\":" + String(liveNetwork, 1) + ",";
        json += "\"blend_temp\":" + String(liveBlended, 1) + ",";
        json += "\"fans\":[";
        for (int i = 0; i < 4; i++) {
            json += String(currentRPMs[i]);
            if (i < 3) json += ","; 
        }
        json += "]}";
        
        client.println(json);
        delay(1); client.stop();
        return;
    }

    bool networkSettingsChanged = false;
    bool regularSettingsChanged = false;

// Handle Form Configuration Processing Loops
    if (req.indexOf("POST /submit") != -1 || req.indexOf("GET /submit?") != -1) {
        String body = "";
        if (req.indexOf("POST") != -1) {
            // Allow TCP packets to populate the buffer
            unsigned long postTimeout = millis();
            while (!client.available() && (millis() - postTimeout < 500)) {
                delay(1);
            }
            while (client.available()) {
                String line = client.readStringUntil('\n');
                if (line == "\r") {
                    body = client.readString();
                    break;
                }
            }
        } else {
            body = req;
        }

        String unitParam = getUrlParam(body, "unit=");
        bool submittedAsFahrenheit = (unitParam == "F");
        
        String tminStr = getUrlParam(body, "tmin=");
        String tmaxStr = getUrlParam(body, "tmax=");
        
        if (tminStr.length() > 0 && tmaxStr.length() > 0) {
            float parsedMin = tminStr.toFloat();
            float parsedMax = tmaxStr.toFloat();
            
            if (submittedAsFahrenheit) {
                config.tMin = (parsedMin - 32.0) * 5.0 / 9.0;
                config.tMax = (parsedMax - 32.0) * 5.0 / 9.0;
            } else {
                config.tMin = parsedMin;
                config.tMax = parsedMax;
            }
        }

        // Fixed: Use parsed form boolean instead of the prototype function address
        config.isFahrenheit = submittedAsFahrenheit;
        config.is24Hour = (getUrlParam(body, "clk=") == "24");
        config.tzOffset = getUrlParam(body, "tz=").toInt();
        config.fanCount = getUrlParam(body, "fancnt=").toInt();

        // [Remaining string parsing parameters follow as before...]

        String nodeParam = getUrlParam(body, "nodeid=");
        if (nodeParam.length() > 0) {
            nodeParam.replace(" ", "_");
            strncpy(config.nodeID, nodeParam.c_str(), sizeof(config.nodeID) - 1);
            config.nodeID[sizeof(config.nodeID) - 1] = '\0';
        }

        String hostParam = getUrlParam(body, "hahost=");
        if (hostParam.length() > 0 && hostParam != "") {
            hostParam.replace(" ", ""); 
            strncpy(config.haHost, hostParam.c_str(), sizeof(config.haHost) - 1);
            config.haHost[sizeof(config.haHost) - 1] = '\0';
        }

        String portParam = getUrlParam(body, "haport=");
        if (portParam.length() > 0) {
            int checkPort = portParam.toInt();
            if (checkPort > 0) config.haPort = checkPort;
        }

        String sensorParam = getUrlParam(body, "hasensor=");
        if (sensorParam.length() > 0 && sensorParam != "") {
            sensorParam.replace(" ", "_");
            strncpy(config.haSensor, sensorParam.c_str(), sizeof(config.haSensor) - 1);
            config.haSensor[sizeof(config.haSensor) - 1] = '\0';
        }

        String tokenParam = getUrlParam(body, "hatoken=");
        if (tokenParam.length() > 0 && tokenParam != "") {
            strncpy(config.haToken, tokenParam.c_str(), sizeof(config.haToken) - 1);
            config.haToken[sizeof(config.haToken) - 1] = '\0';
        }

        String ipStr = getUrlParam(body, "ip=");
        if (ipStr.length() > 0) {
            parseIpString(ipStr, config.ip);
            parseIpString(getUrlParam(body, "sub="), config.subnet);
            parseIpString(getUrlParam(body, "gw="), config.gateway); // 🌟 FIXED: Target gateway correctly
            parseIpString(getUrlParam(body, "dns="), config.dns);
            networkSettingsChanged = true;
        }

        saveSettings();
        regularSettingsChanged = true;
    }

    if (networkSettingsChanged || regularSettingsChanged) {
        client.println("HTTP/1.1 303 See Other");
        client.println("Location: /"); 
        client.println("Connection: close\r\n");
        client.stop();
        if (networkSettingsChanged) {
            delay(500); ESP.restart(); 
        }
        return;
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Cache-Control: no-cache, no-store, must-revalidate"); 
    client.println("Pragma: no-cache");
    client.println("Expires: 0");
    client.println("Connection: close");
    client.println(); 

    // ==========================================
    // 🌟 FIXED: Map display thresholds straight to your master system struct tracks
    // ==========================================
    float displayMin = config.isFahrenheit ? ((config.tMin * 9.0 / 5.0) + 32.0) : config.tMin;
    float displayMax = config.isFahrenheit ? ((config.tMax * 9.0 / 5.0) + 32.0) : config.tMax;
    String scaleSymbol = config.isFahrenheit ? "F" : "C";

    float initialLocal   = config.isFahrenheit ? ((localTempC * 9.0 / 5.0) + 32.0) : localTempC;
    float initialNetwork = config.isFahrenheit ? ((networkTempC * 9.0 / 5.0) + 32.0) : networkTempC;
    float initialBlended = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;


    int currentHour = hour();
    int currentMinute = minute();
    int currentSecond = second();

    client.println("<!DOCTYPE html><html><head><title>S3 Dashboard</title>");
    client.println("<style>body{font-family:sans-serif; background:#f4f7f6; padding:20px; text-align:center;} .box{background:white; max-width:550px; margin:auto; padding:25px; border-radius:6px; box-shadow:0 2px 10px rgba(0,0,0,0.05); text-align:left;} input[type=text], input[type=number], select, textarea{width:100%; padding:10px; margin:5px 0 15px 0; border:1px solid #ccc; border-radius:4px; box-sizing:border-box;} .grid{display:flex; justify-content:space-between; margin-bottom:15px; flex-wrap:wrap;} .card{background:#edf1f5; padding:12px; border-radius:4px; width:30%; text-align:center; font-size:12px; box-sizing:border-box;} .card-large{width:100%; margin-bottom:15px; background:#edf1f5; padding:15px; border-radius:4px; text-align:center;} .fan-box{background:#f8f9fa; padding:10px; margin:5px 0; border-left:4px solid #17a2b8; display:flex; justify-content:space-between; font-size:14px;} .btn{width:100%; padding:14px; background:#28a745; color:white; border:none; font-weight:bold; font-size:16px; border-radius:4px; cursor:pointer; margin-top:20px;}</style>");
    
    client.println("<script>");
    client.print("let h = "); client.print(currentHour); client.println(";");
    client.print("let m = "); client.print(currentMinute); client.println(";");
    client.print("let s = "); client.print(currentSecond); client.println(";");
    client.print("const is24 = "); client.print(config.is24Hour ? "true" : "false"); client.println(";");
    
    client.println("function updateLiveClock() {");
    client.println("  s++; if(s>=60){ s=0; m++; if(m>=60){ m=0; h++; if(h>=24){ h=0; } } }");
    client.println("  let displayH = h; let ampm = '';");
    client.println("  if(!is24) { ampm = displayH >= 12 ? ' PM' : ' AM'; displayH = displayH % 12; if(displayH === 0) displayH = 12; }");
    client.println("  let strH = displayH < 10 ? '0'+displayH : displayH;");
    client.println("  let strM = m < 10 ? '0'+m : m;");
    client.println("  let strS = s < 10 ? '0'+s : s;");
    client.println("  let timeElement = document.getElementById('liveClockText');");
    client.println("  if(timeElement) { timeElement.innerText = strH + ':' + strM + ':' + strS + ampm; }");
    client.println("}");
    
    client.println("function fetchLiveTelemetry() {");
    client.println("  fetch(\"/ajax_data\").then(response => response.json()).then(data => {");
    client.println("    document.getElementById(\"liveLocalText\").innerText = data.local_temp;");
    client.println("    document.getElementById(\"liveNetText\").innerText = data.net_temp;");
    client.println("    document.getElementById(\"liveBlendText\").innerText = data.blend_temp;");
    client.println("    data.fans.forEach((rpm, index) => {");
    client.println("      let fanEl = document.getElementById(\"fanRpm_\" + index);");
    client.println("      if(fanEl) fanEl.innerText = rpm + \" RPM\";");
    client.println("    });");
    client.println("  }).catch(err => console.error(\"Data drop:\", err));");
    client.println("}");
    
    client.println("setInterval(updateLiveClock, 1000);");
    client.println("setInterval(fetchLiveTelemetry, 2000);");
    client.println("</script>");

    client.println("</head><body>");
    
    client.println("<div class='box'><h2 style='text-align:center; color:#0056b3; margin-top:0;'>ESP32-S3 Network Matrix Console</h2>");
    client.println("<div class='card-large'>🕒 <strong>System Clock</strong><br><span id='liveClockText' style='font-size:20px; color:#0056b3; font-weight:bold;'>Syncing...</span></div>");
    
    client.println("<div class='grid'>");
    client.print("<div class='card'>📌 <strong>Local Probe</strong><br><span style='font-size:14px; color:#28a745; font-weight:bold;'><span id='liveLocalText'>"); client.print(initialLocal, 1); client.println("</span> &deg;" + scaleSymbol + "</span></div>");
    client.print("<div class='card'>🌐 <strong>HA Network</strong><br><span style='font-size:14px; color:#0056b3; font-weight:bold;'><span id='liveNetText'>"); client.print(initialNetwork, 1); client.println("</span> &deg;" + scaleSymbol + "</span></div>");
    client.print("<div class='card'>⚖️ <strong>Blended Avg</strong><br><span style='font-size:14px; color:#e0a800; font-weight:bold;'><span id='liveBlendText'>"); client.print(initialBlended, 1); client.println("</span> &deg;" + scaleSymbol + "</span></div>");
    client.println("</div><hr>");
    
    client.println("<h3>📊 Live Tachometer Metrics</h3>");
    for(int i = 0; i < 4; i++) {
        client.print("<div class='fan-box'><span><strong>Fan Channel " + String(i+1) + "</strong> ");
        if (i < config.fanCount) {
            client.print("<span style='color:#28a745; font-size:11px;'>[Active]</span>");
        } else {
            client.print("<span style='color:#dc3545; font-size:11px;'>[Disabled/Expansion]</span>");
        }
        client.print("</span><span id='fanRpm_" + String(i) + "' style='font-weight:bold; color:#17a2b8;'>0 RPM</span></div>");
    }
    client.println("<hr>");

    client.println("<form action='/submit' method='post'>");
    
    client.println("<h3>⚙️ Active Core Infrastructure Scaling</h3>");
    client.print("<div style='margin-bottom:20px; display:flex; gap:15px;'>");
    client.print("<label><input type='radio' name='fancnt' value='1' " + String(config.fanCount == 1 ? "checked" : "") + "> 1 Fan</label>");
    client.print("<label><input type='radio' name='fancnt' value='2' " + String(config.fanCount == 2 ? "checked" : "") + "> 2 Fans</label>");
    client.print("</div>");

    client.println("<h3>🌡️ Thermal Profile Constraints</h3>");
    client.print("<div style='margin-bottom:15px;'>");
    client.print("<label style='margin-right:15px;'><input type='radio' name='unit' value='C' " + String(!config.isFahrenheit ? "checked" : "") + "> Celsius</label>");
    client.print("<label><input type='radio' name='unit' value='F' " + String(config.isFahrenheit ? "checked" : "") + "> Fahrenheit</label>");
    client.print("</div>");
    
    client.print("Min Activation Temp Limit: <input type='text' name='tmin' value='"); client.print(displayMin, 1); client.println("'>");
    client.print("Max Capacity Temp Limit: <input type='text' name='tmax' value='"); client.print(displayMax, 1); client.println("'>");

    client.println("<h3>🕒 Time Synchronizations</h3>");
    client.print("Timezone Offset (Hours): <input type='number' name='tz' value='"); client.print(config.tzOffset); client.println("'>");
    client.print("<div style='margin-bottom:15px;'><label style='font-weight:bold; display:block; margin-bottom:5px;'>Format Mode:</label>");
    client.print("<label style='margin-right:15px;'><input type='radio' name='clk' value='12' " + String(!config.is24Hour ? "checked" : "") + "> 12-Hour</label>");
    client.print("<label><input type='radio' name='clk' value='24' " + String(config.is24Hour ? "checked" : "") + "> 24-Hour</label></div>");

    client.println("<h3>🌐 Static Network Layer Infrastructure</h3>");
    client.print("Static IP Assignment: <input type='text' name='ip' value='"); client.print(config.ip.toString()); client.println("'>");
    client.print("Subnet Mask Filter: <input type='text' name='sub' value='"); client.print(config.subnet.toString()); client.println("'>");
    client.print("Gateway Link Node: <input type='text' name='gw' value='"); client.print(config.gateway.toString()); client.println("'>");
    client.print("DNS Nameserver Node: <input type='text' name='dns' value='"); client.print(config.dns.toString()); client.println("'>");

    client.println("<h3>🏡 Home Assistant Cluster Integration</h3>");
    client.print("Server IP / Host Address: <input type='text' name='hahost' value='"); client.print(config.haHost); client.println("' placeholder='e.g., 192.168.10.85' maxlength='63'>");
    
    // 🌟 FIXED: Changed 'config.haToken' back to 'config.haPort' so it displays 8123 instead of your security signature string!
    client.print("Server API Connection Port: <input type='text' name='haport' value='"); client.print(config.haPort); client.println("' placeholder='e.g., 8123' maxlength='10'>");
    
    client.print("Local Outbound Entity ID: <input type='text' name='nodeid' value='"); client.print(config.nodeID); client.println("' placeholder='e.g., fan_controller_01' maxlength='63'>");
    client.print("Remote Inbound Sensor ID: <input type='text' name='hasensor' value='"); client.print(config.haSensor); client.println("' placeholder='e.g., rack_temperature' maxlength='63'>");
    client.print("Long-Lived Bearer Token string:<br><textarea name='hatoken' rows='4' maxlength='450'>"); client.print(config.haToken); client.println("</textarea>");

    // 🌟 FORCE FLASH: Clears the network transmission pipeline right before printing long file names
    client.flush(); 
    delay(5);

    // 🛠️ Firmware Build Artifact Tracking Section Layout Block
    client.println("<hr><div style='background:#edf1f5; padding:15px; border-radius:4px; margin:20px 0; font-size:12px; color:#555;'>");
    client.println("<label style='font-weight:bold; display:block; margin-bottom:5px; color:#333;'>🛠️ Firmware Compilation Artifacts</label>");
    
    // Clean string processor extracting ONLY the filename so it wraps perfectly on phones and browsers
    client.print("<div style='word-break: break-all; white-space: normal;'><strong>Source File:</strong> "); 

    // 🌟 FIXED: Reads the master file name string captured at the very first millisecond of compilation!
    client.print(masterProjectFileName); 
    client.println("</div>");
    
    client.print("<strong>Build Date:</strong> ");  client.print(__DATE__); client.println("<br>");
    client.print("<strong>Build Time:</strong> ");  client.print(__TIME__); client.println("");
    client.println("</div>");

    client.println("<input type='submit' class='btn' value='Apply Parameters &amp; Save'>");
    client.println("</form></div></body></html>");

    client.stop(); // Safe connection termination breakout checkpoint
}




