## Changelog for Pelion Border Router

### Release 2.3.1 (11.10.2021)

 - Updated Device Management Client to 4.11.1.

### Release 2.3.0 (21.09.2021)

 - Updated Device Management Client to 4.11.0.
 - Updated Mbed OS to 6.14.0.
 - Network Manager
     - Added OFDM support.
     - Sequence change in the application: The mesh interface is started after registering to the Pelion server.

### Release 2.2.0 (08.07.2021)

 - Updated Device Management Client to 4.10.0.
 - Updated Mbed OS to 6.12.0.

### Release 2.1.1 (17.06.2021)

 - Updated Device Management Client to 4.9.1.
 - Network Manager
     - Added resource to get Neighbor Information.
     - Added resource to reset statistics.

### Release 2.1.0 (21.05.2021)

 - Updated Device Management Client to 4.9.0.
 - Updated Mbed OS to 6.9.0.
 - Network Management
     - Added subscription feature for the statistics resources.
     - Added factory configuration support.
     - Added RADIUS server configuration as Wi-SUN configuration.

### Release 2.0.0 (07.12.2020)

 - Updated Device Management Client to 4.7.0.
 - Updated Mbed OS to 5.15 feature branch `feature-wisun` with nanostack v12.6.2.
 - Added Network Manager for Wi-SUN configuration and statistics.
 - Added delta-tool support for delta update.

### Release 1.0.0 (14.05.2020)

 - Initial release of Pelion Border router. 
 - This release contain Wi-SUN network capable border router, which is also running Pelion Device Management Client. This enables Pelion Device Management services for the border router, such as Firmware Update.

