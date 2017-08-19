# esp8266-domoticz-zeroconf

# Sources
Built with PlatformIO : http://platformio.org/platformio-ide

# Infrastructure setup

(using Fedora for the following examples)

## Setup Domoticz service

Notice the HTTP port (-www 8181) and web root (-webroot domoticz) setup below.

```
#> vim /etc/systemd/system/domoticz.service

[Unit]
Description=Domoticz Home Automation
After=network.target

[Service]
User=domoticz
Group=domoticz
WorkingDirectory=/opt/domoticz
StandardOutput=null
ExecStart=/opt/domoticz/domoticz -www 8181 -sslwww 0 -log /var/log/domoticz/domoticz.log -webroot domoticz -dbase /opt/domoticz-data/domoticz.db
KillMode=process

[Install]
WantedBy=multi-user.target
Alias=domoticz.sh.service
```

## Publish domoticz using avahi-daemon

```
#> vim /etc/avahi/services/domoticz.service

<?xml version="1.0" standalone='no'?><!--*-nxml-*-->
<!DOCTYPE service-group SYSTEM "avahi-service.dtd">

<service-group>

  <name replace-wildcards="yes">%h</name>

  <service>
    <type>_domoticz._tcp</type>
    <port>8181</port>
    <txt-record>path=/domoticz</txt-record>
  </service>

</service-group>
```

## Setup Firewall
```
#> firewall-cmd --add-service=mdns --permanent
#> firewall-cmd --permanent --new-service=domoticz
#> firewall-cmd --permanent --service=domoticz --add-port=8181/tcp
#> systemctl restart firewalld
```
