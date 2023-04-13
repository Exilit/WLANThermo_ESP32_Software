/*************************************************** 
    Copyright (C) 2019  Martin Koerner

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    HISTORY: Please refer Github History
    
****************************************************/

#include <SPI.h>
#include <Wire.h>
#include <driver/ledc.h>
#include "SystemNanoLolinD32.h"
#include "temperature/TemperatureMcp3208.h"
#include "display/DisplayOled.h"
#include "Constants.h"

// TEMPERATURES
#define CS_MCP3208 0u

// PITMASTER
#define PITMASTER0IO1 25u   // Fan Pin
#define PITMASTER0IO2 33u   // Data Pin
#define PITMASTERSUPPLY 13u // StepUp Pin

// OLED
#define OLED_RESET_IO 4u

// BUZZER
#define BUZZER_IO 2u

// BLUETOOTH
#define BLE_RESET_PIN 4u

#define STANDBY_SLEEP_CYCLE_TIME 500000u // 500ms

enum ledcChannels
{
  // Channel 0, Timer0
  ledcPitMaster0IO1 = 0,
  // Channel 1, Timer0
  ledcPitMaster0IO2 = 1,
  // Channel 4, Timer2 --> change to REF_TICK because of automatic light sleep mode
  ledcBuzzer = 4
};

RTC_DATA_ATTR boolean SystemNanoVx::didSleep = false;  // standby ram
RTC_DATA_ATTR boolean SystemNanoVx::didCharge = false; // standby ram

SystemNanoVx::SystemNanoVx() : SystemBase()
{
}

void SystemNanoVx::hwInit()
{
  // initialize battery in hwInit!
  battery = new Battery();
  battery->update();

  pinMode(PITMASTERSUPPLY, OUTPUT);
  digitalWrite(PITMASTERSUPPLY, 0u);

  // handle sleep during battery charge
  if (battery->requestsStandby())
  {
    if (didSleep != true || battery->isCharging() != didCharge)
    {
      Wire.begin();
      Wire.setClock(700000);
      DisplayOled::drawCharging();
      didCharge = battery->isCharging();
    }

    didSleep = true;
    esp_sleep_enable_timer_wakeup(STANDBY_SLEEP_CYCLE_TIME);
    esp_deep_sleep_start();
  }

  didSleep = false;

  Wire.begin();
  Wire.setClock(700000);
}

void SystemNanoVx::init()
{
  deviceName = "nano";
  hardwareVersion = 3u;
  wlan.setHostName(DEFAULT_HOSTNAME + String(serialNumber));

  // configure PIN mode
  pinMode(CS_MCP3208, OUTPUT);

  // set initial PIN state
  digitalWrite(CS_MCP3208, HIGH);

  // initialize SPI interface
  SPI.begin();

  // initialize temperatures
  temperatures.add(new TemperatureMcp3208(4u, CS_MCP3208));
  temperatures.add(new TemperatureMcp3208(5u, CS_MCP3208));
  temperatures.add(new TemperatureMcp3208(6u, CS_MCP3208));
  temperatures.add(new TemperatureMcp3208(7u, CS_MCP3208));

  // add blutetooth feature
  bluetooth = new Bluetooth(&Serial2, BLE_RESET_PIN);
  bluetooth->loadConfig(&temperatures);
  bluetooth->init();

  // load config
  temperatures.loadConfig();

  // initialize buzzer
  buzzer = new Buzzer(BUZZER_IO, ledcBuzzer);

  // initialize pitmasters
  Pitmaster::setSupplyPin(PITMASTERSUPPLY);
  pitmasters.add(new Pitmaster(PITMASTER0IO1, ledcPitMaster0IO1, PITMASTER0IO2, ledcPitMaster0IO2));

  //        Name,      Nr, Aktor,  Kp,    Ki,  Kd, DCmin, DCmax, JP, SPMIN, SPMAX, LINK, ...
  profile[pitmasterProfileCount++] = new PitmasterProfile{"SSR SousVide", 0, 0, 104, 0.2, 0, 0, 100, 100};
  profile[pitmasterProfileCount++] = new PitmasterProfile{"BLOWER50", 1, 1, 7.0, 0.01, 200, 25, 100, 80, 25, 75, 0, 1};
  profile[pitmasterProfileCount++] = new PitmasterProfile{"Servo MG995", 2, 2, 104, 0.2, 0, 0, 100, 100, 25, 75};
  profile[pitmasterProfileCount++] = new PitmasterProfile{"Custom", 3, 1, 7.0, 0.2, 0, 0, 100, 100, 0, 100};

  // default profiles and temperatures, will be overwritten when config exists
  pitmasters[0u]->assignProfile(profile[0]);
  pitmasters[0u]->assignTemperature(temperatures[0]);

  pitmasters.loadConfig();

  pbGuard = new PbGuard();

  powerSaveModeSupport = true;

  initDone = true;
}
