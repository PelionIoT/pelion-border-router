# Pelion Border Router

Pelion Border router is a Pelion Device Management enabled border router implementation that provides the Wi-SUN border router logic.

A border router is a network gateway between a wireless Wi-SUN mesh network and a backhaul network. It controls and relays traffic between the two networks. In a typical setup, a Wi-SUN border router is connected to another router in the backhaul network (over Ethernet) which in turn forwards traffic to and from the internet or a private company LAN.

The code layout is organized like this:

```
configs/                  Contains Mbed TLS configs and Wisun Certificates
bootloader/               Contains Boot-loader for DISCO_F769NI
app/                      Contains the application code
mbed_app.json             Build time configuration file
```

## Prerequisites

To work with the pelion-border-router application, you need the following:

* [Serial connection](https://os.mbed.com/docs/latest/tutorials/serial-comm.html) to your device with open terminal connection (baud rate 115200, 8N1). This is optional, but helps with debugging and confirming that the client connects to Device Management and the Wi-SUN interface is up.
* [Arm Mbed CLI](https://os.mbed.com/docs/mbed-os/latest/tools/index.html) installed. See [installation instructions](https://os.mbed.com/docs/latest/tools/installation-and-setup.html).
  * Make sure that all the Python components are in par with the `pip` package [requirements.txt](https://github.com/ARMmbed/mbed-os/blob/master/requirements.txt) list from Mbed OS.
* [Pelion Device Management account](https://www.pelion.com/docs/device-management/current/account-management/index.html).
* An [API key](https://www.pelion.com/docs/device-management/current/user-account/api-keys.html) (with `Administrators` group privilages) for your **Device Management** account. The API key is needed for auto-generating the developer certificate and for firmware update.

## Memory Consumption

On border router the memory that is needed on board depends on network size. The RAM memory needed for a node on a network is about 650 bytes and needed KV store size is about 100 bytes for a node. KV store is needed to store Wi-SUN parameters during power cycle. Some Wi-SUN parameters need to be stored to KV store periodically, e.g. once in an hour. The size of periodically stored parameters is less than hundred bytes.

## Configuring and compiling pelion-border-router application

1. Clone the repository if not done yet:
```
git clone https://github.com/ARMmbed/pelion-border-router.git
```    
3. Go to `pelion-border-router` and deploy the dependencies:
```
cd pelion-border-router
mbed deploy
```

3. Configure Mbed CLI to use your **Device Management** account and board for `DISCO_F769NI`.
```
mbed config -G CLOUD_SDK_API_KEY ak_123....abc
mbed target DISCO_F769NI`
mbed toolchain GCC_ARM
```
4. Use Mbed CLI to download a developer certificate and to create an update-related configuration for your device:
```
mbed device-management init -d arm.com --model-name example-app --force -q
```
Alternatively, you can generate a developer certificate from [Pelion Device Management Portal](https://www.pelion.com/docs/device-management/current/connecting/provisioning-development-devices.html) and replace the dummy-file `mbed_cloud_dev_credentials.c` with your own certificate file.

5. Configure the application for your Wi-SUN network:
	* Use the Wi-SUN certificate definitions file `configs/wisun_certificates.h`, or generate your own Wi-SUN certificates (recommended) file to the same location.
	* Ensure the required Wi-SUN certificates (in file `configs/wisun_certificates.h`) are valid (`WISUN_ROOT_CERTIFICATE`, `WISUN_SERVER_CERTIFICATE`, `WISUN_SERVER_KEY`), and match the settings you are using with the border router. Invalid certificates or certificates that don't match prevent mesh network formation.

<span class="tips">**Tip:** Use the same Mbed OS version in the border router and the application (Device Management Client).</span>

<span class="notes">**Note:** When you go to production, please do not use the example Wi-SUN certificate files provided as is due to security reasons.</span>

6. Wi-SUN configuration:
[mbed_app.json](https://github.com/ARMmbed/pelion-border-router/blob/master/mbed_app.json) file contains configuration for Pelion Border Router application.
The Wi-SUN specific parameters are listed below.

	| Field                               | Description                                                   |
	|-------------------------------------|---------------------------------------------------------------|
	| `wisun-network-name`                | Network name for Wi-SUN the network, MUST be same for all the devices in the network |
	| `wisun-regulatory-domain`           | Defines regulatory domain, refer to [ws_management_api](https://github.com/ARMmbed/mbed-os/blob/master/features/nanostack/sal-stack-nanostack/nanostack/ws_management_api.h) for correct values for your region. |
	| `wisun-operating-class`             | Defines operating class, limited by the regulatory domain |
	| `wisun-operating-mode`              | Defines the operating mode, limited by the regulatory domain |
	| `wisun-uc-channel-function`         | Unicast channel function |
	| `wisun-bc-channel-function`         | Broadcast channel function |
	| `wisun-uc-fixed-channel`            | Fixed channel for unicast |
	| `wisun-bc-fixed-channel`            | Fixed channel for broadcast |
	| `wisun-uc-dwell-interval`           | Unicast dwell interval. Range: 15-255 milliseconds |
	| `wisun-bc-interval`                 | Broadcast interval. Duration between broadcast dwell intervals. Range: 0-16777216 milliseconds |
	| `wisun-bc-dwell-interval`           | Broadcast dwell interval. Range: 15-255 milliseconds |
	| `certificate-header`                | Wi-SUN certificate header file |
	| `root-certificate`                  | Root certificate |
	| `own-certificate`                   | Own certificate |
	| `own-certificate-key`               | Own certificate's key |

Regulatory domain, operating class and operating mode are defined in the Wi-SUN PHY-specification.

7. Backhaul connectivity:
The Pelion border router application should be connected to a backhaul network. This enables the border router to connect to the pelion server as well as the Wi-SUN mesh network to the internet or a private LAN. The application supports Ethernet backhaul connectivity:
	
8. Enable Dual-Bank mode on DISCO_F769NI:
	* Connect the device using [STLink-Utility software](https://www.st.com/en/development-tools/stsw-link004.html)
	* Go to Target->Option Bytes. Uncheck nDBANK option.
	* Apply and disconnect.
	
8. Compile the application for `DISCO_F769NI`:

```
mbed compile -m DISCO_F769NI -t GCC_ARM
```

## Running the pelion border router application

1. Find the  binary file `pelion-border-router.bin` in the `BUILD` folder.
2. Copy the binary to the USB mass storage root of the development board. It is automatically flashed to the target MCU. When the flashing is complete, the board restarts itself. Press the **Reset** button of the development board if it does not restart automatically.
3. The program begins execution.
4. Open the [serial connection](#serial-connection-settings), for example PuTTY.
5. Obtain your device's Device ID either from device console logs or from Device Management Portal. When the client has successfully connected, the terminal shows:
	```
	Client registered
	Endpoint Name: <Endpoint name>
	Device ID: <Device ID>
	```
6. To verify the connection with Device Management Portal:
	* Log in to Device Management Portal.
	* Select Device directory from the menu on the left.
	* When your device is listed on the Devices page, it is connected and available.
Your device is now connected and ready for the firmware update. For development devices, the Endpoint name and Device ID are identical.

### Updating the Firmware

1. To update the firmware on your device:
	```	
	mbed device-management update device -D <Device ID>
	```
2. When the update starts, the client tracing log shows:
	```
		Update progress = 0%
	```
4. After this, the device reboots automatically and registers to Device Management.	

### Program Flow

1. Initialize, connect and register to Pelion DM
2. Initialize and bring up Wi-SUN Interface
3. Interact with the user through the serial port (115200 bauds)
   - Press enter through putty/minicom to simulate button
   - Press `i` to print endpoint name
   - Press Ctrl-C to to unregister
   - Press `r` to reset storage and reboot (warning: it generates a new device ID!)

## Serial connection settings

Serial connection settings are as follows:

* Baud-rate = 115200.
* Data bits = 8.
* Stop bits = 1.

If there is no input from the serial terminal, press the **Reset** button of the development board.

In the PuTTY main screen, save the session, and click **Open**. This opens a console window showing debug messages from the application. If the console screen is blank, you may need to press the **Reset** button of the board to see the debug information. The serial output from the pelion border router looks something like this in the console:
```
Mbed Bootloader
No Update image
[DBG ] Active firmware up-to-date
booting...
[INFO][plat]: Pelion-Border-Router
Connect to network
[INFO][IPV6]: Start Bootstrap
[INFO][addr]: Tentative Address added to IF 1: fe80::280:e1ff:fe2d:3d
[INFO][addr]: DAD passed on IF 1: fe80::280:e1ff:fe2d:3d
[INFO][addr]: Tentative Address added to IF 1: 2401:4900:3369:4e2c:280:e1ff:fe2d:3d
[INFO][addr]: DAD passed on IF 1: 2401:4900:3369:4e2c:280:e1ff:fe2d:3d
[INFO][IPV6]: IPv6 bootstrap ready
Network initialized, connected with IP 2401:4900:3369:4e2c:280:e1ff:fe2d:3d

Start developer flow
```