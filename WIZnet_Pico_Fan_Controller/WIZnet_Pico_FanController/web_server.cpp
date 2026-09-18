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
    String req = client.readStringUntil('\r');

    if (req.indexOf("POST /save_network") != -1 || req.indexOf("GET /save_network?") != -1) {
        String body = (req.indexOf("POST") != -1) ? client.readString() : req;

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
        client.flush();
        delay(1000);
        rp2040.reboot();
        return;
    }

    // Network Config Portal Page
    client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
    client.println("<!DOCTYPE html><html><head><title>Pico Ethernet Setup</title>");
    client.println("<style>body{font-family:Arial;margin:40px;background:#f0f2f5}.card{background:#fff;padding:25px;max-width:450px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}input[type=text]{width:100%;padding:8px;margin:8px 0;box-sizing:border-box}.btn{background:#0066cc;color:#fff;padding:10px 15px;border:none;border-radius:4px;cursor:pointer;width:100%}</style>");
    client.println("<script>function toggleFields(isDhcp){document.getElementById('static_fields').style.display = isDhcp ? 'none' : 'block';}</script>");
    client.println("</head><body><div class='card'><h2>WIZnet Pico LAN Settings</h2>");
    client.println("<form action='/save_network' method='post'>");

    client.printf("<p><label><input type='radio' name='dhcp' value='1' %s onclick='toggleFields(true)'> Dynamic IP (DHCP)</label></p>", config.useDHCP ? "checked" : "");
    client.printf("<p><label><input type='radio' name='dhcp' value='0' %s onclick='toggleFields(false)'> Static IP Address</label></p>", !config.useDHCP ? "checked" : "");

    client.printf("<div id='static_fields' style='display:%s;'>", config.useDHCP ? "none" : "block");
    client.printf("IP Address:<input type='text' name='ip' value='%s'>", config.ip.toString().c_str());
    client.printf("Subnet Mask:<input type='text' name='sub' value='%s'>", config.subnet.toString().c_str());
    client.printf("Gateway:<input type='text' name='gw' value='%s'>", config.gateway.toString().c_str());
    client.printf("DNS Server:<input type='text' name='dns' value='%s'>", config.dns.toString().c_str());
    client.println("</div>");

    client.println("<p><input type='submit' class='btn' value='Save & Reboot'></p>");
    client.println("</form></div></body></html>");
    client.stop();
}