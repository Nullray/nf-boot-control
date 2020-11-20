#include <boost/asio/posix/stream_descriptor.hpp>
#include <gpiod.hpp>
#include <string.h>
#include <iostream>
#include <ctype.h>
#include <sdbusplus/asio/object_server.hpp>
#include <variant>

#define MAX_NF_CARD_NUMS 16

namespace nf_pwr_ctrl
{
static boost::asio::io_service io;
static std::shared_ptr<sdbusplus::asio::connection> conn;
static std::shared_ptr<sdbusplus::asio::dbus_interface> nfpwrIface[MAX_NF_CARD_NUMS];

static constexpr const char* nfPowerService = "xyz.openbmc_project.Control.NF.Power";
static constexpr const char* nfPowerIface = "xyz.openbmc_project.Control.NF.Power";
static std::string nfPowerPath = "/xyz/openbmc_project/control/nf/";
static std::string nfpwrTemplate = "slot_x_pwr";
static std::string nfpwrPath[MAX_NF_CARD_NUMS];
static std::string nfpwrOut[MAX_NF_CARD_NUMS];

static bool setGPIOOutput(const std::string& name, const int value,
                          gpiod::line& gpioLine)
{
    // Find the GPIO line
    gpioLine = gpiod::find_line(name);
    if (!gpioLine)
    {
        std::cerr << "Failed to find the " << name << " line.\n";
        return false;
    }

    // Request GPIO output to specified value
    try
    {
        gpioLine.request({__FUNCTION__, gpiod::line_request::DIRECTION_OUTPUT},
                         value);
    }
    catch (std::exception&)
    {
        std::cerr << "Failed to request " << name << " output\n";
        return false;
    }

    std::cerr << name << " set to " << std::to_string(value) << "\n";
    return true;
}

static void PowerStateMonitor()
{
    static auto match = sdbusplus::bus::match::match(
        *conn,
        "type='signal',member='PropertiesChanged', "
        "interface='org.freedesktop.DBus.Properties', "
        "arg0namespace=xyz.openbmc_project.Control.NF.Power",
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
                if (state == "xyz.openbmc_project.Control.NF.Power.On")
                    value = 0;
                else
                    value = 1;
                setGPIOOutput(line_name, value, line);
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
		/** set gpio name */
        gpio_name.clear();
        gpio_name.assign(nf_pwr_ctrl::nfpwrTemplate);
        gpio_name.replace(gpio_name.find("x"), 1, std::to_string(i));

		/** set gpio dbus object */
        nf_pwr_ctrl::nfpwrPath[i] = 
            nf_pwr_ctrl::nfPowerPath + gpio_name;

		/** set phsical gpio name */
        nf_pwr_ctrl::nfpwrOut[i].assign(gpio_name);

		/** setup new dbus REST interface */
        nf_pwr_ctrl::nfpwrIface[i] = 
            server.add_interface(
                 nf_pwr_ctrl::nfpwrPath[i], nf_pwr_ctrl::nfPowerIface);

		/** add new dbus event to the newly dbus object */
        nf_pwr_ctrl::nfpwrIface[i]->register_property("Asserted",
            std::string("xyz.openbmc_project.Control.NF.Power.On"),
            sdbusplus::asio::PropertyPermission::readWrite);
        nf_pwr_ctrl::nfpwrIface[i]->initialize();
    }

    // Initialize SLOT_PWR GPIOs
	// NOTE: All NF slots would be powered off (GPIO output 1) after booted
    gpiod::line gpioLine;
    for (i = 0; i < MAX_NF_CARD_NUMS; i++) {
        if (!nf_pwr_ctrl::setGPIOOutput(
                nf_pwr_ctrl::nfpwrOut[i], 1, gpioLine))
            return -1;
    }
    // Release gpioLine
    gpioLine.reset();

    nf_pwr_ctrl::PowerStateMonitor();

    nf_pwr_ctrl::io.run();

    return 0;
}
