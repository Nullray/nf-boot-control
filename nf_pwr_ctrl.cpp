#include <boost/asio/io_service.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <gpiod.hpp>
#include <string.h>
#include <iostream>
#include <ctype.h>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/vtable.hpp>
#include <variant>

#define MAX_NF_CARD_NUMS 16

namespace nf_pwr_ctrl
{
static boost::asio::io_service io;
static std::shared_ptr<sdbusplus::asio::connection> conn;
static std::shared_ptr<sdbusplus::asio::dbus_interface> nfBladeIface[MAX_NF_CARD_NUMS];

static constexpr const char* nfPowerService = "xyz.openbmc_project.nf.power.manager";
static constexpr const char* nfPowerIface = "xyz.openbmc_project.NF.Blade.Power";
static std::string nfPowerPath = "/xyz/openbmc_project/control/nf/";
static std::string nfBladePath[MAX_NF_CARD_NUMS];

static std::string nfpwrTemplate = "slot_x_pwr";
static std::string nfpwrOut[MAX_NF_CARD_NUMS];

static std::string nfprsntTemplate = "slot_x_prsnt";
static std::string nfprsntIn[MAX_NF_CARD_NUMS];

static bool GPIOLine(const std::string& name, const int out,
                          gpiod::line& gpioLine, int& value)
{
    // Find the GPIO line
    gpioLine = gpiod::find_line(name);
    if (!gpioLine)
    {
        std::cerr << "Failed to find the " << name << " line.\n";
        return false;
    }

    // Request GPIO line as input/output
    try
    {
		if(out)
			gpioLine.request({__FUNCTION__, gpiod::line_request::DIRECTION_OUTPUT},
                         value);
		else {
			gpioLine.request({__FUNCTION__, gpiod::line_request::DIRECTION_INPUT});
			value = gpioLine.get_value();
		}
    }
    catch (std::exception&)
    {
			std::cerr << "Failed to request " << name << "\n";
      return false;
    }

    return true;
}

static void PowerControl()
{
    static auto match = sdbusplus::bus::match::match(
        *conn,
        "type='signal',member='PropertiesChanged', "
        "interface='org.freedesktop.DBus.Properties', "
        "arg0namespace=xyz.openbmc_project.NF.Blade.Power",
        [](sdbusplus::message::message& m) {
            std::string intfName;
            boost::container::flat_map<std::string,
                                           std::variant<bool, std::string>>
                    propertiesChanged;

            m.read(intfName, propertiesChanged);
            std::string obj_path;
            obj_path = m.get_path();

            std::string line_name;
            size_t pos = 0;
            std::string token;
            std::string delimiter = "/";
            while((pos = obj_path.find(delimiter)) != std::string::npos) {
                 token = obj_path.substr(0, pos);
                 obj_path.erase(0, pos + delimiter.length());
            }
            line_name.assign(obj_path);
            std::cerr << "line_name: " << line_name << "\n";

            try
            {
                auto state = std::get<std::string>(propertiesChanged.begin()->second);
                std::cerr << "state: " << state << "\n";
                gpiod::line line;
                int value;
								
								if (state == "Power.On")
									value = 0;
								else
									value = 1;
								
								GPIOLine(line_name, 1, line, value);

                // Release line
                line.reset();
            }
            catch (std::exception& e)
            {
                std::cerr << "Unable to read property\n";
                return;
            }
        });
}
}

int main(int argc, char* argv[])
{
    std::cerr << "Start NF card power control service...\n";
    nf_pwr_ctrl::conn =
        std::make_shared<sdbusplus::asio::connection>(nf_pwr_ctrl::io);

    nf_pwr_ctrl::conn->request_name(nf_pwr_ctrl::nfPowerService);

    sdbusplus::asio::object_server server =
        sdbusplus::asio::object_server(nf_pwr_ctrl::conn);

    std::string gpio_name;
    int i;

    for (i = 0; i < MAX_NF_CARD_NUMS; i++) {
			/** construct slot_x_pwr gpio name */
			gpio_name.clear();
			gpio_name.assign(nf_pwr_ctrl::nfpwrTemplate);
			gpio_name.replace(gpio_name.find("x"), 1, std::to_string(i));
			
			/** set slot_x_pwr physical gpio name */
			nf_pwr_ctrl::nfpwrOut[i].assign(gpio_name);
			
			/** construct slot_x_prsnt gpio name */
			gpio_name.clear();
			gpio_name.assign(nf_pwr_ctrl::nfprsntTemplate);
			gpio_name.replace(gpio_name.find("x"), 1, std::to_string(i));
			
			/** set slot_x_prsnt physical gpio name */
			nf_pwr_ctrl::nfprsntIn[i].assign(gpio_name);
			
			/** set nf blade dbus path */
			nf_pwr_ctrl::nfBladePath[i] = 
				nf_pwr_ctrl::nfPowerPath + "blade" + std::to_string(i);
			
			/** setup nf/blade<x> dbus object */
			nf_pwr_ctrl::nfBladeIface[i] = 
				server.add_interface(
						nf_pwr_ctrl::nfBladePath[i], nf_pwr_ctrl::nfPowerIface);
			
			/** add *Asserted* dbus property to nf/blade<x>/ dbus object */
			nf_pwr_ctrl::nfBladeIface[i]->register_property("Asserted",
					std::string("Power.Off"),
					sdbusplus::asio::PropertyPermission::readWrite);
			
			/** add *Attached* dbus property to nf/blade<x>/ dbus object */
			nf_pwr_ctrl::nfBladeIface[i]->register_property_r("Attached",
					std::to_string(i) + std::string(".false"),
					sdbusplus::vtable::property_::none,
					//custom get
					[](const std::string& property) {
						std::string line_name;
						size_t pos = 0;
							
						/* construct slot_x_prsnt as name of the GPIO line */
						pos = property.find(".");
						line_name = "slot_" + property.substr(0, pos) + "_prsnt";
							
						/* read GPIO line */
						gpiod::line line;
						int value;
						nf_pwr_ctrl::GPIOLine(line_name, 0, line, value);
						line.reset();
							
						return property.substr(0, pos + 1) + (value ? "false" : "true");
			});

      nf_pwr_ctrl::nfBladeIface[i]->initialize();
		}

    // Initialize SLOT_PWR and SLOT_PRSNT GPIOs
		// NOTE: Each power GPIO pin of a NF slot would be asserted (poweroff) after booted
    gpiod::line gpioLine;
		int value = 1;
    for (i = 0; i < MAX_NF_CARD_NUMS; i++) {
			// set output GPIO with initial value 1
			if (!nf_pwr_ctrl::GPIOLine(
						nf_pwr_ctrl::nfpwrOut[i], 1, gpioLine, value))
				return -1;
			
			// set input GPIO
			if (!nf_pwr_ctrl::GPIOLine(
						nf_pwr_ctrl::nfprsntIn[i], 0, gpioLine, value))
				return -1;
    }
    // Release gpioLine
    gpioLine.reset();

    nf_pwr_ctrl::PowerControl();

    nf_pwr_ctrl::io.run();

    return 0;
}
