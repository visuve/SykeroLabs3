# SykeröLabs3

- Automating my NFT (Nutrient Film Technique) driven greenhouse
- 3rd iteration made with Raspberry Pi & C++
	- See previous in https://github.com/visuve/SykeroLabs2

## Parts 

- [Raspberry Pi 5](https://www.raspberrypi.com/products/raspberry-pi-5/)
- [Waveshare RPi Relay Board (B)](https://www.waveshare.com/rpi-relay-board-b.htm)
- [Noctua NF-A20 PWM](https://noctua.at/en/nf-a20-pwm)

## Pins

- https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio-and-the-40-pin-header

| Usage            | Capability    | Name     | Pin | Pin | Name     | Capability     | Usage            |
|------------------|---------------|:--------:|:---:|:---:|:--------:|----------------|------------------|
| None             |               | +3.3v    | 1   | 2   | +5v      |                | None             |
| None             | I2C SDA       | GPIO 2   | 3   | 4   | +5v      |                | None             |
| None             | I2C SCL       | GPIO 3   | 5   | 6   | Ground   |                | None             |
| None             |               | GPIO 4   | 7   | 8   | GPIO 14  | UART TX        | None             |
| None             |               | Ground   | 9   | 10  | GPIO 15  | UART RX        | None             |
| None             |               | GPIO 17  | 11  | 12  | GPIO 18  | PCM CLK / PWM0 | None             |
| Reservoir Pump 1 |               | GPIO 27  | 13  | 14  | Ground   |                | None             |
| None             |               | GPIO 22  | 15  | 16  | GPIO 23  |                | Fan 1 Tachometer |
| None             |               | +3.3v    | 17  | 18  | GPIO 24  |                | Fan 2 Tachometer |
| None             | SPI MOSI      | GPIO 10  | 19  | 20  | Ground   |                | None             |
| None             | SPI MISO      | GPIO 9   | 21  | 22  | GPIO 25  |                | None             |
| None             | SPI CLK       | GPIO 11  | 23  | 24  | GPIO 8   | SPI CE0        | None             |
| None             |               | Ground   | 25  | 26  | GPIO 7   | SPI CE1        | None             |
| None             | I2C ID EEPROM | GPIO 0   | 27  | 28  | Reserved | I2C ID EEPROM  | None             |
| NFT Pump 1       |               | GPIO 5   | 29  | 30  | Ground   |                | None             |
| NFT Pump 2       |               | GPIO 6   | 31  | 32  | GPIO 12  | PWM0           | Fan 1 & 2 PWM    |
| None             | PWM1          | GPIO 13  | 33  | 34  | Ground   |                | None             |
| Fan 1 Toggle     | PWM1 / PCM FS | GPIO 19  | 35  | 36  | GPIO 16  |                | Reservoir Pump 2 |
| Relay 8 (unused) |               | GPIO 26  | 37  | 38  | GPIO 20  | PCM DIN        | Fan 2 Toggle     |
| None             |               | Ground   | 39  | 40  | GPIO 21  | PCM DOUT       | Relay 7 (unused) |


## Notes

### Prerequisites

- In order to make the PWM work ``/boot/firmware/config.txt`` needs to be stabbed.
	- Change the line ``dtoverlay=something something`` to ``dtoverlay=pwm,pin=12,func=4``. Note the pin diagram above.
	- See https://github.com/dotnet/iot/blob/main/Documentation/raspi-pwm.md for more details.

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

