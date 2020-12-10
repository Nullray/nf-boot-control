# nf-power-control
This daemon is to generate NF card slot power control DBus objects and 
could be set from host to control NF card slot. 

* DBus Service: ```xyz.openbmc_project.nf.power.manager```
* DBus Interface: ```xyz.openbmc_project.NF.Blade.Power```

* Attached Property (read-only): ```/xyz/openbmc_project/control/nf/blade<x>/attr/Attached```   
(true: One NF card is attached onto slot #.x)   
(false: NO NF card is attached onto slot #.x)   
Such property is controlled by GPIO line SLOT_PRSNT

* Asserted Property (read-write): ```/xyz/openbmc_project/control/nf/blade<x>/attr/Asserted```
(Power.On: Power on the NF card on slot #.x)   
(Power.Off: Power off the NF card on slot #.x)   
Such property is controlled by GPIO line SLOT_PWR

* WarmReset Property (read-write): ```/xyz/openbmc_project/control/nf/blade<x>/attr/Reset```
(Force: trigger warm reset of the NF card on slot #.x, used in PUT)   
(Done: warm reset of the NF card on slot #.x is finished, queried from GET)   
Such property is controlled by GPIO line SLOT_RESETN

SLOT_PRSNT, SLOT_PWR and SLOT_RESET are the GPIO pins from GPIO expanders and 
they have to be set as gpio-line-names in Kernel DTS file.
* SLOT_PRSNT gpio-line-names: ```slot_x_prsnt```     
* SLOT_PWR gpio-line-names: ```slot_x_pwr```     
* SLOT_RESETN gpio-line-names: ```slot_x_resetn```     

