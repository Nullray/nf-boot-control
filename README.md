# nf-power-control
This daemon is to generate NF card slot power on/off DBus path and could be set from host to control NF card slot. 

* DBus Service: ```xyz.openbmc_project.Control.NF.Power```
* DBus Interface: ```xyz.openbmc_project.Control.NF.Power```
* DBus SLOT_PWR Path: ```/xyz/openbmc_project/control/nf/slot_x_pwr```

SLOT_PWR are the GPIO pins from GPIO expanders and they have to be set as gpio-line-names in Kernel DTS file.
* SLOT_PWR gpio-line-names: ```slot_x_pwr```
