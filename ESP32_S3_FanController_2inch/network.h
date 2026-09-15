#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

extern EthernetServer server;
extern EthernetUDP Udp;

// Resets the W5500, brings up SPI + Ethernet with the static config, starts
// the UDP socket (for NTP) and the HTTP server.
void networkInit();

// Blocking NTP time fetch (used as the TimeLib sync provider).
time_t getNtpTime();

#endif // NETWORK_H
