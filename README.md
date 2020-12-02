# nf-power-control
This daemon is to generate NF card slot power control DBus objects and 
could be set from host to control NF card slot. 

* DBus Service: ```xyz.openbmc_project.nf.power.manager```
* DBus Interface: ```xyz.openbmc_project.NF.Blade.Power```

* Attached Property (read-only): ```/xyz/openbmc_project/control/nf/bladex/attr/Attached```   
(x.true: One NF card is attached onto slot #.x)   
(x.false: NO NF card is attached onto slot #.x)   
Such property is controlled by GPIO line SLOT_PRSNT

* Asserted Property (read-write): ```/xyz/openbmc_project/control/nf/bladex/attr/Asserted```
(Power.on: Power on the NF card on slot #.x)   
(Power.off: Power off the NF card on slot #.x)   
Such property is controlled by GPIO line SLOT_PWR

SLOT_PRSNT and SLOT_PWR are the GPIO pins from GPIO expanders and 
they have to be set as gpio-line-names in Kernel DTS file.
* SLOT_PRSNT gpio-line-names: ```slot_x_prsnt```     
* SLOT_PWR gpio-line-names: ```slot_x_pwr```     

