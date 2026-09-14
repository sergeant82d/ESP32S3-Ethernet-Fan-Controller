void saveSettings() {
    File f = LittleFS.open("/settings.cfg", "w");
    if (f) {
        f.write((uint8_t*)&config, sizeof(config));
        f.close();
        Serial.println("Settings saved safely and cleanly.");
    }
}

void loadSettings() {
    if (LittleFS.exists("/settings.cfg")) {
        File f = LittleFS.open("/settings.cfg", "r");
        if (f) {
            if (f.available() >= sizeof(config)) {
                f.read((uint8_t*)&config, sizeof(config));
            }
            f.close();
            
            if (config.fanCount < 2 || config.fanCount > 4) {
                config.fanCount = 2; 
                Serial.println(" -> Config Count auto-realigned to 2 active channels.");
            }
        }
    }
}

String getUrlParam(String src, String param) {
    int idx = src.indexOf(param);
    if (idx == -1) return "";
    int start = idx + param.length();
    int end = src.indexOf('&', start);
    if (end == -1) end = src.length();
    String val = src.substring(start, end);
    val.replace("+", " "); 
    String decoded = "";
    for (size_t i = 0; i < val.length(); i++) {
        if (val[i] == '%' && i + 2 < val.length()) {
            char high = val[i+1]; char low  = val[i+2];
            int hVal = (high >= 'A') ? (high - 'A' + 10) : (high - '0');
            int lVal = (low >= 'A')  ? (low - 'A' + 10)  : (low - '0');
            if (high >= 'a') hVal = high - 'a' + 10;
            if (low >= 'a')  lVal = low - 'a' + 10;
            decoded += (char)((hVal << 4) | lVal); i += 2;
        } else { decoded += val[i]; }
    }
    return decoded;
}
