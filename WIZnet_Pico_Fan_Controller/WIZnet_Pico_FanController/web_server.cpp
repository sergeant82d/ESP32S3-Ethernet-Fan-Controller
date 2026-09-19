#include "web_server.h"
#include "config.h"
#include <Ethernet.h>

static String getParam(String src, String key) {
    int idx = src.indexOf(key);
    if (idx == -1) return "";
    int start = idx + key.length();
    int end = src.indexOf('&', start);
    if (end == -1) end = src.indexOf(' ', start);
    if (end == -1) end = src.length();
    return src.substring(start, end);
}

static void parseIp(String str, IPAddress &out) {
    int a, b, c, d;
    if (sscanf(str.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        out = IPAddress(a, b, c, d);
    }
}

void handleNativeWebTraffic(EthernetClient& client) {
    String req = client.readStringUntil('\n');

    if (req.indexOf("/save_network") != -1) {
        String body = "";
        if (req.indexOf("POST") != -1) {
            // Skip headers until blank line
            while (client.connected()) {
                String line = client.readStringUntil('\n');
                if (line == "\r" || line.length() == 0) {
                    break; 
                }
            }
            // Read actual payload
            while (client.available()) {
                body += (char)client.read();
            }
        } else {
            body = req;
        }

        config.useDHCP = (getParam(body, "dhcp=") == "1");
        if (!config.useDHCP) {
            parseIp(getParam(body, "ip="), config.ip);
            parseIp(getParam(body, "sub="), config.subnet);
            parseIp(getParam(body, "gw="), config.gateway);
            parseIp(getParam(body, "dns="), config.dns);
        }
        saveSettings();

        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
        client.println("<h2>Settings Applied! Rebooting board...</h2>");

        client.println("</form></div></body></html>");
        client.flush();
        client.stop();

        delay(1000);
        rp2040.reboot();
        return;
    }

}