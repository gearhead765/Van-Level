# Integrating with Home Assistant
## Introduction
Since The Van Level app is basically a RESTful API, to integrate with Home Assistant, all that is needed is to create a sensor to request data periodically from the REST API, and create a Dashboard card to display that information.
## Configuring Home Assistant
### Edit `configuration.yaml`
The first step is to turn on [packages](https://www.home-assistant.io/docs/configuration/packages/) in Home Assistant. Edit Home Assistant's configuration file `configuration.yaml` located in the `config/` directory.  You need to add the following line to the `homeassistant:` key:
```
    homeassistant:
      packages: !include_dir_named packages
```
You may need to create the directory `packages/` under your Home Assistant's `config/` directory if it doesn't already exist.
### The Van Image
Create the `www/` directory under `config/` if it does not already exist and copy the [van picture](Van_Level_App/Van_Level/HomeAssistant/van_top_view.png) into the `config/www/` directory.
### The REST sensor
Place the [van_leveling.yaml](Van_Level_App/Van_Level/HomeAssistant/van_leveling.yaml) file into the `/config/packages/` directory in Home Assistant.  There is only a single edit to this file needed to customize it to your specific installation. At the top of the file you will see the following:

```
  - platform: rest
    name: "Van Level Data"
    resource: http://YOUR_IP_HERE/readings   <=== EDIT THIS
    method: GET
    scan_interval: 1
    value_template: "{{ value_json.pitch }}"  # required, even if we only use attributes
 ```
 Replace the words `YOUR_IP_HERE` with the IP address of your Van Level device.  You can also adjust the `scan_interval` if you desire.  Setting this to a larger value will reduce the traffic on your network, but will also reduce the *responsiveness* of the Dashboard card.
 
 ### The Dashboard Card
 Copy the contents of [van_leveling_lovelace.yaml](Van_Level_App/Van_Level/HomeAssistant/van_leveling_lovelace.yaml) to your Dashboard. You can add this to the dashboard via the Raw Configuration Editor or through the YAML mode.  I suggest creating a new dashboard tab and adding this all by itself.
 
 
 ### Restarting Home Assistant
 
 Once the necessary changes have all been made, click on [Developer Tools](https://www.home-assistant.io/docs/tools/dev-tools/), select the `YAML` tap and click `CHECK CONFIGURATION`.  If you get a confirmation that the configuration is good, click `RESTART`, and choose restart Home Assistant.
 
 ### Your Van Level Dashboard
 Hopefully, you now have a working Van Level display on your Home Assistant dashboard.