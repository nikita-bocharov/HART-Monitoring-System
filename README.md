# PLCnect HART Integration

The goal of this C++ library is to allow the PLCnext to connect with HART devices through the Phoenix Contact HART MUX.

## Installation
compile with `g++ -pthread -o hart_mux_server main.cpp lib/*.cpp`

## Usage
Running `./hart_mux_server` will start an HTTP server at `localhost:5900`.

## Issues
* `lib/hart_device.cpp`:`setTypeInfo(uint16_t deviceTypeCode)`
    
    For some reason, strings with more than 15 characters cause `addrUniq` and a lot of other class vars to get wiped out. The theoretical limit of the string data type should be practically infinite, so not sure where the memory leak is occurring.
    
* `lib/hart_device.cpp`:`setTypeInfo(uint16_t deviceTypeCode)`

    This functino re-parses the CSV file each time a lookup needs to be done (once per device discovery/update). It would be good to parse the file once and keep the info in memory.

* About 30% of the time, when the `hart_mux_server` is started, it is incapable of discovering gateways no matter how many times you ask it. When trying to access `/gw/{serialNo}/info`, the server throws the following error:
    ```
    terminate called after throwing an instance of 'nlohmann::detail::type_error'
    what():  [json.exception.type_error.302] type must be string, but is null
    ```

    It is known that occasionally, after the discovery network broadcast is sent out, the response appears to be sourced from the broadcase address (i.e. 192.168.254.255) instead of from the gateway that sent the response. When this happens the first time in the server, it seems that no matter how many times the server attempts this process, the result is the same. This may mean that there is some outside condition that must be true when the server starts.

* The `hart_mux_server` does not yet handle autodiscovery cases when there are multiple gateways on the network.