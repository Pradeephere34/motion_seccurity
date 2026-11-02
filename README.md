# Motion Security IoT

The ESP32 reads a PIR sensor on GPIO 27 and publishes its state to an MQTT
broker. Node-RED can subscribe to that topic and show the state on a dashboard.

## Hardware

- ESP32 DevKit
- PIR sensor OUT connected to GPIO 27
- PIR sensor VCC and GND connected according to the sensor module's voltage
  requirements, with a shared ground

Allow the PIR module to warm up after powering it on. The ESP32 publishes the
current state at startup and whenever the state changes. Messages are retained
by MQTT so a dashboard can display the latest state after reconnecting.

## Configure and build

1. Install or start an MQTT broker, such as Mosquitto, on a computer on the same
   LAN as the ESP32.
2. Find that computer's LAN IP address. Do not use `localhost` for the ESP32's
   broker address; `localhost` would refer to the ESP32 itself.
3. In ESP-IDF, open **Menuconfig** and go to **Motion Security IoT**:
   - Set the Wi-Fi SSID and password.
   - Set the MQTT broker URI, for example
     `mqtt://192.168.1.25:1883` (replace the IP with the broker computer's IP).
   - Keep the topic `home/security/pir/state`, or choose another topic.
   - Keep the PIR GPIO at `27` for the wiring above.
4. Save the configuration, then build and flash the project.

The Wi-Fi password is stored as plaintext in the local `sdkconfig` file. Do not
commit or share that file. This setup uses an unauthenticated, unencrypted MQTT
connection intended only for a trusted local network. Do not expose the broker
or port 1883 to the public internet.

The MQTT payload is JSON:

```json
{"motion":true,"gpio":27,"timestamp_us":123456789}
```

`motion` is `true` while the PIR output is high and `false` when it is low.
The message uses QoS 1 and the retained flag.

## Node-RED dashboard

1. Install and start Node-RED on the broker computer. Install the FlowFuse
   Node-RED Dashboard package (`@flowfuse/node-red-dashboard`) from the
   Palette Manager.
2. Create an **MQTT in** node subscribed to `home/security/pir/state`. Set its
   broker host to `localhost` if Mosquitto and Node-RED are on the same
   computer; otherwise use the broker's address.
3. Connect the MQTT input to a **JSON** node, then to a **Function** node with
   this code:

   ```javascript
   msg.payload = msg.payload.motion ? "Motion detected" : "No motion";
   return msg;
   ```

   Connect the Function node to a Dashboard 2 **Text** widget and set the
   widget value to `msg.payload`.
4. Deploy the flow and open the dashboard URL shown by Node-RED, usually
   `http://localhost:1880/dashboard/motion`.

If the ESP32 connects to Wi-Fi but not MQTT, check that the broker is listening
for connections from the LAN, the URI uses the computer's LAN IP, and the
computer firewall allows the broker port (1883 by default).
