// ==========================================
// 🧱 Tab 7: F_Display.ino
// ==========================================

void drawStaticUIFrame() {
    screenMain.fillRect(0, 0, 240, 40, ST77XX_BLUE);
    screenMain.setTextColor(ST77XX_WHITE); screenMain.setTextSize(2);
    screenMain.setCursor(10, 12); screenMain.print("S3 ECOSYSTEM");
}

void updateMainDashboardUI() {
    struct tm timeinfo;
    
    // Pass ST77XX_BLACK as the second argument to clear old clock digits dynamically!
    screenMain.setTextColor(ST77XX_YELLOW, ST77XX_BLACK); 
    screenMain.setTextSize(2);
    screenMain.setCursor(10, 48);

    if (getLocalTime(&timeinfo)) {
        char dStr[16], tStr[16];
        strftime(dStr, sizeof(dStr), "%m/%d/%Y", &timeinfo);
        if (config.is24Hour) strftime(tStr, sizeof(tStr), "%H:%M:%S", &timeinfo);
        else strftime(tStr, sizeof(tStr), "%I:%M %p", &timeinfo);

        screenMain.print(dStr);
        screenMain.setCursor(130, 48); screenMain.print(tStr);
    }
    else {
        screenMain.print("ESP32-S3 Network Cooling System");
    }

    // Main Temperature Average Block Updates
    screenMain.setCursor(15, 85);
    if (!localSensorHealthy && !networkSensorHealthy) {
        screenMain.setTextSize(4); 
        screenMain.setTextColor(ST77XX_RED, ST77XX_BLACK); 
        screenMain.print("CRIT!");
    }
    else {
        float dispAvg = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;
        screenMain.setTextSize(4);
        
        uint16_t tempColor = (blendedAverageC > (config.tMax - 5.0)) ? ST77XX_RED : ST77XX_GREEN;
        screenMain.setTextColor(tempColor, ST77XX_BLACK);
        
        screenMain.print(dispAvg, 1);
        screenMain.setTextSize(2); screenMain.print(config.isFahrenheit ? " F AVG" : " C AVG");
    }

    // Lower Fan RPM Print Frames
    screenMain.drawFastHLine(0, 135, 240, ST77XX_DARKGRAY);
    screenMain.setTextSize(2);
    
    // Channel 1 Text Frame Print
    screenMain.setCursor(10, 145); 
    screenMain.setTextColor(ST77XX_WHITE, ST77XX_BLACK); 
    screenMain.print("F1: "); screenMain.print(currentRPMs[0]);
    screenMain.print("    "); 

    // Channel 2 Text Frame Print
    screenMain.setCursor(125, 145); 
    screenMain.setTextColor(ST77XX_WHITE, ST77XX_BLACK); 
    screenMain.print("F2: "); screenMain.print(currentRPMs[1]);
    screenMain.print("    "); 
}

void updateLeftGaugesUI() { 
    int barWidth = 28; int spacing = 15; int startX = 35; 
    screenLeft.fillRect(0, 0, 160, 80, ST77XX_BLACK);
    for (int i = 0; i < NUM_FANS; i++) { 
      int currentX = startX + (i * (barWidth + spacing)); 
      int fillHeight = map(constrain(currentRPMs[i], 0, 5000), 0, 5000, 0, 60); 
      screenLeft.drawRect(currentX, 5, barWidth, 60, ST77XX_WHITE); 
      screenLeft.fillRect(currentX + 2, 5 + (56 - fillHeight), barWidth - 4, fillHeight, ST77XX_CYAN); 
    } 
}

void updateRightGaugesUI() {
    int barWidth = 28; int spacing = 15; int startX = 35; 
    screenRight.fillRect(0, 0, 160, 80, ST77XX_BLACK); 
    float maxScale = config.isFahrenheit ? ((config.tMax * 9 / 5) + 32) : config.tMax; 
    
    if (localSensorHealthy) {
        float dispL = config.isFahrenheit ? ((localTempC * 9 / 5) + 32) : localTempC; 
        int hL = map(constrain(dispL, 0, maxScale), 0, maxScale, 0, 60); 
        screenRight.drawRect(startX, 5, barWidth, 60, ST77XX_WHITE); 
        screenRight.fillRect(startX + 2, 5 + (56 - hL), barWidth - 4, hL, ST77XX_ORANGE);
    } else { 
        screenRight.drawRect(startX, 5, barWidth, 60, ST77XX_RED);
    }
    
    if (networkSensorHealthy) { 
        float dispN = config.isFahrenheit ? ((networkTempC * 9 / 5) + 32) : networkTempC; 
        int hN = map(constrain(dispN, 0, maxScale), 0, maxScale, 0, 60); 
        screenRight.drawRect(startX + barWidth + spacing, 5, barWidth, 60, ST77XX_WHITE); 
        screenRight.fillRect(startX + barWidth + spacing + 2, 5 + (56 - hN), barWidth - 4, hN, ST77XX_MAGENTA);
    } else { 
        screenRight.drawRect(startX + barWidth + spacing, 5, barWidth, 60, ST77XX_RED); 
    } 
}
