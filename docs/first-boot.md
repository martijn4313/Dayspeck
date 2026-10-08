---
description: "First boot of the Dayspeck ESP8266 weather display: the setup WiFi network, connecting to your network and setting the location."
---

# First boot

1. If there are no WiFi credentials yet the device starts its own network after about 30 s. With a saved
   network it waits 90 s after power-on, and 5 minutes after a restart (such as a firmware update) or when a
   working network drops. While its own network is on it keeps trying the saved network every 30 s (when no
   phone is connected to it) and switches back as soon as that works.
   The OLED shows the network name (**Dayspeck**), its password and `192.168.4.1`.
2. Join that network and open `http://192.168.4.1`. Sign in with user **`admin`** and the password
   from the display. The default password is `moto` plus six hex digits derived from the chip ID.
3. Enter your WiFi network under *WiFi Configuration* and **set your own admin password**. The device
   restarts after a password change.
4. Once the device is on your WiFi, open its page again and search for your town or village under
   *Set Location*. (The search needs internet; in the setup network you can type the coordinates
   instead.)
5. Afterwards the page is at `http://dayspeck.local`. See the [web UI](web-ui.md) for what each card does.

If the WiFi connection is lost later, the device keeps retrying on its own and only opens the setup
network again after 5 minutes without a connection.
