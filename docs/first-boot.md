# First boot

1. If there are no (working) WiFi credentials the device starts its own network after about 30 s.
   The OLED shows the network name (**WeatherWise**), its password and `192.168.4.1`.
2. Join that network and open `http://192.168.4.1`. Sign in with user **`admin`** and the password
   from the display. The default password is `moto` plus six hex digits derived from the chip ID.
3. Enter your WiFi network under *WiFi Configuration*, pick your city (or add per-network
   locations), and **set your own admin password**. The device restarts after a password change.
4. Afterwards the page is at `http://weatherwise.local`. See the [web UI](web-ui.md) for what each card does.

If the WiFi connection is lost later, the device keeps retrying on its own and only opens the setup
network again after 5 minutes without a connection.
