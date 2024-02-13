# SykeröLabs3

- Automating my NFT (Nutrient Film Technique) driven greenhouse
- 3rd iteration made with Raspberry Pi & C++
	- See previous in https://github.com/visuve/SykeroLabs2

## Parts 

- [Raspberry Pi 5](https://www.raspberrypi.com/products/raspberry-pi-5/)
- [Waveshare RPi Relay Board (B)](https://www.waveshare.com/rpi-relay-board-b.htm)
- 2x [Noctua NF-A20 PWM](https://noctua.at/en/nf-a20-pwm) fans
	- 2x 10k ohm resistors (for RPM inputs)
- [Iduino SE029](https://www.openplatform.cc/index.php/home/index/details/apiid/188) temperature sensor
- [AD20P-1230E](https://botland.store/pumps/14873-electric-liquid-pump-ad20p-1230e-12v-240lh-5904422342739.html) 12v liquid pumps
- [Biltema Solar Panel Controller](https://www.biltema.fi/en-fi/car---mc/electrical-system/solar-panels/solar-panel-controller-pwm-20-a-2000045548) 20A
- SolarXon ~50W solar panel 
- An old car battery
- A heap of wires of different sizes

## Pins

- https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio-and-the-40-pin-header

| Usage                 | Capability    | Name     | Pin | Pin | Name     | Capability     | Usage                   |
|-----------------------|---------------|:--------:|:---:|:---:|:--------:|----------------|-------------------------|
| None                  |               | +3.3v    | 1   | 2   | +5v      |                | Fan Tachometer +5v      |
| None                  | I2C SDA       | GPIO 2   | 3   | 4   | +5v      |                | Temperature +5v         |
| None                  | I2C SCL       | GPIO 3   | 5   | 6   | Ground   |                | Temperature Ground      |
| Temperature Signal    |               | GPIO 4   | 7   | 8   | GPIO 14  | UART TX        | None                    |
| None                  |               | Ground   | 9   | 10  | GPIO 15  | UART RX        | None                    |
| None                  |               | GPIO 17  | 11  | 12  | GPIO 18  | PCM CLK / PWM0 | None                    |
| Reservoir Pump 1      |               | GPIO 27  | 13  | 14  | Ground   |                | None                    |
| None                  |               | GPIO 22  | 15  | 16  | GPIO 23  |                | Fan 1 Tachometer Signal |
| None                  |               | +3.3v    | 17  | 18  | GPIO 24  |                | Fan 2 Tachometer Signal |
| None                  | SPI MOSI      | GPIO 10  | 19  | 20  | Ground   |                | None                    |
| None                  | SPI MISO      | GPIO 9   | 21  | 22  | GPIO 25  |                | None                    |
| None                  | SPI CLK       | GPIO 11  | 23  | 24  | GPIO 8   | SPI CE0        | None                    |
| None                  |               | Ground   | 25  | 26  | GPIO 7   | SPI CE1        | None                    |
| None                  | I2C ID EEPROM | GPIO 0   | 27  | 28  | Reserved | I2C ID EEPROM  | None                    |
| NFT Pump 1            |               | GPIO 5   | 29  | 30  | Ground   |                | None                    |
| NFT Pump 2            |               | GPIO 6   | 31  | 32  | GPIO 12  | PWM0           | Fan 1 & 2 PWM           |
| None                  | PWM1          | GPIO 13  | 33  | 34  | Ground   |                | None                    |
| Fan 1 Toggle          | PWM1 / PCM FS | GPIO 19  | 35  | 36  | GPIO 16  |                | Reservoir Pump 2        |
| Relay 8 (unused)      |               | GPIO 26  | 37  | 38  | GPIO 20  | PCM DIN        | Fan 2 Toggle            |
| Fan Tachometer Ground |               | Ground   | 39  | 40  | GPIO 21  | PCM DOUT       | Relay 7 (unused)        |


## Notes

### Prerequisites

- In order to make the PWM and 1-Wire to work ``/boot/firmware/config.txt`` needs to be stabbed.
	- Find the line ``dtoverlay=something something`` and add the following lines below:
		- ``dtoverlay=pwm,pin=12,func=4``
		- ``dtoverlay=w1-gpio,gpiopin=4``
		- Note the diagram above...
	- See https://github.com/dotnet/iot/blob/main/Documentation/raspi-pwm.md and https://www.waveshare.com/wiki/Raspberry_Pi_Tutorial_Series:_1-Wire_DS18B20_Sensor for more details.


### Debugging

- The application logs can be viewed with ``journalctl -f -t sykerolabs``.
	- Assuming you use [Rasbpberry Pi OS](https://www.raspberrypi.com/software/) on the target...

### References

- I do not want to convolute the code with a lot of URLs/comments so here are some good quality links:
	- https://github.com/torvalds/linux/blob/master/tools/gpio
	- https://man7.org/linux/man-pages/index.html
	- https://pubs.opengroup.org/onlinepubs/9699919799.2018edition/
	- https://en.cppreference.com
	- https://cmake.org/cmake/help/latest/index.html
	- https://www.raspberrypi.com/documentation/
	- https://www.waveshare.com/wiki/Raspberry_Pi_Tutorial_Series:_1-Wire_DS18B20_Sensor
