#include <boost/asio/io_service.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <gpiod.hpp>
#include <string.h>
#include <iostream>
#include <ctype.h>
#include <unistd.h>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/vtable.hpp>
#include <variant>

#define MAX_NF_CARD_NUMS 16

#define GPIO_IN   0
#define GPIO_OUT  1

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

  static std::string nfresetnTemplate = "slot_x_resetn";
  static std::string nfresetnOut[MAX_NF_CARD_NUMS];

  static std::string nfintfselTemplate = "nf_uart_jtag_x_sel";
  static std::string nfintfselOut[MAX_NF_CARD_NUMS];

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

  static void ObjPathtoLineName(std::string &obj_path, std::string &line_name)
  {
    size_t pos = 0;
    std::string token;    
    std::string delimiter = "/";
    std::string blade_fix = "blade";

    while((pos = obj_path.find(delimiter)) != std::string::npos) {
      token = obj_path.substr(0, pos);
      obj_path.erase(0, pos + delimiter.length());
    }
    line_name = "slot_" + obj_path.substr(blade_fix.length(), 2) + line_name;
    std::cerr << "line_name: " << line_name << "\n";
  }

  static void CriticalhighPowerControl(sdbusplus::message::message& msg) {
      std::string interfaceName;
      boost::container::flat_map<std::string,
          std::variant<bool, std::string>> propertiesChanged;
      try
      {
          msg.read(interfaceName,propertiesChanged);
      }
      catch (sdbusplus::exception_t&)
      {
          std::cerr << "[PWCTL_log]error reading message"<<std::endl;
          return;
      }
      // state: interface property state
      bool state = std::get<bool>(propertiesChanged.begin()->second);
      // alarm high or alarm low
      bool highAlarm = (propertiesChanged.begin()->first == "CriticalAlarmHigh");
      if (state == false || highAlarm == false)return;

      static std::string pathFront = "/xyz/openbmc_project/sensors/power/NF";
      static std::string pathEnd = "_power";
      
      std::string path = std::string(msg.get_path());
      //regex path to get the name of the sensor who send the critical message
      if (path.substr(0, pathFront.length()) == pathFront) {
          path.erase(path.length() - pathEnd.length());     
          std::string boardIndex = path.substr(std::string(pathFront).length(), 2);
          std::cerr << "[PWCTL_log] boardIndex should be a integer : " << boardIndex<<std::endl;

          // this following code copy from bmcweb /redfish-core/lib/systems.hpp.
          // set the  dbus property state to power.off.
          {
              std::string command = "Power.Off";
              std::string systemId_path;
              systemId_path.assign("nf/blade"+boardIndex);

              conn->async_method_call(
                  [systemId_path](
                      const boost::system::error_code ec,
                      const std::variant<std::string>& property)
                  {
                      if (ec)
                      {
                          std::cerr << "[PWCTL_log] Critical Warning could not find Dbus power control";
                          return;
                      }

                      else
                      {
                          std::string status;
                          const std::string* value =
                              std::get_if<std::string>(&property);

                          status.assign(*value);

                          if (status == "false")
                          {
                              std::cerr << "[PWCTL_log] internal error.";
                              return;
                          }
                      }

                      // send SET command to D-Bus
                      conn->async_method_call(
                          [](const boost::system::error_code ec2)
                          {
                              if (ec2)
                              {
                                  std::cerr << "[PWCTL_log] send stop message failed";
                                  return;
                              }
                          },
                          "xyz.openbmc_project.nf.power.manager",
                              "/xyz/openbmc_project/control/" + systemId_path,
                              "org.freedesktop.DBus.Properties", "Set",
                              "xyz.openbmc_project.NF.Blade.Power",
                              "Asserted",
                              std::variant<std::string>{"Power.Off"});
                  },
                  "xyz.openbmc_project.nf.power.manager",
                      "/xyz/openbmc_project/control/" + systemId_path,
                      "org.freedesktop.DBus.Properties", "Get",
                      "xyz.openbmc_project.NF.Blade.Power", "Attached");
          }
      }
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
        std::variant<bool, std::string>> propertiesChanged;
        
        m.read(intfName, propertiesChanged);
        std::string obj_path;
        obj_path = m.get_path();

        std::string line_name = "_pwr";

        ObjPathtoLineName(obj_path, line_name);
        
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
          
          GPIOLine(line_name, GPIO_OUT, line, value);

          // Release line
          line.reset();
        }
        catch (std::exception& e)
        {
          std::cerr << "Unable to read property\n";
          return;
        }
      });
    static auto powermatch = sdbusplus::bus::match::match(
        *conn,
        "type='signal',member='PropertiesChanged',"
        "path_namespace='/xyz/openbmc_project/sensors/power',"
        "arg0='xyz.openbmc_project.Sensor.Threshold.Critical'",
        [](sdbusplus::message::message& m) {
            CriticalhighPowerControl(m);
        }
    );
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
    
    /** construct slot_x_resetn gpio name */
    gpio_name.clear();
    gpio_name.assign(nf_pwr_ctrl::nfresetnTemplate);
    gpio_name.replace(gpio_name.find("x"), 1, std::to_string(i));
    
    /** set slot_x_prsnt physical gpio name */
    nf_pwr_ctrl::nfresetnOut[i].assign(gpio_name);

    /** construct nf_uart_jtag_x_sel gpio name */
    gpio_name.clear();
    gpio_name.assign(nf_pwr_ctrl::nfintfselTemplate);
    gpio_name.replace(gpio_name.find("x"), 1, std::to_string(i));
    
    /** set slot_x_pwr physical gpio name */
    nf_pwr_ctrl::nfintfselOut[i].assign(gpio_name);

    std::string current_blade;
    
    /** set nf blade dbus path */
    nf_pwr_ctrl::nfBladePath[i] = 
    nf_pwr_ctrl::nfPowerPath + "blade" + std::to_string(i);

    current_blade.assign(nf_pwr_ctrl::nfBladePath[i].c_str());
    
    /** setup nf/blade<x> dbus object */
    nf_pwr_ctrl::nfBladeIface[i] = 
    server.add_interface(
      nf_pwr_ctrl::nfBladePath[i], nf_pwr_ctrl::nfPowerIface);
    
    /** add *Asserted* dbus property to nf/blade<x>/ dbus object */
    nf_pwr_ctrl::nfBladeIface[i]->register_property("Asserted", 
      std::string("Power.Off"), 
      sdbusplus::asio::PropertyPermission::readWrite);
    
    /** add *Reset* dbus property to nf/blade<x>/ dbus object */
    nf_pwr_ctrl::nfBladeIface[i]->register_property("WarmReset", 
      std::string("Done"), 
        //custom set
      [current_blade](const std::string& req, const std::string& property) {
        std::string line_name = "_resetn";
        std::string obj_path;
        obj_path.assign(current_blade.c_str());

        nf_pwr_ctrl::ObjPathtoLineName(obj_path, line_name);

        if (req == "Force")
        {
            /* reset output */
          gpiod::line line;
          int value = 0;
          nf_pwr_ctrl::GPIOLine(line_name, GPIO_OUT, line, value);

          usleep(5000);

          value = 1;
          nf_pwr_ctrl::GPIOLine(line_name, GPIO_OUT, line, value);

          line.reset();
        }
          /* There is no need to update D-Bus attribute value */
        return 1;
      });
    
    /** add *Attached* dbus property to nf/blade<x>/ dbus object */
    nf_pwr_ctrl::nfBladeIface[i]->register_property_r("Attached", 
      std::string("false"), 
      sdbusplus::vtable::property_::none,
        //custom get
      [current_blade](const std::string& property) {
        std::string line_name = "_prsnt";
        std::string obj_path;
        obj_path.assign(current_blade.c_str());
        
        nf_pwr_ctrl::ObjPathtoLineName(obj_path, line_name);

          /* read GPIO line */
        gpiod::line line;
        int value;
        nf_pwr_ctrl::GPIOLine(line_name, GPIO_IN, line, value);
        line.reset();
        
        return (value ? "false" : "true");
      });
    
    nf_pwr_ctrl::nfBladeIface[i]->initialize();
  }
  
  // Initialize SLOT_PWR, SLOT_PRSNT and SLOT_RESETN GPIOs
  // NOTE: Each power GPIO pin of a NF slot would be asserted (poweroff) after booted
  // NOTE: Each warm reset GPIO pin of a NF slot would be asserted (no reset) after booted
  gpiod::line gpioLine;
  int value;
  for (i = 0; i < MAX_NF_CARD_NUMS; i++) {
    value = 1;

    // set output GPIO with initial value 1
    if (!nf_pwr_ctrl::GPIOLine(
      nf_pwr_ctrl::nfpwrOut[i], GPIO_OUT, gpioLine, value))
      return -1;
    
    if (!nf_pwr_ctrl::GPIOLine(
      nf_pwr_ctrl::nfresetnOut[i], GPIO_OUT, gpioLine, value))
      return -1;

    value = 0;
    // set UART and JTAG of each NF card to BH motherboard as default
    if (!nf_pwr_ctrl::GPIOLine(
      nf_pwr_ctrl::nfintfselOut[i], GPIO_OUT, gpioLine, value))
      return -1;
    
    // set input GPIO
    if (!nf_pwr_ctrl::GPIOLine(
      nf_pwr_ctrl::nfprsntIn[i], GPIO_IN, gpioLine, value))
      return -1;
  }
  
  // Release gpioLine
  gpioLine.reset();
  
  nf_pwr_ctrl::PowerControl();
  
  nf_pwr_ctrl::io.run();
  
  return 0;
}
